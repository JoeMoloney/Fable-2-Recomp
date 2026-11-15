# Native renderer — offline prototype handoff

2026-10-26. **Offline prototype approved; implementation has not started.**

## Latest authorization and hard boundaries

User: “Cool, lets do this offline prototype, you can use the local
bonsai-2:27b if needs be, should we start a new session for context or continue?”

- Proceed with an isolated desktop offline prototype, including its own builds
  and test execution. This supersedes the earlier read-only restriction **for
  this prototype only**, not for the gameplay renderer or installation.
- Optional local `bonsai-2:27b` use is now permitted. Look up its exact available
  provider/model ID before delegation; no model or subagent was used in this
  research session. Keep delegated work scoped and independently verify results.
- No game launch, in-game hooks, SDK replacement, draw/resolve suppression,
  gameplay build/staging, save/config/cache changes, commits, pushes or cleanup.
- Deck capture/diagnostic remains paused. No Deck access is needed.
- Preserve all existing modified/untracked parent and SDK work and the desktop
  gameplay installation. Read the workspace gate in
  `NATIVE_RENDERER_SOURCE_AUDIT.md:230–242` before implementation/builds.
- Recommended fresh session for implementation. This handoff was written before
  creating any branch/worktree, implementation, build or launch.

Read with `NATIVE_RENDERER_FEASIBILITY.md`, `NATIVE_RENDERER_SOURCE_AUDIT.md`,
and `NATIVE_RENDERER_FABLE_BOUNDARIES.md`. Memory-service project 3 entries
170–171 record the latest authorization/findings; 164–169 are historical context.
The latest authorization above takes precedence over earlier restrictions.

## Proposed first experiment

A standalone native Vulkan textured-quad/mip test, **not a replacement for the
game renderer**. Use one verified Fable case: a 512×512 texture and its
256×256, 128×128 and 64×64 output levels. Derive inputs and a corrected reference
from the retained desktop mip capture; compare each output, not only a final PNG.

Prefer a small independent CMake project in an isolated research worktree, with
its own output/dependencies. Do not configure the Fable root project or reuse its
gameplay SDK/plugin output: root codegen/staging can modify source/install files.
Inventory dirty dependencies and satisfy backup/isolation gates first. A clean
worktree does not automatically contain the untracked plans, game data or existing
uncommitted fixes. Copy only reviewed dependencies; do not initialize/fetch or
replace an SDK merely for convenience.

Stages:

1. Reproduce read-only bank/container/declaration parsing in a bounded analysis
   tool with bounds checks and tests. Read original banks/caches without changing
   them; write derived fixtures only into the isolated research area.
2. Establish a provenance-checked source image, operation state and **corrected**
   reference from the existing capture. Offline RenderDoc replay/extraction is
   part of the proposed test, not permission to launch/capture the game. Check
   headless/offline tooling availability and isolate any replay cache/output.
3. Render the selected operation with native images and a small verified shader
   equivalent. A general Xenos converter is not required for this two-shader case.
   No upstream application code copying; retain dependency license notices.
4. Compare every level numerically and visually: channel order, alpha, orientation,
   edges, pixel centers, filtering/LOD and format rounding. Specify tolerance
   before inspecting native results; report exact equality and error distribution,
   not just pass/fail. Synthetic patterns supplement, not replace, real inputs.
5. Stop and review correctness. Only then discuss tiled-memory round trips or an
   in-game bridge. Offline success is not publication/lifetime correctness, full
   renderer coverage, a game-frame benchmark, or evidence of Deck speedup.

## Baseline and evidence limits

- Parent HEAD: `fb9d2fb9554bff5a207015f4cac197272cabc497`.
- SDK HEAD: `521aa5906e8aa138870bf8d36bd602eef590ed55`.
- Ghidra `default.xex`, PowerPC BE, 32-bit pointers, SHA-256
  `88c4ef2e18e65409444d1b068eff921d1f7e180a5ae64edc64ba6b0872372662`.
- Prior archive: `out/performance/native-renderer-feasibility-20261026/`.
- The follow-up results below came from read-only Ghidra calls and transient
  `python3 -B` analysis. **Those follow-up scripts/results were not archived**;
  this document records the findings and reproduction footholds, not a complete
  independently executable evidence package. Reproduce and save them in the
  isolated project rather than assuming phase2 already contains them.

## Shader banks and reflection: resolved for the selected pair

| File | Bytes | SHA-256 |
|---|---:|---|
| `data/Shaders/Shaders.sbk` | 731123 | `60d1e468c909c1403758f54ee1df50de500c28604ade01acf174a79106621fa7` |
| `data/Shaders/ShadersRelease.sbk` | 653060 | `c2732102a0aedb8c7a950e5781fa15092a3c12c2721380944c4e717dade5e736` |

Both contain 1,317 engine entries mapping to 588 unique slots (137 VS, 451 PS),
in 147 groups of four. All 588 containers have nonzero constant-table offsets.
Exact microcode comparison against the phase2 Release XESH copy matched all
156 retained PS but initially only 6/82 VS. Do not confuse engine IDs, slot
indices, container hashes and microcode hashes.

Loader chain: `0x82B7CD18` validates the bank header/version, then calls
`0x82B7C488`, `0x82B7C5A8`, `0x82B7C858`. Entry reader `0x82B8A758` handles
debug names versus release IDs, stage and slot; lazy loader `0x82B7E050` selects
slot/4 and slot%4, decompresses through `0x82A1D440`, and calls `0x82B8AA48`.

Parsing footholds (validate against source, do not blindly trust input):

- 14-byte `ShaderBankFile` header; BE32 version 3; one debug-name flag byte.
- At offset 19: count of parameter groups. Each has nine BE32 fields, a BE32
  count and that many NUL-terminated names followed by a BE32 value.
- Next: BE32 constant-name count and NUL-terminated names; BE32 engine-entry
  count, BE32 auxiliary value; entries contain name (debug) or BE32 ID (release),
  one stage byte (2 VS / 1 PS), BE32 slot.
- Next: group count, BE32 uncompressed-size array, BE32 compressed-offset array,
  BE32 blob length, compressed blob. Blob offsets are `0x17ACE` / `0x90E1`.
- Each group has a BE32 compressed length plus zlib data. A bounded
  `decompressobj` produced exactly the declared output size; streams had no EOF
  marker (flush-style ending). `zlib.decompress` rejected them as incomplete.
  Do not accept arbitrary truncation: verify compressed spans, output size,
  unconsumed/unused data and every downstream bound.
- Decompressed group starts with a base offset and four relative record offsets.
  Each record has two BE32-counted arrays of 16-byte entries, four 64-bit masks,
  BE32 container length, then the container.
- Container flags match `0x102A1100` masked by `0xFFFFFF00`; virtual+physical
  sizes matched container length. Shader bytes are at
  `container + virtualSize + shader.physicalOffset`, of `shader.size` bytes.
  Pinned converter layouts are archived in `phase2/converter/shader.h` and
  `constant_table.h`; this is layout study, not a converter execution result.

| Engine ID | Debug name | Slot | Raw microcode XXH3 |
|---|---|---:|---|
| `0x9D` | `VSHADER_TEXTUREDQUAD` | 71 | `1b1de457ca90ee5f` |
| `0x9E` | `PSHADER_TEXTUREDQUAD` | 132 | `06547412e4b72251` |

Both selected containers are 240 bytes, identical between the two banks.
Container SHA-256s: VS
`9b9b88247c7a16efa576338ddae5b228be395dd3520651fec2d4a5930e5c5706`,
PS `36b9e36dff56e3ec71f171b5c4a5286461b5de3c45a22413122b639e3a6abbc6`.
PS reflects `g_TextureSampler` at sampler register 13; VS has no named constants.
Across the release bank, reflected bool indices were VS {0}, PS {0,1,4,5,6,7,8,9}.
Raw pixel CF indices ≥128 still require stage-mapping reconciliation, not a
capacity verdict. No general converter compatibility has been established.

### Vertex preparation: exact selected-cache identity established

`0x82B80500` constructs declaration `DAT_834BDC64` using
`0x82B86DF8` / `0x82B86F38`: stream 0, Float3 POSITION0 at byte offset 0,
Float2 TEXCOORD0 at byte offset 12. Quad stride is 20 bytes (five dwords).
Static packed declaration types from `DAT_83316B80` are `0x002A23B9` and
`0x002C23A5`; semantic mapping is in `DAT_83316D14`.

Shader preparation `0x8221B140` → `0x821F75E8` → `0x821D3318` patches fetch
format, stream, stride, offset and swizzle; it also combines the fetch extent
and marks a mini fetch. For this selected declaration, reconstructing the two
fetch instructions at instruction addresses 3 and 4 gives:

```
25F81000 00393A88 00000005
05F80000 40253FC8 00000305
```

Replacing only those instructions in the 96-byte bank VS produced **exact
byte equality**, not just a hash match, with cached VS `d5e12a97b3a84881`.
This closes the selected VS mapping; it is not a general shader patcher test.
Sources: generated `.203.cpp:466` (`0x821D3318`), Ghidra declaration constructors,
SDK `include/rex/graphics/format/ucode.h`.

## Selected operation contract

`0x821FC4A0` creates/binds temporary targets and draws through `0x821FA0D0` →
`0x821F27E0` → `0x8220A528` → `0x82217EE8`, then resolves to the destination mip.
It requires even base dimensions and multiple levels; it stops when either
destination dimension is <=16. Do not replace it with an unrestricted full chain.

`0x821F27E0` builds four 20-byte vertices and indices 0,1,2 / 1,3,2. Its rectangle
coordinates use `2*t-1`; the caller normalizes the destination rectangle by the
current target dimensions and supplies UV endpoints 0..1. Clip/API orientation,
viewport/pixel-center rules and output rounding still need comparison tests.

Selected PS texture instruction words: `10D80001 1F1FF688 00004000`.
SDK ucode decoding: sampler 13, normalized coordinates, computed LOD, no register
LOD/gradients, zero instruction LOD bias/offsets; filtering/aniso from fetch state.

Previously recorded fetch state, **not a fresh capture**:

```
84004802 183E0086 003FE1FF 00A81414 003E00C3 184E0A18
```

Decoded with SDK `xenos.h:1175–1267`: 512×512, pitch512, tiled, format6
`k_8_8_8_8`, unsigned/non-gamma, endian `k8in32`, base `0x183E0000`,
swizzle B,G,R,1, exponent0, clamp-to-edge X/Y, linear mag/min/mip, aniso disabled,
mip clamp0..3, bias−1, 2D, packed-mips flag, mip base `0x184E0000`.
At exact 2:1 reduction, computed LOD plus bias−1 is consistent with selecting
the preceding level; validate this against actual replay state rather than
assuming every mip operation shares it. Preserve swizzle/endian independently.

## Reference caveat — do not use the buggy zeros as golden output

`renderdoc_captures/mip-generation.rdc` still exists. The prior investigation
documents event3027's mip draw and resolves3040/3088/3136 to `0x184E0000`,
`0x18520000`, `0x18530000`. Capture contains an exponent-extraction bug:
fetch word4 was used instead of word3. Prior controlled shader replacement
populated all three output levels. See
`linux-vulkan-realtime-morph-investigation.md:242–303`.

The previously documented temporary paths
`/tmp/opencode/fable2-mip-generation-20261024-191257` and
`/tmp/opencode/fable2-renderdoc-analysis/generation` were absent in this session.
Do not claim those exports/scripts are available. Re-establish a corrected
reference offline from preserved capture/state with recorded hashes and tooling
versions. If that cannot be done safely, report the blocker rather than declaring
the prototype correct from screenshots or synthetic tests alone.

## In-game gates remain open (not first-prototype scope)

- Resolve `0x822069C0` (`generated .199.cpp:871–2995`) decodes destination/mip
  layout through `0x821FA590`, writes device+`0x2A18–0x2A24` copy registers,
  emits its own `DRAW_INDX_2`, updates resource usage fields and can append a
  tagged record through device+`0x34D4`. State changes precede allocation failure;
  falling back after entering it cannot assume unchanged state.
- Backend: `IssueCopy` → `VulkanRenderTargetCache::Resolve` → shared-memory or
  scaled-resolve writes → `TextureCache::MarkRangeAsResolved` →
  `SharedMemory::RangeWrittenByGpu`. Last step invalidates dependent textures and
  marks GPU memory valid; it is **not** guest CPU readback/completion.
  Sources: SDK Vulkan render_target_cache.cpp1128–1316, pipeline/texture/cache.cpp
  321–349, shared_memory.cpp257–273; separate resolve readback in Vulkan
  command_processor.cpp4488–4805. No readback-policy changes authorized.
- More verified draws: `0x822060A0` temporary vertices, `0x8221C9C8` chunked
  advancing-vertex draws, `0x8221E0F0` indexed/topology chunking, `0x82207C30`
  additional indexed state, `0x821EF988` rectangle `DRAW_INDX_2`.
  `0x82B9C068` builds stored draw packets. `0x82286248` publishes indirect-buffer
  packets into the ring, called via `0x82286430` and other paths. The original
  two draw hooks are not universal. Literal scans miss computed headers.

These do not block a private-image offline test; they do block treating that
test as permission for an in-game native bypass.
