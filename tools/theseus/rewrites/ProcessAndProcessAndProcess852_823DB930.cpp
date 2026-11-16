// Readable rewrite of ProcessAndProcessAndProcess852_823DB930 (guest 0x823DB930,
// 27 PPC instructions, 98 callers, 12 of them Lua bindings).
// Gate: python3 tools/theseus/gate.py ProcessAndProcessAndProcess852_823DB930
//
// CRC-32 (the zlib/Ethernet polynomial, reflected, table at 0x82098CA0) of a
// string held by reference: r3 points to a pointer to { const char* data; u32 len }.
// Starts from 0 and has no final xor (so not the zlib crc32() value). Returns 0
// for a null string or length 0. Result is 32-bit, zero-extended in r3.
// Evidence: the guest table equals the standard 0xEDB88320 table byte for byte
// (checked against Ghidra 2026-11-16); 12 Lua bindings call it, likely to hash names.
// Proposed name: Crc32_StringRef_823DB930.
#include "fable_2_pch.h"

#include <array>

namespace {

constexpr uint32_t kPolynomial = 0xEDB88320;  // reflected CRC-32
constexpr uint32_t kStringData = 0, kStringLength = 4;

constexpr std::array<uint32_t, 256> MakeCrcTable() {
  std::array<uint32_t, 256> table{};
  for (uint32_t i = 0; i < 256; ++i) {  // gate: equivalent: entry 0 is 0 whether or not the loop starts at 0
    uint32_t c = i;
    for (int bit = 0; bit < 8; ++bit) c = (c & 1) ? (c >> 1) ^ kPolynomial : c >> 1;
    table[i] = c;
  }
  return table;
}
constexpr auto kCrcTable = MakeCrcTable();

uint32_t Crc32(uint8_t* base, uint32_t string_ref) {
  const uint32_t string = REX_LOAD_U32(string_ref);
  if (string == 0) return 0;
  const uint32_t length = REX_LOAD_U32(string + kStringLength);
  const uint32_t data = REX_LOAD_U32(string + kStringData);
  uint32_t crc = 0;
  for (uint32_t i = 0; i < length; ++i)
    crc = kCrcTable[(REX_LOAD_U8(data + i) ^ crc) & 0xFF] ^ (crc >> 8);
  return crc;
}

}  // namespace

extern "C" REX_FUNC(rewrite_ProcessAndProcessAndProcess852_823DB930) {
  ctx.r3.u64 = Crc32(base, ctx.r3.u32);
}
