/*------------------------------------------------------------------------------------------------
  Universidad Aut  noma de Entre Rios
  Facultad de Ciencia y Tecnolog  a
  Ingenier  a de Telecomunicaciones

  Proyecto de investigaci  n
  Procesamiento de se  ales mediante cluster
  Microcluster LAN
  Programa de prueba para evaluar operaciones de punto flotante en GPU 

  compile with cd ../ && make QPU=1 all

  include program name at ../sources.mk
*/

#include "../Lib/V3DLib.h"
#include "Support/Settings.h"

using namespace V3DLib;

V3DLib::Settings settings;

void walsh(Float::Ptr a, Float::Ptr b, Float::Ptr r1, Float::Ptr r2)
{

 *r1 = *a + *b;
 *r2 = *a - *b;

}
float random_float(float min, float max) {

        return ((float)rand() / RAND_MAX) * (max - min) + min;

}



int main(int argc, const char *argv[]) {

  int numQPUs = 1;                            // Max number of QPUs for v3d

  auto k = compile(walsh);                    // Construct kernel
  k.setNumQPUs(numQPUs);

// Initialise arrays

  Float::Array a(16*numQPUs);
  Float::Array b(16*numQPUs);
  Float::Array r1(16*numQPUs);
  Float::Array r2(16*numQPUs);

// Initialise arrays

  srand(0);
  for (int i = 0; i < 16; i++) {
    a[i]=random_float(0.0,2.0);
    b[i]=random_float(0.0,2.0);
  }

  k.load(&a,&b,&r1,&r2);                  // Load the uniforms
  settings.process(k);                    // Invoke the kernel

  for (int i = 0; i < 16; i++) {          // Display the result

    printf("walsh_float[%2.1f,%2.1f]--(%2.1f,%2.1f)\n", a[i],b[i],r1[i],r2[i]);
  }

  return 0;
}
