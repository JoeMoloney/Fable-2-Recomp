# tools/theseus: understanding and rewriting recompiled code

Offline tools for the "Ship of Theseus" work: inventory every recompiled guest
function, gather naming evidence from the binary, and prove readable rewrites
equivalent to the original. Nothing here changes the game build, Ghidra or the
manifest. Plans: [THESEUS_PILOT](../../plans/THESEUS_PILOT.md),
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
| Rewrite check | `python3 tools/theseus/difftest.py <function>` | `difftest/<function>/result.txt` | ~1 min |
| Caller check | `python3 tools/theseus/caller_usage.py <function>` | printed | ~10 s |

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
- **`caller_usage.py`** checks what every call site does with r3 afterwards and
  that none reads a scratch register first.

**Rewrite contract:** identical full 64-bit r3, non-volatile registers
(r1, r2, r13–r31, f14–f31, cr2–cr4, lr) and memory. Scratch registers may differ
only when `caller_usage.py` shows no caller reads them.

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
