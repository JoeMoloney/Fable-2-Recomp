"""Bounded read-only copy-caller and write-watch source audit.

Inventories direct generated call expressions, snapshots three fixed callers and
runtime sources, and checks a restricted word-array mechanism model. Does not
run translated guest code, codegen, build a runtime, or launch the game.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / "thirdparty/rexglue-sdk"
TARGET = "MemCpy_Aligned_82CA30E8"
SELECTED = (
    "MemCpy_Vectorized_822085D0",
    "ProcessAndProcessAndProcess485_82CC0D58",
    "sub_82DF81A0",
)
RUNTIME = (
    "src/system/mmio_handler.cpp", "src/system/xmemory.cpp",
    "src/graphics/shared_memory.cpp", "src/core/exception_handler_posix.cpp",
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to((ROOT / "out/performance").resolve()) or out.exists():
        parser.error("output must be a NEW directory below out/performance")
    out.mkdir(parents=True)
    (out / "selected").mkdir()
    (out / "runtime").mkdir()
    snapshots = {}
    hashes = {}
    callers = []
    for path in sorted((ROOT / "generated/default").glob("fable_2_recomp.*.cpp")):
        raw = path.read_bytes()
        hashes[str(path.relative_to(ROOT))] = sha(raw)
        text = raw.decode()
        starts = list(re.finditer(r"^DEFINE_REX_FUNC\(([^)]+)\)", text, re.MULTILINE))
        for i, match in enumerate(starts):
            end = starts[i + 1].start() if i + 1 < len(starts) else len(text)
            body = text[match.start():end]
            name = match.group(1)
            if name in SELECTED or name == TARGET:
                snapshots[name] = body
                (out / "selected" / f"{name}.cpp").write_text(body)
            calls = []
            for call in re.finditer(rf"\b{TARGET}\(ctx, base\)", body):
                prefix = body[:call.start()]
                lr = re.findall(r"ctx\.lr = (0x[0-9A-Fa-f]+);", prefix)
                return_pc = int(lr[-1], 16) if lr else None
                calls.append({
                    "source_line": text.count("\n", 0, match.start() + call.start()) + 1,
                    "return_pc": hex(return_pc) if return_pc is not None else None,
                    "guest_call_pc_from_preceding_lr": hex(return_pc - 4) if return_pc else None,
                })
            if calls:
                callers.append({"function": name, "source": str(path.relative_to(ROOT)),
                                "body_line": text.count("\n", 0, match.start()) + 1,
                                "calls": calls})
    if set(SELECTED) - snapshots.keys():
        raise RuntimeError("selected caller missing; reassess")
    for relative in RUNTIME:
        path = SDK / relative
        raw = path.read_bytes()
        hashes[str(path.relative_to(ROOT))] = sha(raw)
        (out / "runtime" / path.name).write_bytes(raw)

    vector = snapshots[SELECTED[0]]
    small = vector.split("loc_82208690:", 1)[0]
    if small.count(f"{TARGET}(ctx, base)") != 1 or vector.count(f"{TARGET}(ctx, base)") != 1:
        raise RuntimeError("audited vector-helper boundary changed")
    helper_lengths = set()
    for length in range(16):
        for source_alignment in range(16):
            remaining = length
            if length >= 8 and source_alignment % 8 == 0:
                remaining -= 8
            if source_alignment % 4 == 0 and remaining >= 4:
                helper_lengths.add(remaining & ~3)
    if helper_lengths != {4, 8, 12}:
        raise RuntimeError("small-copy mechanism model changed")

    # Restricted source mechanism, not execution of the PPC or native candidate.
    # Preconditions: aligned nonwrapping RAM, count>=2, ordered distinct indices.
    cases = 0
    for count in range(2, 65):
        for first in range(count):
            for second in range(first + 1, count):
                words = list(range(count))
                for i in range(second - first - 1):
                    words[first + i] = words[first + i + 1]
                position = second - 1
                for i in range(count - second - 1):
                    words[position + i] = words[position + i + 2]
                expected = [i for i in range(count) if i not in (first, second)]
                if words[:count - 2] != expected:
                    raise RuntimeError("ordered two-index deletion model failed")
                cases += 1
    base, count, first, second = 0x10000, 16, 2, 10
    spans = [
        {"dst": base + first * 4, "src": base + (first + 1) * 4,
         "length": (second - first - 1) * 4},
        {"dst": base + (second - 1) * 4, "src": base + (second + 1) * 4,
         "length": (count - second - 1) * 4},
    ]
    for span in spans:
        span["overlap_bytes"] = max(0, min(span["dst"] + span["length"],
                                             span["src"] + span["length"]) -
                                     max(span["dst"], span["src"]))
    (out / "caller_inventory.json").write_text(json.dumps(callers, indent=2) + "\n")
    (out / "source-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n")
    report = {
        "scope": "static source audit and restricted Python mechanism model, not native/guest execution",
        "fable_head": subprocess.check_output(["git", "-C", ROOT, "rev-parse", "HEAD"], text=True).strip(),
        "sdk_head": subprocess.check_output(["git", "-C", SDK, "rev-parse", "HEAD"], text=True).strip(),
        "target_sha256": sha((ROOT / "default.xex").read_bytes()),
        "direct_call_expressions": sum(len(row["calls"]) for row in callers),
        "caller_bodies": len(callers), "selected_callers": SELECTED,
        "vector_small_helper_lengths_model": sorted(helper_lengths),
        "ordered_two_index_model_cases": cases,
        "overlap_example": {"base": base, "count": count, "first": first,
                            "second": second, "spans": spans},
        "production_inputs_unchanged": all(sha((ROOT / name).read_bytes()) == digest
                                            for name, digest in hashes.items()),
        "watched_production_file_count": len(hashes),
        "artifact_hashes": {str(p.relative_to(out)): sha(p.read_bytes())
                            for p in out.rglob("*") if p.is_file()},
    }
    (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "artifact_hashes"}, indent=2))
    return 0 if report["production_inputs_unchanged"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
