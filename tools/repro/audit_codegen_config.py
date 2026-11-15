"""Read-only manifest/codegen audit and compile-only register-localization probe.

Does not run codegen, link/run guest code, or modify production inputs. The local
probe is a source-level model of builder accessors, NOT regenerated game code or
a proof of whole-PPCContext equivalence. Requires Python 3.11+ and Clang.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tomllib


ROOT = Path(__file__).resolve().parents[2]
OPTIONS = (
    "skip_lr", "skip_msr", "ctr_as_local", "xer_as_local",
    "reserved_as_local", "cr_as_local", "non_argument_as_local",
    "non_volatile_as_local",
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract_function(path, name):
    text = path.read_text()
    start = text.index(f"DEFINE_REX_FUNC({name}) {{")
    end = text.find("\nDEFINE_REX_FUNC(", start + 1)
    return text[start:end if end >= 0 else len(text)].rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True,
                        help="New evidence directory; refuses an existing path")
    parser.add_argument("--compiler", default="clang++")
    args = parser.parse_args()
    out = args.output.resolve()
    # Keep experiment outputs away from the game/SDK source and installed builds.
    permitted = (ROOT / "out/performance").resolve()
    if not out.is_relative_to(permitted):
        parser.error("output must be a new directory below out/performance")
    out.mkdir(parents=True, exist_ok=False)

    manifest = ROOT / "fable_2_manifest.toml"
    parsed = tomllib.loads(manifest.read_text())
    entry = parsed["entrypoint"]
    includes = {}
    for name in entry.get("includes", []):
        included = tomllib.loads((ROOT / name).read_text())
        includes[name] = {key: included[key] for key in OPTIONS if key in included}
    misplaced = []
    for hook in entry.get("midasm_hook", []):
        values = {key: hook[key] for key in OPTIONS if key in hook}
        if values:
            misplaced.append({"name": hook.get("name"),
                              "address": hex(hook["address"]), "values": values})

    evidence = {
        "scope": "static parsing and compile-only probes; no game or fixture execution",
        "entrypoint_options": {key: entry.get(key) for key in OPTIONS},
        "included_top_level_options": includes,
        "misplaced_hook_options": misplaced,
        "compiler_version": subprocess.check_output(
            [args.compiler, "--version"], text=True).splitlines()[0],
        "production_input_hashes_before": {},
        "probes": [],
    }
    watched = [manifest, ROOT / "fable2_switch_tables.toml",
               ROOT / "generated/default/fable_2_pch.h"]
    sdk = ROOT / "thirdparty/rexglue-sdk"
    watched += [sdk / name for name in (
        "src/codegen/config.cpp", "src/codegen/manifest.cpp",
        "src/codegen/builders/context.cpp", "src/codegen/function_graph.cpp",
        "include/rex/codegen/config.h", "include/rex/ppc/context.h",
    )]
    probes = (
        ("GetGlobalPtr3_82240578", "fable_2_recomp.78.cpp", ("r11",)),
        ("MemCpy_Aligned_82CA30E8", "fable_2_recomp.215.cpp",
         ("r0", "ctr", "cr0", "xer")),
    )
    for _, filename, _ in probes:
        watched.append(ROOT / "generated/default" / filename)
    watched = list(dict.fromkeys(watched))
    evidence["production_input_hashes_before"] = {
        str(path.relative_to(ROOT)): sha(path) for path in watched
    }
    census = {"files": 0, "source_bytes": 0, "function_definitions": 0,
              "ctx_lr_assignments": 0, "indirect_call_sites": 0,
              "local_gpr_or_ctr_declarations": 0, "local_cr_declarations": 0}
    for path in sorted((ROOT / "generated/default").glob("fable_2_recomp.*.cpp")):
        data = path.read_bytes()
        text = data.decode()
        census["files"] += 1
        census["source_bytes"] += len(data)
        census["function_definitions"] += text.count("DEFINE_REX_FUNC(")
        census["ctx_lr_assignments"] += len(re.findall(r"\bctx\.lr\s*=", text))
        census["indirect_call_sites"] += text.count("REX_CALL_INDIRECT_FUNC(")
        census["local_gpr_or_ctr_declarations"] += len(re.findall(
            r"\bPPCRegister\s+(?:r\d+|ctr)\{\}", text))
        census["local_cr_declarations"] += len(re.findall(
            r"\bPPCCRRegister\s+cr\d+\{\}", text))
    evidence["generated_source_census_not_runtime_counts"] = census
    flags = ["-std=c++23", "-O3", "-DNDEBUG", "-march=x86-64-v2",
             "-msse4.1", "-mcmodel=large", "-S", "-masm=intel"]
    for include in (ROOT / "generated/default", sdk / "include",
                    sdk / "thirdparty/simde", sdk / "thirdparty/fmt/include",
                    sdk / "thirdparty/spdlog/include"):
        flags += ["-I", str(include)]

    source = '#include "fable_2_pch.h"\n\n'
    for name, filename, registers in probes:
        body = extract_function(ROOT / "generated/default" / filename, name)
        original = body.replace(f"DEFINE_REX_FUNC({name})",
                                f"DEFINE_REX_FUNC(AuditOriginal_{name})", 1)
        local = body.replace(f"DEFINE_REX_FUNC({name})",
                             f"DEFINE_REX_FUNC(AuditLocalModel_{name})", 1)
        declarations = []
        for reg in registers:
            local = re.sub(rf"\bctx\.{reg}\b", reg, local)
            typename = ("PPCCRRegister" if reg.startswith("cr") else
                        "PPCXERRegister" if reg == "xer" else "PPCRegister")
            declarations.append(f"\t{typename} {reg}{{}};")
        local = local.replace("\tREX_FUNC_PROLOGUE();",
                              "\tREX_FUNC_PROLOGUE();\n" + "\n".join(declarations), 1)
        source += original + "\n" + local + "\n"
        evidence["probes"].append({
            "function": name, "source_file": filename,
            "original_body_sha256": hashlib.sha256(body.encode()).hexdigest(),
            "modeled_local_registers": registers,
            "limitations": "Manual accessor model; no actual alternate codegen, "
                           "no full-context equivalence, excludes app tracing header",
        })
    cpp = out / "register_probes.cpp"
    cpp.write_text(source)
    command = [args.compiler, *flags, str(cpp), "-o", str(out / "register_probes.s")]
    result = subprocess.run(command, text=True, capture_output=True)
    (out / "compile.stdout").write_text(result.stdout)
    (out / "compile.stderr").write_text(result.stderr)
    evidence["compile_command"] = command
    evidence["compile_returncode"] = result.returncode
    if result.returncode == 0:
        assembly = (out / "register_probes.s").read_text()
        metrics = {}
        for name, _, _ in probes:
            for variant in ("Original", "LocalModel"):
                symbol = f"__imp__Audit{variant}_{name}"
                start = re.search(rf"^{re.escape(symbol)}:", assembly, re.MULTILINE)
                end = re.search(r"^\.Lfunc_end\d+:", assembly[start.end():], re.MULTILINE)
                body = assembly[start.end():start.end() + end.start()]
                instructions = re.findall(r"^\t([a-z][a-z0-9]*)\b(.*)", body, re.MULTILINE)
                metrics[symbol] = {
                    "static_instruction_lines_not_cycles": len(instructions),
                    "static_context_memory_operand_lines": sum(
                        "[rdi" in operands for _, operands in instructions),
                }
        evidence["assembly_metrics"] = metrics
    evidence["production_inputs_unchanged"] = all(
        sha(ROOT / path) == digest
        for path, digest in evidence["production_input_hashes_before"].items()
    )
    evidence["artifact_hashes"] = {
        str(path.name): sha(path) for path in out.iterdir() if path.is_file()
    }
    (out / "result.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps({"output": str(out), "misplaced": misplaced,
                      "compile_returncode": result.returncode,
                      "production_inputs_unchanged": evidence["production_inputs_unchanged"]},
                     indent=2))
    return result.returncode or (0 if evidence["production_inputs_unchanged"] else 1)


if __name__ == "__main__":
    raise SystemExit(main())
