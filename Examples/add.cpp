/*------------------------------------------------------------------------------------------------
  Universidad Aut  noma de Entre Rios
  Facultad de Ciencia y Tecnolog  a
  Ingenier  a de Telecomunicaciones

  Proyecto de investigaci  n
  Procesamiento de se  ales mediante cluster
  Microcluster LAN
  Programa de prueba para evaluar operaciones enteras de suma en GPU

  compile with cd ../ && make QPU=1 all

  include program name at ../sources.mk
*/


#include "../Lib/V3DLib.h"
#include "Support/Settings.h"

using namespace V3DLib;

V3DLib::Settings settings;
void add(Int::Ptr a, Int::Ptr b, Int::Ptr r)
{
 Int x=*a;
 Int y=*b;

 *r = x + y;
}

int main(int argc, const char *argv[]) {
  int numQPUs = 1;                                // Max number of QPUs for v3d


  auto k = compile(add);                    // Construct kernel
  k.setNumQPUs(numQPUs);

// Initialise arrays
  srand(0);
  Int::Array a(16*numQPUs);
  Int::Array b(16*numQPUs);
  Int::Array r(16*numQPUs);

  for (int i = 0; i < 16; i++) {
    a[i] = 100 + (rand() % 100);
    b[i] = 100 + (rand() % 100);
  }


  //Int::Array result(16*numQPUs);                  // Allocate and initialise array shared between ARM and GPU
  //result.fill(-1);

  //Int::Array index_array(16*numQPUs);
  //index_array.fill(2);

  //k.load(&result, &index_array);                  // Load the uniforms
  k.load(&a,&b,&r);                  // Load the uniforms
  settings.process(k);                            // Invoke the kernel

  for (int i = 0; i < 16; i++) { // Display the result
    printf("a[%d]=%3i: b[%d]=%2i, r[%d]=%2i\n", i, a[i],i,b[i],i,r[i]);
  }
  
  return 0;
}
