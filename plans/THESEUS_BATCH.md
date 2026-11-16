# Ship of Theseus — batch job over the leaf pool (multi-session plan)

Started 2026-11-16. Follows the pilot ([THESEUS_PILOT](THESEUS_PILOT.md), verdict [THESEUS_PILOT_VERDICT](../docs/analysis/THESEUS_PILOT_VERDICT.md)).

**Goal:** turn the pilot's by-hand method into a repeatable pipeline. Each leaf function
goes through the same steps: evidence packet → LLM rewrite + rules → automatic gate →
human review of failures only → shipped in batches as `src/core/hotfunc/theseus/`
overrides behind `--theseus_rewrites`. Not a goal yet: non-leaf functions,
recording inputs from gameplay, Rust.

**Done when:** (1) the gate runs unattended for one function with one command and
records the result in the ledger, (2) mechanical mutants replace hand-written negative
controls (B1: mutants of the original catch input gaps; B2 adds mutants of the rewrite for
the pilot's float-type gaps), (3) at least 3 batches (≥ 75
functions) shipped and A/B checked in game, (4) a short update to the verdict with
pass rate, cost per function and how much human review was needed, plus a decision on
whether to run the rest of the pool unattended (headless `claude -p`) or keep it
in-session.

## The pool (measured 2026-11-16)

Leaf, 6–80 instructions, has an address, no kernel imports, no indirect calls, no
locks/timebase/mid-asm hooks, not already overridden: **4,766** (the inventory doc's
4,931 used an older filter). Only 41 groups of identical bodies (92 functions), so
grouping duplicates saves little.

| Tier | What | Count | Why this order |
|---|---|---:|---|
| A | integer, no stores | 611 | output is r3 only: easiest rules, cheapest gate |
| B | integer, stores | 1,402 | need pointer/len rules; touched-page check matters |
| C | float / vector | 853 | bit-exact FP (fma, NaN payloads, MXCSR); the pilot's slowest kind |
| D | no direct callers (any kind; checked first) | 1,880 | reached through vtables/pointers: `caller_usage.py` can't see who reads scratch registers. Needs a decision (see Open questions) |

Inside a tier: most callers first, then smallest first. Counts are what was queued
after B0 (pilot's 20 removed); 16 functions are flagged speed-tuned (cache-control
instructions).

## The pipeline (target state)

| Step | Tool | Output |
|---|---|---|
| 1. Queue | `tools/theseus/batch_pool.py` | `out/theseus/batch/queue.jsonl` (tier, rank, speed-tuned flag); `--top N A` lists the next ones |
| 2. Evidence packet | `tools/theseus/batch_packet.py <name>` or `--next N A` | `out/theseus/batch/<name>/packet.md`: original body, inventory record, naming evidence, caller names and how they use r3, rules vocabulary, the closest pilot rewrite as an example |
| 3. Rewrite | LLM (in-session first; maybe headless later) | `tools/theseus/rewrites/<name>.cpp` + `.rules` |
| 4. Mutants | `tools/theseus/mutate.py <name>` | mutants of the **original** (flip a branch, signed↔unsigned compare, immediate ±1, drop a store, narrower load). They test the *rules*, independent of whoever wrote the rewrite |
| 5. Gate | `tools/theseus/gate.py <name>` | PASS-ABI + mutant kill rate + caller check + builds as an override → `out/theseus/batch/<name>/gate.json`, ledger row |
| 6. Review | human, failures + a sample of passes | ledger status `reviewed` / `rejected` |
| 7. Ship | `tools/theseus/ship.py <batch>` | override wrappers in `src/core/hotfunc/theseus/`, Release build, user A/B in game |

**Ledger:** `tools/theseus/batch/ledger.tsv` (tracked, one row per function:
name, tier, status, gate result, kill rate, batch, note). Statuses: `queued` →
`written` → `gated` / `gate-fail` → `reviewed` → `shipped` (or `rejected`, `skipped`
with a reason). The pilot's 20 go in as `shipped` (memset: `gated`, kept out for speed).

## Units (one per session)

| Unit | Task | Status |
|---|---|---|
| B0 | `batch_pool.py` (queue + tiers), ledger with the pilot's 20, `batch_packet.py` | ✅ 2026-11-16: pool 4,766, queued 4,746 (A 611, B 1,402, C 853, D 1,880). Ledger `tools/theseus/batch/ledger.tsv` holds only functions that left the queue (pilot: 19 `shipped`, memset `gated`). Packets take ~4 s each (the caller scan reads all generated files; batch it if that adds up). Inventory rerun: it still counts 20 hotfunc overrides, so the 19 `theseus/` wrappers aren't seen as overrides. Harmless here (rewrites are excluded by name); fix in `inventory.py` some time |
| B1 | `mutate.py`: mechanical mutants of the original. Check on the 20 pilot functions: kill rate per function, look at survivors (some are equivalent mutants, e.g. dead code); remove the `fval`/log-uniform/trap-count rules on purpose and confirm the matching mutants then survive (proves mutants find the pilot's 3 gaps) | ✅ 2026-11-16, see "B1 results" below. Kill rate 82–100% per function at 20k cases, every survivor explained. Found 2 real coverage gaps in pilot tests that had passed (fixed). Reproduces 1 of the pilot's 3 gaps (trap count); the 2 float gaps were rewrite-shape mistakes, so B2 adds mutants of the rewrite too |
| B2 | `gate.py`: one command per function: difftest PASS-ABI (200k, `--timeout`); `mutate.py` on the original (floats at 200k: unfused-fma mutants need it); **rewrite mutants** (same idea on `rewrites/<name>.cpp`: `<`↔`<=`, `>`↔`>=`, `std::fma`/`NegMulSub` unfused, integer literal ±1, dropped NaN/sign guard); `caller_usage.py` clean; `gate.json` + ledger. No hard kill-rate threshold (B1: 82–100% with only equivalent survivors): every surviving original mutant is listed and pre-labelled ("likely equivalent": sign swap on a same-object compare or byte value, deleted zero-extend/reload, red-zone/neighbour offset; else "review"); every surviving rewrite mutant = gate-fail. `-j` for parallel runs | ⬜ |
| B3 | Batch 1: top 25 of tier A, written in-session from packets, all through the gate. Measure: minutes per function, first-try pass, how many needed human review | ⬜ |
| B4 | `ship.py` + ship batch 1: wrappers, Release build, user A/B (save load, UI, Bowerstone, FPS) | ⬜ |
| B5 | Batch 2 (tier A, next 25–50) + ship. Decide headless or in-session (needs the user: cost) | ⬜ |
| B6 | Batch 3 from tier B (stores) + ship | ⬜ |
| B7 | Update the verdict; decide on tier C (floats) and D (no direct callers) | ⬜ |

## B1 results (2026-11-16)

`tools/theseus/mutate.py <name>` (`--list`, `--ablate trap,loguniform,fval`, `--cases`, `-j`): single-line
mutants of the **original** recompiled body (operators neg, bound incl. `fsel`, sign, imm, field, store, trap,
fuse, del; at most 8 per operator, fixed seed), all built into one binary (~25 s) and run with an early stop
at the first difference; a hang or crash counts as killed. Skips mutants that can't differ: sign swaps on
compares only tested for `.eq`, deleting an XER carry nobody reads. ~45 s per function.

**Pilot, 20 functions, 459 mutants at 20k cases:** first run 93% killed. Survivors fell into:

| Survivors | Verdict |
|---|---|
| HashCstring: 4 mutants of the 100-byte cap | **real gap**: random strings were ~12 bytes, the cap was never reached. New rule `rN str <max>` (a string of length 0..max, a quarter at max−1..max+1); now killed |
| GrowAndZeroBuffer (`lua_settop`): `bge` → `bgt` | **real gap**: `top == base + idx*8` never happened (top and base independent). New rule `mem32 @r3+8 near @r3+12 512`; now killed |
| FNV1 / GrowAndZero sign swaps on pointer compares | equivalent: both pointers point into one object, so always on one side of the sign bit. A high copy of the data window (0xB000xxxx, a quarter of pointers) was added anyway so compares *between* objects cross it; no pilot mutant needed it |
| StrCmpIgnoreCase/HashCstring sign swaps on byte values and counters; FindChainRoot deleted zero-extend; FindTreeLeaf/GrowAndZero deleted reload of a value already there; MemSet alignment-loop compare; NormalizeVector red-zone/neighbour-lane offsets; floor `fsel` at ±1e18 | equivalent (checked one by one) |
| cos: 5 unfused polynomial `fma`s | real but rare: survive at 20k cases, all killed at 200k → floats need 200k mutant runs |
| StringCopyN/StrLenUTF16: deleted XER carry | equivalent; operator now skips them |

**Ablations (does a mutant notice when a harness feature is removed?):** trap counting off → both
"drop trap" mutants of FindChainRoot survive (gap reproduced). `fval` off and log-uniform floats off →
every cos mutant still killed: the pilot's two float gaps (`>` vs `>=` written into the rewrite, an
unfused split-π step) were mistakes in *how the rewrite was written*, which single-line mutants of the
original don't produce. Hence rewrite mutants in B2.

**Harness changes (all 20 pilot rewrites rechecked at 200k: PASS-ABI):** high window, `str`, `mem32 …
near …` (its random half stays in the referenced value's window), `ptr_lo` (always the low window),
`difftest.py --timeout` (default 600 s → `RESULT FAIL (timeout)`), `DIFFTEST_STOP` early stop, mutant
table build (`-DMUTANTS`), ablation switches `NO_TRAP_COUNT` / `NO_LOG_UNIFORM`. The first recheck hung:
`lua_settop` with `base` in the high window and `top` in the low one swept 1.8 GB; fixed by the
same-window `near`.

## Open questions

- **Tier D (no direct callers):** scratch-register safety can't be checked from the
  call sites. Options: require scratch registers to match too (less readable rewrites),
  or find the vtable/pointer users first (`containers.py`/RTTI data). Decide at B7.
- **memset and other speed-tuned originals:** a plain loop is correct but may cost FPS.
  Flag functions with unrolled/aligned paths in the packet; ship them only with a
  speed check.
- **Names:** batch names go to `docs/re/RE_FINDINGS.md` and Ghidra per batch (same
  revert-list method as the pilot), never the manifest.

## Next session prompt

"Theseus batch: read plans/THESEUS_BATCH.md and do the next unit marked ⬜."
