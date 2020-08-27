#include "BufferObject.h"
#include <cassert>
#include <memory>
#include <cstdio>
#include "../Support/basics.h"
#include "../Support/debug.h"

namespace QPULib {
namespace emu {

namespace {

// Defined as a smart ptr to avoid issues on program init
std::unique_ptr<BufferObject> emuHeap;

}


/**
 * Allocate heap if not already done so
 */
void BufferObject::alloc_heap(uint32_t size) {
	assert(arm_base  == nullptr);

	arm_base = new uint8_t [size];
	m_size = size;
}



void BufferObject::check_available(uint32_t n) {
	assert(arm_base != nullptr);

	if (m_offset + n >= m_size) {
		fatal("QPULib: heap overflow (increase heap size)\n");
	}
}


uint32_t BufferObject::alloc_array(uint32_t size_in_bytes, uint8_t *&array_start_address) {
	assert(arm_base != nullptr);
	check_available(size_in_bytes);

	uint32_t address = m_offset;
	array_start_address = arm_base + m_offset;
	m_offset += size_in_bytes;
	return address;
}


BufferObject &getHeap() {
	if (!emuHeap) {
		//debug("Allocating emu heap v3d\n");
		emuHeap.reset(new BufferObject(BufferObject::DEFAULT_HEAP_SIZE));
	}

	return *emuHeap;
}


}  // namespace emu
}  // namespace QPULib
