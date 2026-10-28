#include <chrono>
#include <thread>
#include <rex/graphics/vulkan/draw_identity_capture.h>

int main() {
  using namespace rex::graphics::vulkan;
  {
    DrawIdentityCapture capture;
    capture.StartFromEnvironment();
    for (int i = 0; i < 200 && !capture.IsActive(); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!capture.IsActive()) return 10;
    for (uint32_t i = 0; i < 4; ++i) {
      DrawIdentityCapture::Record record;
      record.frame = 77;
      record.submission = 91;
      record.vertex_shader_hash = 0x1122334455667788ull;
      record.index_count = i + 1;
      capture.RecordDraw(record);
    }
    for (int i = 0; i < 200; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      if (!capture.IsActive()) break;
    }
  }
  return 0;
}
