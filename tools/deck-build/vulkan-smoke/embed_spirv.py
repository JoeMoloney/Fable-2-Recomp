#!/usr/bin/env python3
"""Embed two validated little-endian SPIR-V modules in a generated C++ header."""
import hashlib
import struct
import sys
from pathlib import Path


def words(path: Path):
    data = path.read_bytes()
    if len(data) < 20 or len(data) % 4:
        raise ValueError(f"invalid SPIR-V byte length: {path}")
    values = struct.unpack("<" + "I" * (len(data) // 4), data)
    if values[0] != 0x07230203:
        raise ValueError(f"invalid SPIR-V magic: {path}")
    return data, values


def emit(out: Path, vertex: Path, fragment: Path):
    vert_data, vert = words(vertex)
    frag_data, frag = words(fragment)
    lines = [
        "#pragma once",
        "#include <array>",
        "#include <cstdint>",
        "namespace texture1d_shader_spirv {",
        f"// vertex sha256={hashlib.sha256(vert_data).hexdigest()}",
        f'inline constexpr char vertex_sha256[] = "{hashlib.sha256(vert_data).hexdigest()}";',
        f"inline constexpr std::array<std::uint32_t, {len(vert)}> vertex = {{{{",
    ]
    lines.extend("  0x%08xu," % word for word in vert)
    lines.extend([
        "}};",
        f"// fragment sha256={hashlib.sha256(frag_data).hexdigest()}",
        f'inline constexpr char fragment_sha256[] = "{hashlib.sha256(frag_data).hexdigest()}";',
        f"inline constexpr std::array<std::uint32_t, {len(frag)}> fragment = {{{{",
    ])
    lines.extend("  0x%08xu," % word for word in frag)
    lines.extend(["}};", "}  // namespace texture1d_shader_spirv", ""])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines))


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: embed_spirv.py OUTPUT.hpp VERT.spv FRAG.spv")
    emit(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]))


if __name__ == "__main__":
    main()
