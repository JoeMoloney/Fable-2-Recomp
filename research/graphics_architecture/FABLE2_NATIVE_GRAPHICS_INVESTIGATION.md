# Fable II native graphics architecture investigation

**Scope/date:** Read-only source and existing-artifact investigation, 2026-10-28. No game launch, build, deployment, capture, or renderer modification was performed. This report is on `feature/native-vulkan-renderer`.

## Executive summary

Fable II owns substantial high-level graphics work: its recompiled PowerPC code updates device/state structures, selects and binds engine shader/resource wrappers, tracks resource references, prepares draw data, emits Xenos PM4 packets, and requests presentation. ReXGlue/Xenia owns the generic GPU contract after submission: guest MMIO/ring-buffer handling, packet decoding, register-file state, Xenos shader/primitive interpretation, host resource synchronization, Vulkan pipeline construction, and Vulkan draw/resolve execution.

There are real, identifiable game-side boundaries, but no demonstrated stable “render this material/mesh” native ABI. The low-level draw helper at **`0x8221C518`** is a concrete redirect candidate: its generated PPC body flushes dirty register banks and emits a `PM4_DRAW_INDX` packet (`0xC0012201`). It is not a pure draw call: it mutates guest state and relies on earlier geometry, shader/fetch, and render-target setup. A full redirect there would either reconstruct those semantics or silently omit them. The smallest bounded strategy-C experiment with the strongest existing call-chain evidence is instead the **texture mip-generation operation `0x821FC4A0`**, replacing one call under an opt-in path while preserving the guest wrapper/state/resource contract. This exercises the real integration boundary without pretending a whole scene can be bypassed.

Recommendation: **C merits a tightly scoped prototype, but not as a performance claim or full renderer replacement.** Retain A as the correctness baseline and use B only after profiling identifies repeatable Fable-specific hotspots. Prototype one isolated mip operation or a similarly pure sub-operation, preserve the existing implementation as fallback, and measure only after correctness and synchronization are proven. For a representative scene draw, the current standalone Event 34258 replay proves captured-data Vulkan execution is possible; it does not establish an in-game CPU interception interface or parity.

## Architecture and representative call flow

```text
Fable II recompiled PPC/game code
  MainRenderLoop_82B9CD68 (0x82B9CD68)
    -> frame/drawlist work and game-side resource/material/state setup
    -> dirty state + geometry helpers
       0x8221C518: flushes state banks, writes PM4_DRAW_INDX words
       (other paths exist; mip helper uses 0x82217EE8 and resolve uses 0x822069C0)
    -> guest command memory / command-buffer cursor
    -> __imp__VdGetSystemCommandBuffer, __imp__VdSwap
       [generated/default/fable_2_recomp.45.cpp:21072, 21125]
    -> GPU MMIO write / command ring write pointer
       [ReXGlue GraphicsSystem::WriteRegister, CP_RB_WPTR]
    -> CommandProcessor::ExecutePrimaryBuffer / ExecutePacket
       -> ExecutePacketType3_DRAW_INDX
       -> ExecutePacketType3Draw -> virtual IssueDraw
    -> VulkanCommandProcessor::IssueDraw (0xXenia host code, not guest address)
       -> analyze guest shader/state, process primitives and memory
       -> pipeline, descriptor, barriers/render-target setup
       -> vkCmdDraw / vkCmdDrawIndexed through deferred command buffer
    -> submit/present or resolve/readback as required
```

This is a representative path, not proof every Fable draw uses `0x8221C518`. The existing source audit found a second emitter (`0x82217EE8`) on the mip/blit path and explicitly leaves other draws/prebuilt command buffers to inventory.

## Detailed findings

### 1. Game-side graphics work and candidate functions

**Verified from the retail XEX with read-only Ghidra and reconciled to generated code:** active Ghidra program is `/data/FableII/default.xex`, Xenon360 PPC big-endian, image base `0x82000000`, SHA-256 `88c4ef2e18e65409444d1b068eff921d1f7e180a5ae64edc64ba6b0872372662`. The generated source is in `/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/generated/default/`.

* **Frame/presentation root — `MainRenderLoop_82B9CD68` / `0x82B9CD68`.** `generated/default/fable_2_recomp.45.cpp` shows a `VdGetSystemCommandBuffer` import at line 21072 and `VdSwap` at line 21125. The function also writes command-related data and calls game helpers. `docs/ICON_BUTTON_A_NOTES.md` records drawlist buckets (Lights/Opaque/Transparent/PostProcess) and drawable dispatch as prior disassembly-backed investigation, but this report did not identify the scene-mesh drawlist consumer by name. Treat it as a frame orchestration entry, not a single-draw hook.
* **Dirty-state and draw emitter — `ProcessAndProcessAndProcess841_8221C518` / `0x8221C518`.** Registered in `generated/default/fable_2_register.cpp:1653`; body at `fable_2_recomp.262.cpp:1070+`. Ghidra decompilation and generated PPC both show several dirty-state banks being flushed (float constants at register bases `0x4000`/`0x4400`, other register spans, fetch groups, booleans), then an emitted packet whose first word is `0xC0012201`. SDK `xenos.h` identifies PM4 opcode `0x22` as `PM4_DRAW_INDX`; generated packet fields carry draw mode/index-count-related values, with chunking when counts exceed packet limits. **Interpretation:** this is a useful state/draw boundary, but replacing it with `vkCmdDrawIndexed` alone would drop the preceding state effects and guest packet-visible behavior.
* **Alternative draw emitter — `0x82217EE8`.** `plans/NATIVE_RENDERER_FABLE_BOUNDARIES.md:127–153` traces a distinct temporary-geometry path: `0x821FC4A0` → `0x821FA0D0` → `0x821F27E0` → `0x8220A528` → `0x82217EE8`, followed by resolve `0x822326B0` → `0x822069C0`. The generated wrapper builds four vertices/six indices in temporary storage and publishes a command cursor. This is direct evidence that hooking only `0x8221C518` misses real rendering work.
* **Resource/shader management — selected verified boundaries.** `0x821B7020` merges six fetch words from a resource into the device fetch bank, clamps mip state, stores a bound-resource pointer, and records prior use; it is not merely a descriptor bind. `0x8222C268` and `0x82208C48` select shader entries from an engine table and lazily load via `0x82B7E050`; binding helpers update device dirty masks/lifetime state and process embedded state blocks. Guest references and destruction are also semantic: `0x821FC048` retains; `0x821FC1F0` releases and may release a parent; `0x821F6490` performs typed cleanup, and can call `0x82242628` progress synchronization before freeing. Evidence and caveats are documented in `plans/NATIVE_RENDERER_FABLE_BOUNDARIES.md:44–109, 155–170`.
* **Bounded operation candidate — mip generation `0x821FC4A0`.** It creates/binds temporary targets, draws and resolves levels; it checks dimensions/level count and stops when a dimension reaches 16 or less. Its draw path is separate from the general `0x8221C518` emitter. `plans/NATIVE_RENDERER_OFFLINE_PROTOTYPE.md:158–186` further specifies the quad, destination/UV behavior and sampled texture state, while explicitly listing remaining orientation/rounding validation.

Function names generated from recompilation are descriptive labels or address-derived names, not necessarily recovered original engine symbols. Addresses are more stable lookup keys than partition numbers/line numbers.

### 2. Guest submission to ReXGlue/Xenia Vulkan

**Verified source path:**

1. `generated/default/fable_2_recomp.45.cpp:21072,21125` shows the render-loop code obtaining the system command buffer and calling `__imp__VdSwap`.
2. `thirdparty/rexglue-sdk/src/graphics/graphics_system.cpp:124–153` sets up the guest GPU and maps the GPU register window (`0x7FC80000` range). At `:272–288`, register index `0x01C5` (`CP_RB_WPTR`) calls `command_processor_->UpdateWritePointer(value)`.
3. `command_processor.cpp:731–805` reads the primary command ring, decodes packet type bits, and routes type-3 packets. `:1425–1543` parses draw initiator/index DMA fields then calls `IssueDraw`; `:1546–1564` handles `PM4_DRAW_INDX`.
4. `graphics/vulkan/graphics_system.cpp:27–33` constructs `VulkanCommandProcessor` for the Vulkan graphics system.
5. `graphics/vulkan/command_processor.cpp:3683+` implements `IssueDraw`. It analyzes active guest shaders and Xenos registers, checks primitive/rasterization/depth state, identifies needed pixel shader and memexport behavior, processes geometry, updates system constants and descriptors, requests vertex ranges from shared memory, and synchronizes relevant memory. At `:4204–4247` it enters the render-target pass, binds the index buffer where needed, and emits Vulkan draw commands. This makes clear that the GPU backend is doing much more than forwarding a native draw call.

The game produces a command stream whose packet/state conventions are Xenos-specific. ReXGlue is responsible for interpreting that stream and turning it into host API work. Replacing the Vulkan backend only changes the last API implementation; bypassing command interpretation requires game-side replacements that reproduce the observable results and side effects of the skipped packet/state processing.

### 3. Shader/resource evidence from retained captures

The existing `research/native_shader_catalogue/REPORT.md` records a retained runtime sample of 2,048 draw records from one frame (one dropped record; not whole-game frequency). It finds 67 distinct shader pairs. The most frequent pair occurs 851 times in that sample: VS hash `ecd66a10092e6562` / candidate bank slot 245 with PS `8d900846800943c8` / bank slot 375, interpreted as a standard-material family. All observed 31 vertex hashes occur in retained XESH caches; all 49 non-zero pixel hashes match bank microcode. Vertex-fetch patching explains many differences between bank VS bytes and runtime cache variants. The high-value standard material does **not** yet have a validated converted SPIR-V pair; the report notes relevant pixel shader 1D descriptor gaps. Counts are sample-specific, not global workload shares.

The town-square Event 34258 extraction provides a concrete per-draw data point, not a general API: a captured Vulkan SPIR-V vertex/pixel module, 9,576 indices, constants, five textures, 17 shared-memory fetch ranges and captured state. The existing native replay uses RenderDoc-captured Vulkan SPIR-V directly, **not independently converted Xenos shaders**. Its recorded module SHA-256 prefixes are `43f9dd90…11bbcce0` and `d2b6f4d2…554782faf`. The saved evidence is documented in `research/native_mesh/README.md`, with offline artifacts under `/tmp/opencode/`. Prior single-sample results explicitly do not establish 2× MSAA parity. This demonstrates that a captured draw can be reconstructed from serialized draw state; it does not show the game can cheaply or safely provide that state through a high-level hook.

### 4. Strategies A, B and C

| Strategy | What changes | Benefits | Main costs/limits | Evidence-based view |
|---|---|---|---|---|
| **A. Replace Xenia Vulkan components progressively** | Keep guest command interpretation; improve or replace Vulkan-side shader translation, primitive processing, resource caches, render targets and synchronization. | Preserves the established guest packet/state contract; broad compatibility and usable correctness baseline. | Retains PM4 decoding and much Xenos emulation; backend changes are deep and need general coverage. | Current SDK directly shows packet decode → IssueDraw → shader/geometry/state translation → Vulkan recording. Safest fallback and reference. |
| **B. Retain interpretation, specialize for Fable II** | Keep packet stream and register semantics, add narrow Fable-specific caches/fast paths/specializations inside the command processor. | Can exploit known shader families/resources without inventing a game-facing API; avoids bypassing guest state changes. | Still interprets packets and must support variants, aliases, lifetimes, resolves and CPU-visible GPU effects. A cache only helps if hashes/state/resources are stable. | Runtime shader catalogue shows repeated pairs in one sampled frame, but standard-material SPIR-V/1D bindings remain incomplete; no timing evidence that specialization wins. |
| **C. Replace selected Fable graphics CPU operations with native rendering** | Intercept selected game functions and issue native renderer operations instead of their Xenos commands, with explicit output/resource publication and fallback. | Can bypass command generation/interpretation for the selected operation and reuse game-selected inputs. Offers a bounded experiment. | Must reproduce hidden guest side effects, real PPC ABI/registers, resource aliases/refcounts, ordering, barriers, resolves, queries, progress waits and output visibility. Mixed native/Xenos device ownership is significant. | Source establishes candidate functions, not a usable general draw ABI. Offline Event 34258 is not in-game integration. Performance benefit is unknown and must be measured. |

### 5. Direct-native interface requirements and cost accounting

A reliable native draw submission would need, at minimum:

* guest object/resource identity plus generation/lifetime ownership, address and range; data must be copied or pinned where guest stack/temporary memory is reused;
* vertex/index sources, formats/endian conversion, fetch descriptors, strides, offsets, topology, index count and base vertex;
* shader identity and executable modules (or a deterministic translation/cache key), vertex-fetch declaration variants, constants, bool/loop state and resource bindings;
* textures/views including tiled layout, swizzle, endian, format, mip/LOD/filter/address modes and alias invalidation;
* color/depth target identities, sample count, viewport/scissor, blend, depth/stencil, rasterization, sample state and render-pass load/store behavior;
* GPU ordering/barriers, resolve behavior, completion tokens, guest-visible memory writes/readbacks and safe resource retirement.

This mostly **relocates** interpretation unless Fable already exposes those inputs in a compact stable object. Native code would still need shader selection/translation, texture decoding/layout conversion, geometry preparation, pipeline creation, synchronization and lifetime tracking. The captured Event 34258 fixture contains these inputs because RenderDoc observed the fully resolved backend state; the game CPU hook cannot assume the same ready-to-consume representation exists at its function boundary.

No speedup is asserted. A native call may remove PM4 packet decode and some generic state processing for the intercepted operation, but may add extraction, conversion, duplicate GPU work, queue synchronization or CPU/GPU waits. The existing feasibility plan records GPU wait intervals, not guaranteed recoverable frame time; no A/B/C performance comparison was performed here.

## Recommendation and smallest prototype

**Proceed with one opt-in C prototype only after defining a full correctness fallback.** Prefer the isolated `0x821FC4A0` mip-generation operation over redirecting `0x8221C518` or an entire render loop:

1. Preserve original PPC entry/calling semantics and call the original path by default.
2. Capture inputs/outputs at that boundary from already-available offline evidence or a controlled existing capture; do not introduce a new capture system for this research conclusion.
3. Implement one native operation on renderer-owned resources, with explicit layouts, format/swizzle/endian, mip extent/filter/LOD and completion/publication semantics.
4. Keep guest wrapper/reference counts and device dirty state consistent, and preserve the existing Xenos resolve/update path unless the native result is proven visible to every consumer.
5. Compare output bytes/pixels and downstream mip consumers; then measure the exact removed CPU/GPU work against an unmodified fallback. Do not extrapolate to scene rendering.

Why not first redirect `0x8221C518`? It is the smallest obvious *draw packet emitter*, but it is downstream of state setup and is itself the dirty-state flush. It accepts register/device-oriented PPC inputs, not a stable Vulkan DrawItem; it also has direct callers beyond one material and a separate draw emitter exists. A native replacement that merely emits `vkCmdDrawIndexed` would be semantically incomplete. The mip operation is narrower, has a traced temporary-target/draw/resolve chain, and has prior offline shader/resource investigation, though its publication and lifetime contract still needs runtime validation.

### Confidence classification

* **Verified source behavior:** function bodies and SDK paths cited above; packet constants/opcode dispatch; shader catalogue parsing/hashes; retained Event 34258 payload inventory; Vulkan `IssueDraw` responsibilities.
* **Strongly supported interpretation:** Fable originates high-level scene/material choices and Xenos command generation; ReXGlue interprets the generic Xenos command/state model; C can bypass only the exact work whose side effects it replaces.
* **Requires runtime validation:** which game-side function is a stable and frequently used scene-mesh choke point; whether replacing `0x821FC4A0` can publish a native mip result to all consumers without duplicate resolves; whether any strategy improves frame time; actual ABI/hook safety for a selected call in the current app build.

## Principal evidence index

* Game source/decomp audit: `/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/plans/NATIVE_RENDERER_SOURCE_AUDIT.md:140–208`.
* Resource/state/draw boundaries: `/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/plans/NATIVE_RENDERER_FABLE_BOUNDARIES.md:44–170`.
* Mip operation contract and limitations: `/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/plans/NATIVE_RENDERER_OFFLINE_PROTOTYPE.md:158–218`.
* Recompiled draw emitter: `/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/generated/default/fable_2_recomp.262.cpp:1070+`; registration `fable_2_register.cpp:1653`.
* Frame/swap call: `/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/generated/default/fable_2_recomp.45.cpp:21072–21125`.
* ReXGlue MMIO/ring: `thirdparty/rexglue-sdk/src/graphics/graphics_system.cpp:124–153,272–296`.
* Packet decoder: `thirdparty/rexglue-sdk/src/graphics/command_processor.cpp:731–805,1425–1564`.
* Vulkan backend: `thirdparty/rexglue-sdk/src/graphics/vulkan/command_processor.cpp:3683+`, especially `4062–4247`.
* Runtime shader evidence: `research/native_shader_catalogue/REPORT.md`.
* Captured per-draw evidence: `research/native_mesh/README.md` and `/tmp/opencode/town-square-event34258-msaa-20261028-152702/manifest.json` (external ignored evidence, not committed).
