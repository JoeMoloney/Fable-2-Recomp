# Documentation

The project overview and build instructions are in the top-level [README](../README.md).
Work plans are in [plans/](../plans/README.md).

## Guides

| Doc | What it covers |
|---|---|
| [LINUX.md](LINUX.md) | Building and running natively on Linux, current performance status and known limits. |
| [patches.md](patches.md) | Patching the guest image at runtime (Xenia `patch.toml` format). |
| [hotfunc_overrides.md](hotfunc_overrides.md) | Hand-editable copies of recompiled functions under `src/core/hotfunc/` that override the generated code. |
| [RUNTIME_FIXES.md](RUNTIME_FIXES.md) | Windows runtime fixes and the SDK baseline they apply to. |
| [GERMAN_GOTY_SUPPORT.md](GERMAN_GOTY_SUPPORT.md) | Which game editions (XEX hashes) are accepted, including German GOTY. |

## Reverse engineering ([re/](re/))

| Doc | What it covers |
|---|---|
| [RE_STATUS.md](re/RE_STATUS.md) | **Start here.** Where reverse-engineering facts live, the target XEX, and the rules for recording findings. |
| [RE_FINDINGS.md](re/RE_FINDINGS.md) | Address-by-address corrections to earlier function names. |
| [FUNCTION_NAMES.md](re/FUNCTION_NAMES.md) | Historical reference for 520 named functions (role and trigger). Check `RE_FINDINGS.md` for corrections. |
| [FABLE2_LUA_API.md](re/FABLE2_LUA_API.md) | Every custom Lua function the game scripts use. |
| [TEXT_RENDERING_NOTES.md](re/TEXT_RENDERING_NOTES.md) | How UI text is rendered and how to intercept it. |
| [RE_MIGRATION.md](re/RE_MIGRATION.md) | Record of the 2026-10-24 switch to this repo as the source of truth (memory IDs, Ghidra renames, backup in `out/re-migration/`). |

## Bug fixes ([fixes/](fixes/))

| Doc | What it covers |
|---|---|
| [main_menu_crash_fix.md](fixes/main_menu_crash_fix.md) | Source builds crashing at the main menu (AllocFixed). |
| [two_controller_black_menu_fix.md](fixes/two_controller_black_menu_fix.md) | Black main menu when a second controller presses a button. |
| [FPS_CAP_INVESTIGATION.md](fixes/FPS_CAP_INVESTIGATION.md) | How the 30 FPS cap works and how to run uncapped. |
| [ICON_BUTTON_A_NOTES.md](fixes/ICON_BUTTON_A_NOTES.md) | Where the Xbox A-button icon texture lives and what draws it. |

## History ([history/](history/))

| Doc | What it covers |
|---|---|
| [LINUX_PORT_PROGRESS.md](history/LINUX_PORT_PROGRESS.md) | Dated log of the Linux port experiments (Oct 2026). |

## Analysis ([analysis/](analysis/))

| Doc | What it covers |
|---|---|
| [PERF_INVESTIGATIONS_OCT_NOV_2026.md](analysis/PERF_INVESTIGATIONS_OCT_NOV_2026.md) | **Start here.** Summary of the October Steam Deck and November offline perf investigations. |
| [NAMING_EVIDENCE_RESULTS.md](analysis/NAMING_EVIDENCE_RESULTS.md) | First naming run: 596 new names and a map of the third-party libraries (Havok, Microsoft XDK, Gameface, XLip, pub-game framework). |
| [THESEUS_INVENTORY.md](analysis/THESEUS_INVENTORY.md) | Inventory of all 60,753 guest functions and the first differential test of a readable rewrite (FNV1Hash). |
| [OFFLINE_SUBSYSTEM_AUDIT.md](analysis/OFFLINE_SUBSYSTEM_AUDIT.md) | CPU, memory and timing audit (2026-11-10), including the manifest codegen options that are ignored. |
| [LOCALIZATION_ACTUAL_CODEGEN_RESULTS.md](analysis/LOCALIZATION_ACTUAL_CODEGEN_RESULTS.md) | Register-localization codegen experiment: correct, but no speedup. |
| [ARITHMETIC_AND_NATIVE_MEMORY_RESULTS.md](analysis/ARITHMETIC_AND_NATIVE_MEMORY_RESULTS.md) | AVX/FMA failed its equivalence check; native memory copy microbenchmarks. |
| [COPY_CALLER_AND_WRITE_WATCH_AUDIT.md](analysis/COPY_CALLER_AND_WRITE_WATCH_AUDIT.md) | Callers of the guest copy helper and the write-watch fault path. |

## Elsewhere

- [tests/README.md](../tests/README.md): how to run the tests.
- [tools/theseus/README.md](../tools/theseus/README.md): function inventory, naming evidence and rewrite-checking tools (per-game config, setup for other games).
- [tools/repro/README.md](../tools/repro/README.md): reproducible, antivirus-clean Windows builds.
- [launcher/README.md](../launcher/README.md): the Windows launcher.
