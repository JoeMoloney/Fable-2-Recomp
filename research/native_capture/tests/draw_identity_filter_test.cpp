#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <rex/graphics/vulkan/draw_identity_capture.h>

static bool WaitFor(const std::filesystem::path& p, const char* expected) {
  for (int i = 0; i < 500; ++i) {
    std::ifstream in(p);
    std::string value;
    if (in >> value; value == expected) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}
static bool Contains(const std::string& s, const std::string& needle) {
  return s.find(needle) != std::string::npos;
}
int main() {
  using namespace rex::graphics::vulkan;
  DrawIdentityCapture default_filter;
  if (!default_filter.FilterAll()) return 20;
  const auto dir = std::filesystem::temp_directory_path() /
                   ("draw-identity-filter-test-" + std::to_string(
                       std::chrono::steady_clock::now().time_since_epoch().count()));
  if (std::filesystem::exists(dir)) return 21;
  std::filesystem::create_directories(dir);
  const auto output = dir / "capture.jsonl";
  const auto trigger = dir / "arm";
  setenv("FABLE2_DRAW_IDENTITY_LOG", output.c_str(), 1);
  setenv("FABLE2_DRAW_IDENTITY_TRIGGER", trigger.c_str(), 1);
  setenv("FABLE2_DRAW_IDENTITY_MAX_RECORDS", "2", 1);
  setenv("FABLE2_DRAW_IDENTITY_WINDOW_MS", "5000", 1);
  setenv("FABLE2_DRAW_IDENTITY_FILTER", "event349,event362", 1);
  {
    auto capture = std::make_unique<DrawIdentityCapture>();
    capture->StartFromEnvironment();
    std::ofstream(trigger).put('x');
    for (int i = 0; i < 200 && !capture->IsActive(); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!capture->IsActive()) return 10;
    capture->ObserveFrame(100);
    if (capture->GeometryPrefilter(6, 587, false)) return 11;
    if (capture->GeometryPrefilter(4, 587, true)) return 12;
    if (!capture->GeometryPrefilter(6, 587, true)) return 13;
    if (capture->MatchFetch95(0x1BB52243u, 0x100044EBu) != 0) return 14;
    if (capture->MatchFetch95(0x1BB52243u, 0x100044EAu) != 349) return 15;
    DrawIdentityCapture::Record r;
    r.candidate_event = 349;
    r.frame = 100;
    capture->RecordDraw(r);
    if (!capture->GeometryPrefilter(6, 1270, true)) return 16;
    if (capture->MatchFetch95(0x1BB58683u, 0x10006DFAu) != 362) return 17;
    r.candidate_event = 362;
    r.frame = 102;
    capture->ObserveFrame(102);
    capture->RecordDraw(r);
    r.frame = 103;
    capture->RecordDraw(r);  // third admitted record must increment drops and stop.
    if (!WaitFor(output.string() + ".status", "complete")) return 18;
  }
  std::ifstream in(output);
  const std::string data((std::istreambuf_iterator<char>(in)), {});
  if (!Contains(data, "\"schema\":3") || !Contains(data, "\"records\":2") ||
      !Contains(data, "\"dropped_records\":1") ||
      !Contains(data, "\"geometry_prefilter_event349\":1") ||
      !Contains(data, "\"geometry_prefilter_event362\":1") ||
      !Contains(data, "\"descriptor_match_event349\":1") ||
      !Contains(data, "\"descriptor_match_event362\":1") ||
      !Contains(data, "\"start_frame\":100") || !Contains(data, "\"end_frame\":102") || !Contains(data, "\"observed_frames\":2") ||
      !Contains(data, "\"candidate_event\":349") || !Contains(data, "\"candidate_event\":362")) {
    std::cerr << data;
    return 19;
  }
  std::filesystem::remove_all(dir);
  std::cout << "draw identity filter/capacity test passed\n";
  return 0;
}
