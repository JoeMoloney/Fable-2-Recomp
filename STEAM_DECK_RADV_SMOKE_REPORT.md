# Steam Deck RADV smoke-test preparation report

## Status

The test source and scripts are committed on `feature/native-vulkan-renderer`.
They were built and run locally on the CachyOS desktop. A checksum-verified
package is prepared under ignored `out/artifacts/`, but **nothing has been
transferred to or executed on the Steam Deck**. Do not copy or execute it until
the user explicitly approves.

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
feature results do **not** establish the same features on RADV/Deck.

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
all are mandatory.

## Package and approval-gated Deck procedure

After this source is committed, `tools/deck-build/package.sh` will package only
`out/deck-build/vulkan-smoke/vulkan_smoke`. Its manifest records the source
commit, builder identity, ELF requirements/dependencies and binary hash. The
archive and relative-path `.sha256` sidecar remain under ignored `out/artifacts/`.

**Not performed:** no SCP/SSH file transfer, extraction, execution, Podman image
load/start, game launch, shortcut/config/save access, or production-file change.

After explicit user approval, run these commands from the desktop worktree. They
stage only under the Deck user's home and do not target Steam/game paths:

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

Only after reviewing the checksum, manifest and ELF output—and with execution
approved—run the smoke executable once and return its evidence:

```sh
ssh deck@192.168.1.54 'cd ~/vulkan-smoke-stage/approved-run/5ac599d5f4b7/bin && \
  ./vulkan_smoke > ../smoke.json 2> ../smoke.stderr && \
  sha256sum vulkan_smoke ../smoke.json ../smoke.stderr'
scp deck@192.168.1.54:~/vulkan-smoke-stage/approved-run/5ac599d5f4b7/smoke.{json,stderr} \
  out/deck-build/vulkan-smoke/deck/
```

Do not start or alter the existing Distrobox or installed game files. The
executable only creates a Vulkan instance/device and performs a small buffer
fill/readback; it does not open a window or launch Fable 2.

## Blockers and next step

- Deck RADV feature values and an actual GPU fill/readback have not been tested;
  execution is explicitly approval-gated.
- The existing `fable_2` output's GLIBCXX 3.4.36 requirement blocks direct host
  execution; rebuilding it against an older compatible ABI or running it in a
  verified matching container requires separate scope and source provenance.
- The next smallest validation is the approved isolated staging-and-smoke run
  described above. No broader renderer or game validation is implied.
