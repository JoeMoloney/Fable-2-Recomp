# Ship of Theseus pilot: verdict (2026-11-16)

Plan: [THESEUS_PILOT](../../plans/THESEUS_PILOT.md). Background:
[THESEUS_INVENTORY](THESEUS_INVENTORY.md). Names and evidence:
[RE_FINDINGS](../re/RE_FINDINGS.md) ("Theseus pilot" table).

**Question:** can recompiled functions be rewritten as readable C++ that is
*proven* equal to the original, cheaply enough to do at scale?

**Answer: yes for leaf functions.** All 20 pilot functions (6–80 PPC
instructions, strings, C runtime, STL containers, Lua, floats/vectors, Havok,
Scaleform, a hash map, a shader-constant setter) have a readable rewrite that
matches the original on 200,000 random inputs at the ABI level, and every one
has negative controls (deliberately broken copies) that the test catches.
19 of them ran in the game together (`src/core/hotfunc/theseus/`): loading a
save, UI and walking Bowerstone looked the same as with the originals and both
held the 30 FPS cap (A/B with `--theseus_rewrites=false`, 2026-11-16).

## Numbers

| Measure | Result |
|---|---|
| Functions rewritten and passing | 20 / 20 |
| Running in the game | 19 (memset left out: its rewrite is a plain byte loop, a possible FPS cost) |
| Session time per function | about 3–6 minutes (#2–10: 9 in ~30 min; #11–20: 10 in ~55 min), *including* growing the test harness. File timestamps, so rough. Human time: only the in-game check |
| First rewrite already right | 7 of 10 on 2026-11-16 (#11, 12, 15, 17, 18, 20 and 14's maths); the 3 misses are below |
| Names gained | 20 (3 previously unnamed or generic, 1 wrong Ghidra name corrected) |

## What broke

Almost all the effort went into **the test, not the rewrite**:

1. **Inputs that don't reach the interesting paths.** Random memory makes
   tree walks and hash probes loop forever, and random floats never hit a
   cutoff. Fixes became rules: `tree` (a valid std::map), `fixed` struct
   placement, `mem32 @rN+off` / `*global+off`, `fval` edge values,
   log-uniform floats, `data` for image constants. Every function after #4 needed a
   rules file; writing it is the main per-function cost.
2. **Coverage gaps found by negative controls.** Three broken versions first
   *passed*: a missing debug trap (fixed: trap counting), `>` vs `>=` at the
   cos cutoff (fixed: `fval`), and an unfused Cody-Waite step (exact for
   small inputs; fixed: log-uniform floats; still caught only 1 in 50k).
   Lesson: negative controls are not optional, they are the coverage check.
3. **Harness state that wasn't real.** Random `fpscr` bytes (could unmask FP
   exceptions), sticky MXCSR flag bits compared as if they mattered, stack
   below r1 (dead) compared as if live. All fixed.

The rewrite mistakes were all "the original does something odd":

- `dpps` writes *different* NaN payloads to odd and even lanes on the
  i9-10900K; the original reads lane 3 (#14). So the recompiled code's NaN
  results already depend on the host CPU.
- `srawi` without `addze` rounds down, not toward zero (#19).
- cos took its sign from `n`, not `n - 0.5` (#16).
- Smaller ones: `std::floor` quiets signalling NaNs, `fsel` doesn't (#15);
  `lfs`/`stfs` through double quiets sNaNs (#17); a v1 spill into the
  caller's frame and a leftover `r3 = 0` that callers read (#17).

## How readable is "readable"

Integer and pointer code reads like the C/C++ it came from (strcmp,
map::find, hkClass constructor, hash-map lookup). Float code stays close to
the instructions: bit-exactness means keeping fused multiply-adds, the same
add order, the same FPU mode switches and the same NaN handling, so cos is a
clear Cody-Waite + polynomial but written with `fma`/`NegMulSub`, and the
vec3 dot product calls `dpps`. Exactness is the price of proof; a later
"relaxed" mode (equal except NaN payloads / last-bit rounding) could allow
plainer float code, but would need a different test contract.

## Recommendation: run a batch job, leaf functions first

Worth it. Shape:

1. **Pick** from the 4,931-function leaf pool, most callers first, skipping
   functions with kernel calls or indirect calls.
2. **LLM writes** the rewrite *and* the rules file *and* 2–3 negative
   controls (mutants) per function, with the evidence (Ghidra names, strings,
   callers) in the prompt.
3. **Automatic gate:** difftest PASS-ABI, all mutants FAIL, `caller_usage.py`
   clean. Better: generate mutants mechanically (flip a compare, drop a store,
   off-by-one) instead of trusting the LLM's own.
4. **Human review only for failures** and for a sample of passes (names).
5. **Ship** in batches as `src/core/hotfunc/theseus/` overrides behind
   `--theseus_rewrites`, with an in-game A/B per batch.

Before scaling past leaves: non-leaf support (link or stub callees in the
harness) and recording real inputs from gameplay (the `fable2_func_trace`
hook) as extra test cases, which would cut the rules-writing cost.

The 20 names are recorded in `RE_FINDINGS.md` and applied in Ghidra
(revert list `docs/re/NAMES_GHIDRA_REVERT_THESEUS.tsv`); the manifest is unchanged.
