# Offline CPU, memory, and timing audit

Date: 2026-11-10. **Initial audit complete; stop before production changes.**

## Outcome

One confirmed configuration defect is worth a controlled follow-up: all eight
code-generation options at `fable_2_manifest.toml:218–225` belong to the final
`[[entrypoint.midasm_hook]]`, not `[entrypoint]`. The current source loader ignores
them there. Register localization is consequently not enabled by those settings.
Generated source is consistent with the disabled defaults.

Compile-only probes demonstrate removable context bookkeeping in two functions.
They do **not** demonstrate game speedup or certify enabling any option globally.
In particular, **do not relocate all eight options and regenerate the game**:
`skip_msr` changes synchronization, and existing overrides depend on the current
register/save-helper interface.

No game launch, production modification, full game/SDK build, code generation,
Ghidra database modification, external checkout update, or delegation occurred.
Only this report, a reproduction tool, and offline evidence artifacts were added.

## 1. Baseline and evidence ownership

Target: `default.xex`, SHA-256
`88c4ef2e18e65409444d1b068eff921d1f7e180a5ae64edc64ba6b0872372662`.
Ghidra-MCP confirms Xenon360 XEX,
`PowerPC:BE:64:Xenon-VMX128-32addr`, image base `0x82000000`.

| Source | Revision | Role in this audit |
| --- | --- | --- |
| Fable II checkout | `fb9d2fb9554bff5a207015f4cac197272cabc497` | Actual manifest, generated C++, overrides, build rules |
| Vendored ReXGlue SDK | `521aa5906e8aa138870bf8d36bd602eef590ed55` | Actual loader, generator, headers, runtime |
| Separate `/mnt/HDD/git_stuff/rexglue-sdk` | `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518` | Located/revision recorded; not substituted for vendored SDK |
| `my_git/xenia-canary-fable2-linux` | `f448664b6d0eea63ed67a098bd3ca2dd129532a0` | Clock implementation comparison |
| `/mnt/HDD/git_stuff/xenia-canary` | `838850fed8389bdb28dc38f89bcb73bb43db1494` | Located/revision recorded only |
| Unleashed Recompiled | `cf829a9eca8fb680fba4b0409ddeb6ca92f22e3c` | Translator-option configuration comparison |
| Skate 3 recomp | `f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd` | Located/revision recorded; prior source audit reused |

The Fable checkout already contains modified/untracked work, and the SDK contains
pre-existing nested untracked content. Revision IDs alone do not identify all
working-tree bytes. The probe result hashes its eleven relevant production inputs
before/after; revision/status metadata is archived separately. None were edited.

The active generation path is **ReXGlue's integrated generator**, not a separate
XenonRecomp executable: `generated/rexglue.cmake:117–124` invokes `rex::rexglue
codegen`; `build-release-linux_amd64.sh` selects the vendored source SDK and clears
`FABLE2_CODEGEN_TOOL`. A standalone XenonRecomp checkout was not located within
the bounded inventory. Unleashed's `tools/XenonRecomp` directory is empty. This is
not a blocker; do not turn this audit into an unrestricted filesystem search.

`out/build/` is empty in this checkout. No installed game executable or retained
compile database was available there to prove deployment provenance or inspect
its machine code. Instead, representative **current source** was compiled to
assembly with explicit flags. This is not inspection of the user's last game build.

Detailed evidence belongs here/in repository artifacts; memory-service project 3
is the concise durable index. Historical memories are not current task queues.

### Prior work that must not be repeated as a new discovery

| Prior result | Consequence |
| --- | --- |
| `docs/fixes/FPS_CAP_INVESTIGATION.md`, then `docs/RUNTIME_FIXES.md` | Guest vblank gating and host FPS limiting have already been investigated. The later pacing controls refine the old `vsync=0` advice. |
| `PERF_INVESTIGATIONS_OCT_NOV_2026.md` (Oct Deck notes, summarised) | Very high yield counts were measured; a prior 50-us pause reduced CPU usage without improving cadence. Do not treat busy CPU as proof of the frame bottleneck. |
| Same result, following the Oct source review (summarised in `PERF_INVESTIGATIONS_OCT_NOV_2026.md`) | Zero `rt_sigprocmask` calls on the three observed threads excluded the proposed fiber-mask cost in that scene/window. Do not resurrect it from sparse old samples. |
| `PERF_INVESTIGATIONS_OCT_NOV_2026.md` (Oct Deck notes, summarised) | Historical readback fallback had 35–39 ms inclusive CPU waits for queued GPU work; not measured shader time or guaranteed recoverable frame time. CPU reader/freshness questions remain unresolved. |
| `PERF_INVESTIGATIONS_OCT_NOV_2026.md` (Oct Deck notes, summarised) | Old memory-lock percentages predate later fixes. Readback allocation/cleanup was small relative to the old fence waits; allocator caching is not a demonstrated major win. |
| `plans/future/native-renderer/NATIVE_RENDERER_SOURCE_AUDIT.md`, memory 167 | Unleashed/Skate architectures are not drop-in Fable subsystem replacements; reuse/licensing and game-specific contracts matter. |
| Memories 51, 53, 103, 104 | Old synthetic XenonRecomp/custom-runtime results and instruction omissions do not certify or diagnose the current ReXGlue fork. |

Existing measurements are dated, scene/platform-specific evidence—not new desktop
measurements and not proof that the candidate below addresses the dominant stall.

## 2. Initial subsystem boundary map

This maps representative implementation boundaries, not every game allocator,
thread, or caller. Names/comments alone are not verified contracts.

### A. Translated CPU execution

```text
manifest [entrypoint] + includes
  -> ManifestConfig / RecompilerConfig
  -> function graph + instruction builders
  -> generated C++ and generated PCH macros
  -> host compiler
  -> native instructions
```

- Configuration: SDK `src/codegen/manifest.cpp:61–68,110–116`,
  `config.cpp:84–116`, `include/rex/codegen/config.h:90–99`.
- Register representation: SDK `src/codegen/builders/context.cpp:45–112` chooses
  `ctx.rN`/`ctx.crN` versus locals. `function_graph.cpp:640–675` declares locals.
- Instruction lowering: SDK `src/codegen/builders/{memory,vector,system,control_flow}.cpp`.
- Generated memory/dispatch/time interfaces: `generated/default/fable_2_pch.h`.
  Direct calls are native calls. Indirect calls use a range-checked guest-address
  dispatch table with runtime fallback (`:257–275`), not instruction interpretation.
- SIMD already exists: vector builders use SIMDe intrinsics; SDK target settings
  request SSE4.1 and Linux wrappers request x86-64-v2. The reviewed build paths do
  not establish a global `SIMDE_NO_NATIVE` setting. Scalar-only historical synthetic
  harness flags are not evidence that the actual game disables native SIMD.
- Linux target settings also select `-mcmodel=large`. Changing that or the ISA
  baseline needs separate address-range/portability evidence; neither is a free win.
- The application appends `fable2_func_trace.h` to the generated target's PCH.
  Tracing disabled by default still has a relaxed atomic enable gate at function
  entry. That is an implementation fact, not a measured bottleneck. Probes below
  omit that application tracing header to isolate register representation.

**Safe investigation boundary:** generator representation and explicit call ABI.
Do not conflate local bookkeeping with guest memory or remove architectural state
needed by hooks, exceptions, indirect calls, or nonstandard entry points.

### B. Guest memory, bulk operations, and allocation

```text
translated loads/stores -> generated address/endian macros -> mapped guest arena
translated copy/allocator routines -> guest-visible memory/state
imported virtual allocation -> native kernel service -> BaseHeap / host mappings
optional identified CRT hooks -> native memory/heap implementations
```

- `fable_2_pch.h:119–137`: scalar memory operations use volatile host accesses and
  byte swapping. Linux's physical host offset is zero in this generated header;
  Windows has a special high-address adjustment. Volatile is an optimization
  constraint, but removing it globally requires a concurrency/MMIO contract.
- Copy primitives remain translated: e.g. `MemCpy_SdkRuntime_82CA2C60`
  (`fable_2_recomp.271.cpp`) and `MemCpy_Aligned_82CA30E8`
  (`fable_2_recomp.215.cpp`). This audit does not certify all callers as ordinary,
  nonoverlapping RAM copies, or replace them with libc.
- SDK `src/kernel/crt/memory.cpp:20–85` already implements native copy/fill hooks.
  `src/codegen/codegen_writer.cpp:195–203` binds explicitly configured CRT names.
  The current entrypoint has no `rexcrt` mapping. Presence of native hooks in the
  SDK is therefore not proof these guest functions use them.
- SDK `src/kernel/xboxkrnl/xboxkrnl_memory.cpp:63` implements native virtual
  allocation; `src/system/xmemory.cpp:1250` onward manages guest heap/page ranges.
  These are distinct from the game's small-object allocator and CRT heap.
- SDK `src/kernel/crt/heap.cpp` implements an optional mutex-protected o1heap
  allocator with guest-address segments; `rexcrt_heap_enable` defaults false.
  Generated `REXCRT_HEAP` is 0. This does not establish a safe global allocator
  substitution or mean all heap allocation runs through host `malloc`.
- Host page protection and physical write-watch callbacks interact with GPU
  residency (`xmemory.cpp`, `shared_memory.cpp`, `mmio_handler.cpp`). Removing
  protection/copying/waits would change ownership/publication semantics.

**Boundary requirement before replacement:** byte order, address aliases, overlap,
alignment, fault/page-watch behavior, allocation lifetimes, and live guest state.
No complete game-allocator reconstruction was attempted in this initial pass.

### C. Clocks, waits, and system services

```text
guest mftb -> REX_QUERY_TIMEBASE -> Clock::QueryGuestTickCount
guest imports -> native xboxkrnl entries -> XThread / XObject
  -> duration conversion -> host sleep / wait / yield
```

- SDK `builders/system.cpp:266–276`, generated PCH `:293`, and
  `src/core/clock.cpp:66–89,158–169`: the default scaled guest clock goes through
  shared clock state and a mutex. Linux samples `CLOCK_MONOTONIC_RAW` in
  `clock_posix.cpp:39–51`.
- Local Xenia reference `src/xenia/base/clock.cc:95–119` has the same basic
  try-lock/update/wait structure. This is an inherited design, not proof of a
  new ReXGlue regression or significant clock contention.
- `KeDelayExecutionThread_entry` (`xboxkrnl_threading.cpp:377`) calls native
  `XThread::Delay` (`src/system/xthread.cpp:1000–1039`). Relative 100-ns units
  are truncated to milliseconds; a nonalertable zero-ms result yields or sleeps
  100 us depending on priority. Positive absolute deadlines are unsupported in
  this implementation. These are semantic limitations; caller relevance and
  performance impact are unestablished.
- `XObject::Wait`/`WaitMultiple` in `src/system/xobject.cpp:189–277` convert
  timeouts, handle completion/APC/timeout results, and use host wait handles.
  `MaybeYield` (`src/core/threading_posix.cpp:186–189`) invokes `sched_yield`
  and a full fence. These services are native runtime code, not translated game
  instructions that a C++ control-flow cleanup would fix.

**Boundary requirement:** guest units, absolute/relative semantics, timeout
rounding, APC delivery, event reset/ownership, ordering, termination, and pacing
must be specified before changing the clock/wait implementation.

## 3. Selected candidate: register-localization configuration

### Confirmed facts

1. Standard TOML parsing places `skip_lr`, `skip_msr`, `ctr_as_local`,
   `xer_as_local`, `reserved_as_local`, `cr_as_local`, `non_argument_as_local`,
   and `non_volatile_as_local` under hook `fable2_hook_realtime_texture_morphing`,
   address `0x82A7607C`. Comments/blank lines do not end a TOML table.
2. They are absent from `[entrypoint]`; its included switch-table file supplies
   none of them. `ManifestConfig` passes only the entrypoint table to the loader.
3. `RecompilerConfig` defaults these fields to false and reads options only at
   that table's top level. The hook parser (`config.cpp:310–369`) does not read
   them, nor does it reject these unexpected hook properties.
4. The current generated-source census finds **zero local GPR/CTR or CR
   declarations** across 295 translation units containing 60,753 function-definition
   macros. It also finds 351,049 `ctx.lr` assignments and 29,395 indirect-call
   macro sites. These are static textual counts, not runtime frequencies or
   dynamically executed instructions. Ghidra's 50,750 recovered functions are a
   different analysis inventory; count differences were not investigated.
5. Unleashed's `UnleashedRecompLib/config/SWA.toml:1–15` puts the same eight
   options under its `[main]` config before opening hook tables. This supports
   learning from existing projects' configuration, not transplanting their flags
   or claiming their ABI assumptions apply to Fable.

### Compile-only evidence

Tool: `tools/repro/audit_codegen_config.py`. Evidence directory:
`out/performance/offline-subsystem-audit-20261110-v2/`.

The first probe run is retained separately in
`out/performance/offline-subsystem-audit-20261110/`.

The tool extracts two current generated bodies, renames probe symbols, and compiles
against the actual generated PCH/SDK types using Clang 23.1.4, `-O3 -DNDEBUG
-march=x86-64-v2 -msse4.1 -mcmodel=large`. A second body models selected builder
accessors using local registers. **It is manually transformed, not output of an
alternate generator invocation. No code was linked or executed.**

| Function | Original instruction lines | Local-model instruction lines | Original / model context-memory operand lines |
| --- | ---: | ---: | ---: |
| `GetGlobalPtr3_82240578` | 3 | 2 | 2 / 1 |
| `MemCpy_Aligned_82CA30E8` | 220 | 191 | 66 / 41 |

Counts are static assembly lines across all branches, not cycles, memory bandwidth,
dynamic work, or a speedup ratio. In these probes RDI remains the context pointer.

Ghidra independently confirms the getter is `lis r11; addi r3,r11; blr`, and the
copy routine implements destination alignment, word copying, and a byte tail.
The getter's original host assembly stores both scratch r11 and result r3; the
local model stores only r3. The copy probe localizes r0, CTR, CR0, and XER.

**Compatibility limit:** the models intentionally stop publishing those scratch
registers/flags into `PPCContext`. This is not whole-context equivalence. Proving
that callers cannot observe those differences is a separate ABI/liveness task.
XER's model starts zeroed as the generator does, so status bits also require
explicit compatibility analysis. No memory-operation or SIMD replacement was made.

### Why global activation is currently a no-go

- `skip_msr` is **not just register allocation**. In SDK
  `builders/system.cpp:250–257,318–334`, it suppresses fences and the global-lock
  operations used to model MSR behavior. Never enable it as part of this experiment.
- `skip_lr` changes callsite/LR publication used by current overrides and
  diagnostics. Leave it false for the first experiment.
- Nonvolatile localization assumes standard entry ownership. SDK has explicit
  `share_registers` handling for SEH funclets and save/restore-helper elision
  (`builders/context.cpp:45–49,201–226`). That is evidence of real boundary risks.
- Existing overrides still call generated helpers directly. For example,
  `src/core/hotfunc/vblank/FrameLimiterWait_82242628.cpp:192–201` invokes
  `__savegprlr_29` and its epilogue invokes `__restgprlr_29`. Generator comments
  warn that helper bodies under nonvolatile localization can spill zero locals;
  generated callers elide those calls, but existing handwritten callers would
  not automatically change. Alternate actual output has not been generated to
  certify this interaction. Keep nonvolatile localization off initially.
- Mid-assembly hooks pass requested registers through generator accessors
  (`context.cpp:347–393`), which is encouraging but does not certify full hook,
  longjmp, exception, indirect-call, and override interoperability.

### Decision

**GO for a separate, isolated actual-codegen experiment; NO-GO for changing the
production manifest or enabling all options.** This is a concrete representation
opportunity, not a demonstrated fix for the prior frame stalls.

First proposed experiment after review:

1. Preserve production inputs; use a separate output directory and a matched
   generator. Add a schema/scope check that detects codegen options inside hooks.
2. Keep LR/MSR skipping and nonvolatile localization explicitly false. Test one
   narrower register-localization option at a time, first on a verified leaf.
3. Use actual generated A/B output, not manual substitution. Check guest memory,
   ABI-live outputs, preserved registers, and any observable context effects.
   Include explicit negative controls for unexpected cross-call register use.
4. Before widening scope, test direct/indirect calls, save/restore helpers, hooks,
   exceptions, and setjmp/longjmp against the current overrides.
5. Only after correctness tests, run isolated timing tests. Any local benefit
   remains separate from full-game frame-time impact, which ultimately needs
   representative runtime evidence.

Stop if proving live-state ownership requires reconstructing an unbounded caller
graph. Record that blocker instead of enabling global flags speculatively.

## 4. Ranked disposition of other leads

| Lead | Evidence category | Decision now |
| --- | --- | --- |
| Mis-scoped localization configuration | Confirmed config defect + compile-only mechanism evidence | Selected bounded follow-up above |
| Native bulk-memory hooks not mapped for identified copy functions | Confirmed boundary/source fact, unmeasured opportunity | Defer; verify overlap, faults/write-watch and state before replacing |
| Shared guest-clock mutex / coarse timeout conversion | Confirmed implementation characteristics, unmeasured impact | Defer; establish real caller requirements, not a blind lock-free rewrite |
| Native allocator replacement | Incomplete game allocator contracts; no new frequency/contention data | No-go for wholesale substitution |
| Blanket AVX/native flags, volatile removal, skipped waits/MSR | No safe global compatibility proof | No-go |
| Yield/fiber/readback allocation tweaks | Existing negative results or small historical budget | Do not repeat without materially new evidence |

## Reproduction and handoff

From the repository root (choose a **new** output directory):

```bash
python3 tools/repro/audit_codegen_config.py \
  --output out/performance/offline-subsystem-audit-repeat
```

Requires Python 3.11+ and Clang. The tool rejects an existing output directory,
never invokes codegen or the game, archives compiler diagnostics/assembly/hashes,
and checks its eleven watched production inputs remain unchanged. Other compiler
versions can produce different instruction counts. `inventory.json` and
`preservation.json` beside the v2 results record source revisions and final
input checks; they are not complete source backups or deployment verification.

**Next action:** review the isolated actual-codegen proposal above. No capture,
production regeneration, patch activation, or new subsystem expansion is queued
automatically by this report.
