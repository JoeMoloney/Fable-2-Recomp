#!/usr/bin/env python3
"""Re-emit and compile retained 1D-fetch shader fixtures with the local extension."""
from __future__ import annotations

import collections
import hashlib
import importlib.util
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GAME = Path("/mnt/HDD/git_stuff/my_git/Fable-2-Recomp")
OUT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else HERE / "standard-material-1d-batch-v1"
EMITTER = HERE / "build-standard-material-1d/emit_hlsl"
COMMON = HERE / "upstream/xenosrecomp/XenosRecomp/shader_common.h"
DXC_ROOT = HERE / "tools/dxc-1.9.2609/root"
EXPECTED_RELEASE = "c2732102a0aedb8c7a950e5781fa15092a3c12c2721380944c4e717dade5e736"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


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
        raise ValueError("SPIR-V has duplicate set/binding pairs")
    return rows


def main() -> None:
    if OUT.exists():
        raise SystemExit(f"Refusing to reuse output directory: {OUT}")
    if not EMITTER.is_file():
        raise SystemExit("Build the isolated patched converter first")
    parser = load_module("support_bank", ROOT / "research/native_offline/bank.py")
    survey = load_module("support_survey", HERE / "survey_corpus.py")
    release_path = GAME / "data/Shaders/ShadersRelease.sbk"
    release_data = release_path.read_bytes()
    if sha(release_data) != EXPECTED_RELEASE:
        raise ValueError("Release bank identity mismatch")
    parsed = parser.parse(release_data)
    previous = json.loads((HERE / "corpus-1/results/survey.json").read_text())
    previous_by_slot = {row["slot"]: row for row in previous["shaders"]}
    hlsl_dir = HERE / "corpus-1/results/hlsl"
    slots = []
    for path in sorted(hlsl_dir.glob("*.hlsl")):
        if "tfetch1D(" not in path.read_text(errors="strict"):
            continue
        match = re.fullmatch(r"(\d{3})-(vs|ps)\.hlsl", path.name)
        if not match:
            raise ValueError(f"Unexpected shader fixture path: {path}")
        slot = int(match.group(1))
        slots.append((slot, match.group(2)))
    if not slots or len({slot for slot, _ in slots}) != len(slots):
        raise ValueError("No unique retained 1D shader fixtures found")

    OUT.mkdir()
    for name in ("containers", "hlsl", "spirv", "logs", "home", "tmp"):
        (OUT / name).mkdir()
    compiler_manifest = json.loads((DXC_ROOT.parent / "manifest.json").read_text())
    for name in ("bin/dxc", "lib/libdxcompiler.so"):
        if sha((DXC_ROOT / name).read_bytes()) != compiler_manifest["files"]["root/" + name]:
            raise ValueError("Pinned DXC hash mismatch")
    env = os.environ.copy()
    env.update({
        "LD_LIBRARY_PATH": str(DXC_ROOT / "lib"),
        "HOME": str(OUT / "home"), "TMPDIR": str(OUT / "tmp"),
        "XDG_CACHE_HOME": str(OUT / "tmp"), "PYTHONDONTWRITEBYTECODE": "1",
    })
    containers = dict(survey.containers(release_data, parsed))

    def process(item):
        slot, stage = item
        stem = f"{slot:03d}-{stage}"
        container_path = OUT / "containers" / f"{stem}.bin"
        hlsl_path = OUT / "hlsl" / f"{stem}.hlsl"
        spirv_path = OUT / "spirv" / f"{stem}.spv"
        container_path.write_bytes(containers[slot])
        emit = subprocess.run([
            "prlimit", "--core=0", "--cpu=60", "--as=2147483648", "--",
            str(EMITTER), str(container_path), str(COMMON), str(hlsl_path),
        ], capture_output=True, env=env)
        (OUT / "logs" / f"{stem}-emit.log").write_bytes(emit.stdout + emit.stderr)
        row = {
            "slot": slot, "stage": stage,
            "names": previous_by_slot[slot].get("names", []),
            "container_sha256": sha(containers[slot]),
            "baseline_emit_exit": previous_by_slot[slot]["emit_exit"],
            "baseline_compile_exit": previous_by_slot[slot].get("compile_exit"),
            "emit_exit": emit.returncode,
        }
        if emit.returncode:
            return row
        row["hlsl_sha256"] = sha(hlsl_path.read_bytes())
        compile_run = subprocess.run([
            "prlimit", "--core=0", "--cpu=60", "--as=2147483648", "--",
            str(DXC_ROOT / "bin/dxc"), str(hlsl_path), "-E", "main", "-T", stage + "_6_0",
            "-HV", "2021", "-all-resources-bound", "-spirv", "-fvk-use-dx-layout",
            "-fspv-target-env=vulkan1.2", "-Qstrip_debug", "-Fo", str(spirv_path),
            *(["-fvk-invert-y"] if stage == "vs" else []),
        ], capture_output=True, env=env)
        (OUT / "logs" / f"{stem}-dxc.log").write_bytes(compile_run.stdout + compile_run.stderr)
        row["compile_exit"] = compile_run.returncode
        if compile_run.returncode == 0:
            binary = spirv_path.read_bytes()
            row["spirv_sha256"] = sha(binary)
            row["spirv_bytes"] = len(binary)
            row["descriptor_bindings"] = spirv_descriptor_bindings(spirv_path)
            validate = subprocess.run([
                "prlimit", "--core=0", "--cpu=60", "--as=2147483648", "--",
                "spirv-val", "--target-env", "vulkan1.2", "--scalar-block-layout", str(spirv_path),
            ], capture_output=True, env=env)
            (OUT / "logs" / f"{stem}-validate.log").write_bytes(validate.stdout + validate.stderr)
            row["validate_exit"] = validate.returncode
        else:
            row["failure"] = (OUT / "logs" / f"{stem}-dxc.log").read_text(errors="replace")[:1000]
        return row

    workers = min(8, os.cpu_count() or 1)
    with ThreadPoolExecutor(max_workers=workers) as pool:
        rows = list(pool.map(process, slots))
    rows.sort(key=lambda row: (row["slot"], row["stage"]))
    summary = {
        "fixture_count": len(rows),
        "baseline_compiled": sum(row.get("baseline_compile_exit") == 0 for row in rows),
        "new_compiled": sum(row.get("compile_exit") == 0 for row in rows),
        "new_validated": sum(row.get("validate_exit") == 0 for row in rows),
        "baseline_failed_but_new_compiled": sum(
            row.get("baseline_compile_exit") not in (0, None) and row.get("compile_exit") == 0
            for row in rows
        ),
        "new_failures": [row["slot"] for row in rows if row.get("compile_exit", 1) != 0],
    }
    result = {
        "scope": "retained shaders whose prior emitted HLSL uses tfetch1D; no GPU execution or semantic parity claim",
        "source_bank_sha256": sha(release_data),
        "dxc_archive_sha256": compiler_manifest["archive_sha256"],
        "parallel_workers": workers,
        "summary": summary,
        "shaders": rows,
    }
    (OUT / "survey.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"output": str(OUT), "summary": summary}, indent=2))


if __name__ == "__main__":
    main()
