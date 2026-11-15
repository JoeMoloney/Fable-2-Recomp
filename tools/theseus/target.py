"""Per-game settings for tools/theseus (see targets/fable2.toml).

Pick a target with THESEUS_TARGET=<path to .toml>; the default is Fable II.
`python3 tools/theseus/target.py shell` prints the [ghidra] settings as shell
variables for export.sh.
"""
import os
import re
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT = HERE / "targets" / "fable2.toml"


class Target:
    def __init__(self, path=None):
        self.path = Path(path or os.environ.get("THESEUS_TARGET") or DEFAULT).resolve()
        with open(self.path, "rb") as f:
            self.cfg = tomllib.load(f)
        self.name = self.cfg.get("name", self.path.stem)
        self.root = (self.path.parent / self.cfg.get("repo_root", ".")).resolve()
        r = self.cfg["recomp"]
        self.generated = self.root / r["generated_dir"]
        self.generated_glob = r["generated_glob"]
        self.manifest = self.root / r["manifest"]
        self.hotfunc_dir = self.root / r["hotfunc_dir"]
        self.sdk = self.root / r.get("sdk_dir", "thirdparty/rexglue-sdk")
        self.out = Path(os.environ.get("THESEUS_OUT") or self.root / r.get("out_dir", "out/theseus"))
        e = self.cfg.get("evidence", {})
        self.source_root_marker = e.get("source_root_marker", "")
        self.libraries = e.get("library", [])
        self._name_res = [re.compile(lib["name_regex"]) if "name_regex" in lib else None
                          for lib in self.libraries]
        self._file_res = [re.compile(lib["file_regex"], re.I) if "file_regex" in lib else None
                          for lib in self.libraries]

    def partitions(self):
        """Generated partitions in numeric order (name.<N>.cpp)."""
        def num(p):
            m = re.search(r"\.(\d+)\.cpp$", p.name)
            return int(m.group(1)) if m else -1
        return sorted(self.generated.glob(self.generated_glob), key=num)

    def library_of_path(self, p):
        low = p.lower().replace("/", "\\")
        base = low.rsplit("\\", 1)[-1]
        for lib, file_re in zip(self.libraries, self._file_res):
            if any(s.lower() in low for s in lib.get("path_contains", [])):
                return lib["name"]
            if any(f"\\{d}\\" in low for d in lib.get("path_dirs", [])):
                return lib["name"]
            if base in lib.get("path_files", []):
                return lib["name"]
            if file_re and file_re.match(base):
                return lib["name"]
        return None

    def library_of_name(self, name):
        n = name.split("<")[0]
        for lib, name_re in zip(self.libraries, self._name_res):
            if n.startswith(tuple(lib.get("name_prefixes", []))):
                return lib["name"]
            if name_re and name_re.search(n):
                return lib["name"]
        return None


def load(path=None):
    return Target(path)


if __name__ == "__main__":
    t = load()
    if len(sys.argv) > 1 and sys.argv[1] == "shell":
        g = t.cfg["ghidra"]
        print(f'CONTAINER={g["container"]!r}')
        print(f'HOST_GHIDRA={os.path.expanduser(g["host_dir"])!r}')
        print(f'PROJECT={g["project"]!r}')
        print(f'PROGRAM={g["program"]!r}')
        print(f'MAXMEM={g.get("heap", "3G")!r}')
        print(f'OUT_DIR={str(t.out / "ghidra")!r}')
    else:
        print(f"{t.name}: root={t.root} generated={t.generated} partitions={len(t.partitions())}")
