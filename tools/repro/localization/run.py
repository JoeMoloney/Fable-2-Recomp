"""Run bounded actual-emitter A/B leaf tests without starting Fable II.

No downloads, production codegen/build/staging, or SDK edits. The driver uses
unchanged SDK sources, with logging adapters and forbidden-path tripwires.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tomllib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SDK = ROOT / "thirdparty/rexglue-sdk"
OPTIONS = ("skip_lr", "skip_msr", "ctr_as_local", "xer_as_local",
           "reserved_as_local", "cr_as_local", "non_argument_as_local",
           "non_volatile_as_local")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def scope_errors(parsed):
    return [(hook.get("name"), key) for hook in
            parsed.get("entrypoint", {}).get("midasm_hook", [])
            for key in OPTIONS if key in hook]


def body(text, name):
    start = text.index(f"DEFINE_REX_FUNC({name}) {{")
    end = text.find("\nDEFINE_REX_FUNC(", start + 1)
    return text[start:end if end >= 0 else len(text)].strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to((ROOT / "out/performance").resolve()) or out.exists():
        parser.error("output must be a NEW directory below out/performance")
    if not 1 <= args.jobs <= 4:
        parser.error("jobs must be between 1 and 4")
    out.mkdir(parents=True)
    snapshot = out / "driver-source"
    snapshot.mkdir()
    for path in HERE.iterdir():
        if path.is_file():
            shutil.copy2(path, snapshot / path.name)
    report = {"scope": "actual backend emission of two explicit leaf blocks, not CLI/XEX discovery",
              "steps": [], "status": "started"}
    paths = [ROOT / "fable_2_manifest.toml", ROOT / "fable2_switch_tables.toml"]
    for tree in (ROOT / "src", ROOT / "generated/default", SDK / "src", SDK / "include"):
        paths += [p for p in tree.rglob("*") if p.is_file() and
                  p.suffix in {".h", ".cpp", ".c", ".toml", ".inja", ".cmake"}]
    before = {str(p.relative_to(ROOT)): sha(p) for p in sorted(set(paths))}
    (out / "production-before.json").write_text(json.dumps(before, indent=2) + "\n")

    def step(name, command, expected=0, required_text=None):
        print(name, flush=True)
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True,
                                timeout=360)
        (out / f"{name}.stdout").write_text(result.stdout)
        (out / f"{name}.stderr").write_text(result.stderr)
        report["steps"].append({"name": name, "command": list(map(str, command)),
                                "returncode": result.returncode, "expected": expected})
        if result.returncode != expected:
            raise RuntimeError(f"{name}: expected exit {expected}, got {result.returncode}; see logs")
        if required_text and required_text not in result.stdout + result.stderr:
            raise RuntimeError(f"{name}: required result marker absent")
        return result

    try:
        fixtures = json.loads((HERE / "fixtures.json").read_text())
        if sha(ROOT / "default.xex") != fixtures["target_sha256"]:
            raise RuntimeError("target hash mismatch")
        report["target_sha256"] = fixtures["target_sha256"]
        errors = scope_errors(tomllib.loads((ROOT / "fable_2_manifest.toml").read_text()))
        if len(errors) != 8:
            raise RuntimeError("audited production mis-scoping changed; reassess")
        report["production_scope_errors_detected"] = errors
        experiment = ('[entrypoint]\nfile_path = "getter.bin"\n'
                      'out_directory_path = "unused-writer-output"\n' +
                      "".join(f"{key} = {'true' if key == 'non_argument_as_local' else 'false'}\n"
                              for key in OPTIONS))
        if scope_errors(tomllib.loads(experiment)):
            raise RuntimeError("experiment options mis-scoped")
        (out / "experiment.toml").write_text(experiment)
        for name in ("getter", "copy"):
            (out / f"{name}.bin").write_bytes(bytes.fromhex(fixtures[name]["hex"]))
        step("configure", ["cmake", "-S", HERE, "-B", out / "build", "-G", "Ninja",
                           f"-DSDK={SDK}", "-DCMAKE_C_COMPILER=clang",
                           "-DCMAKE_CXX_COMPILER=clang++"])
        step("build", ["cmake", "--build", out / "build", "--target", "emitter",
                       "-j", args.jobs])
        emitted = step("emit", [out / "build/emitter", out, ROOT / "fable_2_manifest.toml"],
                       required_text="PASS: actual production loader")
        if re.search(r"\[(warning|error|critical)\]", emitted.stderr):
            raise RuntimeError("unexpected generator diagnostics; do not continue")
        off = (out / "raw_off.cpp").read_text()
        on = (out / "raw_on.cpp").read_text()
        for name in ("getter", "copy"):
            fixture = fixtures[name]
            original = (ROOT / "generated/default" / fixture["production_file"]).read_text()
            expected = body(original, fixture["production_symbol"])
            actual = body(off, f"probe_off_{name}").replace(
                f"probe_off_{name}", fixture["production_symbol"], 1)
            if expected.split() != actual.split():
                (out / f"mismatch-{name}-expected.cpp").write_text(expected)
                (out / f"mismatch-{name}-actual.cpp").write_text(actual)
                raise RuntimeError(f"baseline regenerated {name} differs from production; reassess")
        if "PPCRegister r11{};" not in on or "PPCRegister r0{};" not in on:
            raise RuntimeError("actual alternate emitter did not localize expected registers")
        if any(token in on for token in ("PPCRegister ctr{};", "PPCXERRegister xer{};",
                                        "PPCCRRegister cr0{};", "PPCRegister r14{};")):
            raise RuntimeError("alternate emitter changed unexpected register groups")
        report["baseline_bodies_match_current_generated_source"] = True
        report["only_option_changed"] = "non_argument_as_local"
        includes = [ROOT / "generated/default", SDK / "include", SDK / "thirdparty/simde",
                    SDK / "thirdparty/fmt/include", SDK / "thirdparty/spdlog/include"]
        flags = ["-std=c++23", "-O3", "-march=x86-64-v2", "-msse4.1", "-mcmodel=large",
                 "-fno-strict-aliasing", "-ffp-model=strict"]
        for path in includes:
            flags += ["-I", str(path)]
        sources = [HERE / "harness.cpp", out / "raw_off.cpp", out / "raw_on.cpp"]
        step("compile", ["clang++", *flags, *sources, "-o", out / "checks"])
        positive = step("correctness", [out / "checks"], required_text="PASS getter_cases=8")
        report["correctness_summary"] = positive.stdout.strip()
        step("negative-memory", [out / "checks", "negative-memory"], expected=1,
             required_text="baseline memory differs from independent copy oracle")
        step("negative-scratch", [out / "checks", "negative-scratch"], expected=1,
             required_text="scratch-live-across-call contract mismatch")
        step("compile-ubsan", ["clang++", *flags, "-fsanitize=undefined",
                              "-fno-sanitize-recover=all", *sources, "-o", out / "checks-ubsan"])
        step("correctness-ubsan", [out / "checks-ubsan"], required_text="PASS getter_cases=8")
        step("benchmark", [out / "checks", "bench"], required_text="BENCH length=4096")
        report["status"] = "passed_bounded_leaf_checks_not_game_compatibility"
    except Exception as error:
        report["status"] = "stopped"
        report["error"] = str(error)
        print(str(error), file=sys.stderr)
    finally:
        changed = [p for p, digest in before.items() if not (ROOT / p).is_file() or sha(ROOT / p) != digest]
        report["changed_watched_production_files"] = changed
        report["watched_production_file_count"] = len(before)
        report["production_inputs_unchanged"] = not changed
        report["driver_source_hashes"] = {p.name: sha(p) for p in HERE.iterdir() if p.is_file()}
        report["artifact_hashes"] = {str(p.relative_to(out)): sha(p) for p in out.iterdir() if p.is_file()}
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k not in ("steps", "artifact_hashes",
                                                                   "driver_source_hashes")}, indent=2))
    return 0 if report["status"].startswith("passed") and not changed else 1


if __name__ == "__main__":
    raise SystemExit(main())
