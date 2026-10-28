#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <rex/graphics/vulkan/draw_identity_capture.h>

using rex::graphics::vulkan::DrawIdentityCapture;

static DrawIdentityCapture::Record MakeTargetRecord() {
  DrawIdentityCapture::Record r{};
  r.frame = 13098;
  r.submission = 22983;
  r.vertex_shader_hash = 0x79eac49585797037ull;
  r.pixel_shader_hash = 0x401a01fd4e5f8757ull;
  r.vertex_shader_dwords = 126;
  r.pixel_shader_dwords = 687;
  r.primitive_type = 4;
  r.index_count = 192;
  r.source_select = 0;
  r.major_mode = 0;
  r.major_mode_explicit = 0;
  r.index_info_present = 1;
  r.index_format = 0;
  r.index_endian = 1;
  r.index_base = 0x1beb1200;
  r.index_length = 384;
  r.index_buffer_count = 192;
  r.guest_vertex_count = 192;
  r.host_vertex_count = 192;
  r.guest_index_base_processed = 0x1beb1200;
  r.host_index_format = 0;
  r.host_index_endian = 1;
  r.index_buffer_type = 1;
  r.host_vertex_shader_type = 0;
  r.tessellation_mode = 0;
  const uint32_t render_targets[] = {
      12582916, 1, 0, 4, 293667936, 4, 7374659, 66544, 15, 16776978,
      786432, 0, 0, 0, 117507841, 4, 330301458, 524288, 2195462, 524288,
      1087, 274799368, 8};
  for (uint32_t i = 0; i < DrawIdentityCapture::kRenderTargetRegisterCount; ++i)
    r.render_target_registers[i] = render_targets[i];
  r.viewport[0] = 0.0f;
  r.viewport[1] = 0.0f;
  r.viewport[2] = 1120.0f;
  r.viewport[3] = 144.0f;
  r.viewport[4] = 0.5f;
  r.viewport[5] = 0.0f;
  r.scissor[0] = 0;
  r.scissor[1] = 0;
  r.scissor[2] = 1120;
  r.scissor[3] = 144;
  r.vertex_fetch_mask[2] = 0xc0000000u;
  r.vertex_fetch_words[94][0] = 0x1be827c3u;
  r.vertex_fetch_words[94][1] = 0x10000322u;
  r.vertex_fetch_words[95][0] = 0x1be6ee43u;
  r.vertex_fetch_words[95][1] = 0x100003eau;
  r.texture_fetch_mask = 25335;
  const uint32_t texture_words[10][7] = {
      {0, 0x88000002, 0x19720054, 0x007fe3ff, 0x00a80d10, 0x000000c3, 0x19820a18},
      {1, 0x88000002, 0x19720054, 0x007fe3ff, 0x00a80d10, 0x000000c3, 0x19820a18},
      {2, 0x84000002, 0x16c20071, 0x003fe1ff, 0x00a80d10, 0x000000c3, 0x16c60a18},
      {4, 0x00804956, 0x1fab105d, 0x0000003f, 0x00280d11, 0x00000003, 0x00000018},
      {5, 0x02004802, 0x1fab0086, 0x000000ff, 0x00280c14, 0x00000003, 0x00000018},
      {6, 0x88004802, 0x12be1097, 0x007fe3ff, 0x00800d11, 0x00000000, 0x00000218},
      {7, 0x88004802, 0x12fe1097, 0x007fe3ff, 0x00800d11, 0x00000000, 0x00000218},
      {9, 0x80404802, 0x1fc40086, 0x00000000, 0x00800c14, 0x00000000, 0x00000218},
      {13, 0x84000002, 0x1a54207b, 0x001fe1ff, 0x00a80d10, 0x00000003, 0x00000218},
      {14, 0x82000002, 0x164a0052, 0x141fe0ff, 0x00a80d10, 0x00000183, 0x164d0e18},
  };
  for (const auto& entry : texture_words) {
    const uint32_t index = entry[0];
    for (uint32_t word = 0; word < 6; ++word)
      r.texture_fetch_words[index][word] = entry[word + 1];
  }
  return r;
}

static bool WaitForStatus(const std::filesystem::path& path, const char* expected) {
  for (int i = 0; i < 500; ++i) {
    std::ifstream input(path);
    std::string value;
    if (input >> value; value == expected) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

int main() {
  using namespace std::chrono_literals;
  unsetenv("FABLE2_DRAW_IDENTITY_LOG");
  unsetenv("FABLE2_DRAW_IDENTITY_TRIGGER");
  unsetenv("FABLE2_DRAW_IDENTITY_FILTER");
  {
    DrawIdentityCapture disabled;
    disabled.StartFromEnvironment();
    if (disabled.IsActive() || disabled.IsTownSquareFilter() ||
        disabled.BufferedRecordCapacity() != 0) return 10;
  }

  auto record = MakeTargetRecord();
  constexpr uint64_t expected_signature = 0x2cbe39913b642d9eull;
  if (DrawIdentityCapture::ComputeStateSignature(record) != expected_signature) return 11;
  if (!DrawIdentityCapture::MatchesStandardMaterialRecord(record, 0x79eac49585797037ull,
                                                          0x401a01fd4e5f8757ull,
                                                          expected_signature)) return 12;

  const auto directory = std::filesystem::temp_directory_path() /
      ("draw-selector-test-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  if (std::filesystem::exists(directory)) return 13;
  std::filesystem::create_directories(directory);
  const auto output = directory / "capture.jsonl";
  const auto trigger = directory / "arm";
  setenv("FABLE2_DRAW_IDENTITY_LOG", output.c_str(), 1);
  setenv("FABLE2_DRAW_IDENTITY_TRIGGER", trigger.c_str(), 1);
  setenv("FABLE2_DRAW_IDENTITY_WINDOW_MS", "5000", 1);
  setenv("FABLE2_DRAW_IDENTITY_FILTER", "town_square_standard_material_v1", 1);
  setenv("FABLE2_DRAW_IDENTITY_VS_HASH", "79eac49585797037", 1);
  setenv("FABLE2_DRAW_IDENTITY_PS_HASH", "401a01fd4e5f8757", 1);
  setenv("FABLE2_DRAW_IDENTITY_STATE_SIGNATURE", "2cbe39913b642d9e", 1);

  {
    DrawIdentityCapture capture;
    capture.StartFromEnvironment();
    std::ofstream(trigger).put('x');
    for (int i = 0; i < 500 && !capture.IsActive(); ++i)
      std::this_thread::sleep_for(10ms);
    if (!capture.IsActive() || !capture.IsTownSquareFilter()) return 14;
    if (capture.BufferedRecordCapacity() != 1) return 19;
    if (!capture.TargetShaderPairMatches(record.vertex_shader_hash, record.pixel_shader_hash)) return 15;

    auto changed_state = record;
    ++changed_state.render_target_registers[10];
    changed_state.draw_ordinal = capture.ObserveDrawOrdinal();
    capture.RecordDraw(changed_state);
    record.draw_ordinal = capture.ObserveDrawOrdinal();
    capture.RecordDraw(record);
    record.draw_ordinal = capture.ObserveDrawOrdinal();
    capture.RecordDraw(record);  // one-shot mode ignores later matching draws.
    if (capture.IsActive()) return 16;
    if (!WaitForStatus(output.string() + ".status", "complete")) return 17;
  }

  std::ifstream input(output);
  const std::string contents((std::istreambuf_iterator<char>(input)), {});
  if (contents.find("\"schema\":3") == std::string::npos ||
      contents.find("\"status\":\"one_shot_complete\"") == std::string::npos ||
      contents.find("\"records\":1") == std::string::npos ||
      contents.find("\"dropped_records\":0") == std::string::npos ||
      contents.find("\"selector_rejected_records\":1") == std::string::npos ||
      contents.find("\"draw_ordinal\":2") == std::string::npos ||
      contents.find("\"state_signature\":\"2cbe39913b642d9e\"") == std::string::npos) {
    std::cerr << contents;
    return 18;
  }
  std::filesystem::remove_all(directory);
  std::cout << "target selector/one-shot test passed\n";
  return 0;
}
