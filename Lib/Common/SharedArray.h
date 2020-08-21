#ifndef _QPULIB_COMMON_SHAREDARRAY_H_
#define _QPULIB_COMMON_SHAREDARRAY_H_
#include "BufferObject.h"
#include "../Support/debug.h"

namespace QPULib {

enum HeapView {
	use_as_heap_view
};

template <typename T>
class SharedArray {
public:
	SharedArray() : m_heap(getBufferObject()) {}
	SharedArray(HeapView do_heap_view) : m_heap(getBufferObject()) {
breakpoint
		assert(do_heap_view == HeapView::use_as_heap_view);
		m_is_heap_view = true;
		m_size = m_heap.size();
		m_usraddr = m_heap.usr_address();
	}
  SharedArray(uint32_t n) : m_heap(getBufferObject()) { alloc(n); }

  uint32_t getAddress() { return m_phyaddr; }
	uint32_t size() const { return m_size; }

	/**
	 * @param n number of 4-byte elements to allocate (so NOT memory size!)
	 */
	void alloc(uint32_t n) {
		assert(n > 0);
		assert(m_size == 0);
		assert(m_usraddr == nullptr);
		assert(m_phyaddr == 0);
		assert(!m_is_heap_view);

		m_phyaddr = m_heap.alloc_array(sizeof(T)*n, m_usraddr);
		assert(m_usraddr != nullptr);
		assert(m_phyaddr > 0);
		assert(m_phyaddr > 0);
		m_size = n;
	}


	/**
	 * Just forget the allocation and size.
	 *
	 * Note that the array is NOT deallocated in the heap.
	 */
	void dealloc() {
		if (m_size > 0) {
			m_phyaddr = 0;
			m_size = 0;
			m_usraddr = nullptr;
		}
	}


  // Subscript
  inline const T operator[] (int i) const {
		assert(i >= 0);
		assert(m_size > 0);
		assert(i < m_size);
		assert(m_usraddr != nullptr);

    T* base = (T *) m_usraddr;
    return (T) base[i];
  }


  // Subscript
  inline T& operator[] (int i) {
		assert(i >= 0);
		assert(m_size > 0);
		assert(i < m_size);
		assert(m_usraddr != nullptr);

    T* base = (T *) m_usraddr;
    return (T&) base[i];
  }

private:
	BufferObject &m_heap;
	uint8_t *m_usraddr = nullptr;  // Start of the heap in main memory, as seen by the CPU
	uint32_t m_phyaddr = 0;        // Starting index of memory in GPU space
	uint32_t m_size = 0;           // Number of contained elements (not memory size!)
	bool     m_is_heap_view = false;
};

}  // namespace QPULib

#ifdef PREVIOUS_VERSION

#include "../Support/debug.h"



#include "../Target/SharedArray.h"

#ifndef QPU_MODE		// NOTE: QPU_MODE and EMULATION_MODE exclude each other

namespace QPULib {
// Use main memory SharedArray directly
template <typename T>
using SharedArray=Target::SharedArray<T>;
}  // QPULib

#else  // QPU_MODE
#include "../vc4/SharedArray.h"

#ifdef USE_V3D_BUFFERS
#include "../v3d/SharedArray.h"
#endif

#endif  // QPU_MODE


#ifdef QPU_MODE

namespace QPULib {

/**
 * Combined SharedArray definition which both EMU and QPU to be used
 * in a given program.
 *
 * ----------------------------------------------------------------------------
 * NOTE
 * ====
 *
 * * Following scenario's must be taken into account:
 *
 *    - non-pi, EMU -> main mem
 *    - pi, vc4     -> main + gpu mem  (main for emulator)
 *    - pi, vc6     -> main mem
 */
template <typename T>
class SharedArray {
public:
	SharedArray() {}

  SharedArray(uint32_t n, BufferType buftype = HeapBuffer) :
		m_buftype(buftype),
		m_size(n),
		m_main_array((buftype == HeapBuffer)?n:0),
		m_gpu_array((buftype == Vc4Buffer) ?n:0)
	{
		assert(m_size > 0);
	}


  uint32_t size() {
		return m_size;
	}


  uint32_t getAddress() {
		uint32_t ret;

		switch(m_buftype) {
		case HeapBuffer:
			ret = m_main_array.getAddress();
			break;
		case Vc4Buffer:
			ret = m_gpu_array.getAddress();
			break;
#ifdef USE_V3D_BUFFERS
		case V3dBuffer:
			ret = m_v3d_array.getPhyAddr();  // TODO Not sure if this is correct; use mem adress instead?
			break;
#endif
		}

		return ret;
  }


	void setType(BufferType buftype) {
		switch(buftype) {
		case HeapBuffer:
			switch(m_buftype) {
			case HeapBuffer:
				// Nothing to do
				break;
			case Vc4Buffer:
				moveTo(m_gpu_array, m_main_array);
				break;
#ifdef USE_V3D_BUFFERS
			case V3dBuffer:
				moveTo(m_v3d_array, m_main_array);
				break;
#endif
			}
			break;
		case Vc4Buffer:
			switch(m_buftype) {
			case HeapBuffer:
				moveTo(m_main_array, m_gpu_array);
				break;
			case Vc4Buffer:
				// Nothing to do
				break;
#ifdef USE_V3D_BUFFERS
			case V3dBuffer:
				moveTo(m_v3d_array, m_gpu_array);
				break;
#endif
			}
			break;
#ifdef USE_V3D_BUFFERS
		case V3dBuffer:
			switch(m_buftype) {
			case HeapBuffer:
				moveTo(m_main_array, m_v3d_array);
				break;
			case Vc4Buffer:
				moveTo(m_gpu_array, m_v3d_array);
				break;
			case V3dBuffer:
				// Nothing to do
				break;
			}
			break;
#endif
		}

		m_buftype = buftype;
	}


  void alloc(uint32_t n) {
		assert(n > 0);
		assert(m_size == 0);

		switch(m_buftype) {
		case HeapBuffer:
			assert(m_main_array.size() == 0);
			m_main_array.alloc(n);
			break;
		case Vc4Buffer:
			breakpoint
			assert(m_gpu_array.size() == 0);
			m_gpu_array.alloc(n);
			break;
#ifdef USE_V3D_BUFFERS
		case V3dBuffer:
			breakpoint
			assert(m_v3d_array.size() == 0);
			m_v3d_array.alloc(n);
#endif
			break;
		}

		m_size = n;
  }


  T& operator[] (int i) {
		assert(i >= 0 && i < m_size);

		T *ret = nullptr;

		switch(m_buftype) {
		case HeapBuffer:
			ret = &m_main_array[i];
			break;
		case Vc4Buffer:
			ret = &m_gpu_array[i];
			break;
#ifdef USE_V3D_BUFFERS
		case V3dBuffer:
			ret = &m_v3d_array[i];
			break;
#endif
		}

		assert(ret != nullptr);
		return *ret;
	}

	operator vc4::SharedArray<T> &() {
		setType(Vc4Buffer);	
		return m_gpu_array;
	}


	operator Target::SharedArray<T> &() {
		breakpoint
		setType(HeapBuffer);	
		return m_main_array;
	}


#ifdef USE_V3D_BUFFERS
	operator v3d::SharedArray<T> &() {
		breakpoint
		setType(V3dBuffer);	
		return m_v3d_array;
	}
#endif


private:
  // Disallow assignment & copying
  void operator=(SharedArray<T> a);
  void operator=(SharedArray<T>& a);
  SharedArray(const SharedArray<T>& a);

	BufferType m_buftype = HeapBuffer;
  uint32_t m_size = 0;

	Target::SharedArray<T>    m_main_array;
	vc4::SharedArray<T> m_gpu_array;
#ifdef USE_V3D_BUFFERS
	v3d::SharedArray<T>       m_v3d_array;
#endif


	template<typename Src, typename Dst>
	void moveTo(Src &src, Dst &dst) {
		if (src.size() == 0) {
			breakpoint
			// Nothing to transfer
			assert(dst.size() == 0);
			return;
		}

		// Transfer data to other buffer
		assert(dst.size() == 0);
		dst.alloc(src.size());

		for (int n = 0; n < src.size(); ++n) {
			dst[n] = src[n];
		}

		src.dealloc();
	}
};


}  // QPULib

#endif  // PREVIOUS_VERSION

#endif  // QPU_MODE
#endif  // _QPULIB_COMMON_SHAREDARRAY_H_
