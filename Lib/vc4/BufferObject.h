#ifndef _V3DLIB_VC4_BUFFEROBJECT_H_
#define _V3DLIB_VC4_BUFFEROBJECT_H_

#ifdef QPU_MODE

#include "../Common/BufferObject.h"

namespace V3DLib {
namespace vc4 {

class BufferObject : public V3DLib::BufferObject {
public:
  ~BufferObject() { dealloc(); }

  static BufferObject &getHeap();

private:
  uint32_t handle = 0;

  void alloc_mem(uint32_t size_in_bytes) override;
  void dealloc();
};



}  // namespace vc4
}  // namespace V3DLib

#endif  // QPU_MODE

#endif  // _V3DLIB_VC4_BUFFEROBJECT_H_
