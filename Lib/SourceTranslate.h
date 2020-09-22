#ifndef _QPULIB_SOURCETRANSLATE_H_
#define _QPULIB_SOURCETRANSLATE_H_
#include "Common/Seq.h"
#include "Source/Syntax.h"
#include "Target/Syntax.h"
#include "Target/CFG.h"

namespace QPULib {

class ISourceTranslate {
public:
	virtual ~ISourceTranslate() {}

	/**
	 * TODO: return value currently not used, remove?
	 *
	 * @return true if handled, false otherwise
	 */
	virtual bool deref_var_var(Seq<Instr>* seq, Expr &lhs, Expr *rhs) = 0;

	virtual void setupVPMWriteStmt(Seq<Instr>* seq, Stmt *s) = 0;
	virtual void storeRequest(Seq<Instr>* seq, Expr* data, Expr* addr) = 0;
	virtual void varassign_deref_var(Seq<Instr>* seq, Var &v, Expr &e) = 0;

	virtual void regAlloc(CFG* cfg, Seq<Instr>* instrs) = 0;
	virtual void add_init(Seq<Instr> &code) = 0; 

protected:
	int get_init_begin_marker(Seq<Instr> &code);
	Seq<Instr> add_uniform_pointer_offset(Seq<Instr> &code);
};

ISourceTranslate &getSourceTranslate();
void set_compiling_for_vc4(bool val);
bool compiling_for_vc4();

}  // namespace QPULib

#endif  // _QPULIB_SOURCETRANSLATE_H_
