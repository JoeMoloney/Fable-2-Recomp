# Evidence-based naming pass (multi-session plan)

Started 2026-11-15. **STATUS: N1 + N4 + N4b done 2026-11-15: 8,468 functions with evidence, 3,268 proposed names
(2,491 from Lua bindings), 3,643 functions mapped to a gameplay system, ~4,700 library functions identified. Results: [NAMING_EVIDENCE_RESULTS.md](../docs/analysis/NAMING_EVIDENCE_RESULTS.md). Next: N2 review.**

**Goal:** name and place as much of the game's code as the binary itself proves,
before any guessing: exact method names from profiler strings, source file and
module from assert paths, classes from RTTI vtables, then module tags spread
along the call graph. Only 1,749 of 60,753 functions (~2% of the code) have
meaningful names today ([THESEUS_INVENTORY.md](../docs/analysis/THESEUS_INVENTORY.md)).

**Done when:** (1) the full export and matcher have run, (2) high-confidence
names are reviewed (spot-check a sample, then all conflicts), (3) accepted names
are recorded (`docs/re/` mapping file; Ghidra annotations only with the user's
OK), (4) a write-up says how much of the code now has a name or a module tag.

## Evidence in the binary (found 2026-11-15)

| Evidence | Amount | Gives | Confidence |
|---|---|---|---|
| Profiler zone strings (`CFleeAgent::ComputeDestination`, Havok `TthkpWorld::…`) | ~108 with `::`, more without | exact class + method of the one function that references it | high (1 referencing function), medium otherwise |
| Assert source paths (`D:\dev\Fable2\GoldBranch\SourceCode\ctg\lhcomponents\lhaudio\…\CSplitter.cpp`) | 139+ | source file and Lionhead folder (module) | high, but see caveat |
| RTTI type descriptors (`.?AV…`): Havok, `LIONHEAD::`, `std::` | 390 | class for each vtable's methods (`Class::vfN`) | high; low for stubs shared by 4+ vtables |
| All defined strings | 23,343 | hints for review/LLM naming later | medium |
| Lua API names (`docs/re/FABLE2_LUA_API.md`) | hundreds | script-visible names | phase 2: needs the binding tables found first |
| Memory-service project 3 `FUNCTION_MEANING` batches (Oct 31, e.g. #197, #198, #230, #287, #306, #317; direction in decision #179) | hundreds of addresses | hand-checked behaviour notes (string helpers, tree containers, XInput wrappers …) | high where marked; ingest as evidence in N2 |

Findings stored in memory-service project 3: #559 (evidence survey), #560 (identical-code-folding caveat), #561 (FNV1Hash / rewrite contract), #562 (export command).

Sample run on real data (5 strings via the Ghidra MCP): 4 previously unnamed
functions got exact names (`sub_82E69FD8` = `CFleeAgent::ComputeDestination`,
`sub_82E817B0` = `CEntityInfoManager::Update`, `sub_82E6FAB0` = `CShootAgent::CanFire`,
`sub_82EAC900` inside `CPathFinder::FindNextMove`), and 4 functions were tagged
with `CSplitter.cpp` / the `lhaudio` module (2 directly, 2 by call-graph spreading).

**Caveat (identical-code folding):** one of those 4 is `FrontMenu_EnsureElement`,
a front-menu function referencing an audio source file. The linker merges identical
template functions (e.g. "find or insert in a map") from different source files
into one copy, so path evidence on small generic helpers is a hint, not proof.
Treat module tags on helpers with many unrelated callers carefully.

## Tools

Full usage, per-game settings (`tools/theseus/targets/fable2.toml`) and setup for
other games: [tools/theseus/README.md](../tools/theseus/README.md).

| Step | Command | Output |
|---|---|---|
| 1. Inventory (done) | `python3 tools/theseus/inventory.py` | `out/theseus/functions.jsonl` |
| 2. Ghidra export | `tools/theseus/ghidra/export.sh` | `out/theseus/ghidra/{strings,rtti,functions}.jsonl` |
| 2b. Lua bindings | `python3 tools/theseus/lua_bindings.py` | `out/theseus/lua_bindings.jsonl` + summary |
| 3. Match | `python3 tools/theseus/evidence.py` | `out/theseus/names.jsonl` + `names_summary.json` |

`export.sh` runs Ghidra headless **inside the `ghidra-mcp` Docker container on a
copy** of the project (the MCP server keeps the live one locked), read-only, with
a 3 GB heap (`MAXMEM=`), and deletes the copy afterwards. It refuses to start
with < 3 GB RAM available (`FORCE=1` overrides). Script:
`tools/theseus/ghidra/ExportFable2Evidence.java`.

`evidence.py` maps Ghidra addresses to the recompiled function **containing**
them (the two disagree on some starts) and names without an address suffix
through the manifest. `THESEUS_OUT=<dir>` runs it on a test folder.

## Units

| Unit | Task | Status |
|---|---|---|
| N0 | Evidence survey, export script, matcher, real-sample test | ✅ 2026-11-15 |
| N1 | Run the export (close big games first), run the matcher, read `names_summary.json`; fix anything the RTTI step gets wrong | ✅ 2026-11-15: added message-string, vtable stub/inheritance, library (path/name/call-graph/address-neighbourhood) rules; contradiction check 0 |
| N2 | Review: all conflicts + a random sample of high-confidence names; confirm/reject the Kaim hypothesis for the `C…Agent`/path-finder classes; parse RTTI class-hierarchy descriptors (exact defining class for inherited vtable slots); ingest memory-service Oct 31 `FUNCTION_MEANING` batches; write `docs/re/NAMES_EVIDENCE.md` | |
| N3 | Record accepted names: a `docs/re/` mapping file now; Ghidra renames only with the user's OK; manifest renames are a separate decision (they change generated symbol names used by hotfuncs) | |
| N4 | Phase 2: find the Lua binding tables (script name → C function) and add them as evidence | ✅ 2026-11-15 (done before N2): `tools/theseus/lua_bindings.py`, 2,491 bindings, validated 768/777 vs strict pairs, 0 wrong |
| N4b | Spread Lua names one level deeper | ✅ 2026-11-15: "implementation" naming failed spot checks (2 of 3 were container templates) → `LuaHelper_` (low) + reliable `lua:<Table>` module tags; 3,643 functions mapped to a gameplay system; 108 Lua glue helpers found |
| N5 | Phase 3: remaining strings + module context as input to LLM-suggested names for the rest, reviewed by sample | |

## Run it (N1)

```bash
# with Star Citizen etc. closed (needs ~3 GB free RAM for a few minutes)
tools/theseus/ghidra/export.sh
python3 tools/theseus/evidence.py
```
