# Reverse-engineering status and source of truth

Updated 2026-10-24. This repository is the authoritative implementation of the
Fable II recompilation. The former standalone XenonRecomp/custom-runtime
prototype is historical research, not the development baseline or a prerequisite
for continuing this fork. The user explicitly selected this migration.

## Evidence and ownership

- Implementation/build configuration: tracked source, manifest and pinned SDK.
- Current facts: this document and [address-based findings](RE_FINDINGS.md), with
  reproducible evidence and explicit limits. Names/comments alone are not proof.
- Historical experiments: [Linux progress](../history/LINUX_PORT_PROGRESS.md), memory records
  and preserved local artifacts. Their dated pending tasks are not current work.
- Memory-service: project **FableII, ID 3**. Search with `projectId: 3` and
  `includeGlobal: false`. Use memory as an index of decisions/evidence, not a
  second implementation or a duplicated live task ledger. A global pointer is
  retained for discovery; new game-specific knowledge belongs in project 3.
- Ghidra: a secondary analysis database. Reconcile annotations against evidence;
  do not bulk-import manifest names or inferred decompiler prototypes.

Record the target hash, guest address, source revision, observation versus
interpretation, and test scope. Generated partition numbers may change after
codegen: locate by address/symbol. Isolated prototype tests do not certify the
SDK, whole console ABI, or game behavior. Preserve negative results and corrections.

## Target and implementation baseline

- Original GOTY USA/Europe `default.xex`, 21,217,280 bytes, SHA-256:
  `88c4ef2e18e65409444d1b068eff921d1f7e180a5ae64edc64ba6b0872372662`.
- Local file hash and live Ghidra program identity match (2026-10-24).
  PowerPC big-endian, four-byte pointers, entry `0x82CBB970`, base `0x82000000`.
- Edition acceptance is defined by `constants/game_versions.json`; this audit
  does not newly certify other editions or DLC completeness.
- Migration source baseline: commit `eb3293c`; SDK gitlink
  `cc5f03dd0488720a0f277412d7c36b63964d5b18`. Preserve the intentional local
  vendored-libmspack patch, exported in
  `thirdparty/rexglue-sdk-linux-vendored-mspack.patch`.
- Native Linux ELF, ReXGlue source runtime, Vulkan; no Wine/Proton.
  [Linux workflow](../LINUX.md): `python tools/linux.py run` uses native Wayland,
  GPU 0, 1280x720 windowed, 30 FPS limit, mouse look off and asynchronous shader
  compilation off. Keep that working baseline while investigating.

## Validated capabilities and limits

Prior user testing on CachyOS/KDE Wayland with RTX 3090 confirmed title/menu,
new-game/early gameplay, Steam Controller hotplug/use, idle cinematic return via
A, Continue loading an existing save, intro skip via Start and in-game cutscene
playback. The user confirmed no missing dialogue or music. Effects, mix and
channel correctness were not separately certified. These are prior session
results, not a new execution during this documentation migration.

Still unresolved:

- Persistence/reload of **newly earned** progress. Existing-save loading is not
  proof of a completed new autosave. Preserve saves and backups before testing.
- Hitching and unspecified graphics defects in the crowded early trader scene
  with the LT zoom prompt. Collect precise screenshots and cold/warm frame times;
  shader compilation is a possible contributor, not a demonstrated root cause.
- Earlier `BaseHeap::Release` not-region-start diagnostic; subsequent execution
  continued and its absence from a later log does not prove a fix.
- Repeatable startup and longer-session stability. Disabling asynchronous shader
  compilation is a provisional white/silent-window workaround, not a root-cause fix.
- Comprehensive resize and Windows regression coverage for the Linux changes.

Do not infer freezes/crashes from diagnostic timeout exits or guest-handled
SIGSEGV alone. Do not delete caches, bypass hashes, change global Steam settings,
weaken ptrace security or reset the SDK as speculative fixes.

## Linux Debug compilation checkpoint — 2026-10-24

`bash build-debug-linux_amd64.sh` now completes successfully after the
remote-control header portability fixes: defer recursive JSON value special
members/lookup until the type is complete, and include `<netinet/tcp.h>` on
non-Windows hosts. The original header fails the isolated Clang compile check;
the fixed parser passes Clang, GCC and ASan/UBSan checks, and all 20 Python tests
pass. See [test instructions](../../tests/README.md#remote-control-json-portability-check).

The Debug executable and its libraries are staged, with `FABLE2_REMOTE_CONTROL`
enabled. Existing Release executable/libraries and all ten Release save files
were hash-checked unchanged. This is **build verification only**: Debug gameplay,
`ping`, `game_state` and screenshot capture have not yet been tested. Start with
`bash run-debug.sh`; Debug keeps its own saves. No game was launched for this fix.

## Development after reconciliation

First verify a completed autosave survives a normal quit/reload, then investigate
the repeatable trader scene and allocator diagnostic with bounded evidence.
Do not resume the old prototype's heap-creator/process-provider queue.
Historical authorization for autonomous work, commits or delegation is not a
new grant for unrelated tasks. No deletion or push is part of this migration;
the user will handle approved directory cleanup.

See [migration record and cleanup rules](RE_MIGRATION.md).
