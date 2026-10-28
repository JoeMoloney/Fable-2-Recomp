# Fable II shader catalogue PoC

## Result

Generated a deterministic catalogue for all **588** shader-bank slots (137 VS, 451 PS), hash joins to the retained runtime draw-identity capture and three saved XESH shader caches, and the frequency of every observed runtime VS/PS hash pair. No game, build, deployment or RenderDoc operation was run. All analysis ran on the desktop; the already-retained Deck JSONL was copied read-only to this research directory and its SHA-256 verified.

## Inputs and provenance

- Debug bank: `/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/data/Shaders/Shaders.sbk`, SHA-256 `60d1e468c909c1403758f54ee1df50de500c28604ade01acf174a79106621fa7`.
- Release bank: `/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/data/Shaders/ShadersRelease.sbk`, SHA-256 `c2732102a0aedb8c7a950e5781fa15092a3c12c2721380944c4e717dade5e736`.
- Existing conversion survey: `../native_reference/corpus-1/results/survey.json`; parser `../native_offline/bank.py`. Existing survey attempted every slot and reports 569 HLSL emissions and 154 validated SPIR-V outputs.
- Existing runtime XESH inventories: `/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/out/performance/native-renderer-feasibility-20261026/phase2/shader-inventory.json`, with source caches Release, Debug, and retained Deck cache. The catalogue reparses XESH and rechecks every bytecode XXH3.
- Runtime draw input copied to `input/draws-20261027T195223Z.jsonl` from `/home/deck/Documents/fable2-native-shadow-live-draw-20261027T182808Z/capture/draws-20261027T195223Z.jsonl`. SHA-256 `a644e1c24cb93e72f95dd85b28ba0e96097f5414e145b8e1f5d15cedae8eabcf`. Header says 2,048 records, one dropped record at capacity, 3-second requested window. All records are frame 13098/submission 22983. This is a single-frame, truncated sample, not whole-game usage frequency.

## Match results and the 4/31 VS discrepancy

Hashing is now directly comparable: the game records use 64-bit XXH3 over guest microcode; the bank and XESH identities were independently recomputed/verified as XXH3-64 over their exact microcode bytes.

| Stage | Unique non-zero runtime hashes | Exact raw bank matches | Exact XESH cache matches | Additional same-size fetch-patch parent candidates | Unresolved parent candidates |
|---|---:|---:|---:|---:|---:|
| Vertex | 31 | 4 | 31 | 21 | 6 |
| Pixel | 49 | 49 | 49 | — | 0 |

Thus **4/31 is specifically a raw-bank-byte comparison, not a missing runtime-cache coverage result**: all 31 observed VS hashes exist in the XESH caches. For 21 of the remaining 27 VS hashes, the cached bytecode has exactly the same length as a bank VS and every differing dword falls inside a 3-dword instruction whose bank opcode bits identify vertex-fetch. This is strong evidence that per-draw vertex-fetch declaration patching changes the VS identity before it is cached/recorded. The SDK’s documented fetch patch path and an independently verified example are in `/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/plans/NATIVE_RENDERER_OFFLINE_PROTOTYPE.md:142-154`: patching VS71 fetch instructions produces the exact distinct XESH hash `d5e12a97b3a84881`. Six VS hashes are present in XESH but do not have a same-size bank parent under this conservative fetch-only comparison; these remain unresolved (not presumed missing or corrupt).

All 49 non-zero PS hashes match bank microcode exactly. This stage asymmetry fits vertex-fetch patching: it changes vertex declarations/fetch instructions, not ordinary pixel shader code. It explains most of the apparent 4/31 VS mismatch without pretending the six remaining cases are solved. Several exact hashes map to multiple named bank entries because the banks reuse identical microcode; the catalogue retains all matching slots/names.

The 21-parent result is a structural comparison, not a general-purpose reverse-patcher. Its opcode test is conservative but not a formal proof of which bank permutation generated every cache item. Parent slots may carry multiple selector names and, in one case, multiple same-length candidate slots.

## Observed pair frequencies

There are **67 distinct hash pairs** including zero-PS entries (62 with non-zero PS); 297 draws have PS hash zero. Top observed pairs:

| Draws | Runtime VS → PS hashes | Bank-family interpretation |
|---:|---|---|
| 851 | `ecd66a10092e6562` → `8d900846800943c8` | VS fetch-patch candidate slot 245, standard material AMB1/NRM1; PS exact slot 375, standard material BLT1/Glt1/Mist0/AMB*/NRM1/SCR1 |
| 193 | `87d4404fb36af71d` → zero | VS candidate slot 151, occlusion AABB query; no pixel shader hash recorded |
| 104 | `ecd66a10092e6562` → `648b965c200a5113` | Same VS candidate 245; PS exact slot 372, standard material BLT2 |
| 74 | `79eac49585797037` → `8d900846800943c8` | VS candidate slot 249, standard material AMB2/NRM1; PS slot 375 |
| 64 | `79eac49585797037` → `401a01fd4e5f8757` | Same VS candidate 249; PS exact slot 343, standard material BLT1/AMB*/NRM1/SP1 |
| 49 | `ecd66a10092e6562` → `789266c3e42d7e59` | VS candidate 245; PS exact slot 373, standard material BLT4 |
| 46 | `fb68a7f2301210e1` → `af1e16ee57e4bb06` | Exact bank VS slot 174 landscape material; exact PS slot 186 |
| 43 | `d4d558da6a82bdc8` → `8d900846800943c8` | VS candidate slot 252, skinned standard material; PS slot 375 |

The pair rows are the runtime observations, not asserted bank permutations: candidate family associations are marked explicitly in JSON/CSV. Pair frequency is within the captured frame only.

## Existing SPIR-V availability for observed pairs

- **Zero** observed pairs have exact raw-bank matches for both stages with validated bank-survey SPIR-V. The four exact-bank VS slots observed here (15, 126, 174, 176) did not compile in the retained survey.
- **Eight distinct observed pairs (22 draw records)** have validated SPIR-V for both *candidate bank-parent* slots. They are low-frequency utility/pass-through families: VS candidates 18, 20, 71/108, 113 and 154 paired with PS slots 25, 117, 119, 135, 147, 155 or 114/116. Treat these as “candidate bank pair is already compiled,” not proof the patched runtime variant has identical translated SPIR-V.
- The highest-value standard-material pairs are **not** ready as matched SPIR-V pairs. Their VS candidates 245 and 249 have validated SPIR-V, but PS slots 343, 372, 373, 375 do not. For example, `results/logs/343-ps-dxc.log` fails on undeclared `g_Extinction_Texture1DDescriptorIndex` and `g_InScattering_Texture1DDescriptorIndex`; these are known 1D binding gaps, not evidence that the runtime pair was mismatched. The existing converter survey documents the broader family of missing 1D descriptor declarations.

## Recommendation

Prioritize the **ordinary standard-material family** for native reconstruction: it dominates this capture’s observed pairs, has clear bank-family candidates, and the VS side already compiles. First unblock the relevant PS 1D texture descriptor/sampler ABI and verify the exact runtime-to-bank permutation/resource contract before drawing. Keep the eight already-convertible utility pairs as fallback, narrowly testable examples, not representative scene materials. Do not select a shader by bank name alone or begin renderer integration from this catalogue.

## Generated files

- `shader_catalogue.json`: all 588 slots, names, release IDs, hashes, reflection, HLSL/SPIR-V status and observed runtime hashes.
- `runtime_shader_matches.json`: per-stage runtime hashes/frequencies, exact bank/cache matches and VS parent-candidate analysis; includes validated XSH source hashes and inventory.
- `runtime_shader_pairs.json` and `.csv`: all 67 observed VS/PS hash pairs, sorted by frequency, with bank-match confidence and pair-level SPIR-V availability.
- `catalogue_manifest.json`: input/output SHA-256s and summary counts.
- `build_catalogue.py`: deterministic desktop-only generator. Re-run with `python research/native_shader_catalogue/build_catalogue.py`; requires Python `xxhash`.

Original `.sbk` and `.xsh` data were read only. The added input copy and generated artifacts stay under `research/native_shader_catalogue/`; no renderer, game source, Deck installation, or production installation was modified.
