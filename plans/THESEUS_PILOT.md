# Ship of Theseus — pilot (multi-session plan)

Started 2026-11-15. **STATUS: in progress. 11 of 20 functions pass offline (#1–10, #13), 2026-11-15; harness unit T1 done.**

**Next session prompt:** "Theseus pilot: read plans/THESEUS_PILOT.md and tools/theseus/README.md.
Do #11 `FindChainRoot` and #12 `FindTreeLeaf` (MSVC `std::map` tree walks: node left +0,
parent +4, right +8, isnil byte +17/+21). Random memory makes them loop forever, so first
add a difftest rule that builds a small valid tree in the data window. Then #14–16 (floats:
exact FP compare) and naming #17–20. Then done-when step (2): run 5 rewrites in the game as
hotfunc overrides."

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
| 2 | `HashCstring_82188CF0` | 19 / 282 | string hash || ✅ offline 2026-11-15: x65599 hash (signed chars, includes terminator, max 100 bytes); ABI pass, r3 exact 64-bit (8 callers read 64-bit); negative control caught |
| 3 | `StrCmpIgnoreCase_82CA6320` | 21 / 211 | C runtime || ✅ offline 2026-11-15: `_stricmp` (ASCII folding only); ABI pass, r3 exact; negative control caught |
| 4 | `StrCmp_Signed_8226D7A8` | 21 / 169 | C runtime || ✅ offline 2026-11-15: strcmp on signed chars, returns -1/0/1; ABI pass, r3 exact; negative control caught |
| 5 | `StringCopyN_822CD828` | 23 / 56 | writes memory: needs input rules | ✅ offline 2026-11-15: it is `strncpy`; rules file; ABI pass (200k, touched-page check), r3 exact, no caller reads scratch (11 sites "unclear"); negative controls caught (no zero-fill 98%, off-by-one 1.6% of cases) |
| 6 | `FindSubstring_821E6388` | 31 / 51 | C runtime || ✅ offline 2026-11-15: `strstr`; needed the new `near` rule (needle = tail of haystack half the time) so the found path is tested; ABI pass, r3 exact; negative control (partial match) caught |
| 7 | `FindChar_82CA3980` | 15 / 46 | C runtime || ✅ offline 2026-11-15: `strchr` (char compared as full int; found address only replaces r3's low 32 bits, kept); ABI pass, r3 exact; negative control caught |
| 8 | `StrLenUTF16_8217AB30` | 9 / 44 | 16-bit loads || ✅ offline 2026-11-15: `wcslen` for 16-bit chars; ABI pass, r3 exact (11 callers read 64-bit); negative control caught |
| 9 | `MemSet_SdkRuntime_82CA3190` | 40 / 479 | writes memory: needs input rules || ✅ offline 2026-11-15: `memset` (speed-tuned original: align, 16-byte blocks, words, tail; rewrite is the plain loop); rules `r5 len 4096`; ABI pass; negative control caught. One caller reads r8 after the call: harmless, neither version writes r8 |
| 10 | `Ctype_Pred_Bit2_82CA6B10` | 7 / 48 | table lookup || ✅ offline 2026-11-15: `isdigit` via the ctype table pointer at 0x832EF350 (_DIGIT 0x4); needed the new `mem32` rule (first negative control passed with a zero table); ABI pass, r3 exact; negative control caught |
| 11 | `FindChainRoot_82A596F0` | 41 / 62 | pointer chasing | |
| 12 | `FindTreeLeaf_825CB448` | 39 / 51 | pointer chasing | |
| 13 | `GrowAndZeroBuffer_82BC55B8` | 29 / 72 | writes memory || ✅ offline 2026-11-15: Lua 5.1 `lua_settop(L, idx)` (top +8, base +12, 8-byte TValues, nil tag at +4); rules put L in low memory and top/base in the window (`mem32 @r3+8`, `slen`): a first try with L inside its own stack swept a gigabyte when a nil hit L->top. ABI pass 200k; negative control (pop off by one) caught only by the touched-page scan. One caller reads r5: harmless, neither version writes it |
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
| T1 | Harness: per-function input rules (which registers are pointers/lengths/counts) and compare **every touched page** (not just the windows) so stray writes are caught. Needed before #5, #9, #13. | ✅ 2026-11-15: `rewrites/<name>.rules` (`r3 ptr`, `r5 len 256`, `int`, `u32`, `ptr_or_null`, `any`); every page either run touched (mincore) compared every 1,000 cases and at the end (outside-window memory is never reset, so a differing stray write stays visible). Tested: a rewrite with a stray write at 0x20001234 passes the old window check but FAILS the new one (page 0x20001000); FNV1Hash still PASS-ABI. Next: #5 StringCopyN with rules. Later the same day: `mem32 <addr> <kind>` rule for globals (table pointers). Also `rN near rM <max>` (pointer inside another argument's string); `caller_usage.py` now treats float compares (`crN.compare(f, f)`) as writes. Then `mem32 @rN+off` (struct field) and `slen` (-max..max). |
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
