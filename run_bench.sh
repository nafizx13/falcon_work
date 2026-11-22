#!/bin/bash
# Falcon benchmark runner.   Usage:  ./run_bench.sh spike | rtl | spike-trace
#
#   spike        compile directly + run spike with NO commit trace.
#                Fast, small log. Use this for correctness and instr counts.
#   spike-trace  via cva6.py WITH the commit trace (needed for profile_csv.py).
#                Produces ~100 bytes of log per instruction - gigabytes for Falcon.
#   rtl          via cva6.py on the Verilator model.
set -u
MODE="${1:-}"
BENCH="${2:-sign}"   # sign (default, key precomputed) | full (runs keygen)
W=$HOME/falcon-work
C=$HOME/cva6
# TARGET selects the core. There is deliberately NO default: cv64a6_imafdc_sv39
# (rv64, hardware double, allows FP=native) is the primary target and carries
# every headline result, while cv32a60x (rv32, no FPU) is the robustness check
# for datapath width. A silent default measures the wrong core for hours, which
# is the same trap CVA6's own Makefile:103 sets -- see docs/FALCON_CVA6_HANDOFF.md
# section 12. Pass it explicitly.
# ISA/ABI strings are copied from cva6.py's own target table so the compile
# matches what the RTL model was built for.
TARGET="${TARGET:?not set. Use TARGET=cv64a6_imafdc_sv39 (primary) or TARGET=cv32a60x (width check)}"
case "$TARGET" in
cv32a60x)
    ISA=rv32imc_zba_zbb_zbs_zbc
    MABI=ilp32
    LD=$C/config/gen_from_riscv_config/$TARGET/linker/link.ld
    ;;
cv64a6_imafdc_sv39)
    ISA=rv64gc_zba_zbb_zbs_zbc_zbkb_zbkx_zkne_zknd_zknh
    MABI=lp64d
    # No generated config dir exists for this target. The generic linker is
    # cva6.py's own documented fallback, and verif/sim/Makefile guards the
    # spike.yaml with ifneq($(wildcard ...)) so its absence is fine.
    LD=$C/config/gen_from_riscv_config/linker/link.ld
    ;;
*)
    echo "unknown TARGET=$TARGET"; exit 1 ;;
esac

# FP selects Falcon's floating-point implementation. emu = software emulation
# (the only option without an FPU); native = hardware double. The defaults in
# falcon/config.h are #ifndef-guarded, so these -D flags win.
# LOGN selects the Falcon parameter set: 9 = Falcon-512 (default), 10 =
# Falcon-1024. Both keypairs live in prof/ and falcon_key.h dispatches on
# FALCON_BENCH_LOGN; every buffer size derives from EMBEDDED_KEY_LOGN, so
# nothing else changes between parameter sets.
LOGN="${LOGN:-9}"
case "$LOGN" in
9|512)    LOGN=9;  N_DEF=""; N_SUF="" ;;
10|1024)  LOGN=10; N_DEF="-DFALCON_BENCH_LOGN=10"; N_SUF="_n1024" ;;
*) echo "unknown LOGN=$LOGN (use 9 or 10)"; exit 1 ;;
esac

FP="${FP:-emu}"
case "$FP" in
emu)    FP_DEF=""; FP_LIBS="" ;;
native) FP_DEF="-DFALCON_FPEMU=0 -DFALCON_FPNATIVE=1 -fno-math-errno"
        FP_LIBS="-lm" ;;
*)      echo "unknown FP=$FP (use emu|native)"; exit 1 ;;
esac
if [ "$FP" = "native" ] && [ "$TARGET" = "cv32a60x" ]; then
    echo "ERROR: cv32a60x has RVF=0/RVD=0 - no FPU - so FP=native cannot work."
    exit 1
fi

# work-ver/ holds ONE Verilator model. Read which target it was built for from
# the config package in its file list, and refuse to run a mismatched model
# rather than silently producing numbers for the wrong core.
model_target() {
    grep -oE "cv[0-9a-z_]+_config_pkg\.sv"         "$C/work-ver/Variane_testharness__verFiles.dat" 2>/dev/null         | sed "s/_config_pkg[.]sv//" | head -1
}

TAG="${TARGET}_${FP}${N_SUF}"

# BENCH=prof selects the instrumented tree (falcon_prof/) built by
# prof/patch_prof.py, plus its own driver. That driver MUST stay inside
# falcon_prof/: a quoted #include resolves against the including file's own
# directory first, so a driver outside the tree picks up falcon/config.h and
# silently builds against a different fpr type, with no diagnostic.
if [ "$BENCH" = "prof" ]; then
    FDIR=$W/falcon_prof
    BENCHFILE=$FDIR/bench_sign_prof.c
    PROF_SRC="$FDIR/falcon_prof.c"
    # PROF_COUNTERS=0 builds the SAME tree with the segment wrappers compiled
    # out. falcon_prof.h is zero-cost when FALCON_PROF is undefined, so this
    # isolates instrumentation cost from every other build difference -- the
    # falcon/ tree is a different tree and is not a valid control for it.
    # Output is tagged _nocount so it cannot overwrite a counters-on record.
    if [ "${PROF_COUNTERS:-1}" = "0" ]; then
        PROF_DEF=""
        TAG="${TAG}_nocount"
    else
        PROF_DEF="-DFALCON_PROF=1"
    fi
else
    FDIR=$W/falcon
    BENCHFILE=$W/bench_${BENCH/full/falcon}.c
    PROF_SRC=""
    PROF_DEF=""
fi

FALCON_SRC="$FDIR/codec.c $FDIR/common.c $FDIR/falcon.c $FDIR/fft.c $FDIR/fpr.c $FDIR/keygen.c $FDIR/rng.c $FDIR/shake.c $FDIR/sign.c $FDIR/vrfy.c $PROF_SRC"

# -fno-tree-loop-distribute-patterns: at -O2 GCC rewrites the byte-fill loop
# inside syscalls64.c's memset into a call to memset -> infinite recursion.
COMMON="-misa-spec=2.2 -fno-tree-loop-distribute-patterns \
$PROF_DEF $FP_DEF $N_DEF -I$W -I$FDIR -I$C/verif/tests/custom/env -I$C/verif/tests/custom/common \
-static -mcmodel=medany -fvisibility=hidden -nostdlib -nostartfiles -O2 -g"
SRCS="$BENCHFILE $W/shims.c $W/syscalls64.c $W/crt_bigstack.S $FALCON_SRC"
# cva6.py passes the test file via --c_tests, so exclude it here or main is duplicated
SRCS_NOMAIN="$W/shims.c $W/syscalls64.c $W/crt_bigstack.S $FALCON_SRC"

# cv32a60x is machine-mode only; sv39 targets have M/S/U. The spike parameter
# file only exists for generated targets - absent is fine, spike uses defaults
# and verif/sim/Makefile guards it with ifneq($(wildcard ...)) for RTL runs.
PRIV=m
[ "$TARGET" = "cv64a6_imafdc_sv39" ] && PRIV=msu
SPIKE_YAML=$C/config/gen_from_riscv_config/$TARGET/spike/spike.yaml
SPIKE_PARAM=""
[ -f "$SPIKE_YAML" ] && SPIKE_PARAM="--param-file $SPIKE_YAML"

case "$MODE" in
spike)
    ELF=$W/bench_${BENCH}_${TAG}.elf
    echo "=== compiling ==="
    riscv-none-elf-gcc $COMMON -march=${ISA}_zicsr_zifencei -mabi=$MABI \
        -T "$LD" $SRCS -lgcc $FP_LIBS -o "$ELF" 2>&1 | grep -vE "RWX permissions|^$"
    [ -f "$ELF" ] || { echo "COMPILE FAILED"; exit 1; }
    riscv-none-elf-size "$ELF"
    echo "=== running spike (no trace) ==="
    START=$(date +%s)
    LD_LIBRARY_PATH="$SPIKE_INSTALL_DIR/lib:${LD_LIBRARY_PATH:-}" \
    stdbuf -oL -eL "$SPIKE_INSTALL_DIR/bin/spike" \
        --isa=${ISA}_zicsr_zifencei --priv=$PRIV $SPIKE_PARAM \
        --log=/dev/null \
        "$ELF" 2>&1 | grep --line-buffered -vE "^\[OK\]|^\[SPIKE\]|misa_we"         | tee "$W/last_spike_run_${TAG}.log"
    END=$(date +%s)
    echo "=== wall clock: $((END-START))s ==="
    echo "=== saved to: $W/last_spike_run.log ==="
    ;;
spike-trace|rtl)
    if [ "$MODE" = "rtl" ]; then
        GCC_OPTS="-DUSE_UART $COMMON $SRCS_NOMAIN -lgcc $FP_LIBS"
        ISS=veri-testharness; OUTDIR=veri-testharness_sim
        EXTRA_A="--isscomp_opts=+define+COMMON_CELLS_ASSERTS_OFF"
        EXTRA_B="--issrun_opts=+debug_disable=1 +time_out=${TIME_OUT:-400000000} +trace_limit=${TRACE_LIMIT:-2000}"
    else
        GCC_OPTS="$COMMON $SRCS_NOMAIN -lgcc $FP_LIBS"
        ISS=spike; OUTDIR=spike_sim; EXTRA_A=""; EXTRA_B=""
    fi
    if [ "$MODE" = "rtl" ]; then
        HAVE=$(model_target)
        if [ -n "$HAVE" ] && [ "$HAVE" != "$TARGET" ]; then
            echo "ERROR: work-ver/ holds a model for $HAVE but TARGET=$TARGET"
            echo "  swap:  mv $C/work-ver $C/work-ver-$HAVE"
            echo "         mv $C/work-ver-$TARGET $C/work-ver"
            exit 1
        fi
        echo "=== model=$HAVE  target=$TARGET  FP=$FP  bench=$BENCH ==="
    fi
    cd "$C/verif/sim" || exit 1
    # cva6.py defaults --gen_timeout to 360 s, which killed the last RTL run.
    # Falcon signing is ~62.8 M instr => ~2.8 h on Verilator. Default here: 12 h.
    START=$(date +%s)
    python3 -W ignore cva6.py --c_tests "$BENCHFILE" --target "$TARGET" \
        --iss="$ISS" --iss_yaml=cva6.yaml --gcc_opts="$GCC_OPTS" $EXTRA_A "$EXTRA_B" --gen_timeout ${GEN_TIMEOUT:-43200} --iss_timeout ${ISS_TIMEOUT:-86400} --linker="$LD"
    RC=$?; END=$(date +%s)
    # The RTL opens its trace files from hardcoded names in write/truncate
    # mode - core/cva6.sv:1834 (trace_hart_%d.dasm), ITI/cva6_iti/iti.sv:34
    # (iti.traces), corev_apu/tb/rvfi_tracer.sv:53 - so every run destroys the
    # previous one and there is no plusarg to redirect them. Rename per
    # target/FP after the run instead of patching RTL (which needs a rebuild).
    for t in trace_hart_0.dasm iti.traces trace_rvfi_hart_0.dasm; do
        if [ -f "$C/verif/sim/$t" ]; then
            mv "$C/verif/sim/$t" "$C/verif/sim/${TAG}__$t"
            echo "kept trace: $C/verif/sim/${TAG}__$t ($(du -h "$C/verif/sim/${TAG}__$t" | cut -f1))"
        fi
    done
    echo "=== exit=$RC  wall clock=$((END-START))s ==="
    LOG=$(ls -t $C/verif/sim/out_*/$OUTDIR/bench_*.iss 2>/dev/null | head -1)
    [ -n "$LOG" ] && { echo "log: $LOG ($(du -h "$LOG" | cut -f1))"
      # Both FP modes produce the SAME .iss filename, so the second run would
      # overwrite the first. Save an extracted copy per target/FP immediately.
      SAVE=$W/rtl_prof_${TAG}.txt
      grep -aE "BENCH_|phase=|_cycles=|_instr=|_rc=|CHECKSUM|sig_len|failures|prof_pair|SEG " "$LOG" > "$SAVE"
      cat "$SAVE"
      grep -aE "[*]{3} (SUCCESS|FAILED)|%Fatal|trap_" "$LOG" | tail -3
      echo "=== results saved to: $SAVE ===" ; }
    ;;
*)
    echo "usage: $0 spike|rtl|spike-trace [sign|full]"; exit 2;;
esac
