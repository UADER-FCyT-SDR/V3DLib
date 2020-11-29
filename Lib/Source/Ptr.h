// This module defines type 'Ptr<T>' type denoting a pointer to a
// value of type 'T'.
//
///////////////////////////////////////////////////////////////////////////////
#ifndef _V3DLIB_SOURCE_PTR_H_
#define _V3DLIB_SOURCE_PTR_H_
#include "Source/Syntax.h"
#include "Support/debug.h"

namespace V3DLib {
  //
  // Extra declaration to prevent error:
  //
  //   error: there are no arguments to ‘assign’ that depend on a template parameter,
  //          so a declaration of ‘assign’ must be available [-fpermissive]
  //          
  void assign(Expr::Ptr lhs, Expr::Ptr rhs);


// ============================================================================
// Types                   
// ============================================================================

// A 'PtrExpr<T>' defines a pointer expression which can only be used on the
// RHS of assignment statements.
template <typename T>
struct PtrExpr : public BaseExpr {
	PtrExpr(Expr::Ptr e) : BaseExpr(e) {}


	/**
	 * Dereference
	 */
  T operator*() {
    return T(mkDeref(expr()), true);
  }


  // Array index
  T& operator[](IntExpr index) {
breakpoint
		T *p = new T;
    p->set_with_index(expr(), index.expr());
    return *p;
  }
};


// A 'Ptr<T>' defines a pointer variable which can be used in both the LHS and
// RHS of an assignment.

template <typename T>
struct Ptr : public BaseExpr {
  // Constructors
  Ptr<T>() : BaseExpr(mkVar(freshVar())) {}

  Ptr<T>(PtrExpr<T> rhs) : Ptr<T>() {
    assign(expr(), rhs.expr());
  }

  // Assignment
  Ptr<T>& operator=(Ptr<T>& rhs) {
    assign(expr, rhs.expr);
    return rhs;
  }

  PtrExpr<T> operator=(PtrExpr<T> rhs) {
    assign(expr(), rhs.expr());
    return rhs;
  }


	/**
	 * Dereference
	 */
  T operator*() {
    return T(mkDeref(expr()), true);
  }

  // Array index
  T operator[](IntExpr index) {
		breakpoint
		T ret;
    ret.set_with_index(expr(), index.expr());
    return ret;
  }
};


// ============================================================================
// Specific operations
// ============================================================================

template <typename T>
inline PtrExpr<T> getUniformPtr() {
  Expr::Ptr e = std::make_shared<Expr>(Var(UNIFORM));
  return PtrExpr<T>(e);
}


template <typename T>
inline PtrExpr<T> operator+(PtrExpr<T> a, int b) {
  Expr::Ptr e = mkApply(a.expr(), Op(ADD, INT32), mkIntLit(4*b));
  return PtrExpr<T>(e);
}


template <typename T>
inline PtrExpr<T> operator+(Ptr<T> &a, int b) {
  Expr::Ptr e = mkApply(a.expr(), Op(ADD, INT32), mkIntLit(4*b));
  return PtrExpr<T>(e);
}


template <typename T> inline PtrExpr<T> operator+=(Ptr<T> &a, int b) {
  return a = a + b;
}

template <typename T> inline PtrExpr<T> operator+(PtrExpr<T> a, IntExpr b) {
  Expr::Ptr e = mkApply(a.expr(), Op(ADD, INT32), (b << 2).expr());
  return PtrExpr<T>(e);
}

template <typename T> inline PtrExpr<T> operator+(Ptr<T> &a, IntExpr b) {
  Expr::Ptr e = mkApply(a.expr(), Op(ADD, INT32), (b << 2).expr());
  return PtrExpr<T>(e);
}

template <typename T> inline PtrExpr<T> operator-(Ptr<T> &a, IntExpr b) {
  Expr::Ptr e = mkApply(a.expr(), Op(SUB, INT32), (b << 2).expr());
  return PtrExpr<T>(e);
}

template <typename T> inline PtrExpr<T> operator-=(Ptr<T> &a, IntExpr b) {
  return a = a - b;
}

}  // namespace V3DLib

#endif  // _V3DLIB_SOURCE_PTR_H_
