#include <cstdint>
#include <iostream>

extern "C" std::uint64_t deck_abi_probe_value(const char* input);

int main() {
  constexpr char payload[] = "x86-64-v2/Deck ABI probe";
  const auto expected = (sizeof(payload) - 1) * 0x100000001b3ULL;
  const auto actual = deck_abi_probe_value(payload);
  if (actual != expected) {
    std::cerr << "shared-library ABI probe mismatch\n";
    return 1;
  }
  std::cout << "ABI probe passed: " << actual << '\n';
  return 0;
}
