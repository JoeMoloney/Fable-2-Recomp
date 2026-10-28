# Fable II extracted content and Lua interfaces

**Investigation date:** 2026-10-28  
**Data root inspected read-only:** `/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/data/`  
**Renderer worktree:** `/mnt/HDD/git_stuff/my_git/Fable-2-native-vulkan-renderer/`

This report is a bounded inventory and reuse assessment, not a full archive
decoder. No extracted game files were changed, relocated, executed, or
re-extracted. No game was launched and no build was performed. For details on
individual Lua APIs, use the existing [`FABLE2_LUA_API.md`](../../FABLE2_LUA_API.md)
catalogue instead of expanding its 2,000+ table members here.

## Executive findings

1. The extracted `data/` tree is **452 files / 63 directories / about 6.5 GiB**.
   Most useful models, textures, levels, and scripts are inside opaque `.bnk`
   banks, not loose mesh/image files. There are only 14 loose `.dds` files and
   two shader banks.
2. `startup.vfsconfig`, per-area `.list` files, and level-specific
   `level.vfsconfig` files provide useful *relationship/index* evidence. The
   global and GUI model/texture header banks are required together with their
   corresponding content banks; the `1024mip0_textures` bank is optional in
   those composites.
3. The renderer repo already has a broad Lua API inventory derived from 1,139
   scripts decompiled from `gamescripts.bnk`, `gamescripts_r.bnk`,
   `guiscripts.bnk` and loose startup scripts. The actual extracted tree only
   exposes a small loose Startup set plus the project-added `scripts/recomp`
   examples.
4. Lua has practical high-level **in-game inspection and scene-control** APIs
   (hero/entity transforms, visibility, camera/freecam, world frame, debug
   overlays, level loading, and persistent frame callbacks). This is useful for
   manual QA and repeatable *setup attempts*, but is not a render-test shortcut:
   the API does not itself expose Vulkan objects, guest render targets, a
   frame-step/freeze contract, or deterministic resource snapshots.
5. Existing Ghidra/generated-code research places rendering below Lua: the game
   selects resources/shaders, mutates guest device state and emits Xenos PM4;
   ReXGlue interprets it and records Vulkan work. No content or Lua discovery
   changes the renderer strategy: retain Xenos command interpretation while
   using Lua as a possible test-scene/inspection aid, not as a replacement GPU
   interface.

## 1. Navigable content inventory

### 1.1 Counts and extension families

Counts measured with read-only `find` over the specified data root:

| Extension / category | Files | Typical role / confidence |
|---|---:|---|
| `.bnk` | 127 | Banked content containers; VFS config names them as banks. Internal schemas vary / not decoded here. |
| `.adb` | 114 | Audio-related database/bank assets by path and naming; not decoded here. |
| `.xml` | 56 | XML content/configuration, especially PubGames/localization. |
| `.bik` | 40 | Bink video; `file` identifies `art/videos/intro.bik` as Bink 1280×720. |
| `.lua` | 35 | Loose startup scripts and project-added recomp utilities. Most gameplay Lua is banked. |
| `.txt` | 15 | Readable data/help/menu files. |
| `.dds` | 14 | Loose DirectDraw Surface images (mostly PubGames/UI); not the main world textures. |
| `.bft` | 12 | Game font files (proprietary font format by extension; not decoded). |
| `.swf` | 7 | PubGames Flash/SWF content. |
| `.gdb` | 7 | Game database files referenced by `.list` files; schema not decoded here. |
| `.babel` | 7 | Localized text payloads by path; not decoded here. |
| `.list` | 6 | Text lists mapping domains to GDB files. |
| `.vfsconfig` | 3 | XML VFS bank/composite configuration. |
| `.csv` | 2 | File-hash indices (audio and language trees). |
| `.sbk` | 2 | ShaderBankFile v3 banks (verified by existing bounded parser and shader research). |
| Other | 5 | `.animation_toc`, `.animation_data`, `.loco`, `.amp`, and `dir.manifest`. |

The extension counts are lexical inventory, not proof of each format's payload
schema. `.bnk`, `.adb`, `.gdb`, `.bft`, `.babel`, animation, and locomotion
payloads remain proprietary/opaque unless an existing parser or file-specific
evidence is cited below.

### 1.2 Directory guide

| Directory / root entry | Files | Contents and useful entry points |
|---|---:|---|
| `audio/` | 213 | Region/creature/music `.bnk` and `.adb`, plus `filehash.csv`; largest directory by count, unrelated to mesh decoding. |
| `pubgames/` | 77 | `keystone/`, `wizardstower/`, `magedice/`, `common/`; XML settings/localization, loose DDS, and SWF assets. |
| `art/` | 46 | `gui/`, `localised/en-uk/gui/`, `particles/particle_bank.bnk`, and Bink cinematics in `videos/`. |
| `language/` | 37 | Locales/fonts/text/speech. `language/en-uk/filehash.csv` maps numeric hashes to named audio files. |
| `scripts/` | 24 | `Startup/` loose game scripts and `recomp/` project-added scripts. |
| `scripts_r/` | 19 | Alternate/release startup-script tree. The two Startup trees contain similarly named script sets; not assumed byte-identical. |
| `Globals/` | 9 | `globals.gdb`, `.list`, global model/texture/header/streaming banks, and speech-action database. |
| `miscellaneous/` | 6 | Readable level, loading, and test-resource metadata such as `levelconnectiongraph.txt` and `e3physicsmeshes.txt`. |
| `worlds/` | 2 | Two Albion demo-door `level.vfsconfig` files; no loose world mesh corpus. |
| `Shaders/` | 2 | `Shaders.sbk`, `ShadersRelease.sbk`. |
| `environmentthemes/` | 2 | `environmentthemes.list` and `environmentthemes.gdb`. |
| `entity/` | 1 | `locomotion.loco`. |
| Root | 12 | `startup.vfsconfig`, `levels.bnk`, `streaming.bnk`, `skeletalmorphs.bnk`, `gamescripts*.bnk`, `guiscripts.bnk`, `fable2_anims.animation_*`, `scenarios.list`, manifest/version files. |

Useful navigation commands (read-only):

```sh
find /mnt/HDD/git_stuff/my_git/Fable-2-Recomp/data -maxdepth 3 -type f | sort
find /mnt/HDD/git_stuff/my_git/Fable-2-Recomp/data -type f -printf '%s %p\n' | sort -nr | head
```

This is a small enough inventory to navigate in Markdown; no separate
machine-readable manifest was warranted.

### 1.3 VFS and resource relationships

`data/startup.vfsconfig` is XML, not a resource archive. Its observed rules:

- Universal banks include `Globals/1024mip0_textures.bnk` and in-memory
  `Globals/globals_model_headers.bnk`.
- Global banks include `Globals/globals_streaming.bnk`, GUI streaming, and
  `skeletalmorphs.bnk`.
- `gamescripts_r.bnk` and `guiscripts.bnk` are mounted in memory.
- A required composite pairs `globals_model_headers.bnk` with
  `globals_models.bnk`.
- Another composite requires `globals_texture_headers.bnk` and at least one of
  the optional `1024mip0_textures.bnk` / `globals_textures.bnk`.
- GUI model and texture composites repeat the same structure using
  `art/gui/gui_*` banks.

Observed text-list relations include:

| List | Referenced database |
|---|---|
| `Globals/globals.list` | `Globals\Globals.gdb` |
| `art/gui/gui.list` | `Art\GUI\GUI.gdb` |
| `environmentthemes/environmentthemes.list` | `EnvironmentThemes\EnvironmentThemes.gdb` |
| localized GUI list | localized GUI GDB in that tree |

These prove declared references, not the complete resource dependency graph or
the internal format of the referenced banks. `worlds/albion/*/level.vfsconfig`
provides per-level VFS entry points. `data/dir.manifest` is the loose-file
manifest used by the game's virtual filesystem; the project F5 loader comments
also document that `loadfile()` uses paths relative to the `data/` root and
requires manifest membership.

### 1.4 Graphics and rendering asset inventory

| Resource family | Extracted evidence | What is / is not established |
|---|---|---|
| Shader bank | `Shaders/Shaders.sbk`, `ShadersRelease.sbk` | Existing `research/native_offline/bank.py` parser validates the `ShaderBankFile` v3 structure; `research/native_shader_catalogue/REPORT.md` counts 1,317 engine entries → 588 unique slots (137 VS / 451 PS), in groups of four. This is substantial reusable shader microcode/reflection evidence. |
| Shader workload | Runtime catalogue files under `research/native_shader_catalogue/` | A retained, single-frame 2,048-record sample has 67 VS/PS pairs; 851 records are one standard-material pair. It is not a whole-game workload census, and the dominant pair's pixel shader is not yet a validated converted SPIR-V pair. |
| Models / textures | `Globals/globals_models.bnk`, `globals_textures.bnk`, header banks; analogous `art/gui/gui_*` banks | Resource families and required pairings are explicit in VFS config. The data tree does not expose loose scene meshes or a direct material JSON inventory. Internal model/material/texture record links were not decoded in this investigation. |
| Streaming / levels | `streaming.bnk`, `Globals/globals_streaming.bnk`, `levels.bnk`, per-level VFS configs | Large level/streaming payload is banked; `.bnk` alone does not prove a particular asset's contents. |
| Particles / animation | `art/particles/particle_bank.bnk`, `fable2_anims.animation_toc`, `fable2_anims.animation_data`, `entity/locomotion.loco` | Resource families are identifiable from naming; schemas and links to render objects not decoded. |
| Video / UI | 40 `.bik` videos; PubGames `.dds` / `.swf`; GUI banks | Bink format confirmed for a representative video. These are useful for UI/cinematic context, not substitutes for scene geometry/shaders. |

The shader format evidence is in `research/native_shader_catalogue/REPORT.md`
and `research/native_offline/bank.py`; do not infer that `.bnk` model banks
share the shader-bank schema. Existing `research/native_mesh/README.md`
states that Event 34258 replay uses captured Vulkan SPIR-V and capture fixtures,
is not connected to the game/plugin, and lacks verified 2× MSAA parity. The
extracted data investigation supplies no evidence changing that limitation.

## 2. Lua script and engine API catalogue

### 2.1 What Lua is present

The loose `scripts/Startup/` and `scripts_r/Startup/` trees contain startup,
audio, console/debug menu, and demo/startup variants. The five files in
`scripts/recomp/` (`F5.lua`, `getPlayerPos.lua`, `modifyMoney.lua`,
`modifyMoralityPurity.lua`, `toggleFreecam.lua`) are project-added development
scripts, not evidence that those helpers shipped in the original game. They
demonstrate a safe-enough *pattern*, not a runtime validation in this report:

- `F5.lua` loads a selected file with `loadfile(path)`, registers a persistent
  per-frame coroutine with `GeneralScriptManager.AddScript`, and polls
  `MessageEvents` to manage menu/message boxes. It documents the LuaPlus/Lua 5.1
  restriction that yielding through `pcall` fails and the menu box's teardown
  hazard.
- `getPlayerPos.lua` queries `QuestManager.HeroEntity`, `Debug.GetHero()` or
  `GetPlayerHero()`, obtains `GetPosition()`, and formats a `CVector3`.
- `toggleFreecam.lua` uses `Debug.GetUseFreeCamera` / `Debug.SetUseFreeCamera`.
- Money and morality scripts call higher-level `Money.Modify`,
  `Debug.SetHeroMorality` and `Debug.SetHeroPurity`.

The API catalogue reports its static input as all three script banks (1,139
decompiled Lua files), plus loose Startup scripts; it cross-checks discovered
calls against recompiled native registration tables and the raw image string
table. It lists 81 native global functions, 2,004 engine tables, 98
engine/metatable methods, 65 registered functions not called by shipped
scripts, plus script-defined APIs. Counts are the catalogue's static-analysis
result, not a dynamic runtime trace.

### 2.2 High-value API/function families

| Goal | API evidence in `FABLE2_LUA_API.md` | Practical value / limit |
|---|---|---|
| Player/scene state | `GetPlayerHero` (1,315 call sites), `GetDog`, `IsLevelLoaded`, `GetLevelLoaded`, `GetCurrentWorldFrame`, entity `GetPosition` | Can sample world/entity state from Lua; a null/dead C++ entity can fault below Lua `pcall` (explicit warning in `scripts/recomp/getPlayerPos.lua`). |
| Visibility/spatial tests | `IsEntityVisibleOnScreen`, `IsPositionVisibleOnScreen`, `IsPositionInCameraFrustrum`, `GetLandscapeHeightAt`, `GetWaterDepthAt`, distance helpers | Useful for finding/reproducing candidate scene locations and confirming entity visibility; not a render-target capture. |
| Debug visualization | `Debug.DrawLine`, `Debug.DrawText`, `Debug.Draw3DText`, `Debug.SetDrawEntityNames`, `Debug.SetDrawEntityNamesFilter`, `Debug.SetDrawNavGraph`, `Debug.StartNewEngineVisMode`, `Debug.ClearEngineVisModes` | Strong in-game inspection affordances. They add debug rendering and so should be disabled/cleared for final image comparisons. |
| Camera | `Debug.GetUseFreeCamera` / `SetUseFreeCamera`; `Debug.MoveCameraToPosition`, `MoveCameraFocusToPosition`; `Camera.SetFOV`, `SetAngles`, `MoveTo`, `GetForwardVector`, `GetFOV`, `GetPositionFromDistanceAndAngle` | Camera placement and freecam can make manual shots more repeatable. Camera and game world may still animate; no verified frozen-time/frame-step interface was established. |
| Scene setup | `Debug.LoadLevel`, `Debug.CreateEntityAtPosition`, `Debug.CreateEntityAt`, `Debug.GetEntityWithName`, `Debug.TeleportToPosition` | High-level ways to set up an in-game test scene, if argument contracts and state dependencies are confirmed. Loading/spawning mutates game state; not safe to treat as an offline unit test. |
| Per-frame control | `GeneralScriptManager.AddScript` and `Update`, `Timing.GetWorldFrame` | A coroutine can poll one-frame state or emit a deterministic script-defined sequence, provided the game reaches the expected level and callbacks continue. |
| File/database inspection | `loadfile`, `LoadFileAsString`, `ReloadGDB` | Access is via the game VFS / game database layer. No evidence these APIs decode scene mesh banks into Lua-visible draw descriptions. |
| Native renderer control | No Lua Vulkan device, command-buffer, guest render-target, SPIR-V capture, or draw-submission API found in the catalog or inspected scripts | Lua cannot directly drive the bespoke Vulkan renderer or provide the fully resolved draw state that Event 34258 fixtures contain. |

For all member details and counts, see [Lua API catalogue §1–§4](../../FABLE2_LUA_API.md).
The catalog indicates `Debug` alone has 425 members, and `Camera` has a broad
set of transforms, FOV, blur/DOF, ray-intersection and debug-camera helpers.
The table above intentionally selects only renderer/test-relevant entries.

### 2.3 Lua opportunity assessment

**Meaningful, but bounded:** Lua can help construct a manually reproducible
*game scene* and report state. A future approved desktop test harness could
load a known level, place the camera, wait for a selected `Timing.GetWorldFrame`
or level-ready condition, report hero/camera positions, and optionally draw
temporary debug markers. F5's persistent coroutine and message IDs are a
concrete example of a game-triggered control loop.

**Not a shortcut to renderer validation:** the scripts operate at engine/game
level. They do not expose the Xenos registers, PM4 packet, shader constants,
resource aliases, render-target pre-state, completion fences, or Vulkan
command stream needed for a controlled native-renderer image test. The
`FABLE2_LUA_API.md` catalogue also counts static call usage; it does not prove
that every API is safe or valid in every level/script state. A manual game
session would still be required to validate the scripts and any chosen scene.

## 3. Compiled code, VFS use, and graphics boundary

### 3.1 File loading evidence

The generated recomp source exists in the sibling code repository at
`/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/generated/default/` (295
`fable_2_recomp.*.cpp` partitions measured). The generated registration files
expose imported `NtOpenFile` / `NtReadFile` routines (for example
`generated/default/fable_2_funcs.h`, entries near 60,749 and 60,753), and
generated guest code calls these imports (`fable_2_recomp.283.cpp:22618–22620`
shows an `NtOpenFile` use). The SDK/app VFS configuration is the host-side
content mapping. This verifies a guest file-open/read path exists; it does not
prove that every `.bnk` is opened through that exact guest path or identify a
single retail loader call for each listed resource family.

### 3.2 Verified graphics path and relevant functions

The active Ghidra program was queried read-only before address-based work:

- `default.xex`, Xenon360 XEX, big-endian 32-bit-address PowerPC with VMX128.
- SHA-256 `88c4ef2e18e65409444d1b068eff921d1f7e180a5ae64edc64ba6b0872372662`,
  image base `0x82000000`.
- Ghidra read-only decompilation of `0x82B7CD18` shows a bank loader validating
  version `3`, the debug/release flag, and dispatching table/entry parsing;
  `0x82B7E050` computes a four-slot group/slot selection, obtains a compressed
  payload, decompresses it, then passes the result to a shader install routine.
  This independently supports the shader-bank analysis already recorded in
  `plans/NATIVE_RENDERER_OFFLINE_PROTOTYPE.md:91–118`.

The existing
[`FABLE2_NATIVE_GRAPHICS_INVESTIGATION.md`](../graphics_architecture/FABLE2_NATIVE_GRAPHICS_INVESTIGATION.md)
traces the frame root `MainRenderLoop_82B9CD68` → `VdGetSystemCommandBuffer` /
`VdSwap` → guest GPU ring/MMIO → PM4 decoder → `VulkanCommandProcessor::IssueDraw`
→ backend-owned Vulkan draw recording. It records verified game-side boundaries:

- `0x8221C518` flushes dirty guest state and emits `PM4_DRAW_INDX`; it is not a
  standalone native draw ABI.
- `0x821B7020` updates vertex-fetch state/resource binding and usage; shader
  selection/lazy-load helpers include `0x8222C268`, `0x82208C48`, and
  `0x82B7E050`.
- Resource retain/release/cleanup include `0x821FC048`, `0x821FC1F0`,
  `0x821F6490`, and progress synchronization before freeing.
- `0x821FC4A0` is a bounded mip-generation candidate, but result publication,
  aliases and synchronization still need validation.

These graphics/resource addresses come from the existing Ghidra/generated-code
audit, not from Lua or file names. The backend owns interpretation of Xenos
state, resource residency, render-target/pipeline state, and submission; Lua
does not bypass those contracts.

## 4. Reuse opportunities and strategy impact

1. **Use Lua for test-scene control, not GPU injection.** If approved later,
   define a small script that reports level/world frame, hero position, camera
   parameters, and an optional marker. First validate its safety in a normal
   game session; do not make it a prerequisite for offline graphics tests.
2. **Prioritize existing shader-bank/reflection work.** The `.sbk` parser and
   `shader_catalogue.json` are already reusable, mechanically validated data.
   The catalogue's standard-material pair is high-frequency only in the
   retained sample; its pixel-shader 1D descriptor gaps remain a conversion
   blocker. Do not mistake `SBK` entries or a script-visible material name for
   a complete live draw contract.
3. **Do not begin with global draw replacement.** Keep Xenos PM4 interpretation
   intact. A game-side native operation needs guest state, resource lifetime,
   output publication, barriers, and fallback semantics. The existing
   architecture report's mip operation is narrower than `0x8221C518`, but still
   requires validation.
4. **No new archive framework from this survey.** 452 files are navigable by
   directory/extension; existing bounded SBK parser and shader catalogue cover
   the proven shader-bank need. Decode other `.bnk` families only when a
   renderer requirement names specific resources and a parser can be bounded.
5. **No strategy change warranted.** Lua is a plausible manual QA/scenario
   driver; it does not make the renderer headless, deterministic, or independent
   of Xenia/ReXGlue's GPU interpretation.

## Evidence and confidence notes

- **Directly measured here:** data root path exists; 452 files, 63 directories,
  approx. 6.5 GiB; extension and top-level counts; representative `file`
  identification; `startup.vfsconfig` and list references.
- **Verified by existing source/artifact analysis:** shader bank v3 layout,
  shader counts, retained runtime shader sample, Event 34258 fixture limits,
  graphics call chain, generated import use, and Ghidra image identity.
- **Static API evidence:** Lua calls/registrations from existing
  `FABLE2_LUA_API.md`; source-level behavior in the five project-added
  `scripts/recomp` examples. The current investigation did not execute scripts.
- **Not established:** complete `.bnk`/`.gdb` schemas; material → mesh → texture
  record IDs for all levels; exact runtime safety/signatures for every Lua API;
  deterministic frame freeze/step or render-target capture from Lua; runtime
  parity/performance of any native renderer integration.

The four workers were launched read-only with the configured OpenAI GPT-6 Luna
model. One worker incorrectly reported the data path unavailable; direct
coordinator inspection confirmed it is present and all counts above are from
that path. The worker findings were treated as leads, not ground truth.
