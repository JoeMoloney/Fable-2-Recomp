#include <cstdint>
#include <string>

#if defined(ABI_LIBRARY_BUILD)
#define ABI_EXPORT __attribute__((visibility("default")))
#else
#define ABI_EXPORT
#endif

extern "C" ABI_EXPORT std::uint64_t deck_abi_probe_value(const char* input) {
  const std::string value = input ? input : "";
  return static_cast<std::uint64_t>(value.size()) * 0x100000001b3ULL;
}
