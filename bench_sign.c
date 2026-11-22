/* bench_sign.c - Falcon-512 SIGN + VERIFY benchmark for CVA6 cv32a60x.
 *
 * Keygen is NOT run here.  The keypair is precomputed on the host by
 * host/gen_key.c and embedded as falcon_key.h.  Measured on Spike, keygen
 * was still running after 40 minutes; on the host it takes 122 ms.  It is
 * also a provisioning-time operation in real deployments, and it does not
 * exercise SamplerZ - it uses a different, coarser sampler.
 *
 * Dropping keygen also removes the ~260 KB stack demand of
 * solve_NTRU_intermediate, so the default 128 KB stack is enough again.
 *
 * Deterministic: fixed key + fixed seed => Spike and RTL must produce the
 * same SIG_CHECKSUM.  That equality is the correctness check.
 */
#include "cva6_io.h"
#include "falcon.h"
#include "inner.h"
#include "falcon_key.h"

#define LOGN        EMBEDDED_KEY_LOGN
#define TMP_SIGNDYN FALCON_TMPSIZE_SIGNDYN(LOGN)
#define TMP_VERIFY  FALCON_TMPSIZE_VERIFY(LOGN)

static uint8_t tmp[TMP_SIGNDYN];               /* 39,943 B - in .bss, not stack */
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

/* RCDT conformance: the two extremes of the 18-entry table. */
static void test_rcdt_extremes(void) {
    prng p; uint8_t v[9]; int z;
    for (int i = 0; i < 9; i++) v[i] = 0x00;
    for (int i = 0; i < 9; i++) p.buf.d[i] = v[i];
    p.ptr = 0;
    z = Zf(gaussian0_sampler)(&p);
    io_report("rcdt_zero_input", (uint64_t)z);
    expect(z == 18, "rcdt_zero_input != 18");

    for (int i = 0; i < 9; i++) p.buf.d[i] = 0xFF;
    p.ptr = 0;
    z = Zf(gaussian0_sampler)(&p);
    io_report("rcdt_max_input", (uint64_t)z);
    expect(z == 0, "rcdt_max_input != 0");
}

int main(void) {
    shake256_context rng;
    size_t sig_len;
    uint64_t c0, c1, i0, i1;
    int r;

    io_init();
    io_puts("BENCH_SIGN_START\n");
    io_report("logn", LOGN);

    c0 = rd_mcycle(); c1 = rd_mcycle();
    io_report("counter_overhead_cycles", c1 - c0);

    /* the embedded key survived the build intact */
    io_report("privkey_checksum", (uint64_t)fnv1a(falcon512_privkey, sizeof falcon512_privkey));
    io_report("pubkey_checksum",  (uint64_t)fnv1a(falcon512_pubkey,  sizeof falcon512_pubkey));
    expect(fnv1a(falcon512_privkey, sizeof falcon512_privkey) == EMBEDDED_PRIVKEY_CHECKSUM,
           "privkey checksum mismatch");
    expect(fnv1a(falcon512_pubkey, sizeof falcon512_pubkey) == EMBEDDED_PUBKEY_CHECKSUM,
           "pubkey checksum mismatch");

    test_rcdt_extremes();

    shake256_init_prng_from_seed(&rng, "cva6-falcon-bench", 17);

    /* ---------------- SIGN ---------------- */
    io_puts("phase=sign\n");
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

    /* ---------------- VERIFY ---------------- */
    io_puts("phase=verify\n");
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
    io_puts("BENCH_SIGN_DONE\n");
    return failures != 0;
}
