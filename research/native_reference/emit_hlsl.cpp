// Thin isolated driver around the unchanged pinned XenosRecomp HLSL emitter.
// No game launch, SDK linkage, global compiler setup, or cache generation.
#include "converter_compat.h"
#include "upstream/xenosrecomp/XenosRecomp/shader_recompiler.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static std::vector<uint8_t> read(const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot read input");
    const auto size = input.tellg();
    if (size <= 0 || size > 4 * 1024 * 1024) throw std::runtime_error("Input size limit");
    std::vector<uint8_t> data(static_cast<size_t>(size));
    input.seekg(0); input.read(reinterpret_cast<char*>(data.data()), data.size());
    if (!input) throw std::runtime_error("Short read");
    return data;
}
int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("Usage: emit_hlsl validated.container common.hlsl NEW.hlsl");
        auto data = read(argv[1]);
        auto common = read(argv[2]);
#ifdef FABLE_XENOS_1D_COMMON_PATH
        auto one_d_common = read(FABLE_XENOS_1D_COMMON_PATH);
        common.insert(common.end(), one_d_common.begin(), one_d_common.end());
#endif
        if (data.size() < sizeof(ShaderContainer)) throw std::runtime_error("Truncated header");
        auto* header = reinterpret_cast<const ShaderContainer*>(data.data());
        if ((header->flags & 0xFFFFFF00) != 0x102A1100 ||
            uint64_t(header->virtualSize) + uint64_t(header->physicalSize) != data.size())
            throw std::runtime_error("Container identity/length mismatch");
        if (std::filesystem::exists(argv[3])) throw std::runtime_error("Refusing to overwrite output");
        // The Python bank parser validates metadata first. Upstream still uses
        // raw pointers/assertions: only run on the pinned corpus, sandboxed.
        ShaderRecompiler recompiler;
        recompiler.recompile(data.data(), std::string_view(reinterpret_cast<char*>(common.data()), common.size()));
        std::ofstream output(argv[3], std::ios::binary);
        output.write(recompiler.out.data(), recompiler.out.size());
        if (!output) throw std::runtime_error("Output write failed");
        std::cout << (recompiler.isPixelShader ? "ps" : "vs") << " hlsl_bytes=" << recompiler.out.size() << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
