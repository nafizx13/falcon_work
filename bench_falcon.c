/* bench_falcon.c - Falcon-512 baseline benchmark for CVA6 cv32a60x.
 *
 * Runs identically on Spike and on the Verilator RTL model:
 *   Spike     : ... -I$HOME/falcon-work -I$HOME/falcon-work/falcon
 *   Verilator : ... -DUSE_UART   (same sources)
 *
 * Everything is deterministic: a fixed seed means Spike and RTL must
 * produce byte-identical signatures.  Comparing SIG_CHECKSUM between the
 * two runs IS the correctness check - Spike's Falcon is the reference,
 * so agreement proves the RTL run computed real Falcon.
 *
 * All large buffers are static globals.  crt.S gives 128 KB of stack and
 * FALCON_TMPSIZE_SIGNDYN(9) alone is 39,943 bytes; putting that on the
 * stack would corrupt memory silently rather than fail loudly.
 */
#include "cva6_io.h"
#include "falcon.h"
#include "inner.h"

#define LOGN        9                       /* Falcon-512 */
#define TMP_KEYGEN  FALCON_TMPSIZE_KEYGEN(LOGN)     /* 15,879 */
#define TMP_SIGNDYN FALCON_TMPSIZE_SIGNDYN(LOGN)    /* 39,943 */
#define TMP_VERIFY  FALCON_TMPSIZE_VERIFY(LOGN)     /*  4,097 */
#define FALCON_TMP_MAX     TMP_SIGNDYN

static uint8_t tmp[FALCON_TMP_MAX];
static uint8_t privkey[FALCON_PRIVKEY_SIZE(LOGN)];
static uint8_t pubkey [FALCON_PUBKEY_SIZE(LOGN)];
static uint8_t sig    [FALCON_SIG_PADDED_SIZE(LOGN)];

static const char MSG[] = "CVA6 Falcon SamplerZ baseline message";
#define MSG_LEN (sizeof MSG - 1)

static int failures = 0;

static void expect(int ok, const char *what) {
    if (!ok) { io_puts("FAIL "); io_puts(what); io_putc('\n'); failures++; }
}

/* ---------- FNV-1a over the signature, so Spike and RTL can be compared
 *            without printing 666 bytes of hex. ---------- */
static uint32_t fnv1a(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;
    while (n--) { h ^= *p++; h *= 16777619u; }
    return h;
}

/* ================= RCDT conformance (BaseSampler) =================
 * gaussian0_sampler reads a 72-bit value and walks an 18-entry CDT.
 * Feeding all-zero bytes must yield 18; feeding 2^72-1 must yield 0.
 * These are the two extremes of the table your accelerator will hold.
 */
static void prefill_prng(prng *p, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) p->buf.d[i] = buf[i];
    p->ptr = 0;
}

static void test_rcdt_extremes(void) {
    prng p;
    uint8_t v[9];
    int z;

    for (int i = 0; i < 9; i++) v[i] = 0x00;
    prefill_prng(&p, v, 9);
    z = Zf(gaussian0_sampler)(&p);
    io_report("rcdt_zero_input", (uint64_t)z);
    expect(z == 18, "rcdt_zero_input != 18");

    for (int i = 0; i < 9; i++) v[i] = 0xFF;
    prefill_prng(&p, v, 9);
    z = Zf(gaussian0_sampler)(&p);
    io_report("rcdt_max_input", (uint64_t)z);
    expect(z == 0, "rcdt_max_input != 0");
}

/* ========================= main ========================= */
int main(void) {
    shake256_context rng;
    size_t sig_len;
    uint64_t c0, c1, i0, i1, ov0, ov1;
    int r;

    io_init();
    io_puts("BENCH_FALCON_START\n");
    io_report("logn", LOGN);

    /* cost of the instrument itself - subtract from every bracket below */
    ov0 = rd_mcycle();
    ov1 = rd_mcycle();
    io_report("counter_overhead_cycles", ov1 - ov0);

    test_rcdt_extremes();

    /* Fixed seed: makes the whole run reproducible and lets Spike and RTL
       be compared byte for byte. */
    shake256_init_prng_from_seed(&rng, "cva6-falcon-bench", 17);

    /* ---------------- keygen ---------------- */
    io_puts("phase=keygen\n");
    c0 = rd_mcycle(); i0 = rd_minstret();
    r = falcon_keygen_make(&rng, LOGN,
                           privkey, sizeof privkey,
                           pubkey,  sizeof pubkey,
                           tmp,     TMP_KEYGEN);
    c1 = rd_mcycle(); i1 = rd_minstret();
    expect(r == 0, "keygen");
    io_report("keygen_rc",     (uint64_t)(uint32_t)r);
    io_report("keygen_cycles", c1 - c0);
    io_report("keygen_instr",  i1 - i0);

    /* ---------------- sign ---------------- */
    io_puts("phase=sign\n");
    sig_len = sizeof sig;
    c0 = rd_mcycle(); i0 = rd_minstret();
    r = falcon_sign_dyn(&rng, sig, &sig_len, FALCON_SIG_PADDED,
                        privkey, sizeof privkey,
                        MSG, MSG_LEN,
                        tmp, TMP_SIGNDYN);
    c1 = rd_mcycle(); i1 = rd_minstret();
    expect(r == 0, "sign");
    io_report("sign_rc",     (uint64_t)(uint32_t)r);
    io_report("sign_cycles", c1 - c0);
    io_report("sign_instr",  i1 - i0);
    io_report("sig_len",     (uint64_t)sig_len);
    io_report("SIG_CHECKSUM", (uint64_t)fnv1a(sig, sig_len));

    /* ---------------- verify ---------------- */
    io_puts("phase=verify\n");
    c0 = rd_mcycle(); i0 = rd_minstret();
    r = falcon_verify(sig, sig_len, FALCON_SIG_PADDED,
                      pubkey, sizeof pubkey,
                      MSG, MSG_LEN,
                      tmp, TMP_VERIFY);
    c1 = rd_mcycle(); i1 = rd_minstret();
    expect(r == 0, "verify");
    io_report("verify_rc",     (uint64_t)(uint32_t)r);
    io_report("verify_cycles", c1 - c0);
    io_report("verify_instr",  i1 - i0);

    /* key checksums: confirm Spike and RTL generated the same keypair */
    io_report("PRIVKEY_CHECKSUM", (uint64_t)fnv1a(privkey, sizeof privkey));
    io_report("PUBKEY_CHECKSUM",  (uint64_t)fnv1a(pubkey,  sizeof pubkey));

    io_report("failures", (uint64_t)(uint32_t)failures);
    io_puts(failures == 0 ? "BENCH_RESULT=PASS\n" : "BENCH_RESULT=FAIL\n");
    io_puts("BENCH_FALCON_DONE\n");
    return failures != 0;
}
