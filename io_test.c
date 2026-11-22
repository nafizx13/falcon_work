/* io_test.c - proves output + cycle counting work on Spike AND Verilator.
 *
 *   Spike     : riscv-none-elf-gcc ... io_test.c            (HTIF printf)
 *   Verilator : riscv-none-elf-gcc ... -DUSE_UART io_test.c (UART MMIO)
 */
#include "cva6_io.h"

/* Deliberately not static and not inlined, so it shows up as its own
   symbol in a profile -- the same property SamplerZ's sub-functions need. */
__attribute__((noinline))
uint64_t work(uint32_t iters) {
    uint64_t acc = 1;
    for (uint32_t i = 0; i < iters; i++) {
        acc = acc * 6364136223846793005ULL + 1442695040888963407ULL;
        acc ^= acc >> 17;
    }
    return acc;
}

int main(void) {
    io_init();
    io_puts("IO_TEST_START\n");

    /* Fixed overhead of the measurement itself. Subtract this from
       every bracket to avoid attributing the counter reads to the code. */
    uint64_t o0 = rd_mcycle();
    uint64_t o1 = rd_mcycle();
    io_report("measure_overhead_cycles", o1 - o0);

    /* Bracket a known amount of work. */
    uint64_t c0 = rd_mcycle();
    uint64_t i0 = rd_minstret();
    uint64_t r  = work(1000);
    uint64_t c1 = rd_mcycle();
    uint64_t i1 = rd_minstret();

    io_report("work_cycles",   c1 - c0);
    io_report("work_instr",    i1 - i0);
    io_report("work_result_lo", (uint32_t)r);   /* keeps the loop alive */

    /* mcycle at exit. Compare against the "after N cycles" line the
       Verilator testbench prints: the difference is boot overhead. */
    io_report("mcycle_at_exit", rd_mcycle());

    io_puts("IO_TEST_DONE\n");
    return 0;
}
