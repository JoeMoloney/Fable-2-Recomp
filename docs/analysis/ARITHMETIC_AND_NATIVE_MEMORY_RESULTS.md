# Arithmetic lowering and native-memory comparison

2026-11-10. User-approved bounded offline follow-up to
[the localization experiment](LOCALIZATION_ACTUAL_CODEGEN_RESULTS.md).

## Outcome

1. **Do not enable blanket AVX/FMA flags.** Identical source compiled with those
   features failed the strict equivalence gate on a NaN sign in a guest FPR.
   No arithmetic speedup is claimed; the arithmetic benchmark was not run.
2. **Native bulk copying is a substantially stronger local lead.** The SDK's
   existing native `memcpy` body was approximately **8.7–56 times faster** than
   the translated aligned-copy routine in these hot-RAM microbenchmarks.
   RAM correctness, page-edge tests, and UBSan passed.
3. **No production replacement is ready.** The native adapter does not reproduce
   the original's scratch-register/flag updates. Caller contracts, production
   write-watch/fault behavior, and frame-time importance remain unestablished.

No game launch, production edit/regeneration, SDK source change, installed build,
staging, Ghidra annotation, security setting change, or delegation occurred.

## Baseline and scope

- Fable HEAD: `fb9d2fb9554bff5a207015f4cac197272cabc497`.
- Vendored SDK HEAD: `521aa5906e8aa138870bf8d36bd602eef590ed55`.
- Associated target SHA-256:
  `88c4ef2e18e65409444d1b068eff921d1f7e180a5ae64edc64ba6b0872372662`.
- Ghidra-MCP identity checked: Xenon360 XEX, PowerPC BE/VMX128.
- Host: Intel Core i9-10900K, Clang 23.1.4, Linux x86-64.
- Explicit test flags: `-O3 -DNDEBUG -march=x86-64-v2 -msse4.1
  -mcmodel=large -fno-strict-aliasing -ffp-model=strict`.

The ISA baseline follows the Linux wrapper. These are recorded **test flags**,
not recovered flags or disassembly from an installed game binary. In particular,
SDK directory-level compiler settings are not assumed to propagate automatically
to all application targets. The installed build tree remains unavailable here.

## A. Arithmetic investigation

### Samples and evidence

A bounded source census selected small FMA-containing leaves, plus an existing
computational override:

| Address / source | Observed computation |
| --- | --- |
| `0x82CF8DF0`, generated `.231.cpp` | Three scalar components, squared terms/FMA accumulation, square root |
| `0x82FD2088`, generated `.68.cpp` | Scalar differences, products, fused subtraction, comparisons and division |
| `0x82C62548`, active `src/core/hotfunc/unsorted/InterpolateFloatArray_82C62548.cpp` | Array/filter-style loop with repeated scalar FMA; active strong override used, not just unused generated copy |

Names/roles above describe the inspected operations, not recovered original engine
symbols or complete ABI contracts. No runtime frequency or critical-path cost was
established by selecting these samples.

Both variants use the same source bodies; only exported test names differ. The
targeted variant adds **only** `-mavx -mfma`, not `fast-math`, reordering, reduced
precision, or a replacement algorithm. Register-localization options were not
changed in this experiment.

Assembly confirms the baseline references the math-library `fma` symbol, while
the targeted variant uses scalar hardware FMA/FMSUB instructions. Single-precision
guest operations retain the source's double-FMA then float conversion sequence.
The library itself may use hardware FMA: this is evidence of call/interface
overhead, **not proof of software-emulated arithmetic**.

For the two routines requiring global constants, an isolated sparse guest arena
maps only test RAM and one constant page. Ghidra read-only memory confirmed bytes
at `0x82000C08`: `3f 80 00 00 00 00 00 00` (binary32 one and zero). This is not a
game loader, emulator session, or full mapped game image.

### Strict numerical gate: failed and retained

The harness compares entire guest contexts, accessible test RAM, host exception
flags/control state, and errno. Planned coverage includes four rounding modes,
two initial flush configurations, edge values, and seeded random inputs.

**The suite stopped in the first rounding/flush configuration**, during the
geometry sample, with exceptional inputs. The remaining configurations/filter
suite were not reached and must not be reported as passed.

Observed first failure:

```text
rounding=FE_TONEAREST, initial flush disabled
context byte offset 343: baseline ff, targeted 7f
f0 bits: fff8000220000000 / 7ff8000220000000
exception flags: 28 / 28
MXCSR: 1fa8 / 1fa8
errno: 0 / 0
```

Only the sign bit of that quiet-NaN FPR differed in this retained case; other
context bytes, including the return register, matched. The case includes infinities
and NaNs. Target assembly contains fused subtraction where the source expresses
`std::fma(..., -operand)`; that is a plausible explanation, but the exact first
diverging guest instruction was not single-stepped or independently proved.

This is **not evidence that ordinary finite results are wrong**, nor proof the
scratch FPR difference affects gameplay. C++ does not generally promise identical
NaN payload/sign choices across implementations. It nevertheless fails this
project's strict tested compatibility contract, and that contract was not weakened
to permit a blanket flag change.

Two standalone negative controls were run separately and detected their intended
failures: deliberately corrupted output, and unfused multiply/add versus the known
fused cancellation result `-2^-54`. They validate gate sensitivity, not completion
of the stopped positive suite. **No arithmetic benchmark was run.**

### Stop decision

Keep production ISA settings unchanged. Do not launch a compiler/NaN investigation
without a specific justified boundary. A later, narrowly justified arithmetic
replacement would need explicit special-value and caller-observability contracts,
not merely a faster assembly listing.

## B. Native copy kernel

### What was compared

- Exact current translated `MemCpy_Aligned_82CA30E8`, with only its exported test
  name changed (`generated/default/fable_2_recomp.215.cpp`). The previous actual
  emitter experiment independently matched this baseline body.
- Verbatim `native_memcpy` body extracted from SDK
  `src/kernel/crt/memory.cpp:20–22`, called through a small guest-address adapter:

```cpp
static void* native_memcpy(void* dst, const void* src, size_t n) {
  return std::memcpy(dst, src, n);
}
```

This is reuse of a primitive already present in our SDK, not imported application
code. The test does **not** register a CRT hook, modify a guest function mapping,
or instantiate the production runtime memory system. The adapter uses the current
generated address macro on low guest addresses in ordinary mapped RAM.

### Correctness contract and results

Inputs are valid disjoint ranges with no address wrapping or MMIO. The final
stimulus mixes high index bits so aligned source/destination blocks do not begin
with identical periodic contents. An independent byte-by-byte oracle checks the
entire accessible buffer, including untouched bytes.

| Check | Result |
| --- | --- |
| 16 × 16 alignments, 81 sizes, two seeded states | 41,472 cases pass |
| Source/destination at protected-page boundaries | 156 cases pass |
| Same positive suite under fail-fast UBSan | Pass, no diagnostics |
| Corrupt byte oracle | Expected failure detected |
| Corrupt native nonvolatile register | Expected failure detected |
| Omit native copying entirely | Expected failure detected |

The original may modify r0, r4–r10, CTR, and CR0; all other context bytes are
checked against entry state. The native adapter preserves its entire input
context. **The two outputs are deliberately not claimed to be full-context
equivalent.** A real caller may depend on those original scratch/pointer/count
updates, particularly if this is used as an internal copy helper.

### Hot-RAM timings

One warmup round, eight measured rounds of 20,000 calls per variant, alternating
execution order. Includes guest argument setup and the function call. Destination
is aligned; source alignment is indicated. No affinity, clocks, or scheduler
settings were changed. There is no LTO across the separate test translation units.
The built native adapter was disassembled and performs a tail dispatch to the
host copy implementation; it was not optimized into an empty test function.

| Bytes | Source alignment | Translated median ns | Native median ns | Local ratio |
| ---: | ---: | ---: | ---: | ---: |
| 64 | 0 | 17.333 | 1.984 | 8.74× |
| 64 | 1 | 30.361 | 2.197 | 13.82× |
| 256 | 0 | 56.738 | 4.087 | 13.88× |
| 256 | 1 | 111.247 | 5.129 | 21.69× |
| 4096 | 0 | 849.049 | 30.903 | 27.47× |
| 4096 | 1 | 1741.893 | 31.105 | 56.00× |

A preceding run showed the same broad local advantage. These are not cold-memory
bandwidth results, game copy-size distributions, Steam Deck measurements, or FPS
predictions. The gain removes both translated algorithm costs and incidental guest
bookkeeping; it must not be attributed exclusively to SIMD or byte swapping.

### Next useful gate — not integration

**GO for a bounded caller-contract audit of this copy family. NO-GO for installing
the adapter or configuring a global CRT replacement yet.**

The next audit should establish:

1. Whether actual callers consume only copied bytes/r3, or also depend on updated
   r4/r5/r6, other scratch registers, or flags. Start with a small fixed caller set,
   not an unbounded traversal.
2. Whether a narrow entry is restricted to nonoverlapping ordinary RAM, or also
   reaches device/physical aliases, concurrent buffers, or unusual layouts.
3. How a native write interacts with the production fault handler/write-watch
   instruction decoder and GPU publication. Guard-page tests do not establish this.
4. Whether existing retained profiles offer a useful time budget for this copy
   family. Static call-site counts and local speedup cannot establish FPS benefit.

Do not generalize from this helper to the larger `0x82CA2C60` copy routine, all
game allocators, or the entire memory subsystem without their own contracts.

## Evidence and reproduction

Arithmetic (expected to stop at its retained strict numerical failure):

```bash
python3 tools/repro/arithmetic/run.py \
  --output out/performance/arithmetic-repeat
```

Native RAM-kernel test:

```bash
python3 tools/repro/native_memory/run.py \
  --output out/performance/native-memory-repeat
```

Use new output paths. Neither tool downloads dependencies, invokes production
codegen, builds the game, installs anything, or starts it.

- `out/performance/arithmetic-lowering-20261110-preflight/`: initial assembly only.
- `out/performance/arithmetic-lowering-20261110-v1/`: first numerical failure.
- `out/performance/arithmetic-lowering-20261110-v2/`: same failure with additional
  diagnostics; no numerical tolerance/oracle changes.
- `out/performance/native-memory-kernel-20261110-v1/`: first passing kernel run.
- `out/performance/native-memory-kernel-20261110-v2/`: final stronger nonperiodic
  stimulus, added omitted-copy negative control, and repeated timings.
- `out/performance/arithmetic-native-summary-20261110/`: environment, verification,
  arithmetic negative controls, parsed native ratios, and wrapper disassembly.

Each run retains commands, source snapshots, source/artifact hashes, and diagnostics.
All 320 arithmetic and 316 native watched production inputs remain unchanged.
These are scoped input checks, not complete repository backups. SDK tracked state
remains clean; pre-existing user work was not reset or overwritten.
