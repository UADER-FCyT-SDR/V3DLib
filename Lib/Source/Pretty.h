#ifndef _V3DLIB_SOURCE_PRETTY_H_
#define _V3DLIB_SOURCE_PRETTY_H_

#include "Source/Syntax.h"

namespace V3DLib {

// Pretty printer for the V3DLib source language
void pretty(FILE *f, Expr* e);
void pretty(FILE *f, BExpr* b);
void pretty(FILE *f, CExpr* c);
void pretty(FILE *f, Stmt* s);
void pretty(Stmt* s);

}  // namespace V3DLib

#endif  // _V3DLIB_SOURCE_PRETTY_H_
