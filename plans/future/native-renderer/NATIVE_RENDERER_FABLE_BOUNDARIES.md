# Fable native renderer — resource boundaries and shader requirements

2026-10-26, second bounded read-only pass. Continues
[NATIVE_RENDERER_SOURCE_AUDIT.md](NATIVE_RENDERER_SOURCE_AUDIT.md).

Later follow-up: [NATIVE_RENDERER_OFFLINE_PROTOTYPE.md](NATIVE_RENDERER_OFFLINE_PROTOTYPE.md)
resolves the selected shader containers/declaration rewrite, expands resolve and
command-buffer mapping, and records approval for a private-image offline test.
The unresolved statements below describe this earlier audit checkpoint.

## Outcome

There are identifiable Fable device, resource and draw boundaries worth adapting.
However, **a native renderer must replace resource lifetime and state semantics,
not merely translate draw calls**. The texture/mip candidate is now traced into a
second draw emitter; hooking only the previously found emitter misses it.

The cached shader corpus provides concrete compatibility tests. Sonic's converter
is useful reference code but is not currently established as a usable Fable tool.
No prototype build, game launch or runtime performance result in this pass.

## Evidence and scope

Same app/SDK baselines as the initial audit. Ghidra program SHA-256 again confirmed
`88c4ef2e18e65409444d1b068eff921d1f7e180a5ae64edc64ba6b0872372662`.
No Ghidra mutation. Original guest instructions/generated source checked where
decompilation dropped arguments or mishandled reservation-based atomics.

Evidence beneath `out/performance/native-renderer-feasibility-20261026/`:

- `phase2_inventory.py`: source-slice and read-only cache inventory helper.
- `phase2/function-index.json`, `function-index-supplement.json`: 31 function
  slices, generated partition/line references, source and slice hashes. Direct
  caller counts are static generated `bl` comments, not indirect/runtime coverage.
- `phase2/shader-cache-{0,1,2}.xsh`: immutable copies of Release, Debug and prior
  Deck evidence cache; their origins and hashes are in `shader-inventory.json`.
- `survey_shader_features.py`, `phase2/shader-feature-survey.json`: conservative
  static encoded-feature census, with explicit scope and sanity tests.
- `phase2/converter/`: five converter files fetched at Unleashed's exact pinned
  XenosRecomp gitlink, with a separate URL/hash manifest. No checkout initialized.

This is selected research evidence, **not** a backup of the gameplay installation.

## 1. Resource lifetime: guest references and GPU completion are separate

### Verified reference-count protocol

- `0x821FC048` increments a guest-endian 32-bit count at object+4 atomically.
  Generated `.179.cpp:686` onward reconciles the lwarx/stwcx loop.
- `0x821FC1F0` decrements that count; if it becomes zero, it calls `0x821F6490`.
  For header low nibble 4 plus flag `0x40000000`, it first releases a parent
  resource loaded from object+0x18. The increment routine similarly increments
  this parent on the 0→1 transition. This is a real dependent-resource edge, not
  proof of every aliasing or resource-view case.
- Disassembly `0x821FC210–0x821FC220` confirms decrement/retry. The decompiler's
  reservation pseudocode alone is not a faithful implementation specification.
- `0x821FBFC8` copies the wrapper's resource/metadata at +4/+8, releasing the old
  resource and retaining the new one; wrappers are not themselves GPU payloads.

### Destruction is not simply free(object)

`0x821F6490`, generated `.264.cpp:783` onward, dispatches on the resource header's
low nibble. Several branches read object+8 and call `0x82242628` with the device,
that value and a type/reason code **before** freeing resource storage.

`0x82242628` compares device progress-counter values and polls progress. It is
currently named `FrameLimiterWait_82242628` and overridden under `hotfunc/vblank/`,
but this destruction call chain shows why its name/directory must not be treated
as proof that it is only frame pacing. Its actual active override must be retained
in a research baseline and separately understood; no change was made here.

For type-4 resources, `0x821F6490` manages a tile allocation when an ownership flag
is set, and skips that ownership branch when the parent/dependency flag is set.
`0x821F4EF8` stores allocation size as tile_count×0x1400; SDK xenos.h describes
5120-byte EDRAM tiles. This supports an EDRAM-surface interpretation of this path,
not a complete console resource-type enumeration.

**Adapter requirements:**

1. Preserve guest reference-count/wrapper behaviour unless every observer is
   replaced. Host shared_ptr/Vulkan object ownership is not the guest ABI.
2. Track host GPU submission lifetime separately; guest release does not imply
   that a queued native command has finished using an allocation.
3. Give reused guest addresses a new host resource generation. Address-only
   caches can accidentally serve data from an old allocation.
4. Retain real progress semantics. Do not fake the guest counter, skip waits, or
   equate command-packet processing to actual host GPU completion.
5. Characterize dependent views/overlapping ranges and guest/host writes before
   implementing alias invalidation. One parent flag does not solve all aliasing.

## 2. Temporary surface creation and binding

The mip path uses a resource wrapper with a vtable at +0, resource at +4 and
metadata at +8 (observed fields, not a public stable C++ layout):

| Function | Observed role / caveat |
|---|---|
| `0x821FC7A8` | releases prior wrapper resource, calls `0x821F4D90`, stores new resource and metadata; allocation failure path exists |
| `0x821F4D90` | allocates a 0x30-byte descriptor via `0x8222CA28`; fills via `0x821F4EF8`; ownership/allocation branches depend on arguments |
| `0x821F4EF8` | initializes type nibble 4, refcount 1, dimensions/format/pitch/sample-related fields and tile byte size; exact format cases need further decoding |
| `0x821FC270` | copies/retains wrapper into global target state, calls `0x82286508` for target 0, clears other targets, updates related state, releases input wrapper reference |
| `0x82286508` | writes target table at device+0x3098+4×index; copies surface information and modifies multiple dirty/state/format paths; not a setter we may safely reduce to a single pointer store |
| `0x82A14E70` | wrapper cleanup releases resource through `0x821FC1F0`, zeros wrapper resource/metadata |

Likewise `0x821B7020` is a texture-binding boundary, not a trivial host descriptor
bind: it copies/merges six fetch words from resource+0x1C onward into the device
fetch bank, applies mip clamps, stores bound-resource pointer and records previous
resource usage for lifetime handling. It has 96 distinct direct callers in the
generated-source scan; this establishes fan-out, not complete texture coverage.

## 3. State banks and a second draw path

### Verified dirty-state emission cluster

`0x8221C518` and `0x82217EE8` both contain dirty-bank flushing before draw packets:

- Device+0/+8: masks for the two float-constant banks, passed to `0x8221DF98`
  with register bases 0x4000/0x4400 and device data at +0x780/+0x1780.
- Other masks select register spans through `0x8221C908`.
- `0x8221D2E0` emits six-word fetch groups from device+0x480 onward to 0x4800.
- Boolean emission includes register base 0x4900.

SDK `register_table.inc` independently names 0x4000/0x4400 as float constants,
0x4800 as fetch constants and 0x4900 as booleans. These are useful reconciliation
points, **not** a full device layout or proof that all writes use these setters.

### The mip draw does not use only 0x8221C518

Verified chain:

```text
0x821FC4A0  mip-level orchestration / temporary target lifecycle
  0x821FA0D0  configure source texture and blit state
    0x821F27E0  construct four vertices / six indices on guest stack
      0x8220A528  flush state, request temporary geometry storage, copy data
        0x82217EE8  dirty-state flush, geometry allocation, DMA draw emission
  0x822326B0  resolve wrapper
    0x822069C0  larger resolve/state command-emission routine
```

At generated `0x821F2B18–0x821F2B30`, the geometry wrapper passes r3=4, r4=2,
r5/r6 pointing to its local indices/vertices and r7=20 (vertex stride). The next
wrapper multiplies its primitive count by three and requests storage through
`0x82217EE8`. That lower function emits `0xC0032201` with DMA index address/size,
returns allocation pointers and stores the eventual command cursor. The wrapper
copies payloads and publishes that cursor. **Transient inputs must be copied or
pinned before guest stack/temporary memory is reused.** Do not treat argument
pointers as indefinitely valid render-thread resources.

The original draw helper `0x8221C518` has 11 direct call sites in Ghidra/source
scan, including the measured memexport producer. That count does not make it a
universal draw interception point. More variants and prebuilt command buffers
remain to be mapped.

## 4. Shader selection has engine metadata and side effects

- `0x8222C268` and `0x82208C48` cache an engine ID, select an entry at
  `DAT_834BDB8C + ID×0x70`, lazily load when needed through `0x82B7E050`, and bind
  the resulting resource via `0x822324E0` / `0x82208CE8`.
- Those binders store resources at device+0x3198 / +0x3194 respectively, update
  dirty masks/lifetime tracking, and process embedded state blocks that can copy
  or mask device state. Their shapes support vertex/pixel shader roles, but
  engine-ID→raw-microcode-hash association has **not** been captured here.
- Loader `0x82B7E050` accesses runtime tables/stream state. Static ID arithmetic
  does not identify a shader's bytes without resolving the actual loaded data.
- In the mip blit, selector IDs 0x9D and 0x9E therefore are **not shader hashes**.

Replacing these functions with native PSO binding alone would drop guest state
updates. A staged renderer needs either compatible shadow state or a complete
replacement of the affected state/consumer paths.

## 5. Cached shader inventory and conservative encoded-feature survey

Parsed XESH version 0x20201219 using current SDK storage format. Every entry's
XXH3 identity verified; source cache re-read hashes matched snapshots. No writes
to original caches. Stored microcode is guest big-endian.

| Cache snapshot | Vertex | Pixel | Total |
|---|---:|---:|---:|
| Desktop Release | 82 | 156 | 238 |
| Desktop Debug | 49 | 74 | 123 |
| Prior Deck evidence | 69 | 111 | 180 |

The union is 238 unique stage/hash pairs: these Debug/Deck snapshots are subsets
of the Release snapshot. This is retained-cache population, **not all game shaders,
currently visible shaders, or proof of identical runtime settings**.

Small Python census follows SDK CF unpacking/minimum-EXEC-address bound and checks
instruction ranges. Counts are static encoded occurrences, not reachable/executed
paths or an independent complete shader analyzer:

| Feature encoded in retained shader | Number of shaders |
|---|---:|
| Mini vertex fetch | 57 |
| Loop CF | 8 (7 vertex, 1 pixel) |
| Call/return/jump CF category | 93 (combined category, not 93 calls) |
| Memory-allocation CF and eM destination encoding | 3 |
| Addressed-constant flags | 23 (not proof of used dynamic operands) |

Conditional EXECs reference raw bool index 0 in vertex code and 128/133–137 in
pixel code. **Do not equate raw index≥16 with exceeding 16 per-stage booleans:**
guest stage/register mapping and converter reflection remapping must be reconciled.
The survey's raw ge16 count (64 pixel shaders) is metadata, not an incompatibility
verdict. No claim that register-dynamic addressing is absent.

Sanity checks pass: paired-CF unpack fixture; synthetic bool31/mini-fetch/memory
allocation/export fixture; truncated-input rejection; known producer CF bound28,
six mini-fetches and eA/eM0 destinations agree with prior SDK analyzer/disassembly;
known pixel shader bound1/color0; repeated shader bodies agree across caches.
This limited validation does not certify a new translator.

### Measured-pair implications

The memexport producer `695413a9831d88da` itself has six mini fetches. Two other
cached vertex shaders encode eM destinations (`f6c546d6c2645541`,
`bac4e55f76ef5fb5`); the latter also contains loops. These are static candidates
for future classification, not newly observed producers or identified subsystems.
Keep the prior runtime consumer/freshness limits unchanged.

## 6. Converter choices: what the pinned source adds to the README

Pinned `XenosRecomp/shader_recompiler.cpp` inspection:

- :1103–1114 asserts the original container signature and nonzero constant-table
  metadata. XESH microcode copies alone do **not** satisfy that input contract.
- :161–180 looks up vertex inputs by instruction address in reflected
  vertexElements. A native vertex-declaration solution could replace some guest
  fetch behaviour, but requires recovering/correctly constructing that mapping.
- :581–632 handles pixel colour/depth, vertex position and interpolator exports;
  no explicit eA/eM handler in this inspected dispatch. Consistent with its
  documented lack of memexport, not a measured conversion failure.
- :1254–1258 maps reflected bool indices into a shared host mask with pixel-stage
  offset16. Compare actual reflection and guest indices before making capacity
  claims. Raw Fable index128 alone proves no failure.
- :1644–1677 contains loop lowering. Therefore loops are a test requirement,
  **not evidence that all loop shaders are unsupported**. README's unimplemented
  integer constants and limited control-flow testing still need resolution.

No converter build/run or Fable HLSL conversion attempted. Potential semantics
issues in CF lowering require differential tests, not speculative patches.

The existing ReXGlue SPIR-V translator is another source of reusable knowledge,
but its output ABI includes shared-memory/EDRAM descriptor sets and system
constants (`spirv_translator.h:215,349–363`; vertex fetch loads shared memory in
`spirv_translator_fetch.cpp:126`). **Existing translated SPIR-V is not automatically
a shader for a native vertex-buffer/image renderer.** Options are adapting its
fetch/export lowering, extending an offline converter, or purpose-built shaders
for selected verified operations. Keep shader semantics separate from backend
command emulation when evaluating reuse.

## 7. Refined desktop-first next step

Do not implement an in-game mip replacement yet. It is a useful representative
operation, but depends on resource/resolve publication and a second draw path.
First specify a small Fable-native operation contract:

- complete source/destination view descriptors (format, mip, layer, ranges,
  swizzle, endian, tiling, filtering, exponent and coordinate conventions);
- ownership/generation plus transient data copy rules;
- all original guest state/refcount/dirty changes, including failure paths;
- exact ordering relative to guest packet processing and host GPU submission;
- publication into every original downstream consumer, not only the final image;
- genuine query/progress/readback behaviour and GPU-safe retirement;
- fallback before side effects, or explicitly validated restoration after them.

Avoid direct Vulkan recording from an arbitrary guest hook: execution belongs on
the appropriate host graphics thread and must retain ordering with remaining
emulated work. The exact bridge/token mechanism is an open design item, not
already implemented infrastructure.

Next read-only tasks: trace resolve `0x822069C0` and texture view/layout helpers
into a narrow descriptor/publication map; examine draw/command-buffer variants;
resolve whether representative shader containers/reflection exist in local data.
Only then select the offline representative operation and implementation workspace.
No user action is currently required. No branch/build/source changes or Deck work.
