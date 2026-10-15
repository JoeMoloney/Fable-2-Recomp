// fable_2 - Direct mouse-to-camera look (shared host/guest state)
//
// Separates the mouse from the keyboard->gamepad mapping. Instead of writing
// mouse movement into the synthetic pad's right stick (which the game then
// integrates at a fixed stick rate), the host input driver accumulates the raw
// cursor displacement here (src/input/keyboard_gamepad.h) and a mid-asm hook in
// the active camera's per-frame update (fable2_hook_direct_mouse_look in
// src/core/fable2_hooks.cpp) drains it once per frame and applies it directly
// to the camera orientation. The result is a desktop-FPS style "move the mouse
// that far, turn the camera that far" feel with no rate cap.
//
// Threading: the input driver (host/SDK input poll) only ever ADDS to the
// accumulators; the guest camera hook only ever EXCHANGES them to zero. A delta
// can be split across two consecutive drains by a poll that lands between them,
// which is sub-perceptual and self-corrects.
#pragma once

#include <cmath>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/system/xmemory.h>  // rex::memory::Memory (guest address -> host pointer)

namespace fable2::mouselook {

// Accumulated raw cursor displacement (host pixels) since the last drain.
// X = right is positive, Y = down is positive (screen coordinates); the hook
// applies the sign/scale from fable2_config.
inline std::atomic<int32_t>& dx_acc() {
  static std::atomic<int32_t> v{0};
  return v;
}
inline std::atomic<int32_t>& dy_acc() {
  static std::atomic<int32_t> v{0};
  return v;
}

// Host input driver: add this poll's cursor displacement.
inline void add_delta(int32_t dx, int32_t dy) {
  dx_acc().fetch_add(dx, std::memory_order_relaxed);
  dy_acc().fetch_add(dy, std::memory_order_relaxed);
}

// Guest camera hook: consume and reset the accumulated displacement.
inline void take_delta(int32_t& out_dx, int32_t& out_dy) {
  out_dx = dx_acc().exchange(0, std::memory_order_relaxed);
  out_dy = dy_acc().exchange(0, std::memory_order_relaxed);
}

// Guest memory, set once at image load (Fable2App::OnPostLoadXexImage) so the
// camera hook can translate the camera object's guest address to a host
// pointer. Null until set; the hook no-ops while it is null.
inline std::atomic<rex::memory::Memory*>& memory_ref() {
  static std::atomic<rex::memory::Memory*> m{nullptr};
  return m;
}
inline void set_memory(rex::memory::Memory* mem) { memory_ref().store(mem, std::memory_order_release); }

// --- Big-endian guest float access (matches REX_LOAD_U32 / REX_STORE_U32) ----
inline float read_guest_float(rex::memory::Memory* mem, uint32_t guest_addr) {
  const uint32_t* p = mem->TranslateVirtual<const uint32_t*>(guest_addr);
  uint32_t bits = __builtin_bswap32(*p);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}
inline void write_guest_float(rex::memory::Memory* mem, uint32_t guest_addr, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  uint32_t* p = mem->TranslateVirtual<uint32_t*>(guest_addr);
  *p = __builtin_bswap32(bits);
}

// --- Big-endian guest quaternion (4 floats) access ---------------------------
// The active camera's orientation target is a 4-float vector (x, y, z, w) at
// camera+144 that CECCameraUpdateFrame lerps into the rendered view.
struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
};

inline Quat read_guest_quat(rex::memory::Memory* mem, uint32_t guest_addr) {
  Quat q;
  q.x = read_guest_float(mem, guest_addr + 0);
  q.y = read_guest_float(mem, guest_addr + 4);
  q.z = read_guest_float(mem, guest_addr + 8);
  q.w = read_guest_float(mem, guest_addr + 12);
  return q;
}
inline void write_guest_quat(rex::memory::Memory* mem, uint32_t guest_addr, const Quat& q) {
  write_guest_float(mem, guest_addr + 0, q.x);
  write_guest_float(mem, guest_addr + 4, q.y);
  write_guest_float(mem, guest_addr + 8, q.z);
  write_guest_float(mem, guest_addr + 12, q.w);
}

// Quaternion multiply (a * b).
inline Quat quat_mul(const Quat& a, const Quat& b) {
  return Quat{
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// Rotate a guest quaternion in place by a yaw (around the up axis, (0,1,0)) and
// a pitch (around the right axis, (1,0,0)), applied in the camera's local frame
// (post-multiply), then renormalized. This turns the camera without adding roll.
inline void rotate_guest_quat(rex::memory::Memory* mem, uint32_t guest_addr, float yaw, float pitch) {
  Quat q = read_guest_quat(mem, guest_addr);
  float sy = std::sin(yaw * 0.5f), cy = std::cos(yaw * 0.5f);
  Quat yaw_q{0.0f, sy, 0.0f, cy};
  float sp = std::sin(pitch * 0.5f), cp = std::cos(pitch * 0.5f);
  Quat pitch_q{sp, 0.0f, 0.0f, cp};
  Quat n = quat_mul(q, quat_mul(yaw_q, pitch_q));
  float m = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z + n.w * n.w);
  if (m > 1e-6f) { n.x /= m; n.y /= m; n.z /= m; n.w /= m; }
  write_guest_quat(mem, guest_addr, n);
}

// --- Persistent look offset --------------------------------------------------
// The game's hero-follow logic recomputes the camera orientation from scratch
// every frame, so a per-frame mouse rotation springs back to the hero the moment
// the cursor stops. Instead we accumulate a running look offset (radians) and
// re-apply it to the freshly-reset hero-relative orientation each frame, so the
// camera stays where it is aimed. Only the guest camera hook touches these.
inline float& cum_yaw() { static float v = 0.0f; return v; }
inline float& cum_pitch() { static float v = 0.0f; return v; }
inline void reset_cumulative() { cum_yaw() = 0.0f; cum_pitch() = 0.0f; }

}  // namespace fable2::mouselook
