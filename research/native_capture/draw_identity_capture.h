#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/graphics/xenos.h>

namespace rex::graphics::vulkan {

// One-shot metadata observer. It never records Vulkan commands or reads guest
// memory. IssueDraw is the sole producer; a worker arms it from a trigger file
// and writes the completed fixed-size buffer after recording stops.
class DrawIdentityCapture {
 public:
  static constexpr uint32_t kHardMaxRecords = 4096;
  static constexpr uint32_t kVertexFetchCount = 96;
  static constexpr uint32_t kTextureFetchCount = 32;
  static constexpr uint32_t kDescriptorSetCount = 4;
  static constexpr uint32_t kRenderTargetRegisterCount = 23;

  struct Record {
    uint32_t candidate_event = 0;
    uint64_t ordinal = 0;
    uint64_t draw_ordinal = 0;
    uint64_t frame = 0;
    uint64_t submission = 0;
    uint64_t vertex_shader_hash = 0;
    uint64_t pixel_shader_hash = 0;
    uint32_t vertex_shader_dwords = 0;
    uint32_t pixel_shader_dwords = 0;
    uint64_t host_pipeline = 0;
    uint64_t pipeline_layout = 0;
    uint64_t descriptor_sets[kDescriptorSetCount] = {};
    uint32_t descriptor_values_valid = 0;
    uint32_t descriptor_bound_valid = 0;
    uint32_t primitive_type = 0;
    uint32_t index_count = 0;
    uint32_t source_select = 0;
    uint32_t major_mode = 0;
    uint32_t major_mode_explicit = 0;
    uint32_t index_info_present = 0;
    uint32_t index_format = 0;
    uint32_t index_endian = 0;
    uint32_t index_base = 0;
    uint32_t index_length = 0;
    uint32_t index_buffer_count = 0;
    uint32_t guest_vertex_count = 0;
    uint32_t host_vertex_count = 0;
    uint32_t guest_index_base_processed = 0;
    uint32_t host_index_format = 0;
    uint32_t host_index_endian = 0;
    uint32_t index_buffer_type = 0;
    uint32_t host_vertex_shader_type = 0;
    uint32_t tessellation_mode = 0;
    uint32_t render_target_registers[kRenderTargetRegisterCount] = {};
    float viewport[6] = {};
    int32_t scissor[4] = {};
    uint32_t vertex_fetch_mask[3] = {};
    uint32_t texture_fetch_mask = 0;
    uint32_t vertex_fetch_words[kVertexFetchCount][2] = {};
    uint32_t texture_fetch_words[kTextureFetchCount][6] = {};
    uint64_t state_signature = 0;
  };

  DrawIdentityCapture() = default;
  DrawIdentityCapture(const DrawIdentityCapture&) = delete;
  DrawIdentityCapture& operator=(const DrawIdentityCapture&) = delete;
  ~DrawIdentityCapture() { Stop(); }

  void StartFromEnvironment() {
    if (worker_.joinable()) return;
    const char* output = std::getenv("FABLE2_DRAW_IDENTITY_LOG");
    const char* trigger = std::getenv("FABLE2_DRAW_IDENTITY_TRIGGER");
    if (!output || !*output || !trigger || !*trigger) return;

    output_path_ = output;
    trigger_path_ = trigger;
    max_records_ = ParseBounded("FABLE2_DRAW_IDENTITY_MAX_RECORDS", 4096, 1, kHardMaxRecords);
    window_ms_ = ParseBounded("FABLE2_DRAW_IDENTITY_WINDOW_MS", 15000, 250, 30000);
    const char* filter = std::getenv("FABLE2_DRAW_IDENTITY_FILTER");
    filter_all_ = !filter || !*filter || std::string(filter) == "all";
    filter_event349_ = filter && std::string(filter).find("event349") != std::string::npos;
    filter_event362_ = filter && std::string(filter).find("event362") != std::string::npos;
    filter_town_square_ = filter &&
        std::string(filter) == "town_square_standard_material_v1";
    if (filter_town_square_) {
      if (!ParseHex64("FABLE2_DRAW_IDENTITY_VS_HASH", target_vs_hash_) ||
          !ParseHex64("FABLE2_DRAW_IDENTITY_PS_HASH", target_ps_hash_) ||
          !ParseHex64("FABLE2_DRAW_IDENTITY_STATE_SIGNATURE", target_state_signature_)) {
        WriteStatus("disabled_invalid_target_selector");
        output_path_.clear();
        trigger_path_.clear();
        return;
      }
      max_records_ = 1;
      one_shot_ = true;
    }
    if (std::filesystem::exists(output_path_)) {
      WriteStatus("disabled_output_already_exists");
      output_path_.clear();
      trigger_path_.clear();
      return;
    }
    worker_ = std::thread([this] { WorkerMain(); });
  }

  bool IsActive() const { return active_.load(std::memory_order_acquire); }
  bool FilterAll() const { return filter_all_; }
  bool IsTownSquareFilter() const { return filter_town_square_; }
  std::size_t BufferedRecordCapacity() const { return records_.capacity(); }
  bool TargetShaderPairMatches(uint64_t vertex_hash, uint64_t pixel_hash) const {
    return filter_town_square_ && vertex_hash == target_vs_hash_ &&
           pixel_hash == target_ps_hash_;
  }

  bool TargetGeometryPrefilter(uint64_t vertex_hash, uint64_t pixel_hash,
                               uint32_t primitive, uint32_t index_count,
                               bool index_info_present, uint32_t index_format,
                               uint32_t index_endian, uint32_t index_base,
                               uint32_t index_length, uint32_t texture_mask,
                               uint32_t vertex_fetch_94_word0,
                               uint32_t vertex_fetch_94_word1,
                               uint32_t vertex_fetch_95_word0,
                               uint32_t vertex_fetch_95_word1) const {
    return IsActive() && TargetShaderPairMatches(vertex_hash, pixel_hash) &&
           primitive == 4 && index_count == 192 && index_info_present &&
           index_format == 0 && index_endian == 1 && index_base == 0x1BEB1200u &&
           index_length == 384 && texture_mask == 25335u &&
           vertex_fetch_94_word0 == 0x1BE827C3u &&
           vertex_fetch_94_word1 == 0x10000322u &&
           vertex_fetch_95_word0 == 0x1BE6EE43u &&
           vertex_fetch_95_word1 == 0x100003EAu;
  }

  static uint64_t ComputeStateSignature(const Record& record) {
    uint64_t hash = UINT64_C(14695981039346656037);
    auto add_u32 = [&hash](uint32_t value) {
      for (uint32_t shift = 0; shift < 32; shift += 8) {
        hash ^= uint8_t(value >> shift);
        hash *= UINT64_C(1099511628211);
      }
    };
    auto add_u64 = [&add_u32](uint64_t value) {
      add_u32(uint32_t(value));
      add_u32(uint32_t(value >> 32));
    };
    auto add_float = [&add_u32](float value) {
      uint32_t bits = 0;
      std::memcpy(&bits, &value, sizeof(bits));
      add_u32(bits);
    };
    add_u64(record.vertex_shader_hash);
    add_u64(record.pixel_shader_hash);
    const uint32_t draw_fields[] = {
        record.primitive_type, record.index_count, record.source_select,
        record.major_mode, record.major_mode_explicit, record.index_info_present,
        record.index_format, record.index_endian, record.index_base,
        record.index_length, record.index_buffer_count, record.guest_vertex_count,
        record.host_vertex_count, record.guest_index_base_processed,
        record.host_index_format, record.host_index_endian,
        record.index_buffer_type, record.host_vertex_shader_type,
        record.tessellation_mode};
    for (uint32_t value : draw_fields) add_u32(value);
    for (uint32_t value : record.render_target_registers) add_u32(value);
    for (float value : record.viewport) add_float(value);
    for (int32_t value : record.scissor) add_u32(uint32_t(value));
    for (uint32_t value : record.vertex_fetch_mask) add_u32(value);
    for (uint32_t i = 0; i < kVertexFetchCount; ++i) {
      if (!(record.vertex_fetch_mask[i / 32] & (uint32_t(1) << (i % 32)))) continue;
      add_u32(i);
      add_u32(record.vertex_fetch_words[i][0]);
      add_u32(record.vertex_fetch_words[i][1]);
    }
    add_u32(record.texture_fetch_mask);
    for (uint32_t i = 0; i < kTextureFetchCount; ++i) {
      if (!(record.texture_fetch_mask & (uint32_t(1) << i))) continue;
      add_u32(i);
      for (uint32_t word : record.texture_fetch_words[i]) add_u32(word);
    }
    return hash;
  }

  static bool MatchesStandardMaterialRecord(const Record& r, uint64_t vertex_hash,
                                             uint64_t pixel_hash, uint64_t state_signature) {
    return r.vertex_shader_hash == vertex_hash && r.pixel_shader_hash == pixel_hash &&
           r.primitive_type == 4 && r.index_count == 192 && r.index_info_present &&
           r.index_format == 0 && r.index_endian == 1 && r.index_base == 0x1BEB1200u &&
           r.index_length == 384 && r.index_buffer_count == 192 &&
           r.guest_vertex_count == 192 && r.host_vertex_count == 192 &&
           r.host_index_format == 0 && r.host_index_endian == 1 &&
           r.index_buffer_type == 1 && r.host_vertex_shader_type == 0 &&
           r.texture_fetch_mask == 25335u &&
           r.vertex_fetch_words[94][0] == 0x1BE827C3u &&
           r.vertex_fetch_words[94][1] == 0x10000322u &&
           r.vertex_fetch_words[95][0] == 0x1BE6EE43u &&
           r.vertex_fetch_words[95][1] == 0x100003EAu &&
           ComputeStateSignature(r) == state_signature;
  }

  void ObserveFrame(uint64_t frame) {
    if (!IsActive()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_.load(std::memory_order_relaxed)) return;
    if (!have_frame_) { start_frame_ = frame; have_frame_ = true; }
    if (!have_frame_count_ || end_frame_ != frame) ++observed_frame_count_;
    have_frame_count_ = true;
    end_frame_ = frame;
  }

  // Counts every IssueDraw observed while armed, not only selector matches.
  // This separates repeated identical resource/state signatures in one run.
  uint64_t ObserveDrawOrdinal() {
    if (!IsActive()) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_.load(std::memory_order_relaxed)) return 0;
    return ++draw_ordinal_;
  }

  bool GeometryPrefilter(uint32_t primitive, uint32_t index_count, bool index16) {
    if (filter_all_) return true;
    if (primitive != 6 || !index16) return false;
    const bool e349 = filter_event349_ && index_count == 587;
    const bool e362 = filter_event362_ && index_count == 1270;
    if (!(e349 || e362)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_.load(std::memory_order_relaxed)) return false;
    if (e349) ++geometry_matches_349_;
    if (e362) ++geometry_matches_362_;
    return true;
  }

  uint32_t MatchFetch95(uint32_t word0, uint32_t word1) {
    constexpr uint32_t kVertexType = 3;
    if ((word0 & 3u) != kVertexType) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_.load(std::memory_order_relaxed)) return 0;
    if (filter_event349_ && word1 == 0x100044EAu) { ++descriptor_matches_349_; return 349; }
    if (filter_event362_ && word1 == 0x10006DFAu) { ++descriptor_matches_362_; return 362; }
    return 0;
  }

  void RecordDraw(const Record& record) {
    if (!IsActive()) return;
    Record captured = record;
    captured.state_signature = ComputeStateSignature(record);
    if (filter_town_square_ && !MatchesStandardMaterialRecord(
            captured, target_vs_hash_, target_ps_hash_, target_state_signature_)) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (active_.load(std::memory_order_relaxed)) ++selector_rejected_records_;
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_.load(std::memory_order_relaxed)) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - capture_start_ >= std::chrono::milliseconds(window_ms_)) {
      active_.store(false, std::memory_order_release);
      completion_status_ = "window_complete";
      complete_.store(true, std::memory_order_release);
      condition_.notify_one();
      return;
    }
    if (record_count_ >= max_records_) {
      ++dropped_records_;
      active_.store(false, std::memory_order_release);
      completion_status_ = "capacity_reached_truncated";
      complete_.store(true, std::memory_order_release);
      condition_.notify_one();
      return;
    }
    captured.ordinal = ++ordinal_;
    records_.push_back(captured);
    ++record_count_;
    if (one_shot_) {
      active_.store(false, std::memory_order_release);
      completion_status_ = "one_shot_complete";
      complete_.store(true, std::memory_order_release);
      condition_.notify_one();
    }
  }

  void Stop() {
    if (!worker_.joinable()) return;
    stop_.store(true, std::memory_order_release);
    condition_.notify_one();
    worker_.join();
  }

 private:
  static bool ParseHex64(const char* name, uint64_t& value_out) {
    const char* text = std::getenv(name);
    if (!text || !*text) return false;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text, &end, 16);
    if (end == text || *end != '\0') return false;
    value_out = uint64_t(parsed);
    return true;
  }

  static uint32_t ParseBounded(const char* name, uint32_t fallback, uint32_t min,
                               uint32_t max) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    char* end = nullptr;
    unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || *end != '\0') return fallback;
    if (parsed < min) return min;
    if (parsed > max) return max;
    return uint32_t(parsed);
  }

  void WriteStatus(const char* text) const {
    if (output_path_.empty()) return;
    std::ofstream status(output_path_ + ".status", std::ios::out | std::ios::trunc);
    if (status) status << text << '\n';
  }

  void WorkerMain() {
    bool armed = false;
    while (!stop_.load(std::memory_order_acquire)) {
      if (!armed) {
        std::error_code ec;
        if (std::filesystem::exists(trigger_path_, ec) && !ec) {
          std::filesystem::remove(trigger_path_, ec);
          if (!ec) {
            bool allocation_succeeded = false;
            {
              std::lock_guard<std::mutex> lock(mutex_);
              try {
                records_.clear();
                records_.reserve(max_records_);
                record_count_ = 0;
                dropped_records_ = 0;
                ordinal_ = 0;
                draw_ordinal_ = 0;
                selector_rejected_records_ = 0;
                capture_start_ = std::chrono::steady_clock::now();
                active_.store(true, std::memory_order_release);
                allocation_succeeded = true;
              } catch (...) {
                completion_status_ = "record_buffer_allocation_failed";
                complete_.store(true, std::memory_order_release);
              }
            }
            armed = true;
            if (allocation_succeeded) {
              WriteStatus("armed");
            } else {
              WriteStatus("record_buffer_allocation_failed");
              return;
            }
          }
        }
      } else if (active_.load(std::memory_order_acquire) &&
                 std::chrono::steady_clock::now() - capture_start_ >=
                     std::chrono::milliseconds(window_ms_)) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_.exchange(false, std::memory_order_acq_rel)) {
          completion_status_ = "window_complete";
          complete_.store(true, std::memory_order_release);
        }
      }

      if (complete_.load(std::memory_order_acquire)) {
        WriteCapture();
        return;
      }
      std::unique_lock<std::mutex> lock(mutex_);
      condition_.wait_for(lock, std::chrono::milliseconds(25), [this] {
        return stop_.load(std::memory_order_acquire) ||
               complete_.load(std::memory_order_acquire);
      });
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (armed) {
      active_.store(false, std::memory_order_release);
      if (!complete_.load(std::memory_order_relaxed)) {
        completion_status_ = "incomplete_shutdown";
        complete_.store(true, std::memory_order_release);
      }
      WriteCaptureLocked();
    } else {
      WriteStatus("stopped_before_arm");
    }
  }

  void WriteCapture() {
    std::lock_guard<std::mutex> lock(mutex_);
    WriteCaptureLocked();
  }

  void WriteCaptureLocked() {
    if (output_written_) return;
    output_written_ = true;
    const std::string temporary_path = output_path_ + ".tmp";
    std::ofstream out(temporary_path, std::ios::out | std::ios::trunc);
    if (!out) {
      WriteStatus("write_error_open");
      return;
    }
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - capture_start_).count();
    out << "{\"type\":\"header\",\"schema\":3,\"status\":\"" << completion_status_
        << "\",\"records\":" << record_count_ << ",\"dropped_records\":"
        << dropped_records_ << ",\"max_records\":" << max_records_ << ",\"window_ms\":"
        << window_ms_ << ",\"elapsed_ms\":" << elapsed_ms << ",\"start_frame\":";
    if (have_frame_) out << start_frame_; else out << "null";
    out << ",\"end_frame\":";
    if (have_frame_) out << end_frame_; else out << "null";
    out << ",\"observed_frames\":" << observed_frame_count_
        << ",\"selector_rejected_records\":" << selector_rejected_records_
        << ",\"selector\":\""
        << (filter_town_square_ ? "town_square_standard_material_v1" :
            (filter_all_ ? "all" : "event_filter"))
        << "\",\"selector_state_signature\":\"" << std::hex
        << target_state_signature_ << std::dec << "\""
        << ",\"geometry_prefilter_event349\":" << geometry_matches_349_
        << ",\"geometry_prefilter_event362\":" << geometry_matches_362_
        << ",\"descriptor_match_event349\":" << descriptor_matches_349_
        << ",\"descriptor_match_event362\":" << descriptor_matches_362_ << "}\n";
    for (uint32_t n = 0; n < record_count_; ++n) {
      const Record& r = records_[n];
      out << "{\"type\":\"draw\",\"candidate_event\":" << r.candidate_event
          << ",\"ordinal\":" << r.ordinal << ",\"draw_ordinal\":" << r.draw_ordinal
          << ",\"frame\":" << r.frame
          << ",\"submission\":" << r.submission << ",\"vs_hash\":"
          << r.vertex_shader_hash << ",\"ps_hash\":" << r.pixel_shader_hash
          << ",\"state_signature\":\"" << std::hex << r.state_signature << std::dec << "\""
          << ",\"vs_dwords\":" << r.vertex_shader_dwords
          << ",\"ps_dwords\":" << r.pixel_shader_dwords
          << ",\"host_pipeline\":" << r.host_pipeline << ",\"pipeline_layout\":"
          << r.pipeline_layout << ",\"descriptor_values_valid\":"
          << r.descriptor_values_valid << ",\"descriptor_bound_valid\":"
          << r.descriptor_bound_valid << ",\"descriptor_sets\":[";
      for (uint32_t i = 0; i < kDescriptorSetCount; ++i) out << (i ? "," : "") << r.descriptor_sets[i];
      out << "],\"primitive\":" << r.primitive_type << ",\"index_count\":" << r.index_count
          << ",\"source_select\":" << r.source_select << ",\"major_mode\":" << r.major_mode
          << ",\"major_mode_explicit\":" << r.major_mode_explicit
          << ",\"index_info_present\":" << r.index_info_present << ",\"index_format\":"
          << r.index_format << ",\"index_endian\":" << r.index_endian << ",\"index_base\":"
          << r.index_base << ",\"index_length\":" << r.index_length << ",\"index_buffer_count\":"
          << r.index_buffer_count << ",\"guest_vertex_count\":" << r.guest_vertex_count
          << ",\"host_vertex_count\":" << r.host_vertex_count
          << ",\"processed_guest_index_base\":" << r.guest_index_base_processed
          << ",\"host_index_format\":" << r.host_index_format << ",\"host_index_endian\":"
          << r.host_index_endian << ",\"index_buffer_type\":" << r.index_buffer_type
          << ",\"host_vertex_shader_type\":" << r.host_vertex_shader_type
          << ",\"tessellation_mode\":" << r.tessellation_mode << ",\"render_target_registers\":[";
      for (uint32_t i = 0; i < kRenderTargetRegisterCount; ++i)
        out << (i ? "," : "") << r.render_target_registers[i];
      out << "],\"viewport\":[";
      for (uint32_t i = 0; i < 6; ++i) out << (i ? "," : "") << r.viewport[i];
      out << "],\"scissor\":[";
      for (uint32_t i = 0; i < 4; ++i) out << (i ? "," : "") << r.scissor[i];
      out << "],\"vertex_fetch_mask\":[" << r.vertex_fetch_mask[0] << ','
          << r.vertex_fetch_mask[1] << ',' << r.vertex_fetch_mask[2]
          << "],\"vertex_fetches\":[";
      bool first = true;
      for (uint32_t i = 0; i < kVertexFetchCount; ++i) {
        if (!(r.vertex_fetch_mask[i / 32] & (uint32_t(1) << (i % 32)))) continue;
        out << (first ? "" : ",") << "{\"index\":" << i << ",\"words\":["
            << r.vertex_fetch_words[i][0] << ',' << r.vertex_fetch_words[i][1] << "]}";
        first = false;
      }
      out << "],\"texture_fetch_mask\":" << r.texture_fetch_mask
          << ",\"texture_fetches\":[";
      first = true;
      for (uint32_t i = 0; i < kTextureFetchCount; ++i) {
        if (!(r.texture_fetch_mask & (uint32_t(1) << i))) continue;
        out << (first ? "" : ",") << "{\"index\":" << i << ",\"words\":[";
        for (uint32_t j = 0; j < 6; ++j)
          out << (j ? "," : "") << r.texture_fetch_words[i][j];
        out << "]}";
        first = false;
      }
      out << "]}\n";
    }
    out.close();
    if (!out) {
      WriteStatus("write_error_stream");
      return;
    }
    std::error_code ec;
    std::filesystem::rename(temporary_path, output_path_, ec);
    if (ec) {
      WriteStatus("write_error_rename");
      return;
    }
    WriteStatus("complete");
  }

  std::string output_path_;
  std::string trigger_path_;
  uint32_t max_records_ = 2048;
  uint32_t window_ms_ = 3000;
  // Allocate the bounded record store only after the explicit trigger. When
  // capture is disabled/idle, the command processor carries no multi-megabyte
  // inline array.
  std::vector<Record> records_;
  uint32_t record_count_ = 0;
  uint32_t dropped_records_ = 0;
  uint64_t ordinal_ = 0;
  uint64_t draw_ordinal_ = 0;
  std::string completion_status_ = "not_started";
  std::chrono::steady_clock::time_point capture_start_{};
  std::mutex mutex_;
  std::condition_variable condition_;
  std::atomic<bool> active_{false};
  std::atomic<bool> complete_{false};
  std::atomic<bool> stop_{false};
  bool output_written_ = false;
  bool filter_all_ = true;
  bool filter_event349_ = false;
  bool filter_event362_ = false;
  bool filter_town_square_ = false;
  bool one_shot_ = false;
  uint64_t target_vs_hash_ = 0;
  uint64_t target_ps_hash_ = 0;
  uint64_t target_state_signature_ = 0;
  bool have_frame_ = false;
  bool have_frame_count_ = false;
  uint64_t observed_frame_count_ = 0;
  uint64_t start_frame_ = 0;
  uint64_t end_frame_ = 0;
  uint64_t geometry_matches_349_ = 0;
  uint64_t geometry_matches_362_ = 0;
  uint64_t descriptor_matches_349_ = 0;
  uint64_t descriptor_matches_362_ = 0;
  uint64_t selector_rejected_records_ = 0;
  std::thread worker_;
};

}  // namespace rex::graphics::vulkan
