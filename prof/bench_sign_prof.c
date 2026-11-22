/* bench_sign_prof.c - instrumented Falcon-512 SIGN + VERIFY for CVA6.
 *
 * Same measurement scope as bench_sign.c (outer falcon_sign_dyn, same
 * embedded key, same message, same seed) so the aggregate must reproduce
 * bench_sign.c's number -- that equality is the check that instrumentation
 * did not change what is being measured.
 *
 * Adds per-segment counters. Lives inside falcon_prof/ deliberately: a
 * quoted #include resolves against the *including file's* directory first,
 * so a driver kept outside the tree silently picks up the wrong config.h
 * and builds against a different fpr type. Do not move this file out.
 */
#include "cva6_io.h"
#include "falcon.h"
#include "inner.h"
#include "falcon_prof.h"
#include "falcon_key.h"

#define LOGN        EMBEDDED_KEY_LOGN
#define TMP_SIGNDYN FALCON_TMPSIZE_SIGNDYN(LOGN)
#define TMP_VERIFY  FALCON_TMPSIZE_VERIFY(LOGN)

static uint8_t tmp[TMP_SIGNDYN];
static uint8_t sig[FALCON_SIG_PADDED_SIZE(LOGN)];

static const char MSG[] = "CVA6 Falcon SamplerZ baseline message";
#define MSG_LEN (sizeof MSG - 1)

static int failures = 0;
static void expect(int ok, const char *what) {
    if (!ok) { io_puts("FAIL "); io_puts(what); io_putc('\n'); failures++; }
}

static uint32_t fnv1a(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;
    while (n--) { h ^= *p++; h *= 16777619u; }
    return h;
}

#define CAL_N 1000

/* Cost of one PROF_ENTER/PROF_EXIT pair on this target, so segment totals
 * can be corrected: true_cycles = cyc[i] - meas[i] * pair_cost. Measured
 * here rather than assumed, since it differs between Spike and RTL. */
static void calibrate(void) {
    uint64_t c0, c1;
    int i;

    falcon_prof_reset();
    c0 = rd_mcycle();
    for (i = 0; i < CAL_N; i++) {
        PROF_ENTER(PROF_KECCAK);
        PROF_EXIT(PROF_KECCAK);
    }
    c1 = rd_mcycle();
    io_report("prof_pair_x1000", c1 - c0);
    falcon_prof_reset();
}

static void dump_segments(void) {
    int i;
    for (i = 0; i < PROF_N; i++) {
        io_puts("SEG ");
        io_puts(falcon_prof_names[i]);
        io_puts(" cyc=");        io_putu64(falcon_prof_cyc[i]);
        io_puts(" calls=");      io_putu64(falcon_prof_calls[i]);
        io_puts(" meas=");       io_putu64(falcon_prof_meas[i]);
        io_putc('\n');
    }
}

int main(void) {
    shake256_context rng;
    size_t sig_len;
    uint64_t c0, c1, i0, i1;
    int r;
    

    io_puts("BENCH_SIGN_PROF_START\n");

    expect(fnv1a(falcon512_privkey, sizeof falcon512_privkey)
           == EMBEDDED_PRIVKEY_CHECKSUM, "privkey_checksum");
    expect(fnv1a(falcon512_pubkey, sizeof falcon512_pubkey)
           == EMBEDDED_PUBKEY_CHECKSUM, "pubkey_checksum");

    calibrate();

    shake256_init_prng_from_seed(&rng, "cva6-falcon-bench", 17);

    /* ---------------- SIGN ---------------- */
    io_puts("phase=sign\n");
    falcon_prof_reset();
    sig_len = sizeof sig;
    c0 = rd_mcycle(); i0 = rd_minstret();
    r = falcon_sign_dyn(&rng, sig, &sig_len, FALCON_SIG_PADDED,
                        falcon512_privkey, sizeof falcon512_privkey,
                        MSG, MSG_LEN, tmp, TMP_SIGNDYN);
    c1 = rd_mcycle(); i1 = rd_minstret();
    expect(r == 0, "sign");
    io_report("sign_rc",     (uint64_t)(uint32_t)r);
    io_report("sign_cycles", c1 - c0);
    io_report("sign_instr",  i1 - i0);
    io_report("sig_len",     (uint64_t)sig_len);
    io_report("SIG_CHECKSUM", (uint64_t)fnv1a(sig, sig_len));
    dump_segments();

    /* ---------------- VERIFY ---------------- */
    io_puts("phase=verify\n");
    falcon_prof_reset();
    c0 = rd_mcycle(); i0 = rd_minstret();
    r = falcon_verify(sig, sig_len, FALCON_SIG_PADDED,
                      falcon512_pubkey, sizeof falcon512_pubkey,
                      MSG, MSG_LEN, tmp, TMP_VERIFY);
    c1 = rd_mcycle(); i1 = rd_minstret();
    expect(r == 0, "verify");
    io_report("verify_rc",     (uint64_t)(uint32_t)r);
    io_report("verify_cycles", c1 - c0);
    io_report("verify_instr",  i1 - i0);

    io_report("failures", (uint64_t)(uint32_t)failures);
    io_puts(failures == 0 ? "BENCH_RESULT=PASS\n" : "BENCH_RESULT=FAIL\n");
    io_puts("BENCH_SIGN_PROF_DONE\n");
    return failures != 0;
}
