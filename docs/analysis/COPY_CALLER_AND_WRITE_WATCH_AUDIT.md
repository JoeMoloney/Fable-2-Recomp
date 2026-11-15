# Copy callers and write-watch boundary audit

2026-11-10. Bounded read-only follow-up to
[the native RAM-kernel comparison](ARITHMETIC_AND_NATIVE_MEMORY_RESULTS.md).

## Decision

**No global `memcpy` replacement.** A verified caller supplies overlapping ranges.
The most useful narrow candidate is an **overlap-safe leftward word-array move**,
not an unrestricted memcpy hook.

The audited vector wrapper reaches this helper only for **4, 8, or 12 bytes**.
Its large-copy branch bypasses it. The previous 4-KiB microbenchmark gain must not
be applied to that wrapper's large-copy path.

One earlier concern is narrowed: ordinary physical write-watch faults are handled
before host instruction decoding. The MMIO decoder does not automatically prevent
native SIMD/string copies from resuming on watched RAM. **That is structural source
evidence, not a successful native-copy integration test.**

Stop here before implementation or broader engine reconstruction. The next useful
experiment is a whole-caller overlap-safe fixture plus a separately scoped real
write-watch test—not another open-ended caller search.

## Baseline and fixed scope

- Fable HEAD `fb9d2fb9554bff5a207015f4cac197272cabc497`.
- Vendored SDK HEAD `521aa5906e8aa138870bf8d36bd602eef590ed55`.
- `default.xex` SHA-256:
  `88c4ef2e18e65409444d1b068eff921d1f7e180a5ae64edc64ba6b0872372662`.
- Ghidra-MCP checked Xenon360 XEX / PowerPC BE VMX128 identity; three callers
  decompiled read-only. No function names, signatures, or annotations changed.

The direct generated-source inventory contains **51 call expressions in 34 caller
bodies**. This is not 51 runtime calls, a frequency estimate, or proof there are no
indirect/address-taken uses. Older manifest wording saying “34 direct call sites”
should not be treated as the current expression count.

Fixed deep-audit set:

| Caller | Call sites to `0x82CA30E8` | Scope |
| --- | --- | --- |
| `0x822085D0`, `MemCpy_Vectorized_822085D0` | `0x82208640` | Vector wrapper; small-copy continuation |
| `0x82CC0D58`, manifest-named `ProcessAndProcessAndProcess485_82CC0D58` | `0x82CC0DB4` | Stack exception-record payload |
| `0x82DF81A0`, `sub_82DF81A0` | `0x82DF81EC`, `0x82DF8234` | Two conditional leftward word-range moves |

Generated source was primary for register effects. Ghidra's inferred extra
parameters and save-helper “noop injection” warnings are not runtime ABI evidence;
the generated save/restore helpers still execute under current configuration.

## 1. Vector wrapper: small path only

Source: `generated/default/fable_2_recomp.223.cpp:716–1161`.

At entry, the wrapper saves its original destination in r26, size in r29, current
destination in r30, and current source in r31. For sizes **>=16**, it branches to
`0x82208690`, bypassing the aligned helper entirely.

For smaller sizes:

1. If size>=8 and source is 8-byte aligned, it copies 8 bytes directly and updates
   r29/r30/r31 itself.
2. If the remaining source is word-aligned and at least 4 bytes remain, it calls
   the helper with `remaining & ~3` bytes.
3. Afterwards it advances its own saved pointers from r29 and reduces its own
   counter. It does **not** take the helper's advanced r4/r5/r6 as continuation state.
4. A byte tail runs if necessary; r3 is restored from r26 before returning.

A restricted enumeration of lengths 0–15 and source alignments 0–15 gives helper
sizes exactly `{4, 8, 12}`. This is a Python mechanism check, not guest execution.

The helper's updated CR0 is overwritten by the wrapper's own `addic.` before the
following CR0 branch; CTR and r0 are not consumed by the inspected continuation.
`__restgprlr_26` only restores r26–r31 and LR via r12. This rules out the suspected
advanced-pointer dependency **within this continuation**, not at every caller or
every observer of the eventual returned context.

**Classification:** not a compelling initial target for the demonstrated large-copy
gain. Buffer ownership, overlap, MMIO exclusion, and lifetime are not proved at
this wrapper's generic entry. Do not replace its entire vector implementation on
the strength of this small-helper audit.

## 2. Exception payload: bounded stack destination, unknown source

Source: `generated/default/fable_2_recomp.130.cpp:23105–23179`.

The routine builds a record on its guest stack, clamps an incoming word count to
15, and copies at most **60 bytes** from incoming r6 to stack `r1+100`. It then
passes the record at `r1+80` to `RtlRaiseException`.

After the copy, r3 is rebuilt; no explicit guest instruction in this local
continuation consumes the helper's advanced pointers or CR0/CTR outputs. However:

- A stack destination does not prove the incoming source cannot alias it.
- The imported exception path may observe broader guest state; that contract
  was deliberately not expanded in this audit.
- No evidence establishes this exception-construction path as a performance cost.

**Classification:** exclude from optimization priority. Do not broaden the task
into exception-system reconstruction to save a small exceptional-path copy.

## 3. Word-array caller: real overlap

Source: `generated/default/fable_2_recomp.155.cpp:25483–25574`.

Observed local inputs are r3=record, r4=first index, r5=second index. The record
contains a data pointer at +0 and a count at +4. No original C++ type, ownership,
or all-input validation contract has been recovered.

Under the explicit restricted assumptions:

- aligned, nonwrapping ordinary RAM;
- count>=2;
- `0 <= first < second < count`;

the mechanism is:

1. Move `(second-first-1)` words from `data[first+1]` to `data[first]`.
2. Reduce count by two.
3. Move the remaining tail from `data[second+1]` to `data[second-1]`.

This removes two distinct indexed words in that domain. A source-level Python
model matches an independent ordered-delete oracle for **43,680** count/index
combinations (counts 2–64). **It is not execution of the translated caller or a
native `memmove` implementation, and does not certify malformed/other inputs.**

Concrete valid-domain overlap example:

```text
data=0x10000, count=16, first=2, second=10
first move:  dst=0x10008, src=0x1000C, len=28; overlap=24 bytes
second move: dst=0x10024, src=0x1002C, len=20; overlap=12 bytes
```

Both move toward lower addresses. The original forward copy works for these
leftward overlaps. Standard `memcpy` has undefined behavior on overlapping ranges,
even if a particular host libc happens to tolerate them. `memmove` is the correct
candidate primitive for this restricted byte-movement contract.

After the first call, the caller uses its own saved r29/r30/r31; it recomputes
the next copy arguments. After the second, it restores its stack/nonvolatiles and
returns. `__restgprlr_29` reads saved r29–r31/r12 from the guest stack and restores
LR; it does not consume the copy's advanced pointers, CTR, or CR0. No such
post-copy dependency was observed in this caller's explicit dataflow.

**Classification:** useful narrow candidate for a whole-caller fixture, not a
proven safe production replacement. Unknowns remain: real valid input domain,
object ownership/concurrency, heap/address classes, external observation of
scratch context, and frequency/critical-path cost.

This also does **not** establish that a global `memmove` replacement is equivalent:
the helper is forward-copying, so other callers' rightward overlaps could have
different propagation behavior. The other 31 caller bodies were not reconstructed.

### A small, testable helper post-state contract

The aligned branch suggests a way to avoid relying solely on “scratch registers
are probably dead.” For a future fixture, restrict the helper to:

- destination and source both 4-byte aligned;
- positive length divisible by four;
- canonical 32-bit input length, no address wrapping;
- source/destination are valid ordinary RAM, no concurrent modification;
- the leftward moves above (or independently established disjoint ranges).

Reading that branch gives this proposed final state, in addition to copied bytes:

```text
r0 = 0
r4 = original source register value + length
r6 = original destination register value + length
r7 = zero-extended big-endian final source word
CTR = 0
CR0 = {lt=0, gt=0, eq=1, so=entry XER.so}
other context slots unchanged (including r3, r5, XER, and LR)
```

In the leftward case the final source word is outside the destination's written
range. This suggests that a native move plus a small amount of final-state
bookkeeping could preserve the **full helper context**, not just a conventional
ABI subset. Zero-length, unaligned, wrapping, and unsupported memory cases must
remain outside this proposed specialization.

**This is a source-derived hypothesis for the next executable test, not a passed
implementation or compatibility claim.** Check it against the original helper
before trying the whole caller; do not silently accept a scratch-register mask if
the predicted full post-state fails.

## 4. Write-watch: fault invalidation is not MMIO emulation

Source trace, paths relative to SDK:

```text
SharedMemory::MakeRangeValid
  -> EnablePhysicalMemoryAccessCallbacks (A/C/E virtual aliases)
  -> PhysicalHeap::EnableAccessCallbacks (watch flags + read-only protection)

CPU write faults
  -> POSIX exception callback
  -> MMIOHandler::ExceptionCallback
       no registered MMIO range:
         -> protection recheck under global lock
         -> Memory::AccessViolationCallback
         -> PhysicalHeap::TriggerCallbacks
         -> SharedMemory::MemoryInvalidationCallback
         -> unprotect / clear watch flags -> retry faulting instruction

       registered MMIO range:
         -> TryDecodeLoadStore -> emulate device access / advance PC
```

Relevant source locations:

- `graphics/shared_memory.cpp:284–315`: marks pages valid and enables callbacks.
- `system/xmemory.cpp:682–690,2065–2172`: arms the A/C/E aliases and protections.
- `system/mmio_handler.cpp:371–433`: **no-range branch returns before decoder**.
- `system/xmemory.cpp:534–562`: finds the physical heap and triggers watched-page
  callbacks. Its call uses `unprotect=true` by default (`include/rex/system/xmemory.h:319–321`).
- `system/xmemory.cpp:2175–2329`: calls invalidation observers, unprotects eligible
  ranges, and clears watch flags. The callback can widen the invalidated range.
- `graphics/shared_memory.cpp:483–535`: invalidates valid/GPU-written flags and
  fires observers; widening avoids unrelated GPU-written data.
- `core/exception_handler_posix.cpp:263–335`: restores the handled context's PC.
  The watch branch does not advance it, allowing the faulting instruction to retry.

### What is established

For a handled ordinary watched-RAM fault, success does not require decoding the
native memcpy's vector or string instruction. The host instruction decoder is
used for registered MMIO access, a different branch. Its limited scalar decoder
must not be cited as proof native write-watch copying is impossible.

The earlier adapter used virtual guest-base addressing. Changing to direct
`physical_membase_` accesses would be a different operation: `xmemory.cpp:536–541`
explicitly excludes that bypass mapping from this invalidation path.

### What remains untested

No native copy was executed through the actual installed callback/heap/shared-memory
path here. In particular, no proof was obtained for cross-page wide stores,
partially progressed string instructions, aliases, rearming, simultaneous writers,
or real GPU publication/freshness.

The source architecture is encouraging for a bounded protected-page test, but
guard pages from the prior kernel experiment did not exercise any of these
production callbacks. Do not skip protections, manually clear validity bits, fake
publication, or bulk-unprotect ranges to make the native kernel work.

## 5. Performance-budget check

The two retained text reports inspected under
`out/performance/steamdeck-investigation-20261025/perf-deep-threads-20261025T101632Z/`
contain no matching aligned-copy or word-array-caller symbol/address. The self
report has only 430 user-space samples and a truncated top list. This is **not
absence evidence**, a call-frequency measurement, or a useful upper bound on the
family's present frame-time cost.

The `memcpy` stage in `PERF_INVESTIGATIONS_OCT_NOV_2026.md` (Oct Deck notes, summarised) measures
backend upload activity, not this guest copy family. Do not apply its percentage
to this proposed guest optimization.

**No frame-time budget was established.** Local kernel speedup remains insufficient
to predict an FPS improvement or justify broad production changes.

## Disposition and next gate

| Scope | Decision |
| --- | --- |
| Global helper replacement with memcpy | **No-go:** verified overlapping caller |
| Global helper replacement with memmove | **No-go:** unreviewed callers, possible different overlap behavior/context contracts |
| Vector wrapper's small call | Deprioritize: only 4/8/12 bytes, no large-copy gain established |
| Exception-record copy | Deprioritize: exceptional path, no budget/complete contract |
| Ordered two-index word-array move | **Candidate only:** test whole-caller behavior with overlap-safe moves |
| Native writes through actual watch callbacks | Separate bounded correctness gate; source structure alone is not certification |

The next experiment, if approved, should have two explicit pass/fail deliverables:

1. An isolated **whole-caller** fixture for `0x82DF81A0`, comparing original
   translated behavior with an overlap-safe candidate for the stated valid
   domain, including memory, count, retained state, and negative controls. First
   validate the proposed aligned helper's **full** post-state above; a failure
   there blocks the caller experiment rather than widening the allowed differences.
2. A separately scoped test using the real runtime watch/invalidation path. If
   bringing that path up requires rebuilding/reconstructing a large runtime,
   stop and report that prerequisite rather than introducing success stubs.

No production activation follows automatically, even if those pass. Performance
priority must still be established; otherwise keep the result as a validated
building block rather than another speculative FPS patch.

## Reproduction and evidence

```bash
python3 tools/repro/audit_copy_boundary.py \
  --output out/performance/copy-boundary-audit-repeat
```

New output path required. This performs source inventory and restricted Python
mechanism checks only—no codegen, native/guest fixture execution, runtime build,
or game launch.

Evidence: `out/performance/copy-boundary-audit-20261110/`:
`caller_inventory.json`, `selected/`, `runtime/`, `source-hashes.json`, `result.json`.
The inventory/trace's **299** generated/runtime source files were hash-checked
unchanged. `verification.json` adds the report/tool/header/profile provenance.
This is not a full repository backup. No existing user work was reset or replaced.
