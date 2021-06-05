#include "KernelDriver.h"
#include <iostream>
#include <memory>
#include "Driver.h"
#include "Source/Translate.h"
#include "Target/SmallLiteral.h"  // decodeSmallLit()
#include "Target/RemoveLabels.h"
#include "instr/Snippets.h"
#include "Support/basics.h"
#include "Support/Timer.h"
#include "SourceTranslate.h"
#include "instr/Encode.h"
#include "Support/basics.h"
#include <bits/stdc++.h>  // set_intersection

namespace V3DLib {

// WEIRDNESS, due to changes, this file did not compile because it suddenly couldn't find
// the relevant overload of operator <<.
// Adding this solved it. HUH??
// Also: the << definitions in `basics.h` DID get picked up; the std::string versions did not.
using ::operator<<; // C++ weirdness

namespace v3d {

using namespace V3DLib::v3d::instr;
using Instructions = V3DLib::v3d::Instructions;

namespace {

// Set intersection
inline std::set<Reg> operator&(std::set<Reg> const &lhs, std::set<Reg> const &rhs) {
  std::set<Reg> ret;

  set_intersection(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                 std::inserter(ret, ret.begin()));

  return ret;
}

std::vector<std::string> local_errors;


/**
 * Translate imm index value from vc4 to v3d
 */
SmallImm encodeSmallImm(RegOrImm const &src_reg) {
  assert(src_reg.is_imm());

  Word w = decodeSmallLit(src_reg.imm().val);
  SmallImm ret(w.intVal);
  return ret;
}


/**
 * For v3d, the QPU and ELEM num are not special registers but instructions.
 *
 * In order to not disturb the code translation too much, they are derived from the target instructions:
 *
 *    mov(ACC0, QPU_ID)   // vc4: QPU_NUM  or SPECIAL_QPU_NUM
 *    mov(ACC0, ELEM_ID)  // vc4: ELEM_NUM or SPECIAL_ELEM_NUM
 *
 * This is the **only** operation in which they can be used.
 * This function checks for proper usage.
 * These special cases get translated to `tidx(r0)` and `eidx(r0)` respectively, as a special case
 * for A_BOR.
 *
 * If the check fails, a fatal exception is thrown.
 *
 * ==================================================================================================
 *
 * * Both these instructions use r0 here; this might produce conflicts with other instructions
 *   I haven't found a decent way yet to compensate for this.
 */
void checkSpecialIndex(V3DLib::Instr const &src_instr) {
  if (src_instr.tag != ALU) {
    return;  // no problem here
  }

  auto srca = src_instr.ALU.srcA;
  auto srcb = src_instr.ALU.srcB;

  bool a_is_elem_num = (srca.is_reg() && srca.reg().tag == SPECIAL && srca.reg().regId == SPECIAL_ELEM_NUM);
  bool a_is_qpu_num  = (srca.is_reg() && srca.reg().tag == SPECIAL && srca.reg().regId == SPECIAL_QPU_NUM);
  bool b_is_elem_num = (srcb.is_reg() && srcb.reg().tag == SPECIAL && srcb.reg().regId == SPECIAL_ELEM_NUM);
  bool b_is_qpu_num  = (srcb.is_reg() && srcb.reg().tag == SPECIAL && srcb.reg().regId == SPECIAL_QPU_NUM);
  bool a_is_special  = a_is_elem_num || a_is_qpu_num;
  bool b_is_special  = b_is_elem_num || b_is_qpu_num;

  if (!a_is_special && !b_is_special) {
    return;  // Nothing to do
  }

  if (src_instr.ALU.op.value() != ALUOp::A_BOR) {
    // All other instructions disallowed
    fatal("For v3d, special registers QPU_NUM and ELEM_NUM can only be used in a move instruction");
    return;
  }

  assertq((a_is_special && b_is_special), "src a and src b must both be special for QPU and ELEM nums");
  assertq(srca == srcb, "checkSpecialIndex(): src a and b must be the same if they are both special num's");
}


/**
 * Pre: `checkSpecialIndex()` has been called
 */
bool is_special_index(V3DLib::Instr const &src_instr, Special index ) {
  assert(index == SPECIAL_ELEM_NUM || index == SPECIAL_QPU_NUM);

  if (src_instr.tag != ALU) {
    return false;
  }

  if (src_instr.ALU.op.value() != ALUOp::A_BOR) {
    return false;
  }

  auto srca = src_instr.ALU.srcA;
  auto srcb = src_instr.ALU.srcB;
  bool a_is_special = (srca.is_reg() && srca.reg().tag == SPECIAL && srca.reg().regId == index);
  bool b_is_special = (srcb.is_reg() && srcb.reg().tag == SPECIAL && srcb.reg().regId == index);

  return (a_is_special && b_is_special);
}


void set_cond_tag(AssignCond cond, Instructions &ret) {
  for (auto &instr : ret) {
    instr.set_cond_tag(cond);
  }
}


bool translateOpcode(V3DLib::Instr const &src_instr, Instructions &ret) {
  bool did_something = true;

  auto reg_a = src_instr.ALU.srcA;
  auto reg_b = src_instr.ALU.srcB;

  auto dst_reg = encodeDestReg(src_instr);

  if (dst_reg && reg_a.is_reg() && reg_b.is_reg()) {
    checkSpecialIndex(src_instr);
    if (is_special_index(src_instr, SPECIAL_QPU_NUM)) {
      ret << tidx(*dst_reg);
    } else if (is_special_index(src_instr, SPECIAL_ELEM_NUM)) {
      ret << eidx(*dst_reg);
    } else if (reg_a.reg().tag == NONE && reg_b.reg().tag == NONE) {
      assert(src_instr.ALU.op.noOperands());

      switch (src_instr.ALU.op.value()) {
        case ALUOp::A_TIDX:  ret << tidx(*dst_reg); break;
        case ALUOp::A_EIDX:  ret << eidx(*dst_reg); break;
        default:
          assertq("unimplemented op, input none", true);
          did_something = false;
        break;
      }
    } else if (reg_a.reg().tag != NONE && reg_b.reg().tag == NONE) {
      // 1 input
      auto src_a = encodeSrcReg(reg_a.reg());
      assert(src_a);

      switch (src_instr.ALU.op.value()) {
        case ALUOp::A_FFLOOR:  ret << ffloor(*dst_reg, *src_a); break;
        case ALUOp::A_FSIN:    ret << fsin(*dst_reg, *src_a);    break;
        default:
          assertq("unimplemented op, input reg", true);
          did_something = false;
        break;
      }
    } else {
      auto src_a = encodeSrcReg(reg_a.reg());
      auto src_b = encodeSrcReg(reg_b.reg());
      assert(src_a && src_b);

      switch (src_instr.ALU.op.value()) {
        case ALUOp::A_ASR:   ret << asr(*dst_reg, *src_a, *src_b);          break;
        case ALUOp::A_ADD:   ret << add(*dst_reg, *src_a, *src_b);          break;
        case ALUOp::A_SUB:   ret << sub(*dst_reg, *src_a, *src_b);          break;
        case ALUOp::A_BOR:   ret << bor(*dst_reg, *src_a, *src_b);          break;
        case ALUOp::A_BAND:  ret << band(*dst_reg, *src_a, *src_b);         break;
        case ALUOp::M_FMUL:  ret << nop().fmul(*dst_reg, *src_a, *src_b);   break;
        case ALUOp::M_MUL24: ret << nop().smul24(*dst_reg, *src_a, *src_b); break;
        case ALUOp::A_FSUB:  ret << fsub(*dst_reg, *src_a, *src_b);         break;
        case ALUOp::A_FADD:  ret << fadd(*dst_reg, *src_a, *src_b);         break;
        case ALUOp::A_MIN:   ret << min(*dst_reg, *src_a, *src_b);          break;
        case ALUOp::A_MAX:   ret << max(*dst_reg, *src_a, *src_b);          break;
        default:
          assertq("unimplemented op, input reg, reg", true);
          did_something = false;
        break;
      }
    }
  } else if (dst_reg && reg_a.is_reg() && reg_b.is_imm()) {
    auto src_a = encodeSrcReg(reg_a.reg());
    assert(src_a);
    SmallImm imm = encodeSmallImm(reg_b);

    switch (src_instr.ALU.op.value()) {
      case ALUOp::A_SHL:   ret << shl(*dst_reg, *src_a, imm);          break;
      case ALUOp::A_SHR:   ret << shr(*dst_reg, *src_a, imm);          break;
      case ALUOp::A_ASR:   ret << asr(*dst_reg, *src_a, imm);          break;
      case ALUOp::A_BAND:  ret << band(*dst_reg, *src_a, imm);         break;
      case ALUOp::A_SUB:   ret << sub(*dst_reg, *src_a, imm);          break;
      case ALUOp::A_ADD:   ret << add(*dst_reg, *src_a, imm);          break;
      case ALUOp::A_FADD:  ret << fadd(*dst_reg, *src_a, imm);         break;
      case ALUOp::A_FSUB:  ret << fsub(*dst_reg, *src_a, imm);         break;
      case ALUOp::M_FMUL:  ret << nop().fmul(*dst_reg, *src_a, imm);   break;
      case ALUOp::M_MUL24: ret << nop().smul24(*dst_reg, *src_a, imm); break;
      case ALUOp::A_ItoF:  ret << itof(*dst_reg, *src_a, imm);         break;
      case ALUOp::A_FtoI:  ret << ftoi(*dst_reg, *src_a, imm);         break;
      case ALUOp::A_BXOR:  ret << bxor(*dst_reg, *src_a, imm);         break;
      default:
        assertq("unimplemented op, input reg, imm", true);
        did_something = false;
      break;
    }
  } else if (dst_reg && reg_a.is_imm() && reg_b.is_reg()) {
    SmallImm imm = encodeSmallImm(reg_a);
    auto src_b   = encodeSrcReg(reg_b.reg());
    assert(src_b);

    switch (src_instr.ALU.op.value()) {
      case ALUOp::A_SHL:   ret << shl(*dst_reg, imm, *src_b);          break;
      case ALUOp::M_MUL24: ret << nop().smul24(*dst_reg, imm, *src_b); break;
      case ALUOp::M_FMUL:  ret << nop().fmul(*dst_reg, imm, *src_b);   break;
      case ALUOp::A_FSUB:  ret << fsub(*dst_reg, imm, *src_b);         break;
      case ALUOp::A_SUB:   ret << sub(*dst_reg, imm, *src_b);          break;
      case ALUOp::A_ADD:   ret << add(*dst_reg, imm, *src_b);          break;
      case ALUOp::A_FADD:  ret << fadd(*dst_reg, imm, *src_b);         break;
      default:
        assertq("unimplemented op, input imm, reg", true);
        did_something = false;
      break;
    }
  } else if (dst_reg && reg_a.is_imm() && reg_b.is_imm()) {
    SmallImm imm_a = encodeSmallImm(reg_a);
    SmallImm imm_b = encodeSmallImm(reg_b);

    switch (src_instr.ALU.op.value()) {
      case ALUOp::A_BOR:   ret << bor(*dst_reg, imm_a, imm_b);          break;
      default:
        assertq("unimplemented op, input imm, imm", true);
        did_something = false;
      break;
    }
  } else {
    assertq("Unhandled combination of inputs/output", true);
    did_something = false;
  }

  return did_something;
}


void handle_condition_tags(V3DLib::Instr const &src_instr, Instructions &ret) {
  auto &cond = src_instr.ALU.cond;

  // src_instr.ALU.cond.tag has 3 possible values: NEVER, ALWAYS, FLAG
  assertq(cond.tag != AssignCond::Tag::NEVER, "NEVER encountered in ALU.cond.tag", true);          // Not expecting it
  assertq(cond.tag == AssignCond::Tag::FLAG || cond.is_always(), "Really expecting FLAG here", true); // Pedantry

  auto const &setCond = src_instr.setCond();

  if (!setCond.flags_set()) {
    // use flag as run condition for current instruction(s)
    set_cond_tag(cond, ret);
    return;
  }

  //
  // Set a condition flag with current instruction
  //
  // The condition is only set for the last in the list.
  // Any preceding instructions are assumed to be for calculating the condition
  //
  assertq(cond.is_always(), "Currently expecting only ALWAYS here", true);

  bool is_final_where_cond = src_instr.comment().find("where condition final") != src_instr.comment().npos;

  if (!is_final_where_cond) {
    ret.back().set_push_tag(setCond);
    return;
  }

  //
  // Process final where condition
  // In this case, condition flag must be pushed for both add and mul alu.
  //

  {
    std::string msg = "handle_condition_tags(): detected final where condition: '";
    msg << src_instr.dump() << "'\n";
    msg << "v3d: " << ret.back().mnemonic() << "'\n";
    debug(msg);
  }

  ret.back().set_push_tag(setCond);

  //
  // Add and mul alus are set to have the same destination, normally this is risky!
  // However, in this case the dst is a dummy (assumption? check), so if the hardware
  // allows it, this is ok.
  //
  Instructions tmp;
  assertq(translateOpcode(src_instr, tmp), "translateOpcode() failed");
  assert(tmp.size() == 1);
  Instr &tmp_instr = tmp[0];
  if(!tmp_instr.alu_mul_set(src_instr.ALU, encodeDestReg(src_instr))) {
    assert(false);
  }

  tmp_instr.set_push_tag(setCond);

  {
    std::string msg = "handle_condition_tags() ";
    msg << "v3d final: " << ret.back().mnemonic() << "'\n";
    msg << "v3d tmp: " << tmp_instr.mnemonic() << "'\n";
    debug(msg);
  }
}


/**
 * @return true if rotate handled, false otherwise
 */
bool translateRotate(V3DLib::Instr const &instr, Instructions &ret) {
  if (!instr.ALU.op.isRot()) {
    return false;
  }

  // dest is location where r1 (result of rotate) must be stored 
  auto dst_reg = encodeDestReg(instr);
  assert(dst_reg);
  assertq(dst_reg->to_mux() != V3D_QPU_MUX_R1, "Rotate can not have destination register R1", true);

  auto reg_a = instr.ALU.srcA;
  auto src_a = encodeSrcReg(reg_a.reg());
  auto reg_b = instr.ALU.srcB;                  // reg b is either r5 or small imm

  if (src_a->to_mux() != V3D_QPU_MUX_R0) {
    ret << mov(r0, *src_a).comment("moving param 2 of rotate to r0. WARNING: r0 might already be in use, check!");
  }


  // TODO: the Target source step already adds a nop.
  //       With the addition of previous mov to r0, the 'other' nop becomes useless, remove that one for v3d.
  ret << nop().comment("NOP required for rotate");

  if (reg_b.is_reg()) {
    breakpoint

    assert(instr.ALU.srcB.reg().tag == ACC && instr.ALU.srcB.reg().regId == 5);  // reg b must be r5
    auto src_b = encodeSrcReg(reg_b.reg());

    ret << rotate(r1, r0, *src_b);

  } else {
    SmallImm imm = encodeSmallImm(reg_b);  // Legal values small imm tested in rotate()
    ret << rotate(r1, r0, imm);
  }

  ret << bor(*dst_reg, r1, r1);

  return true;
}


/**
 * Convert powers of 2 of direct small immediates
 */
bool convert_int_powers(Instructions &output, int in_value) {
  if (in_value < 0)  return false;  // only positive values for now
  if (in_value < 16) return false;  // don't bother with values within range

  int value = in_value;
  int left_shift = 0;

  while (value != 0 && (value & 1) == 0) {
    left_shift++;
    value >>= 1;
  }

  if (left_shift == 0) return false;

  int rep_value;
  if (!SmallImm::int_to_opcode_value(value, rep_value)) return false;

  SmallImm imm(rep_value);

  std::string cmt;
  cmt << "Load immediate " << in_value;

  Instructions ret;
  ret << mov(r0, imm).comment(cmt);
  ret << shl(r0, r0, SmallImm(left_shift));

  output << ret;
  return true;
}


/**
 * Blunt tool for converting all int's.
 *
 * **NOTE:** uses r0, r1 and r2 internally, register conflict possible
 *
 * @param output    output parameter, sequence of instructions to
 *                  add generated code to
 * @param in_value  value to encode
 *
 * @return  true if conversion successful, false otherwise
 */
bool encode_int_immediate(Instructions &output, int in_value) {
  Instructions ret;
  uint32_t value = (uint32_t) in_value;
  uint32_t nibbles[8];  // was 7 (idiot)

  for (int i = 0; i < 8; ++i) {
    nibbles[i] = (value  >> (4*i)) & 0xf;
  }

  bool did_first = false;
  for (int i = 7; i >= 0; --i) {
    if (nibbles[i] == 0) continue;

    SmallImm imm(nibbles[i]);

    // r0 is used as temp value,
    // result is in r1

    if (!did_first) {
      ret << mov(r1, imm);  // TODO Gives segfault on p4-3 (arm32)
                            //      No clue why, works fine on silke (arm64) and pi3

      if (i > 0) {
        if (convert_int_powers(ret, 4*i)) {
          // r0 now contains value for left shift
          ret << shl(r1, r1, r0);
        } else {
          ret << shl(r1, r1, SmallImm(4*i));
        }
      }
      did_first = true;
    } else {
      if (i > 0) {
        if (convert_int_powers(ret, 4*i)) {
          // r0 now contains value for left shift
          //ret << mov(r2, imm);
          //ret << shl(r0, r2, r0);
          ret << shl(r0, imm, r0);
        } else {
          ret << mov(r0, imm);
          ret << shl(r0, r0, SmallImm(4*i));
        }

        ret << bor(r1, r1, r0);
      } else {
        ret << bor(r1, r1, imm);
      }
    }
  }

  if (ret.empty()) return false;  // Not expected, but you never know

  std::string cmt;
  cmt << "Load immediate " << in_value;
  ret.front().comment(cmt);

  std::string cmt2;
  cmt2 << "End load immediate " << in_value;
  ret.back().comment(cmt2);

  output << ret;
  return true;
}


bool encode_int(Instructions &ret, std::unique_ptr<Location> &dst, int value) {
  bool success = true;
  int rep_value;

  if (SmallImm::int_to_opcode_value(value, rep_value)) {  // direct translation
    SmallImm imm(rep_value);
    ret << mov(*dst, imm);
  } else if (convert_int_powers(ret, value)) {            // powers of 2 of basic small int's 
    ret << mov(*dst, r0);
  } else if (encode_int_immediate(ret, value)) {          // Use full blunt conversion (heavy but always works)
    ret << mov(*dst, r1);
  } else {
    success = false;                                      // Conversion failed
  }

  return success;
}


bool encode_float(Instructions &ret, std::unique_ptr<Location> &dst, float value) {
  bool success = true;
  int rep_value;

  if (value < 0 && SmallImm::float_to_opcode_value(-value, rep_value)) {
    ret << nop().fmov(*dst, rep_value)
        << fsub(*dst, 0, *dst);                   // Works because float zero is 0x0
  } else if (SmallImm::float_to_opcode_value(value, rep_value)) {
    ret << nop().fmov(*dst, rep_value);
  } else if ((value == (float) ((int) value))) {  // Special case: float is encoded int, no fraction
    int int_value = (int) value;
    SmallImm dummy(0);                            // TODO why need this???

    if (encode_int(ret, dst, int_value)) {
      ret  << itof(*dst, *dst, dummy);
    } else {
      assertq("Full-int float conversion failed", true);
      success = false;
    }
  } else {
    // Do the full blunt int conversion
    int int_value = *((int *) &value);
    if (encode_int_immediate(ret, int_value)) {
      ret << mov(*dst, r1);                       // Result is int but will be handled as float downstream
    } else {
      success = false;
    }
  }

  return success;
}


Instructions encodeLoadImmediate(V3DLib::Instr const full_instr) {
  assert(full_instr.tag == LI);
  auto &instr = full_instr.LI;
  auto dst = encodeDestReg(full_instr);

  Instructions ret;

  std::string err_label;
  std::string err_value;

  switch (instr.imm.tag()) {
  case Imm::IMM_INT32: {
    int value = instr.imm.intVal();

    if (!encode_int(ret, dst, value)) {
      // Conversion failed, output error
      err_label = "int";
      err_value = std::to_string(value);
    }
  }
  break;

  case Imm::IMM_FLOAT32: {
    float value = instr.imm.floatVal();

    if (!encode_float(ret, dst, value)) {
      // Conversion failed, output error
      err_label = "float";
      err_value = std::to_string(value);
    }
  }
  break;

  case Imm::IMM_MASK:
    debug_break("encodeLoadImmediate(): IMM_MASK not handled");
  break;

  default:
    debug_break("encodeLoadImmediate(): unknown tag value");
  break;
  }

  if (!err_value.empty()) {
    assert(!err_label.empty());

    std::string str = "LI: Can't handle ";
    str += err_label + " value '" + err_value;
    str += "' as small immediate";

    breakpoint
    local_errors << str;
    ret << nop();
    ret.back().comment(str);
  }


  if (full_instr.setCond().flags_set()) {
    breakpoint;  // to check what flags need to be set - case not handled yet
  }

  set_cond_tag(instr.cond, ret);
  return ret;
}


Instructions encodeALUOp(V3DLib::Instr instr) {
  Instructions ret;

  if (instr.isUniformLoad()) {
    Reg dst_reg = instr.ALU.dest;
    uint8_t rf_addr = to_waddr(dst_reg);
    ret << nop().ldunifrf(rf(rf_addr));
   } else if (translateRotate(instr, ret)) {
    handle_condition_tags(instr, ret);
  } else if (translateOpcode(instr, ret)) {
    handle_condition_tags(instr, ret);
  } else {
    assertq(false, "Missing translate operation for ALU instruction", true);  // Something missing, check
  }

  assert(!ret.empty());
  return ret;
}


/**
 * Convert conditions from Target source to v3d
 *
 * Incoming conditions are vc4 only, the conditions don't exist on v3d.
 * They therefore need to be translated.
 */
void encodeBranchCondition(v3d::instr::Instr &dst_instr, V3DLib::BranchCond src_cond) {
  // TODO How to deal with:
  //
  //      dst_instr.na0();
  //      dst_instr.a0();

  if (src_cond.tag == COND_ALWAYS) {
    return;  // nothing to do
  } else if (src_cond.tag == COND_ALL) {
    switch (src_cond.flag) {
      case ZC:
      case NC:
        dst_instr.allna();
        break;
      case ZS:
      case NS:
        dst_instr.alla();
        break;
      default:
        debug_break("Unknown branch condition under COND_ALL");  // Warn me if this happens
    }
  } else if (src_cond.tag == COND_ANY) {
    switch (src_cond.flag) {
      case ZC:
      case NC:
        dst_instr.anyna();  // TODO: verify
        break;
      case ZS:
      case NS:
        dst_instr.anya();  // TODO: verify
        break;
      default:
        debug_break("Unknown branch condition under COND_ANY");  // Warn me if this happens
    }
  } else {
    debug_break("Branch condition not COND_ALL or COND_ANY");  // Warn me if this happens
  }
}


/**
 * Create a branch instruction, including any branch conditions,
 * from Target source instruction.
 */
v3d::instr::Instr encodeBranchLabel(V3DLib::Instr src_instr) {
  assert(src_instr.tag == BRL);
  auto &instr = src_instr.BRL;

  // Prepare as branch without offset but with label
  auto dst_instr = branch(0, true);
  dst_instr.label(instr.label);
  encodeBranchCondition(dst_instr, instr.cond);

  return dst_instr;
}


/**
 * Convert intermediate instruction into core instruction.
 *
 * **Pre:** All instructions not meant for v3d are detected beforehand and flagged as error.
 */
Instructions encodeInstr(V3DLib::Instr instr) {
  Instructions ret;

  // Encode core instruction
  switch (instr.tag) {
    //
    // Unhandled tags - ignored or should have been handled beforehand
    //
    case BR:
      assertq(false, "Not expecting BR any more, branch creation now goes with BRL", true);
    break;

    case INIT_BEGIN:
    case INIT_END:
    case END:         // vc4 end program marker
      assertq(false, "Not expecting INIT or END tag here", true);
    break;

    //
    // Label handling
    //
    case LAB: {
      // create a label meta-instruction
      Instr n;
      n.is_label(true);
      n.label(instr.label());
      
      ret << n;
    }
    break;

    case BRL: {
      // Create branch instruction with label
      ret << encodeBranchLabel(instr);
    }
    break;

    //
    // Handled tags
    //
    case LI:           ret << encodeLoadImmediate(instr); break;
    case ALU:          ret << encodeALUOp(instr);         break;
    case TMU0_TO_ACC4: ret << nop().ldtmu(r4);            break;
    case NO_OP:        ret << nop();                      break;
    case TMUWT:        ret << tmuwt();                    break;

    default:
      fatal("v3d: missing case in encodeInstr");
  }

  assert(!ret.empty());

  if (!ret.empty()) {
    ret.front().transfer_comments(instr);
  }

  return ret;
}


/**
 * This is where standard initialization code can be added.
 *
 * Called;
 * - after code for loading uniforms has been encoded
 * - any other target initialization code has been added
 * - before the encoding of the main body.
 *
 * Serious consideration: Any regfile registers used in the generated code here,
 * have not participated in the liveness determination. This may lead to screwed up variable assignments.
 * **Keep this in mind!**
 */
Instructions encode_init() {
  Instructions ret;
  ret << instr::enable_tmu_read();

  return ret;
}


#ifdef DEBUG

/**
 * Check assumption: uniform loads are always at the top of the instruction list.
 */
bool checkUniformAtTop(V3DLib::Instr::List const &instrs) {
  bool doing_top = true;

  for (int i = 0; i < instrs.size(); i++) {
    V3DLib::Instr instr = instrs[i];
    if (doing_top) {
      if (instr.isUniformLoad()) {
        continue;  // as expected
      }

      doing_top = false;
    } else {
      if (!instr.isUniformLoad()) {
        continue;  // as expected
      }

      return false;  // Encountered uniform NOT at the top of the instruction list
    }
  }

  return true;
}

#endif  // DEBUG

bool uses_mul_alu(V3DLib::Instr const &instr) {
  if (instr.tag != ALU) return false;

  switch (instr.ALU.op.value()) {
    case ALUOp::M_FMUL:
    case ALUOp::M_MUL24:
    case ALUOp::M_ROTATE:
      return true;

    default: break;
  }

  return false;
}


bool uses_add_alu(V3DLib::Instr const &instr) {
  if (instr.tag != ALU) return false;
  return !uses_mul_alu(instr);
}


bool can_use_mul_alu(V3DLib::Instr const &instr) {
  if (instr.tag != ALU) return false;
  if (uses_mul_alu(instr)) return true;

  return can_convert_to_mul_instruction(instr.ALU);
}


/**
 * Combination only possible if instructions not both add ALU or both mul ALU
 */
bool valid_combine_pair(V3DLib::Instr const &instr, V3DLib::Instr const &next_instr, bool &do_converse) {
  if (uses_add_alu(instr) && can_use_mul_alu(next_instr)) return true;

  if (can_use_mul_alu(instr) && uses_add_alu(next_instr)) {
    do_converse = true;
    return true;
  }

  return false;
}


bool valid_combine_pair(V3DLib::Instr const &instr, V3DLib::Instr const &next_instr) {
  bool do_converse;
  return valid_combine_pair(instr, next_instr, do_converse);
}


/**
 * Check if two instructions can be combined
 *
 * Can combine if there are at most two different source values for both instructions.
 * This applies to rf-registers only; the number of accumulators used is free.
 *
 * @return true if can combine, false otherwise
 */
bool can_combine(V3DLib::Instr const &instr, V3DLib::Instr const &next_instr) {
  if (instr.tag != InstrTag::ALU) return false; 
  if (next_instr.tag != InstrTag::ALU) return false; 
  if (!valid_combine_pair(instr, next_instr)) return false;

  auto const &ALU      = instr.ALU;
  auto const &next_ALU = next_instr.ALU;

  // Skip special instructions
  switch(ALU.op.value()) {
  case ALUOp::A_EIDX:  // TODO remove, should work
  case ALUOp::A_FSIN:
    return false;
  default:
    break;
  }

  switch(next_ALU.op.value()) {
  case ALUOp::A_EIDX:  // TODO remove, should work
  case ALUOp::A_FSIN:
    return false;
  default:
    break;
  }


  // Not expecting following
  assert(!ALU.srcA.is_transient());
  //works for now: assert(!ALU.srcB.is_transient());
  //assert(!ALU.dest.is_transient());  TODO
  assert(!next_ALU.srcA.is_transient());
  //works for now: assertq(!next_ALU.srcB.is_transient(), "oops", true);
  //assert(!next_ALU.dest.is_transient()); TODO

  //
  // Two immediate values are only possible if both instructions have the same immediate
  //
  assert(!(ALU.srcA.is_imm() && ALU.srcB.is_imm() && ALU.srcA.imm() != ALU.srcB.imm()));
  bool has_imm = (ALU.srcA.is_imm() || ALU.srcB.is_imm());
  V3DLib::SmallImm imm;
  if (has_imm) {
    imm = ALU.srcA.is_imm()?ALU.srcA.imm():ALU.srcB.imm();
  }

  assert(!(next_ALU.srcA.is_imm() && next_ALU.srcB.is_imm() && next_ALU.srcA.imm() != next_ALU.srcB.imm()));
  bool next_has_imm = (next_ALU.srcA.is_imm() || next_ALU.srcB.is_imm());
  V3DLib::SmallImm next_imm;
  if (next_has_imm) {
    next_imm = next_ALU.srcA.is_imm()?next_ALU.srcA.imm():next_ALU.srcB.imm();
  }

  // Can have only one immediate per instruction
  if (has_imm && next_has_imm) {
    if (imm != next_imm) return false;
  }


  int unique_src_count = 0;

  // An immediate counts as a source value
  if (has_imm || next_has_imm) {
    unique_src_count++;
  }


  // The number of used accumulators is free, only check RF registers
  auto src_regs  = instr.src_regs() + next_instr.src_regs();

/*
  auto intersect = instr.src_regs() & next_instr.src_regs();

  if (!intersect.empty()) {
breakpoint
    return false;
  }
*/

  // Specials can not be combined
  for (auto const &reg : src_regs) {
    if (reg.tag == RegTag::SPECIAL ) return false; 
  }

/*
  // Issue when or -> mov translation enabled, with tmua/tmud in same instruction, eg:
  //   or  tmud, rf13, rf13 ; mov tmua, rf17, rf17  (perhaps other way around??)
  //
  // This causes output to be unstable; after this, any kernel you run may or may
  // not output properly. You need to hard reboot and disable or -> mov to make it work again
  //
  // Using following code solves the instability, but or -> mov enabled still gives wrong output
  

  // Don't write to two specials in same instruction
  auto dst = instr.dst_reg();
  if (dst.tag == SPECIAL) {
    return false;
  }
  auto next_dst = next_instr.dst_reg();
  if (next_dst.tag == SPECIAL) {
    return false;
  }
*/

  // Count distinct number of rf-registers
  for (auto const &reg : src_regs) {
    if (reg.is_rf_reg()) ++unique_src_count;
  }

  if (unique_src_count > 2) { 
    return false;
  }

  // dst of instr should not be used in next_instr
  if ((instr.ALU.dest == next_instr.ALU.srcA)
   || (instr.ALU.dest == next_instr.ALU.srcB)) { 
    return false;
  }

  return true;
/*
  // instr and next_instr can not have same destination
  // NOTE: Yes, they can! Output for first instr is ignored (dummy value)
  return (instr.ALU.dest != next_instr.ALU.dest);
*/
}


/**
 * If possible, combine an add all instruction with a subsequent mul alu instruction
 *
 * Criteria are intentional extremely strict.
 * These will be relaxed when further cases for optimization are encountered.
 *
 * Note that index can change!
 *
 * @return true if two consecutive instructions combined, false otherwise
 */
bool handle_target_specials(Instructions &ret, V3DLib::Instr::List const &instrs, int &index) {
  if (index + 1 >= instrs.size()) return false;

  auto const &instr = instrs[index];
  auto const &next_instr = instrs[index + 1];

  // TODO: add and mul alus have separate condition fields, following too strict;
  //       See set_cond_tag(
  if (instr.assign_cond() != next_instr.assign_cond()) return false;
  if (instr.isCondAssign()) return false;

  if (!can_combine(instr, next_instr)) return false;

  bool do_converse;
  if (!valid_combine_pair(instr, next_instr, do_converse)) {
    assert(false);
  }

  auto const &add_instr = do_converse?next_instr:instr;
  auto const &mul_instr = do_converse?instr:next_instr;

  // Don't combine push tag; boolean logic relies on consecutive pushes
  if (add_instr.setCond().tag() != SetCond::NO_COND) return false;
  if (mul_instr.setCond().tag() != SetCond::NO_COND) return false;

  Instructions tmp;
  assertq(translateOpcode(add_instr, tmp), "translateOpcode() failed");
  assert(tmp.size() == 1);
  Instr &out_instr = tmp[0];

  if (!out_instr.alu_mul_set(mul_instr.ALU, encodeDestReg(mul_instr))) {
    std::string msg;
    msg << "Possible candidate for combine, do_converse = " << do_converse << ":\n"
        << "  instr     : " << instr.dump()      << "\n"
        << "  next_instr: " << next_instr.dump();
    debug(msg);
    return false;
  }

  out_instr.set_cond_tag(instr.assign_cond());
  out_instr.set_push_tag(instr.setCond());

  out_instr.comment(instr.comment());
  out_instr.comment(next_instr.comment());

  index++;
  ret << tmp;
  compile_data.num_instructions_combined++;
/*
  {
    std::string msg;
    msg << "handle_target_specials input, do_converse = " << do_converse << ":\n"
      << "  instr     : " << instr.dump()      << "\n"
      << "  next_instr: " << next_instr.dump();
    debug(msg);
  }

  {
    std::string msg;
    msg << "handle_target_specials combine result:\n"
      << "  " << out_instr.mnemonic(true);
    debug(msg);
  }
*/

  return true;
}


/**
 * Translate instructions from target to v3d
 */
void _encode(V3DLib::Instr::List const &instrs, Instructions &instructions) {
  assert(checkUniformAtTop(instrs));
  bool prev_was_init_begin = false;
  bool prev_was_init_end    = false;

  // Main loop
  for (int i = 0; i < instrs.size(); i++) {
    V3DLib::Instr instr = instrs[i];
    assertq(!instr.isZero(), "Zero instruction encountered", true);
    check_instruction_tag_for_platform(instr.tag, false);

    if (instr.tag == INIT_BEGIN) {
      prev_was_init_begin = true;
    } else if (instr.tag == INIT_END) {
      instructions << encode_init();
      prev_was_init_end = true;
    } else {
      Instructions ret;

      if (!handle_target_specials(ret, instrs, i)) {
        ret = v3d::encodeInstr(instr);
      }

      if (prev_was_init_begin) {
        ret.front().header("Init block");
        prev_was_init_begin = false;
      }

      if (prev_was_init_end) {
        ret.front().header("Main program");
        prev_was_init_end = false;
      }

      instructions << ret;
    }
  }

  instructions << sync_tmu()
               << end_program();
}


using Code       = SharedArray<uint64_t>;
using UniformArr = SharedArray<uint32_t>;


void invoke(int numQPUs, Code &codeMem, int qpuCodeMemOffset, IntList &params) {
#ifndef QPU_MODE
  assertq(false, "Cannot run v3d invoke(), QPU_MODE not enabled");
#else
  assert(codeMem.size() != 0);

  UniformArr unif(params.size() + 3);
  UniformArr done(1);
  done[0] = 0;

  // The first two slots in uniforms for vc4 are used for qpu number and num qpu's respectively
  // We do the same for v3d, so as not to screw up the logic too much.
  int offset = 0;

  unif[offset++] = 0;        // qpu number (id for current qpu) - 0 is for 1 QPU
  unif[offset++] = numQPUs;  // num qpu's running for this job

  for (int j = 0; j < params.size(); j++) {
    unif[offset++] = params[j];
  }

  // The last item is for the 'done' location;
  // Not sure if this is the correct slot to put it
  // TODO: scrutinize the python project for this
  unif[offset] = (uint32_t) done.getAddress();

  Driver drv;
  drv.add_bo(getBufferObject().getHandle());
  drv.execute(codeMem, &unif, numQPUs);
#endif  // QPU_MODE
}


}  // anon namespace


///////////////////////////////////////////////////////////////////////////////
// Class KernelDriver
///////////////////////////////////////////////////////////////////////////////

KernelDriver::KernelDriver() : V3DLib::KernelDriver(V3dBuffer), qpuCodeMem(code_bo)  {}


void KernelDriver::encode() {
  if (instructions.size() > 0) return;  // Don't bother if already encoded
  if (has_errors()) return;              // Don't do this if compile errors occured
  assert(!qpuCodeMem.allocated());

  // Encode target instructions
  _encode(m_targetCode, instructions);
  removeLabels(instructions);

  if (!local_errors.empty()) {
    breakpoint
  }

  errors << local_errors;
  local_errors.clear();
}


/**
 * Generate the opcodes for the currrent v3d instruction sequence
 *
 * The translation/removal of labels happens here somewhere 
 */
std::vector<uint64_t> KernelDriver::to_opcodes() {
  assert(instructions.size() > 0);

  std::vector<uint64_t> code;  // opcodes for v3d

  for (auto const &inst : instructions) {
    code << inst.code();
  }

  return code;
}


void KernelDriver::compile_intern() {
  //Timer t1("compile_intern", true);

  obtain_ast();

  //Timer t3("translate_stmt");
  translate_stmt(m_targetCode, m_body);  // performance hog 2 12/45s
  //t3.end();

  insertInitBlock(m_targetCode);
  add_init(m_targetCode);

  //Timer t5("compile_postprocess");
  compile_postprocess(m_targetCode);  // performance hog 1 31/45s
  //t5.end();

  encode();
}


void KernelDriver::allocate() {
  assert(!instructions.empty());

  // Assumption: code in a kernel, once allocated, doesn't change
  if (qpuCodeMem.allocated()) {
    assert(instructions.size() >= qpuCodeMem.size());  // Tentative check, not perfect
                                                       // actual opcode seq can be smaller due to removal labels
  } else {
    std::vector<uint64_t> code = to_opcodes();
    assert(!code.empty());

    // Allocate memory for the QPU code
    uint32_t size_in_bytes = (uint32_t) (sizeof(uint64_t)*code.size());
    code_bo.alloc(size_in_bytes);
    qpuCodeMem.alloc((uint32_t) code.size());
    qpuCodeMem.copyFrom(code);  // Copy kernel to code memory

    qpuCodeMemOffset = (int) size_in_bytes;
  }
}


void KernelDriver::invoke_intern(int numQPUs, IntList &params) {
  if (numQPUs != 1 && numQPUs != 8) {
    error("Num QPU's must be 1 or 8", true);
  }

  assertq(!has_errors(), "v3d kernels has errors, can not invoke");

  allocate();
  assert(qpuCodeMem.allocated());

  // Allocate memory for the parameters if not done already
  // TODO Not used in v3d, do we need this?
  unsigned numWords = (12*MAX_KERNEL_PARAMS + 12*2);
  if (paramMem.allocated()) {
    assert(paramMem.size() == numWords);
  } else {
    paramMem.alloc(numWords);
  }

  //
  // NOTE: it doesn't appear to be necessary to add the BO for the code to the
  //       used BO list in Driver (used in next call). All unit tests pass without
  //       calling Driver::add_bo() in next call.
  //       This is something to keep in mind; it might go awkwards later on.
  //
  v3d::invoke(numQPUs, qpuCodeMem, qpuCodeMemOffset, params);
}


void KernelDriver::emit_opcodes(FILE *f) {
  fprintf(f, "Opcodes for v3d\n");
  fprintf(f, "===============\n\n");

  if (instructions.empty()) {
    fprintf(f, "<No opcodes to print>\n");
  } else {
    for (auto const &instr : instructions) {
      fprintf(f, "%s\n", instr.mnemonic(true).c_str());
    }
  }

  fprintf(f, "\n");
  fflush(f);
}

}  // namespace v3d
}  // namespace V3DLib
