// Standalone emitter-driver infrastructure only. No instruction builder,
// config parser, graph, memory translation, or generated body is replaced.
// Unsupported paths are fail-closed, not successful no-op implementations.
#include <stdexcept>
#include <rex/logging.h>
#include <rex/codegen/function_scanner.h>
#include <rex/system/export_resolver.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace {
std::shared_ptr<spdlog::logger> logger() {
  static auto value = spdlog::stderr_color_mt("isolated-codegen");
  return value;
}
}
namespace rex {
LogCategoryId RegisterLogCategory(const char*) { return LogCategoryId{0}; }
spdlog::logger* GetLoggerRaw(LogCategoryId) { return logger().get(); }
std::shared_ptr<spdlog::logger> GetLogger() { return logger(); }
}
namespace rex::runtime {
Export* ExportResolver::GetExportByOrdinal(std::string_view, uint16_t) {
  throw std::runtime_error("forbidden import resolution in leaf emitter fixture");
}
}
namespace rex::codegen {
FunctionScanner::FunctionScanner(const BinaryView& binary) : binary_(&binary) {
  throw std::runtime_error("forbidden late jump-table scanner in leaf fixture");
}
std::optional<JumpTable> FunctionScanner::detect_jump_table(rex::guest_addr_t) {
  throw std::runtime_error("forbidden late jump-table detection in leaf fixture");
}
}
