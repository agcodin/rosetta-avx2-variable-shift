# Rosetta 2 gets AVX2 64-bit variable shifts wrong

Rosetta 2 translates `VPSLLVQ` and `VPSRLVQ` incorrectly when a shift count has a nonzero upper half. It decides saturation from the low 32 bits of each 64-bit count, so a count that should produce zero instead performs a small shift. I found this on an Apple M5 Pro running macOS 26.7.1 (build 25G313) while writing a hardware oracle for a different piece of work, and it reproduces in about forty lines of C.

## What the instructions are supposed to do

The AVX2 variable shifts take a vector of data and a vector of counts, then shift each element by its own count. The 64-bit forms are `VPSLLVQ` and `VPSRLVQ`.

The count is a full 64-bit value rather than a masked one, and the shift saturates, so any count at or above the element width gives zero instead of wrapping around. That is the opposite of how the scalar `shl` and `shr` behave, since those mask the count down to its low 6 bits and a count of 64 leaves the value untouched. The Intel SDM states it in the operation section for `VPSLLV` and `VPSRLV`, where each element is compared against the width before the shift happens.

A count of `0x0000000100000001` sits far above 64, so it must produce zero.

## What Rosetta actually does

It shifts the value left by one.

```
VPSLLVQ  src=0x1111111111111111 count=0x0000000000000040 -> 0x0000000000000000  SDM 0x0000000000000000  ok
VPSLLVQ  src=0x1111111111111111 count=0x00000000ffffffff -> 0x0000000000000000  SDM 0x0000000000000000  ok
VPSLLVQ  src=0x1111111111111111 count=0x0000000100000001 -> 0x2222222222222222  SDM 0x0000000000000000  MISMATCH
VPSLLVQ  src=0x1111111111111111 count=0x0000000100000000 -> 0x1111111111111111  SDM 0x0000000000000000  MISMATCH
VPSLLVQ  src=0x1111111111111111 count=0x8000000000000001 -> 0x2222222222222222  SDM 0x0000000000000000  MISMATCH
VPSLLVQ  src=0x1111111111111111 count=0xffffffff00000001 -> 0x2222222222222222  SDM 0x0000000000000000  MISMATCH
VPSLLVQ  src=0x1111111111111111 count=0x00000001000000ff -> 0x0000000000000000  SDM 0x0000000000000000  ok
VPSLLVQ  src=0x1111111111111111 count=0xffffffffffffffff -> 0x0000000000000000  SDM 0x0000000000000000  ok
```

Read down the count column and the rule falls out. Rosetta is using `count & 0xFFFFFFFF` in place of the count. A count of `0x100000001` becomes 1, and a count of `0x100000000` becomes 0, which is why that row returns the source unchanged. The last two rows look correct for the wrong reason, because their low halves are already at or above 64 and saturate on their own.

`VPSRLVQ` behaves the same way, and both the `VEX.256` and `VEX.128` encodings are affected. The 32-bit forms `VPSLLVD`, `VPSRLVD` and `VPSRAVD` are all correct across the same counts, which fits the theory, since a 32-bit element has no upper half to discard.

That gives eight mismatches out of the twenty-two 64-bit cases in the probe, and zero out of fifteen in the 32-bit cases.

## Reproducing it

`vpsllvq_probe.c` in this directory is self-contained and needs no libraries beyond stdio.

```
clang -arch x86_64 -mavx2 -O2 -o vpsllvq_probe vpsllvq_probe.c
./vpsllvq_probe
```

It must be built for x86-64 so that Rosetta translates it. If you write your own version of this and reach for the `_mm256_sllv_epi64` intrinsic, the compiler may fold the whole thing at compile time, and the arm64 compiler folds it correctly according to the SDM, which hides the bug completely. The probe uses inline assembly with memory operands and a `volatile` qualifier so the instruction has to execute at run time.

## Why anyone should care

Most code will never reach this, because shift counts normally come from a small range and a count above the element width is usually a mistake in the caller rather than a deliberate request for zero.

The code that does reach it is code that relies on saturation on purpose. Clamping and masking idioms use a large count to mean "give me zero here" and expect the instruction to deliver, which is exactly why the saturating behaviour is specified rather than left undefined. Compilers emit these shifts from autovectorized loops too, so the count is not always a value a programmer chose. Under Rosetta, such a count silently becomes a shift by its low half, and nothing faults or warns. A wrong number comes out and the program keeps running.

The practical cost lands on anything that treats a translated x86 binary as a reference for what x86 does. I ran into it while building a hardware oracle, which is a test that runs a reimplemented instruction next to the real one and compares the results. Mine was correct and the host was wrong, so the test failed and pointed at my own code. That took a while to unpick, and it is worth knowing about before you spend the same hours.

## Measured on

- Apple M5 Pro, Mac17,9
- macOS 26.7.1, build 25G313
- Rosetta 2, translating an x86-64 binary built by Apple clang

Filed with Apple as FB25600143. The same divergence is recorded as technical debt in [AnyPS5](https://github.com/boykopovar/AnyPS5), where the oracle leaves these counts out of the comparison and names the host it was measured on.
