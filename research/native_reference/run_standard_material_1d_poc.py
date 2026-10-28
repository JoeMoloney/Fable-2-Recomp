#!/usr/bin/env python3
"""Compile the retained standard-material pair and focused regression fixtures."""
from __future__ import annotations

import collections
import hashlib
import importlib.util
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

import xxhash


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GAME = Path("/mnt/HDD/git_stuff/my_git/Fable-2-Recomp")
OUT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else HERE / "standard-material-1d-poc-v2"
RELEASE_BANK = GAME / "data/Shaders/ShadersRelease.sbk"
DEBUG_BANK = GAME / "data/Shaders/Shaders.sbk"
DRAW_RECORDS = ROOT / "research/native_shader_catalogue/input/draws-20261027T195223Z.jsonl"
SURVEY_PATH = HERE / "corpus-1/results/survey.json"
DXC_ROOT = HERE / "tools/dxc-1.9.2609/root"
EMITTER = HERE / "build-standard-material-1d/emit_hlsl"
COMMON = HERE / "upstream/xenosrecomp/XenosRecomp/shader_common.h"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


bank = load_module("poc_bank", ROOT / "research/native_offline/bank.py")
sys.path.insert(0, str(HERE))
survey_corpus = load_module("poc_survey_corpus", HERE / "survey_corpus.py")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run(command: list[str | Path], env: dict[str, str], log: Path) -> int:
    result = subprocess.run([str(item) for item in command], capture_output=True, env=env)
    log.write_bytes(result.stdout + result.stderr)
    return result.returncode


def spirv_descriptor_bindings(path: Path) -> list[dict]:
    assembly = subprocess.check_output(["spirv-dis", "--raw-id", path], text=True)
    descriptors: dict[str, dict[str, int]] = collections.defaultdict(dict)
    for line in assembly.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[0] == "OpDecorate" and parts[2] in ("DescriptorSet", "Binding"):
            descriptors[parts[1]][parts[2]] = int(parts[3])
    rows = [{"id": identifier, **decorations} for identifier, decorations in descriptors.items()
            if "DescriptorSet" in decorations and "Binding" in decorations]
    pairs = [(row["DescriptorSet"], row["Binding"]) for row in rows]
    if len(pairs) != len(set(pairs)):
        duplicates = [pair for pair, count in collections.Counter(pairs).items() if count > 1]
        raise ValueError(f"Duplicate SPIR-V descriptor set/binding assignments: {duplicates}")
    return rows


def get_container_from_code(container: bytes, new_code: bytes) -> bytes:
    parsed = bank.container(container)
    virtual_size = parsed["virtual_size"]
    shader_offset = parsed["shader_offset"]
    reader = bank.Reader(container, shader_offset)
    physical_offset, code_size = reader.u32(), reader.u32()
    if code_size != len(new_code):
        raise ValueError("Runtime VS and parent bank shader have different lengths")
    code_start = virtual_size + physical_offset
    if code_start < virtual_size or code_start + code_size > len(container):
        raise ValueError("Microcode span outside shader container")
    result = bytearray(container)
    result[code_start:code_start + code_size] = new_code
    return bytes(result)


def load_xsh_shader(identity: str, stage: str) -> tuple[bytes, list[str]]:
    inventory = json.loads((GAME / "out/performance/native-renderer-feasibility-20261026/phase2/shader-inventory.json").read_text())
    found, sources = None, []
    for cache in inventory["caches"]:
        raw = (GAME / cache["source"]).read_bytes()
        if sha(raw) != cache["sha256"]:
            raise ValueError(f"XSH cache changed: {cache['source']}")
        for entry in cache["entries"]:
            if entry["hash"] != identity or entry["stage"] != stage:
                continue
            code = raw[entry["offset"] + 12:entry["offset"] + 12 + entry["dwords"] * 4]
            if xxhash.xxh3_64_intdigest(code) != int(identity, 16):
                raise ValueError(f"Cached bytecode identity mismatch: {identity}")
            if found is not None and found != code:
                raise ValueError(f"XSH copies disagree for {identity}")
            found = code
            sources.append(cache["source"])
    if found is None:
        raise ValueError(f"Runtime shader not found in retained XSH caches: {identity}")
    return found, sorted(set(sources))


def main() -> None:
    if OUT.exists():
        raise SystemExit(f"Refusing to reuse existing output directory: {OUT}")
    if not EMITTER.is_file() or not DXC_ROOT.joinpath("bin/dxc").is_file():
        raise SystemExit("Build the isolated native_reference converter target first")
    OUT.mkdir()
    for subdir in ("containers", "hlsl", "spirv", "logs"):
        (OUT / subdir).mkdir()

    release_raw = RELEASE_BANK.read_bytes()
    debug_raw = DEBUG_BANK.read_bytes()
    release, debug = bank.parse(release_raw), bank.parse(debug_raw)
    containers = dict(survey_corpus.containers(release_raw, release))
    if sha(containers[249]) != next(r["container_sha256"] for r in json.loads(SURVEY_PATH.read_text())["shaders"] if r["slot"] == 249):
        raise ValueError("VS249 source container identity changed")
    if sha(containers[343]) != next(r["container_sha256"] for r in json.loads(SURVEY_PATH.read_text())["shaders"] if r["slot"] == 343):
        raise ValueError("PS343 source container identity changed")

    runtime_vs, runtime_vs_sources = load_xsh_shader("79eac49585797037", "vertex")
    runtime_ps, runtime_ps_sources = load_xsh_shader("401a01fd4e5f8757", "pixel")
    bank_vs = bytes.fromhex(release["slots"][249]["microcode_hex"])
    if len(runtime_vs) != len(bank_vs):
        raise ValueError("Runtime VS length does not match bank slot 249")
    bank_words = struct.unpack(">" + "I" * (len(bank_vs) // 4), bank_vs)
    runtime_words = struct.unpack(">" + "I" * (len(runtime_vs) // 4), runtime_vs)
    differing_words = [i for i, (old, new) in enumerate(zip(bank_words, runtime_words)) if old != new]
    changed_instructions = sorted({i // 3 for i in differing_words})
    if not changed_instructions or not all((bank_words[i * 3] & 0x1F) == 0 for i in changed_instructions):
        raise ValueError("VS delta is not limited to source vertex-fetch instructions")
    parent_candidates = []
    for slot, shader in release["slots"].items():
        if shader["stage"] != 2:
            continue
        candidate_code = bytes.fromhex(shader["microcode_hex"])
        if len(candidate_code) != len(runtime_vs):
            continue
        candidate_words = struct.unpack(">" + "I" * (len(candidate_code) // 4), candidate_code)
        differing = [i for i, (old, new) in enumerate(zip(candidate_words, runtime_words)) if old != new]
        changed = sorted({i // 3 for i in differing})
        if all((candidate_words[i * 3] & 0x1F) == 0 for i in changed):
            parent_candidates.append({
                "slot": slot,
                "names": [entry["name_or_id"] for entry in debug["entries"]
                          if entry["slot"] == slot and isinstance(entry["name_or_id"], str)],
                "differing_dword_indices": differing,
                "changed_vertex_fetch_instruction_indices": changed,
            })
    parent_candidates.sort(key=lambda item: item["slot"])
    if [item["slot"] for item in parent_candidates] != [249]:
        raise ValueError(f"Expected unique VS249 raw parent, found {parent_candidates}")
    nonfetch_diffs = [i for i in differing_words if i // 3 not in set(changed_instructions)]
    if nonfetch_diffs:
        raise ValueError(f"Runtime VS differs outside fetch instructions: {nonfetch_diffs}")

    patched_vs = get_container_from_code(containers[249], runtime_vs)
    containers_to_compile: dict[str, tuple[bytes, dict]] = {
        "runtime-vs-79eac49585797037": (patched_vs, {
            "role": "exact runtime XSH bytecode inserted into its verified bank-249 parent container",
            "stage": "vs", "runtime_hash": "79eac49585797037",
            "bank_parent_slot": 249, "cache_sources": runtime_vs_sources,
        }),
        "runtime-ps-401a01fd4e5f8757": (containers[343], {
            "role": "exact raw bank container; runtime identity equals bank-343 microcode",
            "stage": "ps", "runtime_hash": "401a01fd4e5f8757",
            "bank_parent_slot": 343, "cache_sources": runtime_ps_sources,
        }),
    }
    # Existing compile/validate fixtures are re-emitted through the corrected
    # isolated converter to guard the legacy 2D/3D/Cube/sampler binding layout.
    regression_slots = [(20, "vs"), (86, "ps"), (89, "vs"), (90, "ps"),
                        (71, "vs"), (132, "ps")]
    # Additional extant 1D-fetch shaders exercise the new path across uses.
    one_d_slots = [(10, "ps"), (294, "ps"), (506, "ps")]
    for slot, stage in regression_slots + one_d_slots:
        containers_to_compile[f"bank-{slot:03d}-{stage}"] = (containers[slot], {
            "role": "existing immutable survey fixture",
            "stage": stage, "bank_parent_slot": slot,
        })

    tool_manifest = json.loads((DXC_ROOT.parent / "manifest.json").read_text())
    for path in ("bin/dxc", "lib/libdxcompiler.so"):
        if sha((DXC_ROOT / path).read_bytes()) != tool_manifest["files"]["root/" + path]:
            raise ValueError("Pinned DXC file hash mismatch")
    env = os.environ.copy()
    env["LD_LIBRARY_PATH"] = str(DXC_ROOT / "lib")

    results = []
    for name, (container, metadata) in containers_to_compile.items():
        container_path = OUT / "containers" / f"{name}.bin"
        hlsl_path = OUT / "hlsl" / f"{name}.hlsl"
        spirv_path = OUT / "spirv" / f"{name}.spv"
        container_path.write_bytes(container)
        emit_exit = run([EMITTER, container_path, COMMON, hlsl_path], env,
                        OUT / "logs" / f"{name}-emit.log")
        result = {**metadata, "container_sha256": sha(container), "emit_exit": emit_exit}
        if emit_exit == 0:
            hlsl = hlsl_path.read_bytes()
            result["hlsl_sha256"] = sha(hlsl)
            compile_exit = run([
                DXC_ROOT / "bin/dxc", hlsl_path, "-E", "main", "-T", metadata["stage"] + "_6_0",
                "-HV", "2021", "-all-resources-bound", "-spirv", "-fvk-use-dx-layout",
                "-fspv-target-env=vulkan1.2", "-Qstrip_debug", "-Fo", spirv_path,
                *(["-fvk-invert-y"] if metadata["stage"] == "vs" else []),
            ], env, OUT / "logs" / f"{name}-dxc.log")
            result["compile_exit"] = compile_exit
            if compile_exit == 0:
                spirv = spirv_path.read_bytes()
                result["spirv_sha256"] = sha(spirv)
                result["spirv_bytes"] = len(spirv)
                result["spirv_descriptor_bindings"] = spirv_descriptor_bindings(spirv_path)
                result["validate_exit"] = run([
                    "spirv-val", "--target-env", "vulkan1.2", "--scalar-block-layout", spirv_path,
                ], env, OUT / "logs" / f"{name}-validate.log")
        results.append(result)

    lines = DRAW_RECORDS.read_text().splitlines()
    records = [json.loads(line) for line in lines if '"type":"draw"' in line]
    pair_records = [r for r in records if r["vs_hash"] == int("79eac49585797037", 16)
                    and r["ps_hash"] == int("401a01fd4e5f8757", 16)]
    fetch_signatures = collections.Counter(json.dumps(r["vertex_fetches"], sort_keys=True)
                                           for r in pair_records)
    texture_signatures = collections.Counter(json.dumps(r["texture_fetches"], sort_keys=True)
                                             for r in pair_records)
    report = {
        "scope": "offline shader translation/validation only; no game, renderer, GPU execution or draw replay",
        "inputs": {
            "release_bank_sha256": sha(release_raw),
            "debug_bank_sha256": sha(debug_raw),
            "runtime_draw_jsonl_sha256": sha(DRAW_RECORDS.read_bytes()),
            "runtime_vs_xxh3": f"{xxhash.xxh3_64_intdigest(runtime_vs):016x}",
            "runtime_vs_sha256": sha(runtime_vs),
            "bank_249_vs_sha256": sha(bank_vs),
            "runtime_ps_xxh3": f"{xxhash.xxh3_64_intdigest(runtime_ps):016x}",
            "runtime_ps_sha256": sha(runtime_ps),
            "bank_343_ps_sha256": release["slots"][343]["microcode_sha256"],
            "runtime_vs_cache_sources": runtime_vs_sources,
            "runtime_ps_cache_sources": runtime_ps_sources,
        },
        "vs_fetch_patch_comparison": {
            "parent_slot": 249,
            "parent_candidates_by_fetch_only_byte_diff": parent_candidates,
            "dword_count": len(runtime_vs) // 4,
            "differing_dword_indices": differing_words,
            "changed_three_dword_instruction_indices": changed_instructions,
            "changed_instruction_dwords": {
                str(index): {
                    "bank_parent": [f"{v:08x}" for v in bank_words[index * 3:index * 3 + 3]],
                    "runtime_xsh": [f"{v:08x}" for v in runtime_words[index * 3:index * 3 + 3]],
                } for index in changed_instructions
            },
            "nonfetch_dwords_identical": not nonfetch_diffs,
            "pair_capture_draw_records": len(pair_records),
            "pair_capture_vertex_fetch_signatures": [
                {"records": count, "fetches": json.loads(signature)}
                for signature, count in fetch_signatures.most_common()
            ],
            "pair_capture_texture_fetch_signatures_count": len(texture_signatures),
        },
        "toolchain": {
            "dxc": "1.9.2609",
            "dxc_archive_sha256": tool_manifest["archive_sha256"],
            "target": "SPIR-V Vulkan 1.2, SM 6.0, HLSL 2021, DX layout, scalar-block-layout validation",
        },
        "shader_results": results,
    }
    (OUT / "result.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({
        "output": str(OUT),
        "pair_draw_records": len(pair_records),
        "shader_results": [{"name": r.get("role"), "bank_slot": r.get("bank_parent_slot"),
                             "stage": r["stage"], "emit": r["emit_exit"],
                             "dxc": r.get("compile_exit"), "spirv_val": r.get("validate_exit")}
                            for r in results],
    }, indent=2))


if __name__ == "__main__":
    main()
