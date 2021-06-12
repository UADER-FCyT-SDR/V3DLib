#include "Source.h"
#include "Support/basics.h"
#include "Encode.h"

namespace V3DLib {
namespace v3d {
namespace instr {

Source::Source(V3DLib::RegOrImm const &rhs) :
  m_is_location(rhs.is_reg()),
  m_location(encodeSrcReg(rhs.reg())),
  m_small_imm(rhs.is_reg()?0:rhs.imm().val)
{
  assert(!m_is_location || (m_location && rhs.reg().tag != NONE));
}


Source:: Source(V3DLib::v3d::instr::Register const &rhs) :
  m_is_location(true),
  m_location(new Register(rhs))
{}


Source::Source(SmallImm const &rhs) :
  m_is_location(false),
  m_small_imm(rhs.val())
{}


Location const &Source::location() const {
  assert(m_is_location && m_location);
  return *m_location;
}


SmallImm const &Source::small_imm() const {
  assert(!m_is_location);
  return m_small_imm;
}



}  // namespace instr
}  // namespace v3d
}  // namespace V3DLib
