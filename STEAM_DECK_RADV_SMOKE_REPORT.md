# Steam Deck RADV smoke-test preparation report

## Status

The test source and scripts are committed on `feature/native-vulkan-renderer`.
They were built and run locally on the CachyOS desktop, then transferred and run
once on the Steam Deck after explicit approval. The test used a dedicated home
staging directory only. No game, Distrobox, production installation, shortcut,
configuration or save was touched.

## Smoke test

`tools/deck-build/vulkan-smoke/main.cpp` dynamically loads `libvulkan.so.1`,
creates a Vulkan 1.2 instance and device, fills 16 words using `vkCmdFillBuffer`,
uses a transfer-to-host memory barrier and fence wait, then maps and verifies
readback. It also reports the relevant descriptor-indexing features and checks
sampled 1D `R8G8B8A8_UNORM` support. The feature query does not enable those
features, create a descriptor array, or validate the proposed renderer ABI.

The executable was built with Clang 23.1.3 inside the pinned Arch builder and
`-march=x86-64-v2`, then run on the desktop against each host ICD:

| Desktop device | Vulkan driver | Result | 1D sampled format | Descriptor indexing |
|---|---|---|---|---|
| NVIDIA GeForce RTX 3090 | NVIDIA 615.78.08; device API 1.4.351 | buffer fill, barrier, fence and 16-word readback passed | supported; linear filtering supported | runtime arrays, variable count, partially bound, sampled-image nonuniform indexing and update-after-bind all reported true |
| Intel UHD Graphics 630 | Mesa 26.2.4; device API 1.4.354 | buffer fill, barrier, fence and 16-word readback passed | supported; linear filtering supported | same queried features reported true |

Desktop JSON and ELF inspection are under ignored
`out/deck-build/vulkan-smoke/{nvidia,intel}.json` and
`elf-compatibility.json`. The smoke executable requires GLIBC 2.34, GLIBCXX
3.4.32, and CXXABI 1.3.9, below the live Deck host's inspected exports. Desktop
feature results were subsequently confirmed on RADV/Deck by the approved run.

The committed packaging script generated
`out/artifacts/fable2-experimental-5ac599d5f4b7.tar.gz` with source commit
`5ac599d5f4b71bb04174c8e3a40481e274c5a269`, `source_dirty: false`, and builder
image ID `0b5bcd01baa0a4f1fb101c0132e71cc970dab68dfc87a65fe9a3a02a2b32f03e`. Its SHA-256 is
`a45490400c9251d6337a919a24c6793c4e92afe0c596e6190ae0239e7a7f25c4`; the
relative-path sidecar verified successfully. The archive contains only the
smoke executable and metadata, not the full repository or a game build.

## Deck ABI finding: existing Fable executable

Read-only inspection of the existing, ignored
`Fable-2-Recomp/out/build/linux-amd64-release/fable_2` found an undefined dynamic
symbol requiring `GLIBCXX_3.4.36`:

```text
_ZNKSt16basic_format_argISt20basic_format_contextINSt8__format10_Sink_iterIcEEcEE22_M_handle_unrecognizedEv@GLIBCXX_3.4.36
std::basic_format_arg<...>::_M_handle_unrecognized() const
```

That executable's SHA-256 is
`0c0aee424e59e1f66a8faac9a65b68d07d652fd876f9eeaaaef5d5b8893af5ce`. The
companion `librexgpu-xenos.so` reaches only GLIBCXX 3.4.32 (SHA-256
`0d11af8cc78766170d3bd1179d36356860687ec537a32837dac01607791881ea`). The
SteamOS host's actual `libstdc++.so.6.0.35` exports through GLIBCXX 3.4.35 and
CXXABI 1.3.17. Therefore the inspected `fable_2` **cannot run directly against
that Deck host libstdc++** as built: the 3.4.36 symbol version is absent. The
symbol is from libstdc++'s `std::format` implementation; this identifies the ABI
dependency, not the exact source callsite. Current original-checkout sources
contain `std::format` uses, but that checkout has uncommitted work and this
binary's exact source provenance is not asserted here.

The pinned Arch base's `libstdc++.so.6.0.36` does export GLIBCXX 3.4.36, and the
Deck's stopped `fable2-build` container uses the same base-image ID/digest. That
makes container execution plausible, but the stopped container's final package
overlay and runtime resolution were not inspected by starting it. The correct
conclusion is: **the ABI probe appears directly host-compatible; the inspected
Fable executable is not; a matching-container run remains unverified.**

## RADV / descriptor-indexing status

Previous read-only Deck inspection found Vulkan loader 1.4.350.1, the Radeon ICD
manifest pointing at `/usr/lib/libvulkan_radeon.so`, and RADV on AMD Custom GPU
0932 (VANGOGH), driver 26.1.99 / Mesa 26.2.0-devel. `vulkaninfo --summary`
enumerated the GPU but logged a duplicate RenderDoc layer and an LSFG layer
initialization error; it skipped the bad layer and still enumerated RADV. This
smoke task did not query descriptor-indexing features on Deck and did not test
the stopped container's Vulkan device/ICD access.

The feature report covers core dynamic sampled-image indexing,
`runtimeDescriptorArray`, variable descriptor counts, partially-bound bindings,
sampled-image nonuniform indexing, and update-after-bind properties. Whether the
renderer needs update-after-bind/partially-bound depends on its eventual heap
population/update strategy; the smoke test reports support rather than assuming
all are mandatory. The smoke test only queried these features; it did not enable
them in a device, create the proposed descriptor heap, compile a shader, draw, or
sample a 1D texture.

## Package and approval-gated Deck procedure

`tools/deck-build/package.sh` packaged only `vulkan_smoke` and its manifest. The
archive is `out/artifacts/fable2-experimental-5ac599d5f4b7.tar.gz`, SHA-256
`a45490400c9251d6337a919a24c6793c4e92afe0c596e6190ae0239e7a7f25c4`. The Deck
staging copy passed `sha256sum -c`; the embedded manifest and ELF metadata/hash
were checked before execution. It was extracted under
`~/vulkan-smoke-stage/approved-run/5ac599d5f4b7/`.

Executed once with the Deck RADV ICD forced and implicit layers disabled:

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json
VK_LOADER_LAYERS_DISABLE=~implicit~
```

The Deck returned exit status **0** and JSON `result: pass`; stderr was empty.
Device/runtime evidence:

| Item | Deck result |
|---|---|
| Loader/device API | 1.4.350 / 1.4.350 |
| Device/driver | AMD Custom GPU 0932 (RADV VANGOGH), RADV; Mesa 26.2.0-devel (`git-5c65d4fc31`) |
| GPU operation/sync/readback | `vkCmdFillBuffer`; transfer-to-host barrier + fence; all 16 words verified in host-coherent memory |
| Sampled 1D image | `R8G8B8A8_UNORM` sampled 1D image supported; sampled format and linear filtering supported |
| Descriptor indexing | extension advertised; dynamic indexing, descriptor indexing, runtime array, variable count, partially bound, nonuniform sampled-image indexing, sampled-image update-after-bind and update-unused-while-pending all true |
| Update-after-bind sampled-image limits | 8,388,606 per stage and per descriptor set |

Returned evidence is under ignored `out/deck-build/vulkan-smoke/deck/`:

- `smoke.json` SHA-256 `0c3b13e40bf6e2641957e9e77696eeb2653cfac95de5c771f0b4fbdad7872c96`
- `smoke.stderr` SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` (empty)
- `exit-status.txt` SHA-256 `9a271f2a916b0b6ee6cecb2426f0b3206ef074578be55d9bc94f6f3fe3ab86aa` (contents `0`)

The exact staging/verification/run commands used were:

```sh
ssh deck@192.168.1.54 'mkdir -p ~/vulkan-smoke-stage'
scp out/artifacts/fable2-experimental-5ac599d5f4b7.tar.gz{,.sha256} \
  deck@192.168.1.54:~/vulkan-smoke-stage/
ssh deck@192.168.1.54 'cd ~/vulkan-smoke-stage && \
  sha256sum -c fable2-experimental-5ac599d5f4b7.tar.gz.sha256 && \
  tar -tzf fable2-experimental-5ac599d5f4b7.tar.gz && \
  mkdir -p approved-run && \
  tar -xzf fable2-experimental-5ac599d5f4b7.tar.gz -C approved-run && \
  readelf -h -l -d --version-info approved-run/5ac599d5f4b7/bin/vulkan_smoke'
```

After ELF verification, the approved one-time execution and evidence return were:

```sh
ssh deck@192.168.1.54 'cd ~/vulkan-smoke-stage/approved-run/5ac599d5f4b7/bin && \
  ./vulkan_smoke > ../smoke.json 2> ../smoke.stderr && \
  sha256sum vulkan_smoke ../smoke.json ../smoke.stderr'
scp deck@192.168.1.54:~/vulkan-smoke-stage/approved-run/5ac599d5f4b7/smoke.{json,stderr} \
  out/deck-build/vulkan-smoke/deck/
```

The Distrobox remained stopped. The executable only creates a Vulkan
instance/device and performs a small buffer fill/readback; it does not open a
window or launch Fable 2.

## Blockers and next step

- The smoke path is PASS on Deck RADV, but only for instance/device creation,
  capability queries and one transfer/readback operation. It does not prove
  graphics pipeline, descriptor-layout, shader sampling, WSI or frame behavior.
- The existing `fable_2` output's GLIBCXX 3.4.36 requirement blocks direct host
  execution; rebuilding it against an older compatible ABI or running it in a
  verified matching container requires separate scope and source provenance.
- The next smallest validation is a separate minimal graphics-pipeline/1D-image
  descriptor test on RADV, after the runtime binding contract is specified. No
  broader renderer or game validation is implied.
