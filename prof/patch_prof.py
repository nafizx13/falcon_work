# OPTIONAL targets supported
#!/usr/bin/env python3
"""
Regenerate an instrumented copy of the vendored Falcon sources.

Purely additive: each target function is renamed to <name>__impl and a thin
wrapper carrying PROF_ENTER/PROF_EXIT takes over the original name, so every
existing call site -- including recursive ones -- goes through the counters.

Rerun this after any change to falcon/; it never edits falcon/ in place.
"""
import os
import shutil
import sys

# Paths derive from this script's own location (prof/ lives inside the work
# directory), so the bundle is portable. Override with FALCON_WORK if needed.
WORK = os.environ.get("FALCON_WORK") or os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))
PROF = os.path.join(WORK, "prof")
SRC = os.path.join(WORK, "falcon")
DST = os.path.join(WORK, "falcon_prof")
# Optional: the official test suite, for KAT validation of the patched tree.
TEST_FALCON = os.environ.get("FALCON_TEST_C", "")

# (file, segment, decl_line, name_with_args_open, sig, argnames, rettype, impl)
# rettype None => void
TARGETS = [
    ("sign.c", "PROF_SIGN_DYN", "void",
     "Zf(sign_dyn)",
     "int16_t *sig, inner_shake256_context *rng,\n"
     "\tconst int8_t *restrict f, const int8_t *restrict g,\n"
     "\tconst int8_t *restrict F, const int8_t *restrict G,\n"
     "\tconst uint16_t *hm, unsigned logn, uint8_t *tmp",
     "sig, rng, f, g, F, G, hm, logn, tmp", None, "sign_dyn__impl", 0),

    ("sign.c", "PROF_DO_SIGN_DYN", "static int",
     "do_sign_dyn",
     "samplerZ samp, void *samp_ctx, int16_t *s2,\n"
     "\tconst int8_t *restrict f, const int8_t *restrict g,\n"
     "\tconst int8_t *restrict F, const int8_t *restrict G,\n"
     "\tconst uint16_t *hm, unsigned logn, fpr *restrict tmp",
     "samp, samp_ctx, s2, f, g, F, G, hm, logn, tmp", "int", "do_sign_dyn__impl", 0),

    ("sign.c", "PROF_FFSAMPLING", "static void",
     "ffSampling_fft_dyntree",
     "samplerZ samp, void *samp_ctx,\n"
     "\tfpr *restrict t0, fpr *restrict t1,\n"
     "\tfpr *restrict g00, fpr *restrict g01, fpr *restrict g11,\n"
     "\tunsigned orig_logn, unsigned logn, fpr *restrict tmp",
     "samp, samp_ctx, t0, t1, g00, g01, g11, orig_logn, logn, tmp",
     None, "ffSampling_fft_dyntree__impl", 0),

    ("sign.c", "PROF_SAMPLERZ", "int",
     "Zf(sampler)", "void *ctx, fpr mu, fpr isigma",
     "ctx, mu, isigma", "int", "sampler__impl", 0),

    ("sign.c", "PROF_GAUSS0", "int",
     "Zf(gaussian0_sampler)", "prng *p", "p", "int", "gaussian0_sampler__impl", 0),

    ("sign.c", "PROF_BEREXP", "static int",
     "BerExp", "prng *p, fpr x, fpr ccs", "p, x, ccs", "int", "BerExp__impl", 0),

    # FPEMU: out-of-line in fpr.c. FPNATIVE: static inline in fpr.h.
    # Exactly one of these two matches, depending on build mode. OPTIONAL.
    ("fpr.c", "PROF_EXPM", "uint64_t",
     "fpr_expm_p63", "fpr x, fpr ccs", "x, ccs", "uint64_t", "fpr_expm_p63__impl", 1),
    ("fpr.h", "PROF_EXPM", "static inline uint64_t",
     "fpr_expm_p63", "fpr x, fpr ccs", "x, ccs", "uint64_t", "fpr_expm_p63__impl", 1),

    ("fft.c", "PROF_FFT", "void",
     "Zf(FFT)", "fpr *f, unsigned logn", "f, logn", None, "FFT__impl", 0),

    ("fft.c", "PROF_IFFT", "void",
     "Zf(iFFT)", "fpr *f, unsigned logn", "f, logn", None, "iFFT__impl", 0),

    ("common.c", "PROF_HASH2PT", "void",
     "Zf(hash_to_point_vartime)",
     "\n\tinner_shake256_context *sc,\n\tuint16_t *x, unsigned logn",
     "sc, x, logn", None, "hash_to_point_vartime__impl", 0),

    ("shake.c", "PROF_KECCAK", "static void",
     "process_block", "uint64_t *A", "A", None, "process_block__impl", 0),

    ("rng.c", "PROF_PRNG_REFILL", "void",
     "Zf(prng_refill)", "prng *p", "p", None, "prng_refill__impl", 0),
]


# Segments whose wrappers publish a PRNG-attribution context tag.
# Zf(prng_refill) is reached through prng_get_u8/prng_get_u64, which are
# inline in inner.h, so the callee cannot see its caller -- the enclosing
# region publishes the tag instead. Tags nest (samplerz -> gauss0 ->
# samplerz), so PROF_ENTER_CTX saves and restores rather than just setting.
CTX = {
    "PROF_SAMPLERZ": "PCTX_SAMPLERZ",
    "PROF_GAUSS0":   "PCTX_GAUSS0",
    "PROF_BEREXP":   "PCTX_BEREXP",
}


def build(decl, name, sig, args, ret, impl, seg):
    old = "\n%s\n%s(%s)\n{\n" % (decl, name, sig)

    static_impl = "static " + decl if not decl.startswith("static") else decl

    new = "\n/* falcon_prof: forward decl of the real body */\n"
    new += "%s\n%s(%s);\n\n" % (static_impl, impl, sig)
    new += "%s\n%s(%s)\n{\n" % (decl, name, sig)

    if seg == "PROF_PRNG_REFILL":
        # Hand-written: needs the delta value in order to attribute it by
        # context, which the generic ENTER/EXIT pair does not expose.
        new += "#ifdef FALCON_PROF\n"
        new += "\tuint32_t _pf_t0 = falcon_prof_now();\n"
        new += "\t%s(%s);\n" % (impl, args)
        new += "\tuint32_t _pf_d = (uint32_t)(falcon_prof_now() - _pf_t0);\n"
        new += "\tfalcon_prof_cyc[PROF_PRNG_REFILL] += _pf_d;\n"
        new += "\tfalcon_prof_calls[PROF_PRNG_REFILL]++;\n"
        new += "\tfalcon_prof_meas[PROF_PRNG_REFILL]++;\n"
        new += "\tPROF_PRNG_ATTRIB(_pf_d);\n"
        new += "#else\n"
        new += "\t%s(%s);\n" % (impl, args)
        new += "#endif\n}\n\n"
    elif seg in CTX:
        new += "\tPROF_ENTER_CTX(%s, %s);\n" % (seg, CTX[seg])
        if ret is None:
            new += "\t%s(%s);\n" % (impl, args)
            new += "\tPROF_EXIT_CTX(%s);\n}\n\n" % seg
        else:
            new += "\t%s _pf_r = %s(%s);\n" % (ret, impl, args)
            new += "\tPROF_EXIT_CTX(%s);\n\treturn _pf_r;\n}\n\n" % seg
    else:
        new += "\tPROF_ENTER(%s);\n" % seg
        if ret is None:
            new += "\t%s(%s);\n" % (impl, args)
            new += "\tPROF_EXIT(%s);\n}\n\n" % seg
        else:
            new += "\t%s _pf_r = %s(%s);\n" % (ret, impl, args)
            new += "\tPROF_EXIT(%s);\n\treturn _pf_r;\n}\n\n" % seg

    new += "%s\n%s(%s)\n{\n" % (static_impl, impl, sig)
    return old, new


def main():
    if os.path.isdir(DST):
        shutil.rmtree(DST)
    shutil.copytree(SRC, DST)

    only = os.environ.get("PROF_ONLY")
    sel = set(only.split(",")) if only else None

    touched = {}
    fail = 0
    for (fn, seg, decl, name, sig, args, ret, impl, opt) in TARGETS:
        if sel is not None and seg not in sel:
            continue
        path = os.path.join(DST, fn)
        with open(path, "r") as fh:
            text = fh.read()
        old, new = build(decl, name, sig, args, ret, impl, seg)
        n = text.count(old)
        if n != 1:
            if opt and n == 0:
                print("skip %-10s %-24s (not present in this FP mode)" % (fn, name))
                continue
            print("FAIL %-10s %-24s matched %d times (need 1)" % (fn, name, n))
            fail += 1
            continue
        text = text.replace(old, new)
        with open(path, "w") as fh:
            fh.write(text)
        touched.setdefault(fn, []).append(name)
        print("ok   %-10s %-24s -> %s" % (fn, name, seg))

    # add the include to every file we touched.
    # fpr.h has no include guard -- it is a fragment pasted into inner.h,
    # so anchoring on the inner.h include does not work there. Put it
    # immediately before the first injected wrapper instead; falcon_prof.h
    # is self-guarded, so repeat inclusion is harmless.
    NLC = chr(10)
    MARK = "/* falcon_prof: forward decl of the real body */"
    for fn in touched:
        path = os.path.join(DST, fn)
        with open(path, "r") as fh:
            text = fh.read()
        if '#include "inner.h"' in text:
            text = text.replace('#include "inner.h"',
                                '#include "inner.h"' + NLC + '#include "falcon_prof.h"', 1)
        else:
            text = text.replace(MARK,
                                '#include "falcon_prof.h"' + NLC + NLC + MARK, 1)
        with open(path, "w") as fh:
            fh.write(text)

    # Drivers and headers live in prof/ as the source of truth and are copied
    # in on every regeneration. They MUST end up inside the tree: a quoted
    # #include resolves against the including file's own directory first, so a
    # driver left outside picks up the wrong config.h and silently builds
    # against a different fpr type with no diagnostic.
    for extra in ("falcon_prof.h", "falcon_prof.c", "falcon_key.h",
                  "falcon_key_512.h", "falcon_key_1024.h", "cva6_io.h",
                  "prof_host.c", "prof_variance.c", "bench_sign_prof.c"):
        src = os.path.join(PROF, extra)
        if os.path.isfile(src):
            shutil.copy(src, DST)
        else:
            print("note: prof/%s missing, not copied" % extra)
    if TEST_FALCON and os.path.isfile(TEST_FALCON):
        shutil.copy(TEST_FALCON, DST)

    print("\n%d/%d wrappers applied" % (len(TARGETS) - fail, len(TARGETS)))
    return 1 if fail else 0


sys.exit(main())
