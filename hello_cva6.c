/* hello_cva6.c - the hello world that actually prints on real CVA6 RTL.
 *
 * The stock verif/tests/custom/hello_world/hello_world.c fails twice over:
 *   1. no trailing '\n', so syscalls.c's putchar never flushes its buffer
 *   2. printf goes over HTIF, which ariane_tb.cpp never services
 *
 * cva6_io.h fixes both: newline-terminated output, and a UART back-end
 * selected with -DUSE_UART for the RTL simulator.
 */
#include "cva6_io.h"

int main(void) {
    io_init();

    io_puts("0: Hello World !\n");

    /* Prove it is really the core running this, not the testbench:
       report cycles spent inside a small loop. */
    uint64_t c0 = rd_mcycle();
    volatile int sum = 0;
    for (int i = 0; i < 100; i++) sum += i;
    uint64_t c1 = rd_mcycle();

    io_report("sum", (uint64_t)sum);
    io_report("loop_cycles", c1 - c0);

#ifdef USE_UART
    io_puts("backend=UART (Verilator RTL)\n");
#else
    io_puts("backend=HTIF (Spike)\n");
#endif

    io_puts("HELLO_DONE\n");
    return 0;
}
