// Readable rewrite of ProcessAndProcessAndProcess1728_82D4F620 (guest 0x82D4F620,
// 19 PPC instructions, 323 callers). Test:
//   python3 tools/theseus/difftest.py ProcessAndProcessAndProcess1728_82D4F620
//
// Havok reflection: hkClass::hkClass(name, parent, objectSize,
//   implementedInterfaces, numImplementedInterfaces, declaredEnums,
//   numDeclaredEnums, declaredMembers, numDeclaredMembers, defaults,
//   attributes, flags). Evidence: all 323 callers are static initializers in
//   0x83292xxx-0x8329Fxxx; the first (0x832921C0) builds the class named
//   "hkClass" whose member names include "describedVersion" (strings at
//   0x820246FC..). The interfaces pointer (r7) is not stored, and
//   m_describedVersion is always 0. Better name: hkClass_Construct.
// Xbox 360 calling convention: arguments 9+ are in the caller's parameter save
// area, 8-byte slots from r1+84; the high (first) word of each slot is read.
#include "fable_2_pch.h"

namespace {

struct HkClassOffsets {
  static constexpr uint32_t kName = 0, kParent = 4, kObjectSize = 8,
      kNumImplementedInterfaces = 12, kDeclaredEnums = 16, kNumDeclaredEnums = 20,
      kDeclaredMembers = 24, kNumDeclaredMembers = 28, kDefaults = 32,
      kAttributes = 36, kFlags = 40, kDescribedVersion = 44;
};
constexpr uint32_t kStackArg9 = 84, kStackArgSlot = 8;

}  // namespace

extern "C" REX_FUNC(rewrite_ProcessAndProcessAndProcess1728_82D4F620) {
  using O = HkClassOffsets;
  const uint32_t self = ctx.r3.u32;
  auto stack_arg = [&](int n) { return REX_LOAD_U32(ctx.r1.u32 + kStackArg9 + (n - 9) * kStackArgSlot); };
  // Read the stack arguments first: the struct could overlap them.
  const uint32_t members = stack_arg(9), num_members = stack_arg(10), defaults = stack_arg(11),
                 attributes = stack_arg(12), flags = stack_arg(13);
  REX_STORE_U32(self + O::kObjectSize, ctx.r6.u32);
  REX_STORE_U32(self + O::kNumImplementedInterfaces, ctx.r8.u32);  // r7 (the list) dropped
  REX_STORE_U32(self + O::kDeclaredEnums, ctx.r9.u32);
  REX_STORE_U32(self + O::kNumDeclaredEnums, ctx.r10.u32);
  REX_STORE_U32(self + O::kName, ctx.r4.u32);
  REX_STORE_U32(self + O::kParent, ctx.r5.u32);
  REX_STORE_U32(self + O::kDeclaredMembers, members);
  REX_STORE_U32(self + O::kNumDeclaredMembers, num_members);
  REX_STORE_U32(self + O::kDefaults, defaults);
  REX_STORE_U32(self + O::kAttributes, attributes);
  REX_STORE_U32(self + O::kFlags, flags);
  REX_STORE_U32(self + O::kDescribedVersion, 0);
}
