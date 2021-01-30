#include "Matrix.h"
#include <functional>
#include "Support/basics.h"

namespace {
  int N             = 1;  // Dimension of square matrix in blocks of 16 values.
  bool do_readwrite = true;
  bool do_preload   = false;
}

////////////////////////////////////////////////////////////////////////////////
// Utility functions
////////////////////////////////////////////////////////////////////////////////

float random_float() {
  return  (1.0f*(((float) (rand() % 200)) - 100.0f))/100.0f;  // Intention: values between -1 and 1
}


namespace kernels {

using namespace V3DLib;


////////////////////////////////////////////////////////////////////////////////
// Kernel Helper Functions
////////////////////////////////////////////////////////////////////////////////

/**
 * Set value of src to vector element 'n' of dst
 *
 * All other values in dst are untouched.
 *
 * @param n  index of vector element to set. Must be in range 0..15 inclusive
 */
void set_at(Float &dst, Int n, Float &src) {
  Where(index() == n)
    dst = src;
  End 
}


/**
 * Sum up all the vector elements of a register.
 *
 * All vector elements of register result will contain the same value.
 */
void rotate_sum(Float &input, Float &result) {
  result = input;              comment("rotate_sum");
  result += rotate(result, 1);
  result += rotate(result, 2);
  result += rotate(result, 4);
  result += rotate(result, 8);
}


/**
 * Works, but does not improve the performance of matrix in any way.
 * The reason for this is that the dotvector product is already unrolled.
 *
 * Will still be useful in other contexts.
 *
 * ## Usage
 * Given a loop:
 * 
 *   For (Int b_index = 0, b_index < DIM, b_index++)
 *     // Code possibly using b_index
 *   End
 *
 * Replace with following:
 *
 *   loop_unroll(DIM, 8, [&] (Int b_index) {
 *     // Same code as in loop above
 *   });
 */
void loop_unroll(int size, int unroll, std::function<void(Int)> f) {
  assert(size > 0);
  assert(unroll > 0);
  assert(size >= unroll);
  assertq(size % unroll == 0, "loop_unroll(): size must be a multiple of unroll");

  std::string cmt("Loop unroll ");
  cmt << unroll << " for size " << size;

  Int i = 0;  comment(cmt);
  For (, i < size, i += unroll)
    for (int j = 0; j < unroll; ++j) {
      f(i + j);
      cmt = "End loop unroll ";
      comment(cmt << j << "/" << unroll);
    }
  End
}


void pre_read(Float &dst, Ptr<Float> &src) {
  if (!do_readwrite) {
    dst = 0.0f;
    src += 16;
    return;
  }

  if (do_preload) {
    // on vc4, this will use TMU
    gather(src);
    receive(dst);
    src += 16;
  } else {
    // on v3d, this will create the same code as the if-block
    // on vc4, this will use DMA
    dst = *src;
    src += 16;
  }
}


void pre_write(Ptr<Float> &dst, Float &src) {
  if (!do_readwrite) {
    dst += 16;
    return;
  }

  if (do_preload) {
    // on vc4, this will use TMU
    store(src, dst);
    dst += 16;
  } else {
    // on v3d, this will create the same code as the if-block
    // on vc4, this will use DMA
    *dst = src;
    dst += 16;
  }
}


////////////////////////////////////////////////////////////////////////////////
// Class DotVector 
////////////////////////////////////////////////////////////////////////////////

DotVector::DotVector(int size) {
  assertq(size >= 1, "There must be at least one element for DotVector");
  elements.resize(size);  
}


void DotVector::load(Ptr<Float> input) {
  for (int i = 0; i < (int) elements.size(); ++i) {
    pre_read(elements[i], input);
  }
}


void DotVector::save(Ptr<Float> output) {
  for (int i = 0; i < (int) elements.size(); ++i) {
    pre_write(output, elements[i]);
  }
}


/**
 * Calculate the dot product of current instance and `rhs`.
 *
 * All vector elements of the result will contain the same value.
 */
void DotVector::dot_product(Ptr<Float> rhs, Float &result) {
  Float tmp = 0;  comment("DotVector::dot_product()");

  for (int i = 0; i < (int) elements.size(); ++i) {
    Float tmp2;
    pre_read(tmp2, rhs);
    tmp += elements[i]*tmp2;
  }

  rotate_sum(tmp, result);
}


///////////////////////////////////////////////////////////////////////////////n
// Kernels
////////////////////////////////////////////////////////////////////////////////

/**
 * CPU version of matrix multiplication, naive implementation
 *
 * Matrixes are assumed to be square.
 *
 * @param N  dimension of square matrices
 * @param c  Pointer to result array
 */
void matrix_mult_scalar(int N, float *c, float *a, float *b) {
  for (int x = 0; x < N; x++) {
    for (int y = 0; y < N; y++) {
      float result = 0;

      for (int i = 0; i < N; i++) {
        result += a[i + y*N] * b [x + i*N];
      }

      c[x + y*N] = result;
    }
  }
}


/**
 * Multiply two square matrixes
 *
 * Does a matrix multiplication of `a` and `b` and puts the result in `dst`.
 *
 * Input matrix `b` needs to be in transposed form before usage.
 * Template parameters N is dimension of square matrix in blocks of 16 values.
 *
 * ----------------------------------------------------------------------------
 * Optimizations
 * =============
 *
 * - Load one entire row of a into the QPU for fetching one single time
 * - Use prefetching on the TMU (TODO)
 * - unroll the internal loop (does not help, not implemented here)
 * - Use all QPU's (TODO)
 * - All QPU's iterate over b together -> increase cache hits
 * - Maybe utilize wait slots in branches (TODO)
 */
void matrix_mult(Ptr<Float> dst, Ptr<Float> a, Ptr<Float> b) {
  int const DIM = 16*N;  // N is global static

  DotVector vec(N);
  Float result;

  gather_preload();

  For (Int a_index = 0,  a_index < DIM, a_index++)
    Ptr<Float> b_in = b + 0;  // Wonky '+ 0' to ensure pointer value is COPIED, not referenced.
    vec.load(a + 0);          // And again, and below again
                             // TODO fix this very NOT intuitive 'feature'. Bitten me >1 times.

    For (Int b_index = 0, b_index < DIM, b_index++)
      Float tmp;
      vec.dot_product(b_in + 0, tmp);

      set_at(result, b_index & 0xf, tmp);  // intention: b_index % 16

      If ((b_index & 0xf) == 15)
        pre_write(dst, result);
      End

      b_in += DIM;
    End  // IDIOT }  - never forget

    a += DIM;
  End
}



///////////////////////////////////////////////////////////////////////////////n
// Decorator Function
////////////////////////////////////////////////////////////////////////////////

/**
 * Decorator for the matrix multiplication kernel.
 *
 * This passes in a value for the compilation, while leaving the prototype as is.
 *
 * **NOTE:** This function is not thread-safe, it sets global statics.
 *           Since currently multiple threads are neither used nor supported, 
 *           this is not an issue. 
 *
 * @param dimension       dimension of matrixes used in multiplication,
 *                        must be a multiple of 16
 * @param in_do_readwrite if true, read/write to/from main memory (what you
 *                        normally expect). Otherwise, do the kernel operations only.
 *
 * @return  pointer to the actual kernel function
 */
FuncType *matrix_mult_decorator(int dimension, bool in_do_readwrite, bool in_do_preload) {
  assert(dimension > 0);
  assertq(dimension % 16 == 0, "dimension must be a multiple of 16");

  N = dimension >> 4;
  do_readwrite = in_do_readwrite;
  do_preload   = in_do_preload;

  return matrix_mult;
}

}  // namespace kernels
