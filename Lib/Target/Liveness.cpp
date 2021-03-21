///////////////////////////////////////////////////////////////////////////////
// Liveness analysis
//
///////////////////////////////////////////////////////////////////////////////
#include "Support/basics.h"
#include "Support/Platform.h"
#include "Target/Subst.h"
#include "Target/Liveness.h"
#include "Common/CompileData.h"
#include "liveness/Optimizations.h"

namespace V3DLib {
namespace {

/**
 * Replace the variables with the assigned registers for the given instruction
 *
 * This functions assigns real registers to the 'variable registers' of the instruction.
 *
 * The incoming instructions all have REG_A as registers but signify variables.
 * The reg id's indicate the variable at this stage.
 *
 * Allocation is first done with reg types TMP_A/TMP_B,
 * to avoid accidental replacements of registers with same id.
 * This has happened IRL.
 */
void allocate_registers(Instr &instr, RegUsage const &alloc) {

  auto check_regfile_register = [&instr] (Reg const &replace_with, RegId r) -> bool {
    if (replace_with.tag == REG_A) return true;
    if (Platform::compiling_for_vc4() && replace_with.tag == REG_B) return true;

    UseDefReg out;
    useDefReg(instr, &out);

    std::string msg = "regAlloc(): allocated register must be in register file.";
    msg << "\n"
        << "Instruction: " << instr.dump() << ", "
        << "Registers: " << out.dump() << ", "
        << "Reg id : " << r << ", alloc value: " << replace_with.dump();

    error(msg, true);  // true: throw if there is an error

    return false;
  };

  UseDef useDefSet;
  useDefSet.set_used(instr);  // Registers only usage REG_A

  for (int j = 0; j < useDefSet.def.size(); j++) {
    RegId r = useDefSet.def[j];
    assert(!alloc[r].unused());
    Reg replace_with = alloc[r].reg;

    if (!check_regfile_register(replace_with, r)) continue;
    replace_with.tag = (replace_with.tag == REG_A)?TMP_A:TMP_B;

    renameDest(instr, Reg(REG_A, r), replace_with);
  }

  for (int j = 0; j < useDefSet.use.size(); j++) {
    RegId r = useDefSet.use[j];
    assert(!alloc[r].unused());
    Reg replace_with = alloc[r].reg;

    if (!check_regfile_register(replace_with, r)) continue;
    replace_with.tag = (replace_with.tag == REG_A)?TMP_A:TMP_B;

    renameUses(instr, Reg(REG_A, r), replace_with);
  }

  substRegTag(&instr, TMP_A, REG_A);
  substRegTag(&instr, TMP_B, REG_B);
}

}  // anon namespace


///////////////////////////////////////////////////////////////////////////////
// Class LiveSet
///////////////////////////////////////////////////////////////////////////////

void LiveSet::add_not_used(LiveSet const &set, UseDef const &use ) {
  clear();
  for (int j = 0; j < set.size(); j++) {
    if (!use.def.member(set[j]))
      Parent::insert(set[j]);
  }
}


std::string LiveSet::dump() const {
  std::string ret;

  ret << "(";
  for (int j = 0; j < size(); j++) {
    ret << (*this)[j] << ", ";
  }
  ret << ")";

  return ret;
}



///////////////////////////////////////////////////////////////////////////////
// Compute 'use' and 'def' sets
///////////////////////////////////////////////////////////////////////////////

// 'use' set: the variables read by an instruction
// 'def' set: the variables modified by an instruction

/**
 * Compute 'use' and 'def' sets for a given instruction
 *
 * Param 'set_use_where' need only be true during liveness analysis.
 *
 * @param set_use_where  if true, regard assignments in conditional 'where'
 *                       instructions as usage.
 *
 * ============================================================================
 * NOTES
 * =====
 *
 * * 'set_use_where' needs to be true for the following case (target language):
 *
 *    LI A5 <- 0                  # assignment
 *    ...
 *    where ZC: LI A6 <- 1
 *    where ZC: A5 <- or(A6, A6)  # Conditional assignment
 *    ...
 *    S[VPM_WRITE] <- shl(A5, 0)  # last use
 *
 *   If the condition is ignored (`set_use_where == false`), the conditional
 *   assignment is regarded as an overwrite of the previous one. The variable
 *   is then considered live from the conditional assignment onward.
 *   This is wrong, the value of the first assignment may be significant due
 *   to the condition. The usage of `A5` runs the risk of being assigned different
 *   registers for the different assignments, which will lead to wrong code execution.
 *
 * * However, always using `set_use_where == true` leads to variables being live
 *   for unnecessarily long. If this is the *only* usage of `A6`:
 *
 *    where ZC: LI A6 <- 1
 *    where ZC: A5 <- or(A6, A6)  # Conditional assignment
 *
 *   ... `A6` would be considered live from the start of the program
 *   onward till the last usage.
 *   This unnecessarily ties up a register for a long duration, complicating the
 *   allocation by creating a false shortage of registers.
 *   This case can not be handled by the liveness analysis as implemented here.
 *   It is corrected afterwards in methode `Liveness::compute()`.
 */
void useDefReg(Instr instr, UseDefReg* useDef, bool set_use_where) {
  auto ALWAYS = AssignCond::Tag::ALWAYS;

  useDef->use.clear();
  useDef->def.clear();

  switch (instr.tag) {
    case LI:                                     // Load immediate
      useDef->def.insert(instr.LI.dest);         // Add destination reg to 'def' set

      if (set_use_where) {
        if (instr.LI.cond.tag != ALWAYS)         // Add destination reg to 'use' set if conditional assigment
          useDef->use.insert(instr.LI.dest);
      }
      return;

    case ALU:                                    // ALU operation
      useDef->def.insert(instr.ALU.dest);        // Add destination reg to 'def' set

      if (set_use_where) {
        if (instr.ALU.cond.tag != ALWAYS)        // Add destination reg to 'use' set if conditional assigment
          useDef->use.insert(instr.ALU.dest);
      }

      if (instr.ALU.srcA.is_reg())               // Add source reg A to 'use' set
        useDef->use.insert(instr.ALU.srcA.reg());

      if (instr.ALU.srcB.is_reg())               // Add source reg B to 'use' set
        useDef->use.insert(instr.ALU.srcB.reg());
      return;

    case RECV:                                   // Load receive instruction
      useDef->def.insert(instr.RECV.dest);       // Add dest reg to 'def' set
      return;
    default:
      return;
  }  
}


LiveSets::LiveSets(int size) : m_size(size) {
  assert(size > 0);
  m_sets = new LiveSet [size];  // confirmed: default ctor LiveSet called here (notably ctor SmallSeq)
}


LiveSets::~LiveSets() {
  delete [] m_sets;
}


void LiveSets::init(Instr::List &instrs, Liveness &live) {
  LiveSet liveOut;

  for (int i = 0; i < instrs.size(); i++) {
    live.computeLiveOut(i, liveOut);
    useDefSet.set_used(instrs[i]);

    for (int j = 0; j < liveOut.size(); j++) {
      RegId rx = liveOut[j];

      for (int k = 0; k < liveOut.size(); k++) {
        RegId ry = liveOut[k];
        if (rx != ry) (*this)[rx].insert(ry);
      }

      for (int k = 0; k < useDefSet.def.size(); k++) {
        RegId rd = useDefSet.def[k];
        if (rd != rx) {
          (*this)[rx].insert(rd);
          (*this)[rd].insert(rx);
        }
      }
    }
  }
}


LiveSet &LiveSets::operator[](int index) {
  assert(index >=0 && index < m_size);
  return m_sets[index];
}


/**
 * Determine the available register in the register file, to use for variable 'index'.
 *
 * @param index  index of variable
 */
std::vector<bool> LiveSets::possible_registers(int index, RegUsage &alloc, RegTag reg_tag) {
  assert(reg_tag == REG_A || reg_tag == REG_B);

  const int NUM_REGS = Platform::size_regfile();
  std::vector<bool> possible(NUM_REGS);

  for (int j = 0; j < NUM_REGS; j++)
    possible[j] = true;

  LiveSet &set = (*this)[index];

  // Eliminate impossible choices of register for this variable
  for (int j = 0; j < set.size(); j++) {
    Reg neighbour = alloc[set[j]].reg;
    if (neighbour.tag == reg_tag) possible[neighbour.regId] = false;
  }

  return possible;
}


/**
 * Debug function to output the contents of the possible-vector
 *
 * Returns a string of 0's and 1's for each slot in the possible-vector.
 * - '0' - in use, not available for assignment for variable with index 'index'.
 * - '1' - not in use, available for assignment
 *
 * This falls under the category "You probably don't need it, but when you need it, you need it bad".
 *
 * @param index - index value of current variable displayed. If `-1`, don't show. For display purposes only.
 */
void LiveSets::dump_possible(std::vector<bool> &possible, int index) {
  std::string buf = "possible: ";

  if (index >= 0) {
    if (index < 10) buf << "  ";
    else if (index < 100) buf << " ";

    buf << index;
  }
  buf << ": ";

  for (int j = 0; j < (int) possible.size(); j++) {
    buf << (possible[j]?"1":"0");
  }
  debug(buf.c_str());
}


/**
 * Find possible register in each register file
 */
RegId LiveSets::choose_register(std::vector<bool> &possible, bool check_limit) {
  assert(!possible.empty());
  RegId chosenA = -1;

  for (int j = 0; j < (int) possible.size(); j++)
    if (possible[j]) { chosenA = j; break; }

  if (check_limit && chosenA < 0) {
    error("LiveSets::choose_register(): register allocation failed, insufficient capacity", true);
  }

  return chosenA;
}  


///////////////////////////////////////////////////////////////////////////////
// Class Liveness
///////////////////////////////////////////////////////////////////////////////

/**
 * Determine the liveness sets for each instruction.
 */
void Liveness::compute_liveness(Instr::List &instrs) {
  // Initialise live mapping to have one entry per instruction
  setSize(instrs.size());

  UseDef useDef;

  // For temporarily storing live-in and live-out variables
  LiveSet liveIn;
  LiveSet liveOut;

  bool changed = true;
  int count = 0;

  {
    std::string msg;
    msg << "compute_liveness CFG:\n"
        << m_cfg.dump();

    debug(msg);
  }

  // Iterate until no change, i.e. fixed point
  while (changed) {
    changed = false;

    // Propagate live variables backwards
    for (int i = instrs.size() - 1; i >= 0; i--) {
      // Compute 'use' and 'def' sets
      useDef.set_used(instrs[i], true);

      computeLiveOut(i, liveOut);

      // Remove the 'def' set from the live-out set to give live-in set
      liveIn.add_not_used(liveOut, useDef);

      liveIn.add(useDef.use);
/*
      if (i == 25 || i == 26) { 
        std::string msg;
        msg << "count " << count << ", line " << i << ": liveIn: " << liveIn.dump() << ", liveOut: " << liveOut.dump(); 
        debug(msg);
breakpoint
      }
*/
      if (insert(i, liveIn)) {
        changed = true;
      }
    }

    count++;
  }

  std::string msg;
  msg << "compute_liveness count: " << count;
  debug(msg);
}


void Liveness::compute(Instr::List &instrs) {
  m_reg_usage.set_used(instrs);

  compute_liveness(instrs);
  assert(instrs.size() == size());

  {
    std::string msg;
    msg << " Liveness table:\n" << dump();
    debug(msg);
  }

  m_reg_usage.set_live(*this);
  debug(m_reg_usage.dump(true));

  // Adjust first usage in liveness, if necessary 
  for (int i = 0; i < (int) m_reg_usage.size(); ++i) {
    auto &item = m_reg_usage[i];

    if (item.unused() || item.only_assigned())     continue;  // skip special cases
    if (item.first_dst() + 1 == item.first_live()) continue;  // all is well

/*
    {
      std::string msg;
      msg << "m_reg_usage[" << i << "]: discrepancy between first assign and liveness: " << item.dump();
      warning(msg);
    }
*/

    // Remove all liveness of given variable `i` before and including first assign
    assert(item.first_dst() != -1);
    assert(item.first_live() != -1);
    int remove_count = 0;
    for (int j = item.first_live(); j <= item.first_dst(); ++j) {
      int num = m_set[j].remove(i);
      if (num == 1) remove_count++;
    }

    if (remove_count == 0) {
      std::string msg = "Liveness::compute(): ";
      msg << "failed to remove liveness for var " << i 
          << " in range (" << item.first_dst() << ", " << item.first_live() << ")\n"
          << " Usage item: " << item.dump() << "\n"
          << " Liveness table:\n" << dump() << "\n"
          << " Reg usage:\n" << m_reg_usage.dump(true) << "\n"
          << " Code:\n" << instrs.dump(true) << "\n";

      warning(msg);
    }
  }

  compile_data.liveness_dump = dump();

  m_reg_usage.check();
}


/**
 * Compute live sets for each instruction
 *
 * Compute the live-out variables of an instruction, given the live-in
 * variables of all instructions and the CFG.
 */
void Liveness::computeLiveOut(InstrId i, LiveSet &liveOut) {
  liveOut.clear();
  Succs &s = m_cfg[i];

  for (int j = 0; j < s.size(); j++) {
    LiveSet &set = get(s[j]);
    liveOut.add(set);
  }
}


void Liveness::setSize(int size) {
  m_set.set_size(size);
}


bool Liveness::insert(int index, LiveSet const &set) {
  bool changed = false;

  for (int j = 0; j < set.size(); j++) {
    if (m_set[index].insert(set[j])) {
      changed = true;
    }
  }

  return changed;
}


std::string Liveness::dump() {
  std::string ret;

  for (int i = 0; i < m_set.size(); ++i) {
    ret += std::to_string(i) + ": ";

    auto &item = m_set[i];
    bool did_first = false;
    for (int j = 0; j < item.size(); j++) {
      if (did_first) {
        ret += ", ";
      } else {
        did_first = true;
      }
      ret += std::to_string(item[j]);
    }
    ret += "\n";
  }

  if (ret.empty()) ret += "<Empty>";

  ret += "\n";

  return ret;
}


/**
 * Introduce optimizations where possible in the instruction list
 *
 * This is done before the actual liveness analysis.
 * The idea is to minimize beforehand the number of variables considered
 * in the liveness analysis.
 */
void Liveness::optimize(CFG &cfg, Instr::List &instrs, int numVars) {
  Liveness live(cfg, numVars);
  live.compute(instrs);

  compile_data.num_accs_introduced = introduceAccum(live, instrs);
  //std::cout << count_reg_types(instrs).dump() << std::endl;
}


void allocate_registers(Instr::List &instrs, RegUsage const &alloc) {
  for (int i = 0; i < instrs.size(); i++) {
    allocate_registers(instrs.get(i), alloc);
  }
}

}  // namespace V3DLib
