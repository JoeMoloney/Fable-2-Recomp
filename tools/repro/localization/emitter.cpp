// Executes the actual vendored config loader + FunctionNode::emitCpp backend.
// Supplies explicit, bounded leaf blocks; does not run XEX loading/discovery or
// the CLI writer. All emitter/builders are compiled from their original files.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <rex/codegen/binary_view.h>
#include <rex/codegen/config.h>
#include <rex/codegen/function_graph.h>
#include <rex/codegen/manifest.h>
#include <rex/system/module.h>

namespace fs = std::filesystem;
using namespace rex::codegen;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

class FixtureModule final : public rex::runtime::Module {
 public:
  FixtureModule(uint32_t address, const std::vector<uint8_t>& bytes) : Module(nullptr),
      address_(address), size_(bytes.size()) {
    binary_sections_.push_back({".text", address, size_, bytes.data(), true, false});
  }
  const std::string& name() const override { static std::string name = "leaf-fixture"; return name; }
  bool is_executable() const override { return true; }
  uint32_t base_address() const override { return address_; }
  uint32_t image_size() const override { return size_; }
 private:
  uint32_t address_, size_;
};

bool options_off(const RecompilerConfig& c) {
  return !c.skipLr && !c.skipMsr && !c.ctrAsLocalVariable && !c.xerAsLocalVariable &&
         !c.reservedRegisterAsLocalVariable && !c.crRegistersAsLocalVariables &&
         !c.nonArgumentRegistersAsLocalVariables && !c.nonVolatileRegistersAsLocalVariables;
}

int main(int argc, char** argv) {
  try {
    require(argc == 3, "usage: emitter <fixture directory> <production manifest>");
    fs::path dir = argv[1];
    auto production = ManifestConfig::Load(argv[2]);
    require(production.has_value(), "production manifest failed to load");
    require(options_off(production->entrypoint.recompiler),
            "production effective options differ from audited all-false baseline");
    // A controlled copy of TOML option scope, loaded by the real parser. The
    // Python driver separately rejects codegen keys in hook tables.
    auto parsed = toml::parse_file((dir / "experiment.toml").string());
    auto* table = parsed["entrypoint"].as_table();
    require(table != nullptr, "missing experiment entrypoint");
    RecompilerConfig on;
    require(on.LoadFromTable(*table, dir), "experiment config failed to load");
    require(on.nonArgumentRegistersAsLocalVariables, "non_argument_as_local not enabled");
    RecompilerConfig off = on;
    off.nonArgumentRegistersAsLocalVariables = false;
    require(options_off(off), "experiment enabled an additional option");
    for (bool enabled : {false, true}) {
      auto& config = enabled ? on : off;
      std::string tag = enabled ? "on" : "off";
      std::ofstream output(dir / ("raw_" + tag + ".cpp"));
      require(bool(output), "cannot create raw output");
      output << "#include \"fable_2_pch.h\"\n\n";
      for (auto [address, basename] : {
          std::pair{0x82240578u, "getter"}, std::pair{0x82CA30E8u, "copy"}}) {
        std::ifstream input(dir / (std::string(basename) + ".bin"), std::ios::binary);
        require(bool(input), "missing guest bytes");
        std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
        require(bytes.size() == (address == 0x82240578u ? 12u : 168u), "wrong guest size");
        FixtureModule module(address, bytes);
        auto view = BinaryView::fromModule(module);
        FunctionGraph graph;
        auto* node = graph.addFunction(address, bytes.size(), FunctionAuthority::CONFIG, true);
        require(node != nullptr, "cannot register fixture");
        node->setName("probe_" + tag + "_" + basename);
        node->discover({{address, uint32_t(bytes.size())}}, {}, {});
        node->seal();
        std::unordered_set<std::string> referenced;
        EmitContext context{view, config, graph, 0, nullptr, &referenced};
        auto text = node->emitCpp(context);
        require(referenced.empty(), "leaf fixture unexpectedly calls another function");
        require(text.find("REX_FATAL") == std::string::npos &&
                text.find("REX_UNIMPLEMENTED") == std::string::npos &&
                text.find("STUB") == std::string::npos, "unsafe/incomplete generated body");
        output << text << '\n';
      }
    }
    std::cout << "PASS: actual production loader reports all options false; "
                 "actual emitter generated off/on leaves with only non_argument_as_local changed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
