/*
 * Variance study: N Falcon-512 signatures, different messages, one continuous
 * RNG stream (realistic usage). Emits per-signature CALL COUNTS, which are
 * architecture-independent -- combined with the measured per-call RTL cycle
 * costs, they give a distribution for the segment shares without needing N
 * simulator runs.
 *
 * Must live inside falcon_prof/ (see prof_host.c for why).
 */
#include <stdio.h>
#include <string.h>

#include "falcon.h"
#include "inner.h"
#include "falcon_prof.h"
#include "falcon_key.h"

#define LOGN        EMBEDDED_KEY_LOGN
#define TMP_SIGNDYN FALCON_TMPSIZE_SIGNDYN(LOGN)
#ifndef NRUNS
#define NRUNS 200
#endif

static uint8_t tmp[TMP_SIGNDYN];
static uint8_t sig[FALCON_SIG_PADDED_SIZE(LOGN)];

int
main(void)
{
	shake256_context rng;
	char msg[64];
	size_t sig_len;
	int i, r, bad = 0;

	shake256_init_prng_from_seed(&rng, "cva6-falcon-bench", 17);

	printf("run,attempts,samplerz,gauss0,berexp,expm,ffsampling,fft,ifft,keccak,refill\n");
	for (i = 0; i < NRUNS; i++) {
		int n = sprintf(msg, "CVA6 Falcon variance study message %d", i);

		falcon_prof_reset();
		sig_len = sizeof sig;
		r = falcon_sign_dyn(&rng, sig, &sig_len, FALCON_SIG_PADDED,
			falcon512_privkey, sizeof falcon512_privkey,
			msg, (size_t)n, tmp, sizeof tmp);
		if (r != 0) { bad++; continue; }

		printf("%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n", i,
			(unsigned long long)falcon_prof_calls[PROF_DO_SIGN_DYN],
			(unsigned long long)falcon_prof_calls[PROF_SAMPLERZ],
			(unsigned long long)falcon_prof_calls[PROF_GAUSS0],
			(unsigned long long)falcon_prof_calls[PROF_BEREXP],
			(unsigned long long)falcon_prof_calls[PROF_EXPM],
			(unsigned long long)falcon_prof_calls[PROF_FFSAMPLING],
			(unsigned long long)falcon_prof_calls[PROF_FFT],
			(unsigned long long)falcon_prof_calls[PROF_IFFT],
			(unsigned long long)falcon_prof_calls[PROF_KECCAK],
			(unsigned long long)falcon_prof_calls[PROF_PRNG_REFILL]);
	}
	if (bad) fprintf(stderr, "FAILED SIGNATURES: %d\n", bad);
	return bad != 0;
}
