#include "falcon_prof.h"

uint64_t falcon_prof_cyc[PROF_N];
uint64_t falcon_prof_calls[PROF_N];
uint64_t falcon_prof_meas[PROF_N];
uint32_t falcon_prof_depth[PROF_N];

const char * const falcon_prof_names[PROF_N] = {
	"sign_dyn",
	"do_sign_dyn",
	"ffsampling",
	"samplerz",
	"gauss0",
	"berexp",
	"expm",
	"fft",
	"ifft",
	"hash2pt",
	"keccak",
	"prng_refill"
};

void
falcon_prof_reset(void)
{
	int i;

	for (i = 0; i < PROF_N; i++) {
		falcon_prof_cyc[i] = 0;
		falcon_prof_calls[i] = 0;
		falcon_prof_meas[i] = 0;
		falcon_prof_depth[i] = 0;
	}
}
