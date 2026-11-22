#include <stdint.h>
#include <stdio.h>

static inline uint64_t rdcycle64(void) {
    uint64_t c;
    asm volatile ("csrr %0, cycle" : "=r"(c));
    return c;
}

int main(void) {
    printf("HELLO_WITH_NEWLINE works\n");          /* flushes on \n */
    uint64_t t0 = rdcycle64();
    volatile double x = 1.5;
    for (int i = 0; i < 1000; i++) x = x * 1.000001 + 0.5;
    uint64_t t1 = rdcycle64();
    printf("cycles_for_1000_fp_ops=%lu\n", (unsigned long)(t1 - t0));
    printf("DONE\n");
    return 0;
}
