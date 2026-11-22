#include <stdint.h>
__attribute__((noinline)) uint64_t burn(uint32_t n){
  uint64_t a=1;
  for(uint32_t i=0;i<n;i++){ a=a*6364136223846793005ULL+1442695040888963407ULL; a^=a>>17; }
  return a;
}
int main(void){ return (int)(burn(5000) & 1); }
