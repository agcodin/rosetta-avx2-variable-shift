#include <stdio.h>
#include <stdint.h>

int main(void) {
    const uint64_t counts[] = {0x0000000000000040ull, 0x0000000100000001ull, 0x0000000100000000ull};
    for (unsigned i = 0; i < 3; ++i) {
        uint64_t src[2] = {0x1111111111111111ull, 0x1111111111111111ull};
        uint64_t cnt[2] = {counts[i], counts[i]};
        uint64_t dst[2] = {0, 0};
        __asm__ volatile("vmovdqu %1, %%xmm0\n\tvmovdqu %2, %%xmm1\n\t"
            "vpsllvq %%xmm1, %%xmm0, %%xmm2\n\tvmovdqu %%xmm2, %0"
            : "=m"(*(char(*)[16])dst) : "m"(*(const char(*)[16])src), "m"(*(const char(*)[16])cnt)
            : "xmm0", "xmm1", "xmm2", "memory");
        printf("  VPSLLVQ xmm count=0x%016llx -> 0x%016llx  SDM 0x0000000000000000  %s\n",
            (unsigned long long)counts[i], (unsigned long long)dst[0],
            dst[0] == 0 ? "ok" : "MISMATCH");
    }
    return 0;
}
