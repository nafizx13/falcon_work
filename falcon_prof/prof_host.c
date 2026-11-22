/*
 * Host-side segment dump. Mirrors bench_sign_prof.c exactly (outer
 * falcon_sign_dyn, embedded key, same message, same 17-byte seed) so the
 * signature checksum must equal the one the uninstrumented build produced
 * on Spike. Cycle magnitudes are x86 TSC and mean nothing for the paper;
 * the call counts, the invariants and the checksum are the point.
 *
 * Must live inside falcon_prof/: a quoted #include resolves against the
 * including file's own directory first, so a driver kept outside the tree
 * silently picks up the wrong config.h and builds against a different fpr
 * type (uint64_t under FPEMU, double under FPNATIVE) with no diagnostic.
 */
#include <stdio.h>
#include <string.h>

#include "falcon.h"
#include "inner.h"
#include "falcon_prof.h"
#include "falcon_key.h"

#define LOGN        EMBEDDED_KEY_LOGN
#define TMP_SIGNDYN FALCON_TMPSIZE_SIGNDYN(LOGN)

static uint8_t tmp[TMP_SIGNDYN];
static uint8_t sig[FALCON_SIG_PADDED_SIZE(LOGN)];

static const char MSG[] = "CVA6 Falcon SamplerZ baseline message";
#define MSG_LEN (sizeof MSG - 1)

static uint32_t
fnv1a(const uint8_t *p, size_t n)
{
	uint32_t h = 2166136261u;
	while (n--) { h ^= *p++; h *= 16777619u; }
	return h;
}

static int
inv(const char *what, int ok)
{
	printf("%-28s %s\n", what, ok ? "ok" : "VIOLATED");
	return ok ? 0 : 1;
}

int
main(void)
{
	shake256_context rng;
	size_t sig_len;
	int r, i, bad;

	shake256_init_prng_from_seed(&rng, "cva6-falcon-bench", 17);

	falcon_prof_reset();
	sig_len = sizeof sig;
	r = falcon_sign_dyn(&rng, sig, &sig_len, FALCON_SIG_PADDED,
		falcon512_privkey, sizeof falcon512_privkey,
		MSG, MSG_LEN, tmp, sizeof tmp);

	printf("sign_rc=%d sig_len=%zu\n", r, (size_t)sig_len);
	printf("SIG_CHECKSUM=%u\n", (unsigned)fnv1a(sig, sig_len));
	printf("\n%-14s %14s %12s %12s\n", "segment", "cycles", "calls", "measured");
	for (i = 0; i < PROF_N; i++) {
		printf("%-14s %14llu %12llu %12llu\n",
			falcon_prof_names[i],
			(unsigned long long)falcon_prof_cyc[i],
			(unsigned long long)falcon_prof_calls[i],
			(unsigned long long)falcon_prof_meas[i]);
	}

	printf("\n-- invariants --\n");
	bad = 0;
	bad += inv("samplerz >= gauss0+berexp",
		falcon_prof_cyc[PROF_SAMPLERZ] >=
		falcon_prof_cyc[PROF_GAUSS0] + falcon_prof_cyc[PROF_BEREXP]);
	bad += inv("berexp >= expm",
		falcon_prof_cyc[PROF_BEREXP] >= falcon_prof_cyc[PROF_EXPM]);
	bad += inv("sign_dyn >= ffsampling",
		falcon_prof_cyc[PROF_SIGN_DYN] >= falcon_prof_cyc[PROF_FFSAMPLING]);
	bad += inv("berexp calls == expm calls",
		falcon_prof_calls[PROF_BEREXP] == falcon_prof_calls[PROF_EXPM]);
	bad += inv("samplerz calls <= gauss0",
		falcon_prof_calls[PROF_SAMPLERZ] <= falcon_prof_calls[PROF_GAUSS0]);
	bad += inv("samplerz calls == 2n",
		falcon_prof_calls[PROF_SAMPLERZ] == (2u << LOGN));
	bad += inv("ffsampling calls == 2n-1",
		falcon_prof_calls[PROF_FFSAMPLING] == (2u << LOGN) - 1);
	for (i = 0; i < PROF_N; i++) {
		if (falcon_prof_depth[i] != 0) {
			bad += inv("all depths zero", 0);
			break;
		}
	}
	if (i == PROF_N) bad += inv("all depths zero", 1);

	printf("\n%s\n", bad ? "INVARIANTS FAILED" : "INVARIANTS OK");
	return r != 0 || bad != 0;
}
