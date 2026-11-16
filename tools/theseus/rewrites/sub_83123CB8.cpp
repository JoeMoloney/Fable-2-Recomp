// Readable rewrite of sub_83123CB8 (guest 0x83123CB8, 36 PPC instructions, 25 callers).
// Gate: python3 tools/theseus/gate.py sub_83123CB8
//
// r3 = owner, r4 = node with two intrusive links: (+32 link, +36 tag word) and,
// when owner flag 0x80000 (word +40) is set, (+24 link, +28 tag word). A set
// bit 0 in the tag word means "no element". Otherwise the element is
// (link & ~1) - 40 (64-bit); returns it when it is 0 or its code (bits 7..13 of
// element+8) is 84..90 but not 88, else 0.
// Name: unknown.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kOwnerFlags = 40, kAltLinks = 0x80000;
constexpr uint32_t kLinkOffset = 40;

bool IsWantedCode(uint32_t code) {
  return code >= 84 && code <= 90 && code != 88;
}
}  // namespace

extern "C" REX_FUNC(rewrite_sub_83123CB8) {
  const uint32_t node = ctx.r4.u32;
  const bool alt = REX_LOAD_U32(ctx.r3.u32 + kOwnerFlags) & kAltLinks;
  const uint32_t link_off = alt ? 24 : 32;
  if (REX_LOAD_U32(node + link_off + 4) & 1) {
    ctx.r3.u64 = 0;
    return;
  }
  const int64_t elem = int64_t(REX_LOAD_U32(node + link_off) & ~1u) - kLinkOffset;
  if (uint32_t(elem) != 0) {
    const uint32_t code = (REX_LOAD_U32(uint32_t(elem) + 8) >> 7) & 0x7F;
    if (!IsWantedCode(code)) {
      ctx.r3.u64 = 0;
      return;
    }
  }
  ctx.r3.s64 = elem;
}
