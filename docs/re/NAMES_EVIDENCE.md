# Naming evidence: review (N2)

2026-11-15. Review of the proposals in `out/theseus/names.jsonl` produced by the
naming pass ([plans/RE_NAMING_PASS.md](../../plans/RE_NAMING_PASS.md), results in
[NAMING_EVIDENCE_RESULTS.md](../analysis/NAMING_EVIDENCE_RESULTS.md)). Every check
was a Ghidra decompile of the proposed function compared with its name. Nothing
has been renamed. Per-function evidence is in the `evidence` field of each record.

## Hit rate per rule

| Rule | Proposals | Checked | Correct | Verdict |
|---|---:|---:|---:|---|
| Lua binding, name and function in the same call (high) | 1,195 | 2 (+1 inconclusive: no Ghidra function at the address) | 2 | **record** |
| Lua binding, name loaded just before (medium) | 1,074 | 5 | 5 | **record** |
| Profiler method string, 1 referencing function (high) | 37 | 4 (string references) | 4 | **record** |
| Log message starting with a method name (medium) | 27 | 2 | 2 | **record** |
| RTTI vtable slot, one class (high) | 453 | 1 | 1 | **record** (structural: the slot comes from the class's own type info) |
| RTTI vtable slot, shared by 2–3 classes (medium, inherited) | 83 | – | – | **record** with the class marked "or a base/sibling class" |
| STL container helper by shape (medium) | 1,648 | 6 + 12 known | 6 + 12 | **record** |
| LuaHelper (callee of a binding, low) | 290 | 3 | 1 | **don't record** (context tag only) |
| Folded accessor (one body, unrelated names) | 88 | – | by construction | **don't record a name** (keep the list of names) |
| "Calls only container helpers" (composition) | context only | 2 | 1 | not a name |

Also checked: **Lua table attribution** agrees with the catalog for all 1,701
bindings where it can be checked (8 are "unknown", none wrong). The 782 names
missing from the catalog get their table from the registrar's context: an
informed guess (e.g. `GetTotalMovementDistance` filed under `Inventory`).

**Record-ready: 4,519 names** (high + medium, not folded), **4,172 of them for
functions that are unnamed** (`sub_…`) or carry an auto-generated label. Check
counts are small: these rates show the rules are sound, not that every name
is right.

## Conflicts (219), resolved

| Cause | Count | Resolution |
|---|---:|---|
| Identical-code folding: trivial getters shared by unrelated Lua names / vtables | 88 | `folded_accessor_<addr>` (low), all names kept as evidence |
| Inherited vtable slot (same slot in 2–3 subclasses) | 82 | medium, one class chosen, others listed |
| Lua binding that uses a vector inline | 24 | Lua name wins (precedence) |
| LuaHelper vs STL helper | 19 | STL wins (it was container code) |
| Two profiler strings in one function (an inlined callee's zone) | 5 | outer name kept, medium; needs a human |
| STL vs vtable | 1 | vtable wins |

Precedence on equal confidence: Lua binding > method string > vtable > STL > LuaHelper.

## Open questions answered

- **Kynogon middleware: confirmed.** "Kynogon Spatial graph", "Kynogon Astar
  Data", "Kynogon Mesh", "Kynogon PathCost Data" … strings are referenced from
  0x82E7–0x82EA, interleaved with the `CFleeAgent`/`CHideAgent`/`CFollowAgent`/
  `CShootAgent` (0x82E6) and `CPathFinder` (0x82EA) code: that block is **Kynogon
  Kynapse** (AI navigation), 95 functions tagged. Lionhead's own code calls into
  it (`IsKynapseAvailable`, `SetGiveKynapseMoreTime`, and the debug option
  `SetUseOurOwnCodeToFindTheNearestPointOnTheNavGraphBecauseKynapseIsGash`).

## Reference table (N3, done)

[`NAMES_EVIDENCE.tsv`](NAMES_EVIDENCE.tsv): the record-ready names, one row per
function (addr, current name, proposed name, confidence, rule, Lua table, library,
class). Regenerate with `python3 tools/theseus/export_names.py` after rerunning the
pipeline. Look up an address with `grep -i 82676A30 docs/re/NAMES_EVIDENCE.tsv`.

## In Ghidra (N3b, done)

3,513 of the names are applied in the Ghidra project (2026-11-15), each with a plate
comment `Naming pass N3b (docs/re/NAMES_EVIDENCE.tsv): rule=…, confidence=…`. Names
containing `::` are plain names in the global namespace (the decompiler prints `::` as
`__`). Source type IMPORTED, so a hand rename overrides them. 1,006 were skipped because
Ghidra has no function at that address (mostly Lua bindings reached only by pointer).
Undo: `tools/theseus/ghidra/apply_names.sh revert` (uses
[`NAMES_GHIDRA_REVERT.tsv`](NAMES_GHIDRA_REVERT.tsv); only touches functions that still
carry the applied name).

## Next

Originally planned: record the 4,519 record-ready names as a reference table in `docs/re/` (address,
proposed name, rule, confidence, Lua table, library, class) so they survive
`out/` being cleared and can be searched while reading code. Ghidra annotations
need the user's OK; manifest renames are a separate decision (they change
generated symbol names used by hotfuncs).
