# RE migration and user-managed cleanup

Date: 2026-10-24. Source baseline: `eb3293c`.
Decision: this fork is authoritative; preserve useful standalone-runtime evidence
without continuing that prototype. See [status](RE_STATUS.md) and
[reconciled findings](RE_FINDINGS.md). No game/SDK implementation was changed.

## Preservation performed

The local, ignored preservation directory is:

`out/re-migration/20261024T093810396160Z/`

It contains:

- `memories-before.json`: GET exports of known memory IDs 1–143, including
  lifecycle state and 106 project-3 records. This is not a transactional database
  dump or a claim to enumerate future IDs.
- `legacy-files-before.json`: path/type/size/SHA-256 inventory of the old
  `.re-work/` tree, including symlink targets as links.
- `legacy-re-work-before.tar.gz`: complete pre-migration tree. All **2,744**
  non-directory members were read and compared with the inventory. SHA-256:
  `29ac0a5629c6cec01062dab979acf820fc4817ab13bc720c8f6e503c0ec03e9e`.
- `backup-verification.json`, original repository HEAD/status/SDK diff, and a
  normalized before-snapshot of the three changed Ghidra names/pre-comments.
- Post-migration memory/status verification and cleanup inventory, recorded after
  reconciliation. The before-archive intentionally does not include these later
  reports or the ledger's new retirement marker.

Limits: archive-content verification, **not a restore test**; no external symlink
target contents, live Ghidra database, whole AI workspace, game dump or off-device
redundancy are covered. No server restart was performed. Treat the archive as
private: it may contain extracted game instructions and local metadata. Keep it
outside Git/public uploads, and copy it to an independent location before cleanup.

## Memory reconciliation performed

No memory was deleted. Records remain retrievable by ID in all lifecycle states.

| Disposition | IDs / replacement |
|---|---|
| Canonical decision and retirement of old strategy/workflow | 17, 20 superseded by **146** |
| Utility allocation/free correction | 15 superseded by **145** |
| Script-bank/asset classification correction | 18 superseded by **147** |
| Bootstrap role correction | 22–24 superseded by **148** |
| Dispatch, notification, scan-scope and target-hash correction | 25, 26, 27, 30, 36, 37, 39, 41 superseded by **144** |
| Archived obsolete checkpoints, unverified leads, scans and workflow diagnostics | 16, 19, 28, 29, 31, 33, 34, 35, 38, 40, 43, 47, 49 |
| Historical research retained searchable | 51–124 except 78 (another project): **73 records**, original results/evidence retained verbatim beneath a scope notice |
| Existing identity/entry/audit/recovery/import evidence retained | 14, 21, 42, 48, 50 |
| Global discovery pointer | **143** now points to project 3 and canonical docs; its former detailed handoff is preserved in the before-export |
| Older global Linux milestones superseded by pointer 143 | 125–137 and 139; 138/141/142 were already superseded before this migration |

Global 140 remains the local Git-configuration reference. Supersession stays within
the same project/scope; no global record was directly superseded by a project
record. Archive does not mean a scan's raw observations were disproved: obsolete
search context and unverified interpretations are separated from current facts.

The historical scope notice prevents prototype "next" tasks from becoming the
fork's queue; it does not invalidate passing tests, erase failures, or certify
ReXGlue. Correction memories index repository evidence rather than replacing it.

## Ghidra and old workspace

Live target hash matched the local XEX before annotation edits. Only three names
and their pre-comments were changed; old values were captured first:

| Address | Old name | New conservative name |
|---|---|---|
| `0x82170CC8` | `util_dup_bounded` | `CopyByteStringChecked_82170CC8` |
| `0x82CAB770` | `util_alloc_elems2` | `GetStateFieldOrFallback_82CAB770` |
| `0x82CAB630` | `util_free_fwd` | `InvokeCallbackOrTrap_82CAB630` |

Readback confirmed names, revised comments and unchanged body ranges/sizes.
Signatures, bytes, repository manifest symbols and other annotations were not
edited. Annotation persistence across Ghidra restart was **not** retested.

The original AI workspace's `.re-work/FableII-ledger.json` now has an explicit
`historical_frozen_do_not_resume` migration marker and redirected top-level
objective/handoff. Historical task details remain intact; their permissions and
"next" text are not current authorization. Original ledger is in the before-archive.
No changes were made to the AI workspace's service configuration or tracked code.

## Cleanup inventory and recommendation

For the local machine, `LEGACY_AI` refers to
`/mnt/HDD/git_stuff/my_git/AI`; `REPO` is this checkout. Paths below are explicit
candidates, **not deletion commands**. Approximate sizes are allocated disk usage
at audit time, not necessarily unique/reclaimable bytes.

### Do not delete during migration

- Original game dump, ISO and live Ghidra project storage.
- `$REPO/default.xex` and `$REPO/data/` (~6.5 GiB): intentional independent
  writable staging. Do not replace these with links into the original dump.
- `$REPO/out/build/linux-amd64-release/saves/` and `$REPO/out/save-backups/`.
- `$REPO/out/re-migration/20261024T093810396160Z/` until an independent copy and
  recovery check exist. The old scoped archives are not substitutes for this
  complete tree snapshot.
- `$REPO/thirdparty/rexglue-sdk/`: pinned working source and intentional local
  patch. No reset, submodule replacement or broad SDK cleanup.
- `.opencode/`, `.claude/`, plans and unfamiliar tracked files. In particular
  `.opencode/opencode.json` is tracked at this baseline, not the untracked state
  described in earlier handoffs.

### First cleanup candidates, after independent preservation/recovery check

| Exact path | Approximate size | Recommendation |
|---|---:|---|
| `$LEGACY_AI/.re-work/native-linkage/build/` | 29 MiB | Old compiler/toolchain outputs; candidate once archive is recoverable. Keep parent source/fixtures/results. |
| `$LEGACY_AI/.re-work/copy-helper/MS220-native-copy` | 2.5 MiB | Historical failed-attempt executable; keep source, logs and result manifest in archive. |
| `$LEGACY_AI/.re-work/free-block-helper/MS198-native-free-helper` | 2.5 MiB | Historical failed-attempt executable; do not erase failure evidence with it. |

Removing these outputs means old recorded binary hashes no longer match files on
the live tree; retain the before-archive and do not claim unchanged on-disk proof.
Do not execute old runners simply to regenerate discarded artifacts.

### Archive as a unit, not piecemeal, when ready

`$LEGACY_AI/.re-work/` is ~494 MiB in total. This includes:

- `backups/` ~104 MiB, `toolchains/` ~35 MiB;
- larger proof directories such as `native-linkage/` ~29 MiB,
  `original-registration-pair/` ~29 MiB, `range-helper/` ~26 MiB and
  `segment-initializer/` ~26 MiB;
- small, valuable `reviews/`, `pending-memory/`, `xenia-reference/`, imports,
  raw exports, source manifests, source/test code and the historical ledger.

Do not move individual proof folders without preserving their sibling-relative
dependencies. After independent archive storage and a restore check, the **entire
old `.re-work/` tree** can be retired by the user if live prototype browsing is no
longer needed. Keep its retirement pointer somewhere accessible in the old
workspace if the tree is removed. Do not delete the enclosing AI repository:
memory/Ghidra and other services still live there.

`$REPO/generated/` (~309 MiB) is regenerable but useful for current comparison.
Keep it for now. `$REPO/out/` contained ~970 MiB before this migration's backup;
**never delete it wholesale**: it mixes saves, backups, configs, caches, logs and
build outputs. No active shader-cache cleanup or duplicate game-copy deletion is
recommended as part of this task. This migration does not select any old Xenia
checkout for deletion; it remains a source reference.

## Validation and remaining work

- Verified 29 new supersessions, 13 archives and 73 scope annotations; original
  historical result/evidence text is preserved. Project 3 now has 83 active
  records. Focused searches return decision 146 and corrections 144/145 first.
- Verified Markdown local links, ledger JSON, manifest count, unchanged HEAD/SDK
  diff, and `git diff --check`. Of the old tree's inventoried members, only the
  ledger changed; its historical task payloads were checked against the archive.
- All 18 existing Python tests passed using synthetic fixtures; no game launch.
- No game build/launch or old native proof rerun is required for these docs and
  annotation-only changes. Prior gameplay/test claims remain dated evidence.
- Independent backup copy/restore, live Ghidra restart-persistence check and all
  physical deletions remain pending/user-managed. No commit or push performed.
