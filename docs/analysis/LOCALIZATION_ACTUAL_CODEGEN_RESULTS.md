# Actual-codegen localization experiment

2026-11-10. Follow-up to [the initial offline audit](OFFLINE_SUBSYSTEM_AUDIT.md).

**Result: bounded leaf correctness passes; no consistent performance improvement.
Do not activate the option globally or widen the investigation automatically.**

## What changed in the experiment

Only `non_argument_as_local` changed from false to true. LR/MSR skipping,
nonvolatile localization, CTR/CR/XER localization, and reserved-register
localization remained false. The production manifest was not edited.

The active checkout has no prebuilt generator. Instead of rebuilding the whole
SDK, an isolated driver compiled the vendored SDK's unchanged configuration
loader, function graph/emitter, instruction dispatch/builders, disassembler,
binary view, and the small module/path/string support they require.

This exercises **actual `FunctionNode::emitCpp` output**, not manual C++
substitution. It does **not** exercise the CLI writer, full XEX loading, automatic
function discovery, graph recovery, template generation, or a complete game build.

The only infrastructure adapters are:

- An stderr logger, so warnings/errors remain visible rather than being suppressed.
- Import-resolution and late jump-table-scanner tripwires that throw if reached.
  They were not reached; the two fixtures are explicit leaf blocks with no calls.

Original SDK files were compiled directly, not copied/modified or replaced by
test implementations of instructions or configuration parsing.

## Target, source, and boundaries

- Fable HEAD: `fb9d2fb9554bff5a207015f4cac197272cabc497`.
- Vendored SDK HEAD: `521aa5906e8aa138870bf8d36bd602eef590ed55`.
- Target SHA-256:
  `88c4ef2e18e65409444d1b068eff921d1f7e180a5ae64edc64ba6b0872372662`.
- Ghidra-MCP verified Xenon360 XEX/PowerPC identity before reading the copy bytes.
- `0x82240578`: 12-byte constant-pointer getter, previously verified disassembly.
- `0x82CA30E8`: 168-byte aligned-copy routine, read from Ghidra memory in this
  experiment. Fixture provenance and bytes are in `tools/repro/localization/fixtures.json`.

The driver ran the **real production manifest loader** and confirmed all eight
audited options are effectively false. Thus the earlier TOML-scope finding is
also confirmed by actual loader execution, not only by reading the parser source.

For the baseline, both newly emitted bodies match the current production generated
bodies after normalizing whitespace and their test-only symbol names. The altered
output is archived raw and compiled without rewriting its body. Getter r11 and
copy r0 are local; the other register groups remain context-backed.

## Correctness results

| Check | Result |
| --- | --- |
| Getter against independent constant oracle | 8 cases pass |
| Copy matrix: 16 source alignments × 16 destination alignments × 81 lengths × 2 seeded states | 41,472 cases pass |
| Guard-page edge tests, both source-at-end and destination-at-end | 156 cases pass |
| Full buffer comparison against independent libc copy oracle, including untouched bytes | Pass |
| Context comparison against baseline | All bytes agree except getter r11 or copy r0, respectively |
| Localized scratch context slot remains its seeded input | Pass |
| Same positive suite under UBSan, fail-on-first-error | Pass, no diagnostics |
| Deliberately corrupt memory oracle | Expected failure detected, exit 1 |
| Handwritten consumer requiring r11 live after getter call | Expected contract mismatch detected, exit 1 |

Lengths cover 0–65 and boundaries around 128, 256, 512, 1024, and 4096 bytes.
Protected pages check valid page-edge accesses and zero-length operations whose
unused pointer is at the inaccessible boundary. Comparisons include the entire
accessible arena/page, not just the destination bytes.

### What these results do not certify

- The context exclusion is intentional and explicit. This is **not full-context
  equivalence**. In particular, the scratch-consumer negative control demonstrates
  why a caller relying on that scratch register would be incompatible.
- The negative scratch consumer is a handwritten test of the post-call contract,
  not a newly translated direct/indirect caller or proof of game caller liveness.
- Copy inputs are valid, disjoint RAM ranges with no 32-bit address wrapping.
  Overlapping copies, MMIO, physical aliases, production write-watch callbacks,
  concurrent modification, and guest fault/exception behavior are not covered.
- CTR, CR, XER, other registers, and nonvolatile state are compared to the exact
  baseline context; this does not independently certify the baseline Xbox ABI.
- Floating-point/vector operations, mid-assembly hooks, save/restore helpers,
  nonstandard entry points, SEH, and setjmp/longjmp were not exercised.
- The generated PCH is real, but the application's optional function-tracing
  header is omitted. No installed game binary/deployment has been validated.

These limits are reasons not to infer global compatibility from the positive suite.

## Microbenchmark: mixed, small changes

Host: Intel Core i9-10900K; Clang 23.1.4; x86-64-v2/SSE4.1, large code model,
`-O3 -fno-strict-aliasing -ffp-model=strict`. Exact commands, dependency revisions,
compiler version, and environment are archived.

Repeated copies use hot RAM, an aligned destination, and the indicated source
alignment. Measurements include resetting argument registers and the call. There
is one warmup round, then eight measured rounds of 20,000 calls per variant,
alternating off/on order. **No CPU affinity, scheduler, clock, or system settings
were changed; this is not a controlled hardware performance study.**

| Bytes | Source alignment | Baseline median ns/call | Localized median ns/call | Median latency change |
| ---: | ---: | ---: | ---: | ---: |
| 64 | 0 | 17.364 | 18.354 | +5.70% |
| 64 | 1 | 30.444 | 30.046 | −1.31% |
| 256 | 0 | 56.908 | 60.116 | +5.64% |
| 256 | 1 | 111.988 | 109.503 | −2.22% |
| 4096 | 0 | 864.478 | 877.648 | +1.52% |
| 4096 | 1 | 1780.095 | 1749.796 | −1.70% |

Lower latency is better. These are observations from this isolated host run,
not statistically established regressions/improvements, game call-size
distributions, Steam Deck results, or FPS gains. The earlier manual probe also
localized CTR/CR/XER; its assembly counts must not be attributed to this one-option
experiment.

**Interpretation:** `non_argument_as_local` alone is not a demonstrated meaningful
performance win for this copy routine. This does not establish that every other
localization option is ineffective or that the configuration defect is harmless.

## Decision and stop condition

- Keep production unchanged. No basis here for global activation or a game build.
- Retain the configuration defect as confirmed, not fixed.
- Retain the positive and negative tests for future bounded work.
- Do not spend further sessions micro-tuning this leaf or testing every flag
  merely because the configuration issue exists.
- A wider experiment needs a specific reason: e.g. an identified computational
  path where cross-call context traffic is substantial, and a testable ABI
  boundary. Cross-call/hook/helper correctness tests must precede any rollout.

This is the agreed stopping point: the experiment answered a concrete question
without demonstrating enough benefit to justify widening scope automatically.

## Reproduce and retained evidence

```bash
python3 tools/repro/localization/run.py \
  --output out/performance/localization-actual-codegen-repeat --jobs 2
```

Requires Python 3.11+, Clang, CMake, Ninja, and the already initialized vendored
dependencies. There are no downloads or submodule updates. A new output directory
is mandatory; the tool does not overwrite earlier evidence.

Successful evidence:
`out/performance/localization-actual-codegen-20261110-v3/`:

- `result.json`: commands, results, hashes, explicit scope and preservation status.
- `raw_off.cpp`, `raw_on.cpp`: actual unmodified emitted bodies.
- `driver-source/`: exact experiment sources at execution time.
- `production-before.json`: 1,563 watched pre-existing source/header/config inputs.
- `correctness*`, `negative-*`, `benchmark*`: retained results/diagnostics.
- `environment.json`, `benchmark-summary.json`: reference revisions/environment
  and parsed timing observations.

Two setup failures were retained, not hidden: the first attempt omitted the SDK
guest-path dependency, and v2 then omitted its UTF-8 dependency. Both are isolated
driver link failures, not translator/test failures. Their logs, hashes, and driver
source snapshots remain in the unsuffixed and `-v2` directories. V3 links the real
unchanged dependencies; no successful no-op fallback was introduced.

All 1,563 watched production inputs remained byte-identical on all three runs.
The watch covers existing game/generated and SDK src/include inputs, not a full
filesystem backup or every third-party dependency. Dependency revisions and
additional disassembler input hashes were recorded separately after the run.
No game launch, production regeneration, SDK source edit, game build/staging,
security change, commit, or delegation occurred.
