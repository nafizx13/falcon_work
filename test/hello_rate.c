#include "cva6_io.h"
__attribute__((noinline)) uint64_t burn(uint32_t n){
  uint64_t a=1; for(uint32_t i=0;i<n;i++){ a=a*6364136223846793005ULL+1442695040888963407ULL; a^=a>>17; } return a;
}
int main(void){
    io_init();
    io_puts("0: Hello World !\n");
    uint64_t i0=rd_minstret(), c0=rd_mcycle();
    uint64_t r=burn(50000);
    uint64_t i1=rd_minstret(), c1=rd_mcycle();
    io_report("burn_instr",  i1-i0);
    io_report("burn_cycles", c1-c0);
    io_report("r_lo",(uint32_t)r);
    io_report("total_instr_at_exit", rd_minstret());
    io_puts("HELLO_DONE\n");
    return 0;
}
