# Naming evidence: first full run (N1)

2026-11-15. Plan: [plans/RE_NAMING_PASS.md](../../plans/RE_NAMING_PASS.md). Read-only:
Ghidra export on a project copy, then `tools/theseus/evidence.py`. Nothing was
renamed. Output: `out/theseus/names.jsonl` (one record per function with
evidence, proposed name, confidence, module, source file, class, library) and
`out/theseus/names_summary.json`.

## Export

`tools/theseus/ghidra/export.sh`: 3 min 16 s, 3 GB heap, needs Star Citizen closed
(it was using 34 GB of RAM + swap). 50,750 Ghidra functions; 23,343 strings, of which
19,148 are referenced from code; 390 RTTI classes with 330 vtables found.

## Results

| | Count |
|---|---:|
| Functions with any evidence | **4,741** of 60,753 |
| Proposed names | **604** (491 high, 113 medium); **596 for previously unnamed functions** |
| … from vtables (`Class::vfN`) | 538 (534 Havok) |
| … from method strings / log messages | 66 |
| Module (source folder) tags | ~1,300 |
| Library tags | ~4,700 functions, **6.5% of all code** |

### Library map (third-party code; candidates to identify rather than rewrite)

| Library | Functions | Instructions | Main address block |
|---|---:|---:|---|
| Havok 5.1 (physics + animation) | 3,017 | 163,461 | `0x82D4E610–0x82E67C68`, `0x830C3448–0x830D9C70`, parts of `0x832100E8+` |
| Microsoft XDK, June 2008: XGraphics shader compiler (`R400…`) | 957 | 86,682 | `0x830F7430–0x83210060` |
| Gameface 4.0.5 UI (Scaleform GFx, incl. ActionScript `XML`/`XMLNode`) | 312 | 26,567 | `0x82BA8850–0x82BE75D8`, `0x82F4B9C8–0x82FA0700` |
| MGS XeArcade framework (the **pub games**: `GameHost`, `FablePub`) | 172 | 7,286 | `0x82EE4808–0x82EF6510`, `0x82FE6648–0x83004E60` |
| Microsoft XLip (lip sync) | 153 | 8,738 | `0x82BEB720–0x82BF3830` |
| Kynogon Kaim (AI navigation) | 2 | 36 | near `0x832597E0`, `0x8329FC38` |
| Lionhead (`ctg\lhcomponents`, `LIONHEAD::`, `LHMemory::`) | 71 | 4,166 | scattered |

The real library sizes are larger: tags are only added where evidence is direct,
all callers agree, or same-library evidence sits within 16 functions on both sides.

### Game-code names found (examples)

Networking / Xbox Live: `CXboxLiveManager::CreateSession/StartSession/EndSession/
DeleteSession/ModifySession/JoinSessionLocal/JoinSessionRemote/LeaveSessionRemote`,
`CXboxLive::JoinSession`, `NLivePresence::HostSession/FindSessions/FindFriends/
ConnectToFriend/ConnectToPeers/LeaveRegion/AddIncomingConnection`,
`CNetworkManager::HandleLostConnection`. File system: `LHVFS::CWin32Request::WaitForCompletion`,
`CBasicRequestManager_FinalizerThread::ShutdownSignal`. AI/navigation:
`CEntityInfoManager::Update`, `CFleeAgent::ComputeDestination`, `CFollowAgent::ComputeRealDestination`,
`CHideAgent::SearchHidingPlaces`, `CShootAgent::FindValidSideStep`, `CPathFinder::FindNextMove`,
`CHierarchicalPathFinder::FollowPath`, `CHierarchicalGraphManager::MainUpdate`.
**Hypothesis:** the `C…Agent` / path-finder / hierarchical-graph classes (0x82E6–0x82EC)
are Kynogon's Kaim/Kynapse middleware (same terminology; two `Kaim::` names exist),
not Lionhead code. Not yet proven.

## What the run taught us

1. **Lionhead's own code has almost no assert paths** (asserts were stripped from the
   shipping build); 449 of the path references are the Microsoft XDK. Paths mainly
   identify **library** code; game-code names come from profiler strings and log
   messages (and later the Lua binding tables).
2. **Vtable sharing is inheritance:** a function at the same slot in several classes
   is an inherited method (named after one class, medium). One in several slots or
   4+ classes is a shared stub (no name, no library). Slot 0 is usually the deleting
   destructor. Reading the RTTI class-hierarchy descriptors would name the defining
   (base) class exactly: next improvement.
3. **Spreading tags needs care.** Two contradictions in the first version were real
   errors: a shared stub labelled Havok, and a C runtime helper labelled XLip because
   its neighbours had been labelled only through call-graph spreading. Fixed: stubs get no
   library, and the address-neighbourhood rule only anchors on direct evidence. After
   the fix, of 2,114 neighbourhood-tagged functions, 301 are confirmed by the call graph,
   0 contradict it and 1,813 can't be checked.
4. **Identical-code folding:** small template helpers can be shared by unrelated
   code (`FrontMenu_EnsureElement` cites an audio source file), so module tags on
   generic helpers are hints.

## Lua script bindings (run 2, same day)

`tools/theseus/lua_bindings.py` finds which C++ function implements each Lua
script name. 117 registration functions each reference 5+ names from
`docs/re/FABLE2_LUA_API.md` (2,007 of its 2,657 method names and 73 of 81 globals
are strings in the binary). Before each register-helper call they load the Lua
name and the C++ function pointer as constants, e.g. in `sub_823ADF80`:
`r4 = "GetTime"` (0x820AAEEC), `r5 = 0x82191B48` (matches memory-service #198).
Several helper signatures exist (pointer in r5, r6 or r9; the name sometimes first
turned into a string object or kept in r29/r30), and the registrar loads the Lua
**table name** (`"Village"`, `"OpinionReaction"`) before methods. That gives the table.

| | Count |
|---|---:|
| Bindings (Lua name → C++ function) | **2,491** (1,256 high: same call; 1,235 medium: name loaded just before) |
| Distinct C++ functions | 2,357 (**2,319 previously unnamed**) |
| With table | 2,459 (Debug 399, Dog 137, OpinionReaction 115, GUI 107, CombatStyles 82, Player 71, Village 63, Combat 57, Navigation 57, QuestTracker 54 …) |
| Not in the catalog (registered but never called by shipped scripts, e.g. `SetBulletTime`, `SetHeroSprintingEnabled`) | 723 |

Validation: the strict first pass (name in r4 + function in r5 of the same call,
777 pairs) is reproduced 768/777 with **0 paired differently** (9 skipped as
ambiguous: two function pointers in one call). Spot-checks in Ghidra:
`Navigation.SetMovementPaused` (0x82676A30) writes a byte flag when it changes;
`ManagedLocations.IsLocationOccupied` (0x82924448) looks the location up and
returns a query result or 0. Both match their names.

**Combined totals after run 2:** 7,489 functions with evidence; 2,959 proposed names
(2,915 for unnamed functions, 1,721 high); meaningful names went from ~1.5% of the
code to ~5%, plus ~6.5% identified as third-party library code.

## Next

Spread Lua names one call deeper (the bindings are thin wrappers; the gameplay
logic is in the functions they call), then N2 review (plan): spot-check high-confidence names and all medium ones, confirm or
reject the Kaim hypothesis, add class-hierarchy parsing, ingest the Oct 31
memory-service `FUNCTION_MEANING` batches as evidence, then record accepted names.
