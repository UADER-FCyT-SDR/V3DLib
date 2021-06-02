#ifndef _V3DLIB_TARGET_INSTR_INSTR_H_
#define _V3DLIB_TARGET_INSTR_INSTR_H_
#include <set>
#include "Support/InstructionComment.h"
#include "Common/Seq.h"
#include "Label.h"
#include "Imm.h"
#include "Conditions.h"
#include "ALUInstruction.h"
#include "Support/RegIdSet.h"

namespace V3DLib {

inline std::set<Reg> operator+(std::set<Reg> const &lhs, std::set<Reg> const &rhs) {
  std::set<Reg> ret = lhs;
  ret.insert(rhs.begin(), rhs.end());
  return ret;
}


class CmpOp;

// ============================================================================
// Class BranchTarget
// ============================================================================

struct BranchTarget {
  bool relative;      // Branch is absolute or relative to PC+4

  bool useRegOffset;  // Plus value from register file A (optional)
  RegId regOffset;

  int immOffset;      // Plus 32-bit immediate value

  std::string to_string() const;
};


// ============================================================================
// QPU instruction tags
// ============================================================================

enum InstrTag {
  LI,              // Load immediate
  ALU,             // ALU operation
  BR,              // Conditional branch to target
  END,             // Program end (halt)

  // ==================================================
  // Intermediate-language constructs
  // ==================================================

  BRL,             // Conditional branch to label
  LAB,             // Label
  NO_OP,           // No-op
  SKIP,            // For internal use during optimization

  // ==================================================
  // vc4-only instructions
  // ==================================================
  VC4_ONLY,        // Marker in this enum, not an op!

  DMA_LOAD_WAIT = VC4_ONLY, // Wait for DMA load to complete
  DMA_STORE_WAIT,  // Wait for DMA store to complete
  SINC,            // Increment semaphore
  SDEC,            // Decrement semaphore
  IRQ,             // Send IRQ to host

  VPM_STALL,       // Marker for VPM read setup

  END_VC4_ONLY,    // Marker in this enum, not an op!

  // ==================================================
  // v3d/vc4 instructions
  // ==================================================
  // Load receive via TMU
  RECV = END_VC4_ONLY,
  TMU0_TO_ACC4,

  // Init program block (Currently filled only for v3d)
  INIT_BEGIN,      // Marker for start of init block
  INIT_END,        // Marker for end of init block

  // ==================================================
  // v3d-only instructions
  // ==================================================
  V3D_ONLY,        // Marker in this enum, not an op!

  TMUWT = V3D_ONLY,

  END_V3D_ONLY     // Marker in this enum, not an op!
};


void check_instruction_tag_for_platform(InstrTag tag, bool for_vc4);


// ============================================================================
// Class Instr - QPU instructions
// ============================================================================

struct Instr : public InstructionComment {

  class List : public Seq<Instr> {
    using Parent = Seq<Instr>;

  public:
    List() = default;
    List(int size) : Parent(size) {}

    std::string dump(bool with_line_numbers = false) const;
    std::string mnemonics(bool with_comments = false) const;
    int lastUniformOffset();
    int tag_index(InstrTag tag, bool ensure_one = true);
    int tag_count(InstrTag tag);
    std::string check_acc_usage(int first = -1, int last = -1) const;
    int get_free_acc(int first, int last) const;
  };

  InstrTag tag;

  union {
    // Load immediate
    struct {
      SetCond    m_setCond;
      AssignCond cond;
      Reg        dest;
      Imm        imm;
    } LI;

    ALUInstruction ALU;

    // Conditional branch (to target)
    struct {
      BranchCond cond;
      BranchTarget target;
    } BR;

    // ==================================================
    // Intermediate-language constructs
    // ==================================================

    // Conditional branch (to label)
    struct {
      BranchCond cond;
      Label label;
    } BRL;

    // Labels, denoting branch targets
    Label m_label;  // Renamed during debugging
                    // TODO perhaps revert

    // Semaphores
    int semaId;                 // Semaphore id (range 0..15)

    // Load receive via TMU
    struct { Reg dest; } RECV;  // Destination register for load receive
  };


  Instr() : tag(NO_OP) {} 
  Instr(InstrTag in_tag);

  std::string header()  const  { return InstructionComment::header();  }  // grumbl hating that this is needed
  std::string comment() const  { return InstructionComment::comment(); }  // idem
  Instr &header(std::string const &msg) { InstructionComment::header(msg);  return *this; }
  Instr &comment(std::string msg)       { InstructionComment::comment(msg); return *this; }

  void break_point() { m_break_point = true; }
  bool break_point() const { return m_break_point; }

  // ==================================================
  // Helper methods
  // ==================================================
  Instr &setCondFlag(Flag flag);
  Instr &setCondOp(CmpOp const &cmp_op);
  Instr &cond(AssignCond in_cond);
  bool is_branch() const { return tag == InstrTag::BR || tag == InstrTag::BRL; }
  bool isCondAssign() const;
  bool is_always() const;
  AssignCond assign_cond() const;
  bool hasImm() const { return ALU.srcA.is_imm() || ALU.srcB.is_imm(); }
  bool isUniformLoad() const;
  bool isUniformPtrLoad() const;
  bool isTMUAWrite() const;
  bool isRot() const;
  bool isZero() const;
  bool isLast() const;
  bool has_registers() const { return tag == InstrTag::LI || tag == InstrTag::ALU || tag == InstrTag::RECV; }

  Reg dst_reg() const;
  std::set<Reg> src_regs(bool set_use_where = false) const;
  Reg dst_a_reg() const;
  RegIdSet src_a_regs(bool set_use_where = false) const;
  bool is_dst_reg(Reg const &rhs) const;
  bool is_src_reg(Reg const &rhs) const;

  SetCond const &setCond() const;
  std::string mnemonic(bool with_comments = false, std::string const &pref = "") const;
  std::string dump() const;
  uint32_t get_acc_usage() const;

  bool operator==(Instr const &rhs) const {
    // Cheat by comparing the string representation,
    // to avoid having to check the union members separately, and to skip unused fields
    return this->mnemonic() == rhs.mnemonic();
  }

  bool operator!=(Instr const &rhs) const { return !(*this == rhs); }


  static Instr nop();

  /////////////////////////////////////
  // Label support
  /////////////////////////////////////

  bool is_label() const { return tag == InstrTag::LAB; }
  bool is_branch_label() const { return tag == InstrTag::BRL; }

  Label branch_label() const {
    assert(tag == InstrTag::BRL);
    return BRL.label;
  }

  void label_to_target(int offset);
    
  void label(Label val) {
    assert(tag == InstrTag::LAB);
    m_label = val;
  }

  Label label() const {
    assert(tag == InstrTag::LAB);
    return m_label;
  }

  // ==================================================
  // v3d-specific  methods
  // ==================================================
  Instr &pushz();

  Instr &allzc() {
    assert(tag == InstrTag::BRL);
    BRL.cond.tag  = COND_ALL;
    BRL.cond.flag = Flag::ZC;
    return *this;
  }

private:
  SetCond &setCond();

  bool m_break_point = false;
};


void check_zeroes(Instr::List const &instrs);

}  // namespace V3DLib

#endif  // _V3DLIB_TARGET_INSTR_INSTRUCTIONS_H_
