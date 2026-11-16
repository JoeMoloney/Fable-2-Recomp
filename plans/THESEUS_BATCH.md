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
| B2 | `gate.py`: one command per function: difftest PASS-ABI (200k, `--timeout`); `mutate.py` on the original (floats at 200k: unfused-fma mutants need it); **rewrite mutants** (same idea on `rewrites/<name>.cpp`: `<`↔`<=`, `>`↔`>=`, `std::fma`/`NegMulSub` unfused, integer literal ±1, dropped NaN/sign guard); `caller_usage.py` clean; `gate.json` + ledger. No hard kill-rate threshold (B1: 82–100% with only equivalent survivors): every surviving original mutant is listed and pre-labelled ("likely equivalent": sign swap on a same-object compare or byte value, deleted zero-extend/reload, red-zone/neighbour offset; else "review"); every surviving rewrite mutant = gate-fail. `-j` for parallel runs | ✅ 2026-11-16, see "B2 results" below. All 20 pilot functions pass (14 on the first run; the other 6 were 2 gate bugs, 1 junk mutant kind and 7 equivalent survivors, now marked). Found 1 more real rules gap (sub_821E3E10). Caller check is now per register. ~2–3 min per function, 20 in 17 min at `-j 4` |
| B3 | Batch 1: top 25 of tier A, written in-session from packets, all through the gate. Measure: minutes per function, first-try pass, how many needed human review | ✅ 2026-11-16, see "B3 results" below. 25/25 gated (ledger batch 1). Every rewrite passed difftest on the first try; 21/25 passed the gate on the first run, the other 4 were equivalent mutants or rule gaps. The gate found 3 more rule gaps in passing functions and 5 harness gaps (now fixed: null page, string copies, value choices, r11–r13, CPU-time limits). ~4 min per function in session, all 45 rewrites rechecked |
| B4 | `ship.py` + ship batch 1: wrappers, Release build, user A/B (save load, UI, Bowerstone, FPS) | ✅ 2026-11-16, see "B4 results" below. 25 wrappers, incremental Release build 89 s, all 25 overrides strong symbols; in-game run with the rewrites on looked normal (user); ledger batch 1 → `shipped`; 18 names in Ghidra |
| B5 | Batch 2 (tier A, next 25–50) + ship. Decide headless or in-session (needs the user: cost) | ✅ 2026-11-16, see "B5 results" below. In-session (user's choice). 50 tier-A functions, ~1 min each to write; 50/50 difftest-correct, 41/50 gate first run (the rest: 7 rule gaps, 1 UB shift, 1 equivalent line); all 95 rewrites re-gated; shipped, in-game check fine (user); 18 Ghidra names |
| B6 | Batch 3 from tier B (stores) + ship | ✅ 2026-11-16, see "B6 results" below. 25 tier-B functions (map iterators, tree rotations, Lua index2adr, memcpy, constructors); 25/25 difftest-correct, 19/25 gate first run (1 forgotten, 5 rule gaps); all 120 re-gated; shipped, in-game fine (user); 17 Ghidra names |
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

## B2 results (2026-11-16)

`tools/theseus/gate.py <name>... [-j N] [--no-ledger]` runs difftest (200k, `--timeout`), `mutate.py`
on the original (20k; 200k when the body uses floats/vectors), **rewrite mutants**, the caller
check and a compile of the override wrapper with the game's Release flags (read from
`out/build/linux-amd64-release/build.ninja`), then writes `out/theseus/batch/<name>/gate.json`
and the ledger row. Hard failures: difftest, any surviving rewrite mutant, caller check,
override compile. Original-mutant survivors don't fail the gate. They are listed with a
pre-label ("likely equivalent: sign swap between two registers / on a byte value / on a small
counter, deleted zero-extend / reload, stack red-zone offset", else "review").

**Rewrite mutants** (one TU, each copy of the rewrite in its own namespace): `<`↔`<=`, `>`↔`>=`,
`std::fma` unfused (covers `NegMulSub` through its definition), integer literal ±1 (a 64-bit
constant flips its top mantissa bit instead: a last-bit change to a double is invisible), and
`if (` + `isnan/isinf/signbit` guard dropped. Skipped: array sizes, `memcpy`/`memset`/`sizeof`
sizes (+1 is only overflow UB), and lines marked `// gate: equivalent <reason>` (the reason
goes into gate.json). A mutant that doesn't compile counts as killed and the rest rebuild.

**Pilot, 20 functions:** first run 14/20 passed. The 6 failures:

| Failure | What it was |
|---|---|
| NormalizeVector: rewrite mutants didn't build (`shuffle_ps` immediate 0xFF→0x100) | gate bug: now drops non-compiling mutants |
| MemSet ledger note overwritten | gate bug: the human part of a note is kept, only the `gate.py <date>:` tail is replaced |
| 619: `memcpy(…, 4)`→5 (×2) | junk mutant kind: memcpy sizes skipped |
| NormalizeVector: `i < 4`→`<=`, `4`→`5` on the load loop | equivalent: a 5th pass only overruns the local `c[4]` (marked) |
| FindChar / FindSubstring `kNotFound` ±1 | equivalent: internal sentinel mapped to 0 (marked) |
| FindChainRoot `kTrapAssert` 22→23 | harness counts traps, not trap codes; only the log text differs (marked) |
| ComputeSpecialFunction: `isnan` guards dropped, `>`→`>=` vs `LLONG_MAX`, top polynomial fma unfused | equivalent: cvtsd2si of NaN is INT64_MIN on x86 too; n < ~7e7 so 2^63 is unreachable; the top term moves p by ~1e-30 (marked) |

After the fixes, 20/20 pass. Earlier the same day (3-function trial) the gate also found:

- **sub_821E3E10 rules gap** (real, rewrite was correct): rewrite mutant "wrap to slot 1 instead
  of 0" survived. Search keys were 0..7 = the capacity mask, so a probe that wrapped could never
  find its key. Keys now 0..15 in the rules, so the mutant is killed.
- **Callers do read scratch registers**: e.g. 6 callers of sub_821E3E10 keep a value in r7
  across the call (MSVC knew the leaf leaves r7 alone); ComputeWrapped's callers read cr1,
  memset's read r8. The old rule ("no caller reads scratch, else needs PASS-STRICT") would
  have failed 7 of the 20. Now: the registers callers read must not be in difftest's
  `differing_fields`. None are, for all 20.

**Ablation (do rewrite mutants find the pilot's float gaps?):** ComputeSpecialFunction with
`fval` rules and log-uniform floats off: `>=`→`>` at the 2.2e8 cutoff survives (the pilot's
first float gap, reproduced) and so does a typo in that constant. With them on, both are
killed. The unfused split-π `NegMulSub` mutant is killed even with both off.

**Cost:** 100–210 s per function (difftest ~60 s, two mutant builds ~25 s each, caller scan
~10 s, override compile ~10 s). 20 functions at `-j 4` (20 cores) took 17 min. Original
survivors still labelled "review": MemSet `andi.` compare imm (B1: alignment-loop compare,
equivalent) and FindTreeLeaf deleted reload (B1: value already there). The labeller doesn't
catch either yet.

## B3 results (2026-11-16)

**Batch 1 = the top 25 of tier A**, written in-session from the packets, all gated (`gated`, batch 1
in the ledger). Then all 45 rewrites (25 + the pilot's 20) were rerun through the final gate:
45/45 pass. Proposed names are in each rewrite's header. They go to `RE_FINDINGS.md`/Ghidra with
the ship step (B4).

| What | Result |
|---|---|
| Time | 16:13 → 17:06 to write all 25 (incl. the harness work below), ~2 min per rewrite. Gate 120–300 s per function; the final 45 took 28 min at `-j 5` with Star Citizen running |
| Difftest, first try | **25/25** (20k-case check after writing; none needed a fix) |
| Gate, first run | **21/25**. The 4 failures: a dead table entry in IndirectCall (rewrite restructured), the CRC table loop start and the null fallback in sub_82F6D720 (equivalent, marked), and strncmp (needed the new `mirror` rule plus a restructure without a redundant early return) |
| Rule gaps the gate found in functions that passed | 3: sub_82F6AD80 (ref never 1), sub_82D5A318 (probes never wrapped: keys too small), FindChainValue (head and end the same address). All found through "review" survivors and fixed |
| Human review | every function's survivor list was read (in-session, by the writer). After the labeller updates, "review" items left: 8 in 5 functions plus FlushCache's 18 and ChkStk's 5 (both originals are entirely dead code). All checked equivalent |
| Kinds | 2 string compares, strlen, CRC-32, 3 hash lookups, 2 c_str getters, list/array getters, expat's `unicode_byte_type`, flag/compat tests, ASCII tolower, a KPCR read, 2 no-ops (cache flush with the `dcbst`s gone, `__chkstk`) |

**Harness gaps found and fixed** (the pilot's 20 were rechecked with all of them):

- **Null page** (CRC: a dropped `string == 0` check survived, because guest address 4 read as zero).
  Guest 0..64 KB is now `PROT_NONE`. A fault inside the guest call is caught (`sigsetjmp`), and the
  original and the rewrite must fault on the same cases (`null_page_faults: original= rewrite=
  mismatched=`). Unruled functions fault often (FNV1Hash: 17% of cases, where a small integer is
  used as a pointer), and those cases used to read zeros anyway. One pilot rule file had put a
  struct in low memory (GrowAndZeroBuffer `r3 len 0x7FF0`): moved to `r3 fixed 0x60001000`. A rule
  that writes below 64 KB now stops with a clear error, and a crashing test binary now reports
  `RESULT FAIL (crash, signal N)` instead of an empty result.
- `rN mirror rM`: a copy of rM's string, half the time with one byte changed (strncmp's `<`→`<=`
  survived without it: random strings never share a prefix).
- `mem32 <addr> oneof a b [c d]`: lists with 0..3 nodes, u16 pairs that are 0 or 1.
- `mem32 @rN+off nearbits @rM+off`: equal to another word, or one random bit flipped (packed-field compares).
- `fixed_or_null <v>`, and rules for r11..r13 (`r12` stack-probe size, `r13` per-processor block).
- **CPU-time limits** (requested so a game can run in the background): every test limit is CPU
  seconds (`RLIMIT_CPU`, `difftest.run_limited`), with a generous wall-clock backstop that gives
  `INCONCLUSIVE` (gate fails the step: rerun), never a false kill. The gate runs at nice 10. The
  final run above was done with Star Citizen running. Results are deterministic (fixed seeds), so
  load only changes how long the gate takes.
- Labeller: jump-table plumbing (the recompiled `switch` ignores ctr), clrlwi of a small value,
  sign swaps on small bit fields.

## B4 results (2026-11-16)

`tools/theseus/ship.py <batch> [--build] [--confirm]` writes `src/core/hotfunc/theseus/<name>.cpp` for
every `gated` row of the batch with a passing `gate.json` (the same wrapper text the gate's override
check compiled: it `#include`s the tested rewrite, so the game runs exactly the gated code), refuses a
function that already has a hand-written override, then `--build` does an incremental Release build
and checks with `nm` that `<name>` and `rewrite_<name>` are strong symbols. `--confirm` (after the
in-game check) sets the rows to `shipped`.

| What | Result |
|---|---|
| Wrappers | 25 (45 Theseus overrides in the game now, plus the switch) |
| Build | incremental Release, 89 s, only the 2 usual SDK warnings; 25/25 strong symbols |
| In game | `run-release.sh` (rewrites on): save load, menus, Bowerstone, FPS all normal, no issues seen (user). The `--theseus_rewrites=false` run was skipped: it is the recompiled originals, the known baseline; it stays the first thing to try if a problem shows up later |
| Names | 18 Ghidra renames (revert list `docs/re/NAMES_GHIDRA_REVERT_THESEUS.tsv`), table in `docs/re/RE_FINDINGS.md` ("Theseus batch 1"); 7 kept their sub_/generic names (no strong evidence) |

## B5 results (2026-11-16)

**In-session** (the user chose it over headless; nothing else heavy was running, so the gate ran at
`-j 10..16`). Batch 2 = the next 50 of tier A (ledger batch 2, `shipped`).

| What | Result |
|---|---|
| Time | packets 19:45, all 50 written and gated by 20:26 (~1 min per function to write, 4x faster than B3: compact packet viewer, 10 at a time, gate in the background while writing the next 10). Final gate of all 95 rewrites: 21 min at `-j 16`. Ship build 89 s |
| Difftest | **50/50 correct**: no rewrite ever disagreed with the original. 2 first runs stopped at `FAIL (rules)` (my rules wrote into the null page through a null `r3`; moved to fixed nodes) |
| Gate, first run | **41/50**. Rewrite-mutant survivors showing rule gaps: 4 (flag words without bit 0, an owner value whose bytes 1..3 were never 0). "Review" survivors showing rule gaps: 3 (hash multiplier invisible with 8 slots since 0x9E3779B1 is 1 mod 8; tag words without other bits; a not-found path that needs a negative position). 1 rewrite shape fix (a 32-bit shift by 32, undefined in C++), 1 equivalent line marked |
| Human review | all "review" survivors read; besides the 3 gaps above, all equivalent (unread `cr0` from record forms, a debug-assert re-check after the same check, copies/masks of values already in range) |
| Kinds | 8 MSVC CRT functions (`isalpha`, `isspace`, `isxdigit`, `isalnum`, `isleadbyte`, `strrchr`, `memchr`, `wcscmp`), leap year, colour packing, vector size/at, 3 hash lookups, list walks/counts, 3 bit-array helpers, 7-bit type-code classifiers, small getters |
| In game | `run-release.sh` with 94 rewrites on (19 pilot + 25 + 50): fine (user) |

**Harness:** `rN oneof v1 .. v8` (a register that is one of up to 8 values: sub_830ACDE0 compares
against six ids that random values never hit); `mem32 ... oneof` takes up to 8 values.

## B6 results (2026-11-16)

Batch 3 = the top 25 of tier B (functions with stores), in-session, ledger batch 3 `shipped`.

| What | Result |
|---|---|
| Time | packets ~21:00 (25 in under 10 min), all 25 written and gated by ~21:15; final gate of all 120 rewrites 25 min at `-j 16`; ship build ~90 s |
| Difftest | **25/25 correct**: no rewrite ever disagreed with the original (stores, red zone, caller home slots included) |
| Gate, first run | **19/25** (1 not written yet). Rule gaps: Lua index list without 0/1/-9999; `lower_bound` split invisible with sorted keys (needs unsorted keys and a negative count); memcpy word vs byte steps only differ with dst 1..3 bytes after src; a redundant flag test (removed from the rewrite) |
| Families | 8 MSVC `std::map` iterator `++`/`--` (isnil at +21..+33: one template, 8 instances), 4 `_Tree::_Lrotate`/`_Rrotate` (the "List_SpliceNode" names were wrong), 2 builds of Lua 5.1 `index2adr`, CRT `memcpy`, sorted-vector find, debug pair constructor, constructors and bit helpers |
| Tier B lesson | stores were not harder to check than loads: the harness compares every touched page and the stack. The cost is in rules: struct graphs, aliasing (src/dst overlap, nodes that alias the head) and keeping the original store order |
| In game | `run-release.sh` with 119 rewrites: fine (user; saves, NPCs, quests). Startup white screen of ~3 min is pre-existing and intermittent (also with `--theseus_rewrites=false`): the render thread waits on the GPU command thread |

**Harness/tools:** `tree` nodes 48 bytes apart (isnil up to +47). `ship.py`'s "already overridden" check now matches only definitions (it skipped `SortedVector_FindValue` because a hand-written override *calls* it).

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
