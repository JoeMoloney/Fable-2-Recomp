#!/usr/bin/env python3
"""Report ELF loader/dependency/version requirements; optionally enforce ceilings."""
import argparse
import json
import re
import subprocess
import sys


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def inspect(path):
    header = run("readelf", "-h", path)
    program = run("readelf", "-l", path)
    dynamic = run("readelf", "-d", path)
    versions = run("readelf", "--version-info", "--wide", path)
    elf_class = re.search(r"Class:\s+(.*)", header).group(1).strip()
    machine = re.search(r"Machine:\s+(.*)", header).group(1).strip()
    interp_match = re.search(r"Requesting program interpreter:\s*(.*?)\]", program)
    needed = re.findall(r"Shared library: \[(.*?)\]", dynamic)
    runpath_match = re.search(r"\((?:RUNPATH|RPATH)\).*?\[(.*?)\]", dynamic)
    version_names = re.findall(r"Name: ((?:GLIBC|GLIBCXX|CXXABI)_[0-9.]+)", versions)
    symbols = {}
    for family in ("GLIBC", "GLIBCXX", "CXXABI"):
        versions_for_family = [v for v in version_names if v.startswith(family + "_")]
        if versions_for_family:
            def key(value):
                return tuple(int(part) for part in value.removeprefix(family + "_").split("."))
            symbols[family] = max(versions_for_family, key=key)
    return {
        "path": path,
        "class": elf_class,
        "machine": machine,
        "interpreter": interp_match.group(1) if interp_match else None,
        "needed": needed,
        "runpath": runpath_match.group(1).split(":") if runpath_match else [],
        "max_symbol_versions": symbols,
        "elf_header_sha256": subprocess.check_output(["sha256sum", path], text=True).split()[0],
    }


def version_tuple(value):
    return tuple(int(x) for x in value.split("."))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", nargs="+")
    parser.add_argument("--max-glibc")
    parser.add_argument("--max-glibcxx")
    parser.add_argument("--max-cxxabi")
    args = parser.parse_args()
    rows = [inspect(path) for path in args.elf]
    failures = []
    for row in rows:
        if row["class"] != "ELF64" or "X86-64" not in row["machine"]:
            failures.append(f"{row['path']}: not ELF64 x86-64")
        for family, ceiling in (("GLIBC", args.max_glibc),
                                ("GLIBCXX", args.max_glibcxx),
                                ("CXXABI", args.max_cxxabi)):
            used = row["max_symbol_versions"].get(family)
            if ceiling and used:
                required = used.removeprefix(family + "_")
                if version_tuple(required) > version_tuple(ceiling):
                    failures.append(f"{row['path']}: {used} exceeds {family}_{ceiling}")
    print(json.dumps({"elfs": rows, "failures": failures}, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
