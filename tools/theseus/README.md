# tools/theseus: understanding and rewriting recompiled code

Offline tools for the "Ship of Theseus" work: inventory every recompiled guest
function, gather naming evidence from the binary, and prove readable rewrites
equivalent to the original. Nothing here changes the game build or the manifest; only
`ghidra/apply_names.sh` writes to Ghidra (with a backup and a revert list). Plans: [THESEUS_PILOT](../../plans/THESEUS_PILOT.md),
[THESEUS_BATCH](../../plans/THESEUS_BATCH.md),
[RE_NAMING_PASS](../../plans/RE_NAMING_PASS.md). Results:
[THESEUS_INVENTORY](../../docs/analysis/THESEUS_INVENTORY.md),
[NAMING_EVIDENCE_RESULTS](../../docs/analysis/NAMING_EVIDENCE_RESULTS.md).

## The pipeline

| Step | Command | Output (in `out/theseus/`, git-ignored) | Time |
|---|---|---|---|
| 1. Inventory | `python3 tools/theseus/inventory.py` | `functions.jsonl`, `summary.json` | ~11 s |
| 2. Ghidra export | `tools/theseus/ghidra/export.sh` | `ghidra/{strings,rtti,functions}.jsonl` | ~3 min, needs ~3 GB free RAM |
| 2b. Lua bindings | `python3 tools/theseus/lua_bindings.py` | `lua_bindings.jsonl`, `lua_bindings_summary.json` | ~2 s |
| 2c. STL containers | `python3 tools/theseus/containers.py` | `containers.jsonl` | ~15 s |
| 3. Naming evidence | `python3 tools/theseus/evidence.py` | `names.jsonl`, `names_summary.json` | ~1 s |
| 4. Reference table | `python3 tools/theseus/export_names.py` | `docs/re/NAMES_EVIDENCE.tsv` (tracked) | ~1 s |
| 5. Apply to Ghidra (writes!) | `tools/theseus/ghidra/apply_names.sh` (`revert` to undo) | Ghidra project; `docs/re/NAMES_GHIDRA_REVERT.tsv` | ~10 s; stops/restarts `ghidra-mcp`, backs up first |
| Rewrite check | `python3 tools/theseus/difftest.py <function> [--timeout S]` | `difftest/<function>/result.txt` | ~1 min |
| Mutants (test coverage) | `python3 tools/theseus/mutate.py <function> [--list] [--cases N]` | `mutants/<function>/result.json` | ~45 s |
| Caller check | `python3 tools/theseus/caller_usage.py <function>` | printed | ~10 s |
| Batch queue | `python3 tools/theseus/batch_pool.py [--top N A]` | `batch/queue.jsonl`; ledger `tools/theseus/batch/ledger.tsv` (tracked) | ~10 s |
| Batch packet | `python3 tools/theseus/batch_packet.py <function>` or `--next N A` | `batch/<function>/packet.md` | ~4 s each |
| **Gate** (all checks) | `python3 tools/theseus/gate.py <function>... [-j N] [--no-ledger]` | `batch/<function>/gate.json`, ledger row | ~2–3 min each; `-j 4` runs 4 at once |
| Ship a batch | `python3 tools/theseus/ship.py <batch> [--build]`, after the in-game A/B `--confirm` | wrappers in `src/core/hotfunc/theseus/`; `--build`: incremental Release build + `nm` check; `--confirm`: ledger rows -> `shipped` | build ~1–5 min |

Rerun 1 and 3 after codegen or manifest changes; rerun 2 after big Ghidra changes.

- **`inventory.py`** parses every `DEFINE_REX_FUNC` body: size, callers, callees,
  leaf, call-graph level, kernel imports, float/vector use, hooks, hotfunc override.
- **`ghidra/ExportFable2Evidence.java`** (Ghidra script) dumps referenced strings
  with the functions that use them, MSVC RTTI classes with their vtables, and the
  function list. **`export.sh`** runs it headless inside the Ghidra Docker container
  on a **copy** of the project (the MCP server keeps the live one locked).
- **`evidence.py`** turns that into proposals: method names from profiler strings
  and log messages, `Class::vfN` from vtables (inherited slots and stubs handled),
  source file / folder from assert paths, and library tags (from paths, name
  prefixes, call-graph spreading and same-library address neighbourhood).
  Every proposal carries its evidence and a confidence level.
- **`lua_bindings.py`** finds the registration functions (they reference many names
  from `docs/re/FABLE2_LUA_API.md`) and, at each register-helper call, pairs the
  Lua name string with the C++ function pointer loaded as constants. Table names
  loaded by the registrar give the Lua table. Fable-specific in its catalog path
  and register conventions; another game needs checking.
- **`containers.py`** recognises MSVC STL map/set (`std::_Tree`) and vector template
  code by its shape, so naming rules don't mistake it for game logic. Generic for
  MSVC-built games.
- **`difftest.py`** runs the original leaf function and `rewrites/<function>.cpp`
  on 200,000 random CPU states and compares them (contract below).
  `--rewrite <file>` tests another file, e.g. a deliberately broken one.
  Optional `rewrites/<name>.rules` fixes what argument registers hold
  (`r3 ptr`, `r5 len 256`, `r4 ptr_or_null`, `int`, `u32`, `any`; `mem32 <addr> ptr`
  sets a global such as a table pointer, which is otherwise zero; `r4 near r3 32`
  puts r4 inside r3's string half the time, e.g. a needle that will be found;
  `mem32 @r3+8 ptr` sets a field of an argument's struct; `slen 64` = -64..64;
  `tree 17 12` builds a valid MSVC `std::map` tree each case (isnil and key
  offsets) for the kinds `tree_cont`, `tree_node`, `tree_key`; `data <addr> <hex>`
  puts fixed bytes such as a float constant from the game image; `ret f1` makes
  the float return value part of the ABI check; `fval <double>` adds an edge
  value for float arguments; `r3 fixed 0x4000C000` / `mem32 <addr> fixed <v>` pin
  a value, e.g. to place a struct in the window's last 16 KB; `mem32
  *<global>+off <kind>` sets a field of the struct a global points to).
  Float arguments mix specials, uniform -2048..2048 and log-uniform 2^-60..2^60.
  FPU state: `ctx.fpscr` caches the host MXCSR; each case starts with them equal
  (flush bits random) and the MXCSR control bits must match afterwards.
  Traps (`ppc_trap`) only log, so the harness counts log calls and they must
  match. The stack is compared only from r1 up: the red zone below it is dead
  after a return, so differences there are strict-only.
  Keep pointed-to structs apart from the buffers they point at, or a write can
  corrupt a loop bound (see `rewrites/GrowAndZeroBuffer_82BC55B8.rules`). Besides the data
  window and stack, every page either run touched is compared every 1,000 cases,
  so stray writes anywhere fail the test.
- **`mutate.py`** makes single-line mutants of the *original* body (flip/move a branch,
  signed↔unsigned compare, immediate ±1, field offset +4, drop a store/trap, unfuse an fma,
  delete a statement), builds them into one binary and runs each against the original
  with the function's rules. A survivor means the test inputs never reach that spot (a coverage
  gap) or the mutant can't change anything (equivalent); look at each. Floats: use `--cases
  200000` (unfused fmas differ rarely). `--ablate trap|loguniform|fval` switches a harness
  feature off to check that mutants notice. Results: `plans/THESEUS_BATCH.md` "B1 results".
- More rules (B1): `r3 str 100` = a string of length 0..100 (a quarter at 99..101), `r3 ptr_lo` =
  pointer that always stays in the low window, `mem32 @r3+8 near @r3+12 512` = a field close to
  another field's value. A quarter of random pointers now go to a copy of the data window above
  the sign bit (0xB000xxxx). `--timeout` (default 600 s) turns a hang into `RESULT FAIL (timeout)`.
- **`gate.py`** (B2) is the one command per function: difftest at 200k (`--timeout`), `mutate.py` on
  the original (20k cases, 200k for float/vector bodies; survivors listed with a pre-label "likely
  equivalent: …" or "review", they don't fail the gate), **mutants of the rewrite** (`<`↔`<=`,
  `>`↔`>=`, `std::fma` unfused, integer literal ±1 or a top-mantissa-bit flip for 64-bit constants,
  NaN/sign guard dropped; any survivor = gate-fail, a truly equivalent line can be marked
  `// gate: equivalent <reason>`), the caller check (a caller that reads a scratch register after
  the call is fine only if the rewrite leaves that register equal: difftest's `differing_fields`)
  and a compile of the override wrapper with the game's Release flags. Writes the ledger
  (`gated` / `gate-fail`; `reviewed`/`shipped`/`rejected` rows keep their status).
- B3 additions: guest **0..64 KB is a null page** (faults are caught and must match between the
  original and the rewrite; a rule that writes there is an error); `r4 mirror r3` (copy of r3's
  string, half the time one byte changed); `mem32 <addr|@rN+off> oneof a b [c d]`; `mem32 @r4+0
  nearbits @r3+0` (same word or one bit flipped); `fixed_or_null <v>`; rules may set r11..r13.
- B5 additions: `rN oneof v1 .. v8` (a register that is one of up to 8 u32 values, e.g. the ids a
  function compares against); `mem32 ... oneof` now takes up to 8 values too.
- B6: `tree` nodes are 48 bytes apart (isnil/key offsets up to +47; Fable's map nodes put isnil
  at +17..+33).
  All test limits are **CPU seconds** (`difftest.run_limited`): a busy machine slows the gate
  (which also runs at nice 10) but can't fake a hang; if the wall-clock backstop fires the
  result is `INCONCLUSIVE` (rerun).
- **`caller_usage.py`** checks what every call site does with r3 afterwards and
  that none reads a scratch register first.

**Rewrite contract:** identical full 64-bit r3, non-volatile registers
(r1, r2, r13–r31, f14–f31, cr2–cr4, lr) and memory. A scratch register may differ
only when `caller_usage.py` shows no caller reads it after the call (some do: the
compiler knew the callee leaves e.g. r7 alone; `gate.py` checks this per register).

## In the game

`src/core/hotfunc/theseus/<name>.cpp` turns a passing rewrite into a hotfunc
override: it `#include`s `rewrites/<name>.cpp` (so the game runs exactly the
tested code) and `THESEUS_OVERRIDE(<name>)` from `theseus_switch.h`.
`--theseus_rewrites=false` runs the recompiled originals instead (A/B without a
rebuild). New files are picked up by the hotfunc glob on the next build.

## Per-game settings

Everything game-specific is in a target file, default
[`targets/fable2.toml`](targets/fable2.toml): paths, the generated-file pattern, the
manifest, the Ghidra container/project/program, the source-path marker and the
**library rules** (path substrings, folders, file names, name prefixes/regex).
Pick another with `THESEUS_TARGET=tools/theseus/targets/<game>.toml`.
`python3 tools/theseus/target.py` prints what a target resolves to.

## Setting up another ReXGlue game (e.g. Crackdown)

1. Copy `targets/fable2.toml` to `targets/<game>.toml`; set `repo_root`,
   `generated_glob` (e.g. `crackdown_recomp.*.cpp`), `manifest`, `hotfunc_dir`.
2. Import the game's XEX into Ghidra (Xenon360 loader), let auto-analysis finish,
   and set `[ghidra] project`/`program`.
3. Run the pipeline with `THESEUS_TARGET=...`. Check `names_summary.json`
   `top_modules` and the `library_runs` address map.
4. Adjust `source_root_marker` (look at a few assert paths) and the library list:
   Havok and the Microsoft XDK will likely recur; add the game's own middleware.
5. Expect differences: how much RTTI the game kept, whether asserts/profiler
   strings were stripped. Fable II kept profiler strings but stripped its own asserts.

## Later: the Java MCP

The plan is to move steps 1 and 3 plus query tools ("what's known about this
address", "which library owns it", naming stats) into the existing Java MCP
`AI/x360/recomp-mcp` (`x360-recomp-analysis`), which already has per-game
targets (`TargetRegistry`, Crackdown configured) and a generated-code
`FunctionIndex`. The Ghidra export stays a separate script (an MCP container
running `docker exec` would need the Docker socket), and `difftest` stays a
command-line tool (it needs clang and the game's generated headers).
