#include "Syntax.h"
#include "Source/BExpr.h"
#include "Support/basics.h"

namespace V3DLib {

using ::operator<<;  // C++ weirdness

namespace {

int globalLabelId = 0;  // Used for fresh label generation


Instr genInstr(ALUOp::Enum op, Reg dst, Reg srcA, Reg srcB) {
  Instr instr(ALU);
  instr.ALU.cond      = always;
  instr.ALU.dest      = dst;
  instr.ALU.srcA.tag  = REG;
  instr.ALU.srcA.reg  = srcA;
  instr.ALU.op        = ALUOp(op);
  instr.ALU.srcB.tag  = REG;
  instr.ALU.srcB.reg  = srcB;

  return instr;
}


Instr genInstr(ALUOp::Enum op, Reg dst, Reg srcA, int n) {
  Instr instr(ALU);
  instr.ALU.dest              = dst;
  instr.ALU.srcA.tag          = REG;
  instr.ALU.srcA.reg          = srcA;
  instr.ALU.op                = ALUOp(op);
  instr.ALU.srcB.tag          = IMM;
  instr.ALU.srcB.smallImm.tag = SMALL_IMM;
  instr.ALU.srcB.smallImm.val = n;

  return instr;
}


Instr genInstr(ALUOp::Enum op, Reg dst, int n, int m) {
  Instr instr(ALU);
  instr.ALU.dest              = dst;
  instr.ALU.srcA.tag          = IMM;
  instr.ALU.srcA.smallImm.tag = SMALL_IMM;
  instr.ALU.srcA.smallImm.val = n;
  instr.ALU.op                = ALUOp(op);
  instr.ALU.srcB.tag          = IMM;
  instr.ALU.srcB.smallImm.tag = SMALL_IMM;
  instr.ALU.srcB.smallImm.val = m;

  return instr;
}


/**
 * Function to negate a condition flag
 */
Flag negFlag(Flag flag) {
  switch(flag) {
    case ZS: return ZC;
    case ZC: return ZS;
    case NS: return NC;
    case NC: return NS;
  }

  // Not reachable
  assert(false);
	return ZS;
}


const char *pretty(Flag flag) {
  switch (flag) {
    case ZS: return "ZS";
    case ZC: return "ZC";
    case NS: return "NS";
    case NC: return "NC";
		default: assert(false); return "";
  }
}


/**
 * This uses acc4 as interim storage.
 * Also 2 NOP's required; TODO see this can be optimized
 */
Seq<Instr> sfu_function(Var dst, Var srcA, Reg const &sfu_reg, const char *label) {
	using namespace V3DLib::Target::instr;

	Instr nop;
	Seq<Instr> ret;

	ret << mov(sfu_reg, srcA)
	    << nop
	    << nop
	    << mov(dst, ACC4);

	std::string cmt = "SFU function ";
	cmt += label;
	ret.front().comment(cmt);

	return ret;
}

}  // anon namespace


///////////////////////////////////////////////////////////////////////////////
// Class BranchCond
///////////////////////////////////////////////////////////////////////////////

BranchCond BranchCond::negate() const {
	BranchCond ret;

  switch (tag) {
    case COND_NEVER:  ret.tag  = COND_ALWAYS; break;
    case COND_ALWAYS: ret.tag  = COND_NEVER;  break;
    case COND_ANY:    ret.tag  = COND_ALL; ret.flag = negFlag(flag); break;
    case COND_ALL:    ret.tag  = COND_ANY; ret.flag = negFlag(flag); break;
		default:
			assert(false);
			break;
  }

	return ret;
}


std::string BranchCond::to_string() const {
	std::string ret;

  switch (tag) {
    case COND_ALL:
      ret << "all(" << pretty(flag) << ")";
      break;
    case COND_ANY:
      ret << "any(" << pretty(flag) << ")";
      break;
    case COND_ALWAYS:
      ret = "always";
      break;
    case COND_NEVER:
      ret = "never";
      break;
  }

	return ret;
}


///////////////////////////////////////////////////////////////////////////////
// Class SetCond
///////////////////////////////////////////////////////////////////////////////

SetCond::SetCond(CmpOp const &cmp_op) {
	setOp(cmp_op);
}


void SetCond::setOp(CmpOp const &cmp_op) {
	m_tag = NO_COND;

	switch (cmp_op.op) {
		case EQ:  m_tag = Z; break;
		case NEQ: m_tag = Z; break;
		case LT:  m_tag = N; break;
		case GE:  m_tag = N; break;
		default:  assert(false);
	}
}


const char *SetCond::to_string() const {
	switch (m_tag) {
		case NO_COND: return "None";
		case Z:       return "Z";
		case N:       return "N";
		case C:       return "C";
		default:
			assert(false);
			return "<UNKNOWN>";
	}
}


std::string SetCond::pretty() const {
	std::string ret;
	if (flags_set()) {
		ret << "{sf-" << to_string() << "}";
	}
	return ret;
}


void SetCond::setFlag(Flag flag) {
	Tag set_tag = NO_COND;

	switch (flag) {
		case ZS: 
		case ZC: 
			set_tag = SetCond::Z;
			break;
		case NS: 
		case NC: 
			set_tag = SetCond::N;
			break;
		default:
			assert(false);  // Not expecting anything else right now
			break;
	}

	tag(set_tag);
}


///////////////////////////////////////////////////////////////////////////////
// Class AssignCond
///////////////////////////////////////////////////////////////////////////////

namespace {

std::string pretty(AssignCond cond) {
	using Tag = AssignCond::Tag;

  switch (cond.tag) {
    case Tag::ALWAYS: return "always";
    case Tag::NEVER:  return "never";
    case Tag::FLAG:   return pretty(cond.flag);
		default: assert(false); return "";
  }
}

}  // anon namespace


AssignCond always(AssignCond::Tag::ALWAYS);  // Is a global to reduce eyestrain in gdb
AssignCond never(AssignCond::Tag::NEVER);    // idem


AssignCond::AssignCond(CmpOp const &cmp_op) : tag(FLAG) {
	switch(cmp_op.op) {
		case EQ:  flag = ZS; break;
		case NEQ: flag = ZC; break;
		case LT:  flag = NS; break;
		case GE:  flag = NC; break;
		default:  assert(false);
	}
}


AssignCond AssignCond::negate() const {
	AssignCond ret = *this;

  switch (tag) {
    case NEVER:  ret.tag = ALWAYS; break;
    case ALWAYS: ret.tag = NEVER;  break;
    case FLAG:   ret.flag = negFlag(flag); break;
		default:
			assert(false);
			break;
  }

	return ret;
}


std::string AssignCond::to_string() const {
	auto ALWAYS = AssignCond::Tag::ALWAYS;

	if (tag == ALWAYS) {
		return "";
	} else {
		std::string ret;
		ret << "where " << pretty(*this) << ": ";
		return ret;
	}
}


/**
 * Translate an AssignCond instance to a BranchCond instance
 *
 * @param do_all  if true, set BranchCond tag to ALL, otherwise set to ANY
 */
BranchCond AssignCond::to_branch_cond(bool do_all) const {
  BranchCond bcond;
  if (is_always()) { bcond.tag = COND_ALWAYS; return bcond; }
  if (is_never())  { bcond.tag = COND_NEVER; return bcond; }

  assert(tag == AssignCond::Tag::FLAG);

  bcond.flag = flag;

  if (do_all) {
    bcond.tag = COND_ALL;
	} else {
    bcond.tag = COND_ANY;
  }

	return bcond;
}


///////////////////////////////////////////////////////////////////////////////
// Class BranchTarget
///////////////////////////////////////////////////////////////////////////////

std::string BranchTarget::to_string() const {
	std::string ret;

	if (relative) ret << "PC+1+";
	if (useRegOffset) ret << "A" << regOffset << "+";
	ret << immOffset;

	return ret;
}


///////////////////////////////////////////////////////////////////////////////
// Handy syntax functions
///////////////////////////////////////////////////////////////////////////////

// =========================
// Label generation
// =========================

// Obtain a fresh label
Label freshLabel() {
  return globalLabelId++;
}


// Number of fresh labels
int getFreshLabelCount() {
  return globalLabelId;
}

// Reset fresh label generator
void resetFreshLabelGen() {
  globalLabelId = 0;
}

// Reset fresh label generator to specified value
void resetFreshLabelGen(int val) {
  globalLabelId = val;
}


namespace Target {
namespace instr {

Reg const None(NONE, 0);
Reg const ACC0(ACC, 0);
Reg const ACC1(ACC, 1);
Reg const ACC2(ACC, 2);
Reg const ACC3(ACC, 3);
Reg const ACC4(ACC, 4);
Reg const QPU_ID(       SPECIAL, SPECIAL_QPU_NUM);
Reg const ELEM_ID(      SPECIAL, SPECIAL_ELEM_NUM);
Reg const TMU0_S(       SPECIAL, SPECIAL_TMU0_S);
Reg const VPM_WRITE(    SPECIAL, SPECIAL_VPM_WRITE);
Reg const VPM_READ(     SPECIAL, SPECIAL_VPM_READ);
Reg const WR_SETUP(     SPECIAL, SPECIAL_WR_SETUP);
Reg const RD_SETUP(     SPECIAL, SPECIAL_RD_SETUP);
Reg const DMA_LD_WAIT(  SPECIAL, SPECIAL_DMA_LD_WAIT);
Reg const DMA_ST_WAIT(  SPECIAL, SPECIAL_DMA_ST_WAIT);
Reg const DMA_LD_ADDR(  SPECIAL, SPECIAL_DMA_LD_ADDR);
Reg const DMA_ST_ADDR(  SPECIAL, SPECIAL_DMA_ST_ADDR);
Reg const SFU_RECIP(    SPECIAL, SPECIAL_SFU_RECIP);
Reg const SFU_RECIPSQRT(SPECIAL, SPECIAL_SFU_RECIPSQRT);
Reg const SFU_EXP(      SPECIAL, SPECIAL_SFU_EXP);
Reg const SFU_LOG(      SPECIAL, SPECIAL_SFU_LOG);

// Synonyms for v3d
Reg const TMUD(SPECIAL, SPECIAL_VPM_WRITE);
Reg const TMUA(SPECIAL, SPECIAL_DMA_ST_ADDR);

Reg rf(uint8_t index) {
	return Reg(REG_A, index);
}


Instr mov(Var dst, Var src) { return mov(dstReg(dst), srcReg(src)); }
Instr mov(Var dst, Reg src) { return mov(dstReg(dst), src); }
Instr mov(Var dst, int n)   { return mov(dstReg(dst), n); }
Instr mov(Reg dst, Var src) { return mov(dst, srcReg(src)); }
Instr mov(Reg dst, int n)   { return genInstr(ALUOp::A_BOR, dst, n, n); }
Instr mov(Reg dst, Reg src) { return bor(dst, src, src); }


// Generation of bitwise instructions
Instr bor(Reg dst, Reg srcA, Reg srcB)  { return genInstr(ALUOp::A_BOR, dst, srcA, srcB); }
Instr band(Reg dst, Reg srcA, Reg srcB) { return genInstr(ALUOp::A_BAND, dst, srcA, srcB); }
Instr band(Var dst, Var srcA, Var srcB) { return genInstr(ALUOp::A_BAND, dstReg(dst), srcReg(srcA), srcReg(srcB)); }
Instr band(Reg dst, Reg srcA, int n)    { return genInstr(ALUOp::A_BAND, dst, srcA, n); }
Instr bxor(Var dst, Var srcA, int n)    { return genInstr(ALUOp::A_BXOR, dstReg(dst), srcReg(srcA), n); }


/**
 * Generate left-shift instruction.
 */
Instr shl(Reg dst, Reg srcA, int val) {
  assert(val >= 0 && val <= 15);
	return genInstr(ALUOp::A_SHL, dst, srcA, val);
}


Instr shr(Reg dst, Reg srcA, int n) {
  assert(n >= 0 && n <= 15);
	return genInstr(ALUOp::A_SHR, dst, srcA, n);
}


/**
 * Generate addition instruction.
 */
Instr add(Reg dst, Reg srcA, Reg srcB) {
	return genInstr(ALUOp::A_ADD, dst, srcA, srcB);
}


Instr add(Reg dst, Reg srcA, int n) {
  assert(n >= 0 && n <= 15);
	return genInstr(ALUOp::A_ADD, dst, srcA, n);
}


Instr sub(Reg dst, Reg srcA, int n) {
  assert(n >= 0 && n <= 15);
	return genInstr(ALUOp::A_SUB, dst, srcA, n);
}


/**
 * Generate load-immediate instruction.
 */
Instr li(Reg dst, int i) {
  Instr instr(LI);
  instr.LI.cond       = always;
  instr.LI.dest       = dst;
  instr.LI.imm.tag    = IMM_INT32;
  instr.LI.imm.intVal = i;
 
  return instr;
}


Instr li(Var v, int i) {
	return li(dstReg(v), i);
}


Instr li(Var v, float f) {
  Instr instr(LI);
  instr.LI.cond         = always;
  instr.LI.dest         = dstReg(v);
  instr.LI.imm.tag      = IMM_FLOAT32;
  instr.LI.imm.floatVal = f;
 
  return instr;
}


/**
 * Create an unconditional branch.
 *
 * Conditions can still be specified with helper methods (e.g. see `allzc()`)
 */
Instr branch(Label label) {
	Instr instr;
	instr.tag          = BRL;
	instr.BRL.cond.tag = COND_ALWAYS;    // Can still be changed
	instr.BRL.label    = label;

	return instr;
}


Seq<Instr> recip(Var dst, Var srcA)     { return sfu_function(dst, srcA, SFU_RECIP    , "recip"); }
Seq<Instr> recipsqrt(Var dst, Var srcA) { return sfu_function(dst, srcA, SFU_RECIPSQRT, "recipsqrt"); }
Seq<Instr> bexp(Var dst, Var srcA)      { return sfu_function(dst, srcA, SFU_EXP      , "exp"); }
Seq<Instr> blog(Var dst, Var srcA)      { return sfu_function(dst, srcA, SFU_LOG      , "log"); }


/**
 * Create label instruction.
 *
 * This is a meta-instruction for Target source.
 */
Instr label(Label in_label) {
	Instr instr;
	instr.tag = LAB;
	instr.label(in_label);

	return instr;
}


/**
 * Create a conditional branch.
 */
Instr branch(BranchCond cond, Label label) {
	Instr instr;
	instr.tag       = BRL;
	instr.BRL.cond  = cond; 
	instr.BRL.label = label;

	return instr;
}


/**
 * v3d only
 */
Instr tmuwt() {
	Instr instr;
	instr.tag = TMUWT;
	return instr;
}

}  // namespace instr
}  // namespace Target
}  // namespace V3DLib
