# Ship of Theseus — batch job over the leaf pool (multi-session plan)

Started 2026-11-16. Follows the pilot ([THESEUS_PILOT](THESEUS_PILOT.md), verdict [THESEUS_PILOT_VERDICT](../docs/analysis/THESEUS_PILOT_VERDICT.md)).

**Goal:** turn the pilot's by-hand method into a repeatable pipeline. Each leaf function
goes through the same steps: evidence packet → LLM rewrite + rules → automatic gate →
human review of failures only → shipped in batches as `src/core/hotfunc/theseus/`
overrides behind `--theseus_rewrites`. Not a goal yet: non-leaf functions,
recording inputs from gameplay, Rust.

**Done when:** (1) the gate runs unattended for one function with one command and
records the result in the ledger, (2) mechanical mutants replace hand-written negative
controls and catch the pilot's 3 known coverage gaps, (3) at least 3 batches (≥ 75
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
| B1 | `mutate.py`: mechanical mutants of the original. Check on the 20 pilot functions: kill rate per function, look at survivors (some are equivalent mutants, e.g. dead code); remove the `fval`/log-uniform/trap-count rules on purpose and confirm the matching mutants then survive (proves mutants find the pilot's 3 gaps) | ⬜ |
| B2 | `gate.py`: one command, all checks, `gate.json`, ledger update; `-j` to run many in parallel; fewer cases for mutants (e.g. 20k) if build/run time adds up. Threshold for kill rate set from B1's numbers | ⬜ |
| B3 | Batch 1: top 25 of tier A, written in-session from packets, all through the gate. Measure: minutes per function, first-try pass, how many needed human review | ⬜ |
| B4 | `ship.py` + ship batch 1: wrappers, Release build, user A/B (save load, UI, Bowerstone, FPS) | ⬜ |
| B5 | Batch 2 (tier A, next 25–50) + ship. Decide headless or in-session (needs the user: cost) | ⬜ |
| B6 | Batch 3 from tier B (stores) + ship | ⬜ |
| B7 | Update the verdict; decide on tier C (floats) and D (no direct callers) | ⬜ |

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
