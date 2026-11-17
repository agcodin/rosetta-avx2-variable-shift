#include <stdio.h>
#include <stdint.h>
#include <string.h>

enum { SHL_Q, SHR_Q, SHL_D, SHR_D, SAR_D };

static const char* opname(int op) {
    switch (op) {
    case SHL_Q: return "VPSLLVQ";
    case SHR_Q: return "VPSRLVQ";
    case SHL_D: return "VPSLLVD";
    case SHR_D: return "VPSRLVD";
    default:    return "VPSRAVD";
    }
}

static void run(int op, const uint64_t* src, const uint64_t* cnt, uint64_t* dst) {
    switch (op) {
    case SHL_Q:
        __asm__ volatile("vmovdqu %1, %%ymm0\n\tvmovdqu %2, %%ymm1\n\t"
            "vpsllvq %%ymm1, %%ymm0, %%ymm2\n\tvmovdqu %%ymm2, %0"
            : "=m"(*(char(*)[32])dst) : "m"(*(const char(*)[32])src), "m"(*(const char(*)[32])cnt)
            : "ymm0", "ymm1", "ymm2", "memory");
        break;
    case SHR_Q:
        __asm__ volatile("vmovdqu %1, %%ymm0\n\tvmovdqu %2, %%ymm1\n\t"
            "vpsrlvq %%ymm1, %%ymm0, %%ymm2\n\tvmovdqu %%ymm2, %0"
            : "=m"(*(char(*)[32])dst) : "m"(*(const char(*)[32])src), "m"(*(const char(*)[32])cnt)
            : "ymm0", "ymm1", "ymm2", "memory");
        break;
    case SHL_D:
        __asm__ volatile("vmovdqu %1, %%ymm0\n\tvmovdqu %2, %%ymm1\n\t"
            "vpsllvd %%ymm1, %%ymm0, %%ymm2\n\tvmovdqu %%ymm2, %0"
            : "=m"(*(char(*)[32])dst) : "m"(*(const char(*)[32])src), "m"(*(const char(*)[32])cnt)
            : "ymm0", "ymm1", "ymm2", "memory");
        break;
    case SHR_D:
        __asm__ volatile("vmovdqu %1, %%ymm0\n\tvmovdqu %2, %%ymm1\n\t"
            "vpsrlvd %%ymm1, %%ymm0, %%ymm2\n\tvmovdqu %%ymm2, %0"
            : "=m"(*(char(*)[32])dst) : "m"(*(const char(*)[32])src), "m"(*(const char(*)[32])cnt)
            : "ymm0", "ymm1", "ymm2", "memory");
        break;
    default:
        __asm__ volatile("vmovdqu %1, %%ymm0\n\tvmovdqu %2, %%ymm1\n\t"
            "vpsravd %%ymm1, %%ymm0, %%ymm2\n\tvmovdqu %%ymm2, %0"
            : "=m"(*(char(*)[32])dst) : "m"(*(const char(*)[32])src), "m"(*(const char(*)[32])cnt)
            : "ymm0", "ymm1", "ymm2", "memory");
        break;
    }
}

static uint64_t sdm_q(int op, uint64_t value, uint64_t count) {
    if (count >= 64) return 0;
    return op == SHL_Q ? value << count : value >> count;
}

static uint32_t sdm_d(int op, uint32_t value, uint32_t count) {
    if (op == SAR_D) {
        int32_t signed_value = (int32_t)value;
        if (count >= 32) return (uint32_t)(signed_value < 0 ? -1 : 0);
        return (uint32_t)(signed_value >> count);
    }
    if (count >= 32) return 0;
    return op == SHL_D ? value << count : value >> count;
}

static int failures = 0;

static void check_q(int op, uint64_t value, uint64_t count) {
    uint64_t src[4], cnt[4], dst[4];
    for (int i = 0; i < 4; ++i) { src[i] = value; cnt[i] = count; dst[i] = 0; }
    run(op, src, cnt, dst);
    uint64_t want = sdm_q(op, value, count);
    int bad = dst[0] != want;
    failures += bad != 0;
    printf("  %-8s src=0x%016llx count=0x%016llx -> 0x%016llx  SDM 0x%016llx  %s\n",
        opname(op), (unsigned long long)value, (unsigned long long)count,
        (unsigned long long)dst[0], (unsigned long long)want, bad ? "MISMATCH" : "ok");
}

static void check_d(int op, uint32_t value, uint32_t count) {
    uint32_t src[8], cnt[8], dst[8];
    for (int i = 0; i < 8; ++i) { src[i] = value; cnt[i] = count; dst[i] = 0; }
    run(op, (const uint64_t*)src, (const uint64_t*)cnt, (uint64_t*)dst);
    uint32_t want = sdm_d(op, value, count);
    int bad = dst[0] != want;
    failures += bad != 0;
    printf("  %-8s src=0x%08x count=0x%08x -> 0x%08x  SDM 0x%08x  %s\n",
        opname(op), value, count, dst[0], want, bad ? "MISMATCH" : "ok");
}

int main(void) {
    const uint64_t value = 0x1111111111111111ull;
    printf("64-bit element counts (low 32 bits in range, upper half nonzero):\n");
    const uint64_t counts[] = {1, 63, 64, 0xFFull, 0xFFFFFFFFull,
        0x0000000100000001ull, 0x0000000100000000ull, 0x8000000000000001ull,
        0xFFFFFFFF00000001ull, 0x00000001000000FFull, 0xFFFFFFFFFFFFFFFFull};
    for (unsigned i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
        check_q(SHL_Q, value, counts[i]);
        check_q(SHR_Q, value, counts[i]);
    }
    printf("\n32-bit element counts, for contrast:\n");
    const uint32_t counts32[] = {1, 31, 32, 0xFF, 0xFFFFFFFFu};
    for (unsigned i = 0; i < sizeof(counts32) / sizeof(counts32[0]); ++i) {
        check_d(SHL_D, 0x11111111u, counts32[i]);
        check_d(SHR_D, 0x11111111u, counts32[i]);
        check_d(SAR_D, 0x91111111u, counts32[i]);
    }
    printf("\n%d mismatch(es) against the Intel SDM\n", failures);
    return failures != 0;
}
