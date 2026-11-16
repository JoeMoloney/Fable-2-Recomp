// Readable rewrite of ProcessAndProcessAndProcess619_82210418 (guest 0x82210418,
// 48 PPC instructions, 41 callers). Test:
//   python3 tools/theseus/difftest.py ProcessAndProcessAndProcess619_82210418
//
// SetShaderConstantById(uint32_t id /* r3 */, float4 value /* v1 */):
//   entry = g_ShaderConstantTable[id]       (*0x834BDBC4, 12-byte entries:
//                                             +0 register, +4 count)
//   if count < 1: return
//   if bit (register / 4, signed) of the u64 at 0x83491598 is clear: return
//   device = *0x8336019C; device->constants[register] = value  (+1920, 16 bytes each)
//   device->dirty (u64 at +0) |= bit ((uint32_t)register >> 2)
// Bits count from the top (bit 0 = 0x8000000000000000), the PowerPC srd way.
// The original spills v1 to the caller's stack at r1+32 and reads it back, and
// leaves r3 = 0 on the full path (10 callers read r3 afterwards); both kept.
// Better name: SetShaderConstantById.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kIdTable = 0x834BDBC4;      // -> {s32 reg, s32 count, u32}[]
constexpr uint32_t kEnabledMask = 0x83491598;  // u64
constexpr uint32_t kDevice = 0x8336019C;       // -> device
constexpr uint32_t kEntrySize = 12, kEntryCount = 4;
constexpr uint32_t kDeviceDirty = 0, kDeviceConstants = 1920;
constexpr uint32_t kSpill = 32;  // v1 spill slot in the caller's frame

// PowerPC srd: shift amounts 64..127 give 0.
uint64_t PpcSrd(uint64_t value, uint64_t amount) {
  return (amount & 0x40) ? 0 : value >> (amount & 0x3F);
}
constexpr uint64_t kTopBit = 1ull << 63;

}  // namespace

extern "C" REX_FUNC(rewrite_ProcessAndProcessAndProcess619_82210418) {
  // Spill v1 in guest (big-endian) order to the 16-byte slot at r1+32.
  const uint32_t spill = (ctx.r1.u32 + kSpill) & ~0xFu;
  for (int i = 0; i < 4; ++i) REX_STORE_U32(spill + 4 * i, ctx.v1.u32[3 - i]);

  const uint32_t entry = REX_LOAD_U32(kIdTable) + ctx.r3.u32 * kEntrySize;
  const int32_t reg = int32_t(REX_LOAD_U32(entry + 0));
  const int32_t count = int32_t(REX_LOAD_U32(entry + 4));
  if (count < 1) return;
  if ((PpcSrd(kTopBit, uint64_t(int64_t(reg / 4))) & REX_LOAD_U64(kEnabledMask)) == 0) return;

  ctx.fpscr.disableFlushMode();
  const uint32_t device = REX_LOAD_U32(kDevice);
  const uint32_t slot = device + kDeviceConstants + uint32_t(reg) * 16;
  for (int i = 0; i < 4; ++i) {
    // Read back from r1+32 (not the aligned slot), through double like lfs/stfs:
    // a signalling NaN comes out quieted.
    uint32_t bits = REX_LOAD_U32(ctx.r1.u32 + kSpill + 4 * i);
    float f;
    std::memcpy(&f, &bits, 4);
    f = float(double(f));
    std::memcpy(&bits, &f, 4);
    REX_STORE_U32(slot + 4 * i, bits);
  }
  const uint64_t dirty = PpcSrd(kTopBit, uint32_t(reg) >> 2);
  REX_STORE_U64(device + kDeviceDirty, REX_LOAD_U64(device + kDeviceDirty) | dirty);
  ctx.r3.u64 = 0;
}
