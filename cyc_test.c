#include <stdint.h>
#include <stdio.h>

/* machine-mode cycle counter: works in M-mode on both Spike and CVA6 RTL */
static inline uint64_t rdmcycle(void) {
    uint64_t c;
    asm volatile ("csrr %0, mcycle" : "=r"(c));
    return c;
}
/* machine-mode retired-instruction counter */
static inline uint64_t rdminstret(void) {
    uint64_t c;
    asm volatile ("csrr %0, minstret" : "=r"(c));
    return c;
}

int main(void) {
    printf("START\n");
    uint64_t c0 = rdmcycle(), i0 = rdminstret();
    volatile double x = 1.5;
    for (int i = 0; i < 1000; i++) x = x * 1.000001 + 0.5;
    uint64_t c1 = rdmcycle(), i1 = rdminstret();
    printf("mcycle_delta=%lu\n",   (unsigned long)(c1 - c0));
    printf("minstret_delta=%lu\n", (unsigned long)(i1 - i0));
    printf("DONE\n");
    return 0;
}
