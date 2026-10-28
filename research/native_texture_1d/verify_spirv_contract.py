#!/usr/bin/env python3
"""Check the compiled synthetic shader against the Xenos set/offset contract."""
import re
import subprocess
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_spirv_contract.py FRAGMENT.spv")
    assembly = subprocess.check_output(["spirv-dis", sys.argv[1]], text=True)
    decorations = {}
    for line in assembly.splitlines():
        match = re.match(r"\s*OpDecorate (%\S+) (DescriptorSet|Binding) (\d+)$", line)
        if match:
            decorations.setdefault(match.group(1), {})[match.group(2)] = int(match.group(3))
    found = {(v.get("DescriptorSet"), v.get("Binding")) for v in decorations.values()}
    if (3, 0) not in found or (5, 0) not in found:
        raise SystemExit(f"missing sampler set 3/binding 0 or Texture1D set 5/binding 0: {found}")
    if "OpCapability RuntimeDescriptorArray" not in assembly:
        raise SystemExit("SPIR-V did not retain runtime descriptor arrays")
    if "OpCapability PhysicalStorageBufferAddresses" not in assembly:
        raise SystemExit("SPIR-V did not retain the Xenos SharedConstants BDA ABI")
    for member, offset in ((0, 0), (1, 8), (2, 16)):
        if not re.search(rf"OpMemberDecorate %XenosPushConstants {member} Offset {offset}\b", assembly):
            raise SystemExit(f"push constant member {member} offset {offset} missing")
    for word in (52, 53, 76, 77):
        if not re.search(rf"OpConstant %\S+ {word}\b", assembly):
            raise SystemExit(f"SharedConstants word index {word} missing")
    if assembly.count("OpTypeRuntimeArray") < 3:
        raise SystemExit("expected runtime arrays for the two descriptor heaps and BDA words")
    if "NonUniform" in assembly:
        raise SystemExit("synthetic contract should use draw-uniform descriptor indices")
    print("SPIR-V descriptor-set, runtime-array, push-constant, and SharedConstants contract: PASS")


if __name__ == "__main__":
    main()
