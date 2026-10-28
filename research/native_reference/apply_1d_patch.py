"""Apply the local 1D-descriptor correction to the immutable pinned emitter."""
import hashlib
import sys
from pathlib import Path

ORIGINAL_SHA256 = "025afceba969e505f10af1220cb4dca603966e71464bff3fc24419f7b2224b70"


def patched_source(source: str) -> str:
    spirv_anchor = '''            for (size_t j = 0; j < std::size(TEXTURE_DIMENSIONS); j++)
            {
                println("#define {}_Texture{}DescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + {})",
                    constantName, TEXTURE_DIMENSIONS[j], j * 64 + constantInfo->registerIndex * 4);
            }

            println("#define {}_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + {})",
'''
    spirv_replacement = '''            for (size_t j = 0; j < std::size(TEXTURE_DIMENSIONS); j++)
            {
                println("#define {}_Texture{}DescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + {})",
                    constantName, TEXTURE_DIMENSIONS[j], j * 64 + constantInfo->registerIndex * 4);
            }

            // Local extension: append the 1D descriptor lane after legacy shared constants.
            println("#define {}_Texture1DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + {})",
                constantName, 288 + constantInfo->registerIndex * 4);

            println("#define {}_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + {})",
'''
    dxil_anchor = '''            for (size_t j = 0; j < std::size(TEXTURE_DIMENSIONS); j++)
            {
                println("\\tuint {}_Texture{}DescriptorIndex : packoffset(c{}.{});",
                    constantName, TEXTURE_DIMENSIONS[j], j * 4 + constantInfo->registerIndex / 4, SWIZZLES[constantInfo->registerIndex % 4]);
            }

            println("\\tuint {}_SamplerDescriptorIndex : packoffset(c{}.{});",
'''
    dxil_replacement = '''            for (size_t j = 0; j < std::size(TEXTURE_DIMENSIONS); j++)
            {
                println("\\tuint {}_Texture{}DescriptorIndex : packoffset(c{}.{});",
                    constantName, TEXTURE_DIMENSIONS[j], j * 4 + constantInfo->registerIndex / 4, SWIZZLES[constantInfo->registerIndex % 4]);
            }

            // Keep the existing 2D/3D/Cube/sampler layout; 1D occupies c18-c21.
            println("\\tuint {}_Texture1DDescriptorIndex : packoffset(c{}.{});",
                constantName, 18 + constantInfo->registerIndex / 4, SWIZZLES[constantInfo->registerIndex % 4]);

            println("\\tuint {}_SamplerDescriptorIndex : packoffset(c{}.{});",
'''
    if source.count(spirv_anchor) != 1 or source.count(dxil_anchor) != 1:
        raise ValueError("Pinned emitter patch anchors changed; refusing to patch")
    return source.replace(spirv_anchor, spirv_replacement, 1).replace(dxil_anchor, dxil_replacement, 1)


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: apply_1d_patch.py PINNED_SOURCE OUTPUT")
    source_path, output_path = map(Path, sys.argv[1:])
    raw = source_path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != ORIGINAL_SHA256:
        raise SystemExit("Pinned XenosRecomp source hash mismatch")
    output = patched_source(raw.decode())
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(output)


if __name__ == "__main__":
    main()
