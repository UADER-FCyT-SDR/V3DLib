/*
	* Copyright © 2016 Broadcom
	*
	* Permission is hereby granted, free of charge, to any person obtaining a
	* copy of this software and associated documentation files (the "Software"),
	* to deal in the Software without restriction, including without limitation
	* the rights to use, copy, modify, merge, publish, distribute, sublicense,
	* and/or sell copies of the Software, and to permit persons to whom the
	* Software is furnished to do so, subject to the following conditions:
	*
	* The above copyright notice and this permission notice (including the next
	* paragraph) shall be included in all copies or substantial portions of the
	* Software.
	*
	* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
	* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
	* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
	* IN THE SOFTWARE.
	*/

/*
 * Adapted from: broadcom/qpu/tests/qpu_disasm.c
 */
#include <stdio.h>
#include "util/macros.h"
#include "broadcom/common/v3d_device_info.h"
#include "broadcom/qpu/qpu_disasm.h"
#include "broadcom/qpu/qpu_instr.h"


// This is the code dump after assembly of `qpu_cond_push_a` in tests/test_condition_codes.py
static const uint64_t code[] = {
0x3c403180bb802000,
0x3c003182b682d000,
0x3de031807c838002,
0x3c00318238802000,
0x3de031817c83f004,
0x3c003180bb802000,
0x3de071803c83800a,
0x3de03180b683f000,
0x3de83180b683f001,
0x3c00318bb6800000,
0x3c00318cb6812000,
0x04003086bb295000,
0x3de03180b683f000,
0x3dec3006bbfc0001,
0x3c00318bb6800000,
0x3c00318cb6812000,
0x04003086bb295000,
0x3c003180bb802000,
0x3de0b1803c83800a,
0x3de03180b683f000,
0x3dea3180b683f001,
0x3c00318bb6800000,
0x3c00318cb6812000,
0x04003086bb295000,
0x3de03180b683f000,
0x3dee3006bbfc0001,
0x3c00318bb6800000,
0x3c00318cb6812000,
0x04003086bb295000,
0x3c003180bb802000,
0x3de0f1803c83800a,
0x3de03180b683f000,
0x3de83180b683f001,
0x3c00318bb6800000,
0x3c00318cb6812000,
0x04003086bb295000,
0x3de03180b683f000,
0x3dec3006bbfc0001,
0x3c00318bb6800000,
0x3c00318cb6812000,
0x04003086bb295000,
0x3c203186bb800000,
0x3c203186bb800000,
0x3c003186bb800000,
0x3c003186bb800000,
0x3c203186bb800000,
0x3c003186bb800000,
0x3c003186bb800000,
0x3c003186bb800000
};


static void swap_mux(enum v3d_qpu_mux *a, enum v3d_qpu_mux *b) {
	enum v3d_qpu_mux t = *a;
	*a = *b;
	*b = t;
}

static void swap_pack(enum v3d_qpu_input_unpack *a, enum v3d_qpu_input_unpack *b) {
	enum v3d_qpu_input_unpack t = *a;
	*a = *b;
	*b = t;
}



static int test_instr(struct v3d_device_info *devinfo, uint64_t in_code) {
		struct v3d_qpu_instr instr;
		if (!v3d_qpu_instr_unpack(devinfo, in_code, &instr)) {
			printf(" - FAIL (unpack)");
			return 2;
		}

		if (instr.type == V3D_QPU_INSTR_TYPE_ALU) {
			switch (instr.alu.add.op) {
				case V3D_QPU_A_FADD:
				case V3D_QPU_A_FADDNF:
				case V3D_QPU_A_FMIN:
				case V3D_QPU_A_FMAX:
					printf("\nDOING THIS\n");  // Not entered yet

					/* Swap the operands to be sure that we test
						* how the QPUs distinguish between these ops.
					 */
					swap_mux(&instr.alu.add.a,
					&instr.alu.add.b);
					swap_pack(&instr.alu.add.a_unpack,
					&instr.alu.add.b_unpack);
				default:
				break;
			}
		}

		uint64_t repack;
		if (!v3d_qpu_instr_pack(devinfo, &instr, &repack)) {
			printf(" - FAIL (pack)");
			return 1;
		}

		if (repack != in_code) {
			printf("- Repack FAILED: 0x%016llx: ", (long long)repack);
			const char *redisasm = v3d_qpu_disasm(devinfo, repack);
			printf("\"%s\"", redisasm);
			return 2;
		}

	return 0;
}


int main(int argc, char **argv) {
	// NOTE: device info is empty, IRL should be given a value
	//       Notably, ver == 0
	struct v3d_device_info devinfo = { };
	int retval = 0;

	devinfo.ver = 42;
	printf("version v%d.%d\n", devinfo.ver / 10, devinfo.ver % 10);

	for (int i = 0; i < ARRAY_SIZE(code); i++) {
		printf("\t0x%016llx", (long long)code[i]);

		const char *disasm_output = v3d_qpu_disasm(&devinfo, code[i]);
		printf(",  // %-56s", disasm_output);

		int ret = test_instr(&devinfo, code[i]);
		if (ret == 1) {
			retval = 1;
			continue;
		}
		if (ret == 2) {
			retval = 1;
		}


		printf("\n");
	}

	return retval;
}
