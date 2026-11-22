/*
 * Segment profiling counters for Falcon on CVA6.
 *
 * Zero-cost when FALCON_PROF is not defined: the wrappers injected by
 * patch_prof.py become pure pass-throughs and the compiler removes them.
 *
 * Timing uses only the low 32 bits of mcycle -- one CSR read per boundary
 * instead of the three needed for a carry-safe 64-bit read on rv32. Deltas
 * are valid as long as a single measured region is under 2^32 cycles, which
 * every segment here is by orders of magnitude.
 */
#ifndef FALCON_PROF_H__
#define FALCON_PROF_H__

#include <stdint.h>

enum {
	PROF_SIGN_DYN = 0,	/* Zf(sign_dyn): whole inner signing call     */
	PROF_DO_SIGN_DYN,	/* do_sign_dyn: one attempt (retry loop body) */
	PROF_FFSAMPLING,	/* ffSampling_fft_dyntree (recursive)         */
	PROF_SAMPLERZ,		/* Zf(sampler): SamplerZ, the accel target    */
	PROF_GAUSS0,		/* Zf(gaussian0_sampler): BaseSampler / RCDT  */
	PROF_BEREXP,		/* BerExp: rejection test                     */
	PROF_EXPM,		/* fpr_expm_p63: ApproxExp (FPEMU only)       */
	PROF_FFT,		/* Zf(FFT)                                    */
	PROF_IFFT,		/* Zf(iFFT)                                   */
	PROF_HASH2PT,		/* Zf(hash_to_point_vartime)                  */
	PROF_KECCAK,		/* process_block: Keccak-f[1600] permutation  */
	PROF_PRNG_REFILL,	/* Zf(prng_refill): ChaCha20 block refill     */
	PROF_N
};

extern uint64_t falcon_prof_cyc[PROF_N];
extern uint64_t falcon_prof_calls[PROF_N];
extern uint64_t falcon_prof_meas[PROF_N];
extern uint32_t falcon_prof_depth[PROF_N];
extern const char * const falcon_prof_names[PROF_N];

void falcon_prof_reset(void);

static inline uint32_t
falcon_prof_now(void)
{
#if defined(__riscv)
#if __riscv_xlen == 32
	uint32_t x;
#else
	uint64_t x;
#endif
	__asm__ volatile ("csrr %0, mcycle" : "=r" (x));
	return (uint32_t)x;
#elif defined(__x86_64__)
	/* host validation only -- lets the instrumented tree be checked
	 * against the official test suite before spending simulator time */
	uint32_t lo, hi;
	__asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
	(void)hi;
	return lo;
#else
	return 0;
#endif
}

#ifdef FALCON_PROF

/*
 * calls[] counts every entry including recursive ones; meas[] counts only
 * outermost entries, i.e. the number of timed regions -- that is the figure
 * to multiply by per-pair counter overhead when correcting.
 */
#define PROF_ENTER(id)						\
	uint32_t _pf_t0 = 0;					\
	do {							\
		falcon_prof_calls[id]++;			\
		if (falcon_prof_depth[id]++ == 0) {		\
			falcon_prof_meas[id]++;			\
			_pf_t0 = falcon_prof_now();		\
		}						\
	} while (0)

#define PROF_EXIT(id)						\
	do {							\
		if (--falcon_prof_depth[id] == 0) {		\
			falcon_prof_cyc[id] +=			\
			  (uint32_t)(falcon_prof_now() - _pf_t0); \
		}						\
	} while (0)

#else

#define PROF_ENTER(id)	((void)0)
#define PROF_EXIT(id)	((void)0)

#endif

#endif
