# Ship of Theseus — pilot (multi-session plan)

Started 2026-11-15. **STATUS: not started (tooling and first function done offline).**

**Goal:** find out whether rewriting recompiled functions as readable, verified
C++ can be done at scale, by doing it end to end for ~20 small functions and
measuring the cost, pass rate and failure modes. Not a goal yet: rewriting
subsystems, changing data structures, or Rust.

**Done when:** (1) the 20 functions below are rewritten and pass the ABI-level
differential test plus the caller check, (2) at least 5 run in the game as hotfunc
overrides with no visible change, (3) a written verdict: minutes per function,
pass rate, what broke, and whether a batch job (rewrite + test + human review of
failures) is worth running over the 4,931-function pilot pool.

Background and first results: [docs/analysis/THESEUS_INVENTORY.md](../docs/analysis/THESEUS_INVENTORY.md).

## Tools (all offline)

| Tool | What it does |
|---|---|
| `python3 tools/theseus/inventory.py` | Rebuild `out/theseus/functions.jsonl` (all 60,753 functions) and `summary.json`. Rerun after codegen changes. |
| `python3 tools/theseus/difftest.py <name>` | Original vs `tools/theseus/rewrites/<name>.cpp`, 200,000 random cases. `--rewrite <file>` tests another file (use it for negative controls). Leaf functions only. |
| `python3 tools/theseus/caller_usage.py <name>` | What callers do with r3 and whether any reads a scratch register first. Must show "none" for scratch. |

**The contract a rewrite must meet:** identical full 64-bit r3, identical
non-volatile registers (r1, r2, r13–r31, f14–f31, cr2–cr4, lr) and memory;
scratch registers may differ only if `caller_usage.py` shows no caller reads them.

## Pilot functions (all leaf, 6–80 instructions)

| # | Function | Size / callers | Why | Status |
|---|---|---|---|---|
| 1 | `FNV1Hash_821F3C28` | 22 / 2,167 | FNV-1 hash | ✅ offline: ABI pass, r3 exact, negative control caught |
| 2 | `HashCstring_82188CF0` | 19 / 282 | string hash | |
| 3 | `StrCmpIgnoreCase_82CA6320` | 21 / 211 | C runtime | |
| 4 | `StrCmp_Signed_8226D7A8` | 21 / 169 | C runtime | |
| 5 | `StringCopyN_822CD828` | 23 / 56 | writes memory: needs input rules | |
| 6 | `FindSubstring_821E6388` | 31 / 51 | C runtime | |
| 7 | `FindChar_82CA3980` | 15 / 46 | C runtime | |
| 8 | `StrLenUTF16_8217AB30` | 9 / 44 | 16-bit loads | |
| 9 | `MemSet_SdkRuntime_82CA3190` | 40 / 479 | writes memory: needs input rules | |
| 10 | `Ctype_Pred_Bit2_82CA6B10` | 7 / 48 | table lookup | |
| 11 | `FindChainRoot_82A596F0` | 41 / 62 | pointer chasing | |
| 12 | `FindTreeLeaf_825CB448` | 39 / 51 | pointer chasing | |
| 13 | `GrowAndZeroBuffer_82BC55B8` | 29 / 72 | writes memory | |
| 14 | `NormalizeVector_82260BC8` | 24 / 177 | float + vector: exact FP check | |
| 15 | `ComputeWrapped_8222C3E8` | 15 / 77 | float | |
| 16 | `ComputeSpecialFunction_82239E88` | 56 / 158 | float, unknown maths | |
| 17 | `ProcessAndProcessAndProcess619_82210418` | 48 / 41 | shader-constant setter from the Oct Deck work | |
| 18 | `ProcessAndProcessAndProcess1728_82D4F620` | 19 / 323 | generic name: name it too | |
| 19 | `sub_82F6C2D0` | 21 / 198 | unnamed: name it too | |
| 20 | `sub_821E3E10` | 25 / 174 | unnamed: name it too | |

## Units (one per session)

| Unit | Task | Status |
|---|---|---|
| T0 | Inventory, difftest + caller tools, function #1 | ✅ 2026-11-15 (offline) |
| T1 | Harness: per-function input rules (which registers are pointers/lengths/counts) and compare **every touched page** (not just the windows) so stray writes are caught. Needed before #5, #9, #13. | |
| T2 | Functions 2–10 (strings, C runtime) | |
| T3 | Functions 11–17 (pointers, float/vector; check FP exactness, see the AVX/FMA lesson in `docs/analysis/ARITHMETIC_AND_NATIVE_MEMORY_RESULTS.md`) | |
| T4 | Functions 18–20 with naming; record name + evidence in `docs/re/RE_FINDINGS.md` style | |
| T5 | Wire ≥5 rewrites in as hotfunc overrides (`src/core/hotfunc/theseus/`), build, play: no visible change, same FPS | |
| T6 | Verdict + decide on a batch job (LLM-written rewrites, automatic test, human review of failures only) | |

## Notes

- Rewrites stay in `tools/theseus/rewrites/` until T5; nothing changes the game build before then.
- Later, beyond this pilot: recording real inputs during gameplay (the `fable2_func_trace`
  hook) as test cases, non-leaf functions (stub out or link callees), and a
  control-flow-aware `caller_usage.py` that follows r3 through copies and returns.
