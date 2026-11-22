#!/bin/bash
# RTL run progress. Usage: ./progress.sh [expected_cycles]
# Reads the live trace file's cycle counter; falls back to file size if the
# last line is mid-write. Safe to run any time, including when nothing is up.
C=$HOME/cva6
T=$C/verif/sim/trace_hart_0.dasm
EXP="${1:-47000000}"

P=$(pgrep -f "work-ver/Variane_testharness" | head -1)
if [ -z "$P" ]; then
    echo "simulator: NOT RUNNING"
    [ -f "$T" ] && echo "stale trace present ($(du -h "$T" | cut -f1))"
    ls -t $HOME/falcon-work/rtl_prof_*.txt 2>/dev/null | head -3 | sed 's/^/latest results: /'
    exit 0
fi

EL=$(ps -o etimes= -p "$P" | tr -d ' ')
SZ=$(stat -c %s "$T" 2>/dev/null || echo 0)
CYC=$(tail -c 3000 "$T" 2>/dev/null | tail -1 | awk '{print $1}')
case "$CYC" in ''|*[!0-9]*) CYC=$(( SZ / 62 )); NOTE=" (estimated from file size)";; *) NOTE="";; esac

awk -v c="$CYC" -v e="$EL" -v x="$EXP" -v s="$SZ" -v n="$NOTE" 'BEGIN{
  printf "elapsed   : %d s (%.1f min)\n", e, e/60
  printf "cycle     : %d%s\n", c, n
  printf "progress  : %.1f%% of %d expected\n", 100*c/x, x
  if (e>0 && c>0) {
    r = c/e
    printf "rate      : %d cycles/s\n", r
    printf "remaining : %.1f min\n", (x-c)/r/60
  }
  printf "trace     : %.1f MB\n", s/1048576
}'
