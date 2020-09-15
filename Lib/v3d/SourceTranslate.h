#ifndef _QPULIB_V3D_SOURCETRANSLATE_H_
#define _QPULIB_V3D_SOURCETRANSLATE_H_
#include "../SourceTranslate.h"

namespace QPULib {
namespace v3d {

class SourceTranslate : public ISourceTranslate {
public:
	bool deref_var_var(Seq<Instr>* seq, Expr &lhs, Expr *rhs) override;
	void  setupVPMWriteStmt(Seq<Instr>* seq, Stmt *s) override;
	void storeRequest(Seq<Instr>* seq, Expr* data, Expr* addr) override;
	void varassign_deref_var(Seq<Instr>* seq, Var &v, Expr &e) override;

	void regAlloc(CFG* cfg, Seq<Instr>* instrs) override;
	void add_init(Seq<Instr> &code) override;
};


}  // namespace v3d
}  // namespace QPULib


#endif  // _QPULIB_iV3D_SOURCETRANSLATE_H_
