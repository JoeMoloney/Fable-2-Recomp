# Ship of Theseus — pilot (multi-session plan)

Started 2026-11-15. **STATUS: DONE 2026-11-16.** All 20 pass offline, 19 ran in the game with no visible change and the same FPS (30 cap), verdict written: [THESEUS_PILOT_VERDICT](../docs/analysis/THESEUS_PILOT_VERDICT.md) (batch job recommended for leaf functions). Continued in [THESEUS_BATCH](THESEUS_BATCH.md).

**Next session prompt:** "Theseus: the pilot is done (plans/THESEUS_PILOT.md,
docs/analysis/THESEUS_PILOT_VERDICT.md). Write plans/THESEUS_BATCH.md for the batch job
the verdict recommends (leaf pool, LLM rewrite + rules + mechanical mutants, automatic
gate, human review of failures, shipping in batches behind --theseus_rewrites), same
session style as this plan, then do its first unit."

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
| 11 | `FindChainRoot_82A596F0` | 41 / 62 | pointer chasing | ✅ offline 2026-11-16: MSVC checked `std::map`/`set` iterator `operator++` (`_Tree::_Inc`; better name `MapIterator_Increment`). Node left +0, parent +4, right +8, isnil +17; two debug traps (no container: trap and go on; `++end()`: trap and return). New `tree` rule; ABI pass 200k, same trap count (44,968); negative controls caught (no leftmost walk; missing container trap, caught only by the trap count) |
| 12 | `FindTreeLeaf_825CB448` | 39 / 51 | pointer chasing | ✅ offline 2026-11-16: `std::map<uint32_t, 4-byte>::find(key)` (better name `MapU32_Find`): r3 = returned iterator {map, node}, r4 = map (+4 head), r5 = &key; key +12, isnil +21. The original builds the iterator in the red zone below r1 too (dead after return): the harness now compares the stack only from r1 up and reports the red zone as strict-only. ABI pass 200k; negative controls caught (lower_bound instead of find, upper_bound) |
| 13 | `GrowAndZeroBuffer_82BC55B8` | 29 / 72 | writes memory || ✅ offline 2026-11-15: Lua 5.1 `lua_settop(L, idx)` (top +8, base +12, 8-byte TValues, nil tag at +4); rules put L in low memory and top/base in the window (`mem32 @r3+8`, `slen`): a first try with L inside its own stack swept a gigabyte when a nil hit L->top. ABI pass 200k; negative control (pop off by one) caught only by the touched-page scan. One caller reads r5: harmless, neither version writes it |
| 14 | `NormalizeVector_82260BC8` | 24 / 177 | float + vector: exact FP check | ✅ offline 2026-11-16: normalise a vec4 in place by its xyz length (w scaled too), unless length < f1; returns the length (0.0f when too short). Flush mode on for the dot product and the scale. **Lesson:** with two NaN inputs, `dpps` on the i9-10900K writes different NaN payloads to odd and even lanes; the original reads lane 3, so the rewrite does too (and plain C++ `x*x+y*y+z*z` can be reordered by the compiler). The original's NaN result therefore depends on the host CPU. Rules `data` (0.0f, 1.0f from the image), `ret f1`. ABI pass 200k; negative controls caught (no flush mode for the scale: MXCSR check; `<=` instead of `<`) |
| 15 | `ComputeWrapped_8222C3E8` | 15 / 77 | float | ✅ offline 2026-11-16: `floor(x)` (fctidz/fcfid trunc, -1.0 if it went up, fsel keeps |x| > 1e18, ±0, NaN). Rewrite = `std::floor` plus a NaN guard: a signalling NaN comes back unchanged (std::floor would quiet it). ABI pass 200k; negative controls caught (no NaN guard: sNaN cases; trunc) |
| 16 | `ComputeSpecialFunction_82239E88` | 56 / 158 | float, unknown maths | ✅ offline 2026-11-16: `cos(x)` = sin(|x| + π/2): n = round((|x|+π/2)/π), Cody-Waite reduction with π split in two, degree-17 odd sine series (all fused like fmadd/fnmsub), negate if n odd; cos(±0) = 1.0f, |x|+π/2 ≥ 2.2e8 → default NaN. Constant table 0x832EEA60 via `data`. First try took the sign parity from the wrong value (xn, not n). Negative controls caught (no zero case; `>` at the cutoff, needed the new `fval` rule; unfused low-π step, caught only 1 in 50k: the split-π products are exact unless n is large, so the harness now draws log-uniform floats too) |
| 17 | `ProcessAndProcessAndProcess619_82210418` | 48 / 41 | shader-constant setter from the Oct Deck work | ✅ offline 2026-11-16: `SetShaderConstantById(id, float4 v1)`: ID table `*0x834BDBC4` (12-byte {reg, count}), enabled mask u64 0x83491598 (bit reg/4, signed), device `*0x8336019C` (+1920 float4 constants, +0 dirty u64, bit (u32)reg>>2). Keeps the v1 spill to r1+32 (caller's frame, compared) and r3 = 0 on the full path (10 callers read r3). New `mem32 *<global>+off` rule. ABI pass; negative controls caught (unsigned /4, no r3 = 0, sNaN not quieted through lfs/stfs, no spill) |
| 18 | `ProcessAndProcessAndProcess1728_82D4F620` | 19 / 323 | generic name: name it too | ✅ offline 2026-11-16: **Havok `hkClass::hkClass`** (reflection metadata; name `hkClass_Construct`): r4..r10 + 5 stack args (r1+84.., 8-byte slots), r7 (interfaces list) not stored, describedVersion = 0. Evidence: 323 static-init callers; strings `hkClass`, `describedVersion`. ABI pass; negative controls caught |
| 19 | `sub_82F6C2D0` | 21 / 198 | unnamed: name it too | ✅ offline 2026-11-16: **Scaleform GFx `GASFnCall::Arg(n)`** (`GFx_GASFnCall_Arg`): paged stack of 16-byte values, 32 per page, null past the top. Evidence: GFx address range; caller 0x82F11560 is AS2 `Math.abs`. Ghidra's `std_vector_helper` name is wrong. First try failed: `srawi` without `addze` rounds down, not toward zero (only differs on impossible negative spans). New `fixed` kind. ABI pass; negative control (`>=`) caught |
| 20 | `sub_821E3E10` | 25 / 174 | unnamed: name it too | ✅ offline 2026-11-16: open-addressing linear-probe hash map `uint32 → uint32` find, 0 = absent (`HashMapU32_Find`). Evidence: 0x8217E6D0 looks up global map `*0x83496BD0` with an FNV-1 id. Test table has a guaranteed empty slot (a full table loops forever) placed so probes must wrap. ABI pass; negative controls caught (no wrap, wrong miss value). Callers read r6/r7: neither version writes them |

## Units (one per session)

| Unit | Task | Status |
|---|---|---|
| T0 | Inventory, difftest + caller tools, function #1 | ✅ 2026-11-15 (offline) |
| T1 | Harness: per-function input rules (which registers are pointers/lengths/counts) and compare **every touched page** (not just the windows) so stray writes are caught. Needed before #5, #9, #13. | ✅ 2026-11-15: `rewrites/<name>.rules` (`r3 ptr`, `r5 len 256`, `int`, `u32`, `ptr_or_null`, `any`); every page either run touched (mincore) compared every 1,000 cases and at the end (outside-window memory is never reset, so a differing stray write stays visible). Tested: a rewrite with a stray write at 0x20001234 passes the old window check but FAILS the new one (page 0x20001000); FNV1Hash still PASS-ABI. Next: #5 StringCopyN with rules. Later the same day: `mem32 <addr> <kind>` rule for globals (table pointers). Also `rN near rM <max>` (pointer inside another argument's string); `caller_usage.py` now treats float compares (`crN.compare(f, f)`) as writes. Then `mem32 @rN+off` (struct field) and `slen` (-max..max). 2026-11-16: `tree <isnil> <key> [n]` builds a valid MSVC `_Tree` each case (kinds `tree_cont`, `tree_node`, `tree_key`); stack compared only from r1 up (red zone = strict-only); SDK logger stubbed (`FMT_HEADER_ONLY`) and every log call counted, so `ppc_trap` counts must match per case. Floats: `data <addr> <hex>` (image constants, read with Ghidra `read_memory`), `ret f1` (f1 compared bit for bit), `ctx.fpscr` starts as a real MXCSR (flush bits random, host = cache) and the host MXCSR control bits must match after the call (sticky flag bits 0–5 ignored); float inputs now include ±inf, qNaN, sNaN, denormals and the 2^52 / 1e18 edges. The first ABI failure is printed with its inputs. All 15 rewrites re-checked after these changes. |
| T2 | Functions 2–10 (strings, C runtime) | ✅ 2026-11-15 |
| T3 | Functions 11–17 (pointers, float/vector; check FP exactness, see the AVX/FMA lesson in `docs/analysis/ARITHMETIC_AND_NATIVE_MEMORY_RESULTS.md`) | ✅ 2026-11-16 (#13 on 2026-11-15) |
| T4 | Functions 18–20 with naming; record name + evidence in `docs/re/RE_FINDINGS.md` style | ✅ 2026-11-16: all 20 recorded in `docs/re/RE_FINDINGS.md` (Theseus pilot table). Applied in Ghidra 2026-11-16 via the MCP (revert list `docs/re/NAMES_GHIDRA_REVERT_THESEUS.tsv`); manifest unchanged |
| T5 | Wire ≥5 rewrites in as hotfunc overrides (`src/core/hotfunc/theseus/`), build, play: no visible change, same FPS | ✅ 2026-11-16: 19 overrides (all but memset), each `#include`s the tested rewrite so the game runs exactly what difftest checked; `--theseus_rewrites=false` falls back to `__imp__<name>`. Incremental Release build 46 s; `nm` shows all 19 as strong symbols. User A/B (load save, UI, Bowerstone): no visible change (green glass windows are an old, known issue, seen in both), both at the 30 FPS cap |
| T6 | Verdict + decide on a batch job (LLM-written rewrites, automatic test, human review of failures only) | ✅ 2026-11-16: [THESEUS_PILOT_VERDICT](../docs/analysis/THESEUS_PILOT_VERDICT.md): yes, leaf functions first; negative controls (better: mechanical mutants) are the coverage check |

## Notes

- Rewrites live in `tools/theseus/rewrites/`; the game build includes them through `src/core/hotfunc/theseus/<name>.cpp` (since T5).
- Later, beyond this pilot: recording real inputs during gameplay (the `fable2_func_trace`
  hook) as test cases, non-leaf functions (stub out or link callees), and a
  control-flow-aware `caller_usage.py` that follows r3 through copies and returns.
