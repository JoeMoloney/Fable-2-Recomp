#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

// The first 288 bytes remain the existing shared-constant contract. The
// Texture1D descriptor lane appends one uint index per sampler register.
struct SharedConstants1D {
  static constexpr std::size_t kLegacyBytes = 288;
  static constexpr std::size_t kTexture1DDescriptorLaneBytes = 16 * sizeof(std::uint32_t);
  static constexpr std::size_t kInScatteringSamplerRegister = 4;
  static constexpr std::size_t kExtinctionSamplerRegister = 5;
  static constexpr std::size_t kSamplerDescriptorLaneOffset = 192;
  static constexpr std::size_t kTexture1DDescriptorLaneOffset = 288;
  static constexpr std::size_t kSharedConstantsBytes = 512;
  static constexpr std::size_t kInScatteringTexture1DOffset = 304;
  static constexpr std::size_t kExtinctionTexture1DOffset = 308;
  static constexpr std::size_t kInScatteringSamplerOffset = 208;
  static constexpr std::size_t kExtinctionSamplerOffset = 212;

  std::array<std::uint32_t, kLegacyBytes / sizeof(std::uint32_t)> legacy_words{};
  std::array<std::uint32_t, 16> texture_1d_descriptor_indices{};
  std::array<std::uint32_t,
      (kSharedConstantsBytes - kLegacyBytes - kTexture1DDescriptorLaneBytes) /
          sizeof(std::uint32_t)> reserved_after_texture_1d{};

  void set_sampler_descriptor_index(std::size_t sampler_register, std::uint32_t index) {
    const std::size_t offset = kSamplerDescriptorLaneOffset + sampler_register * sizeof(index);
    store(offset, index);
  }

  void set_texture_1d_descriptor_index(std::size_t sampler_register, std::uint32_t index) {
    if (sampler_register >= texture_1d_descriptor_indices.size())
      throw std::out_of_range("Texture1D sampler register");
    texture_1d_descriptor_indices[sampler_register] = index;
  }

  std::uint32_t at_byte_offset(std::size_t offset) const {
    if (offset % sizeof(std::uint32_t) || offset >= sizeof(*this))
      throw std::out_of_range("SharedConstants byte offset");
    if (offset < kLegacyBytes) return legacy_words[offset / sizeof(std::uint32_t)];
    if (offset < kLegacyBytes + kTexture1DDescriptorLaneBytes)
      return texture_1d_descriptor_indices[(offset - kTexture1DDescriptorLaneOffset) /
                                           sizeof(std::uint32_t)];
    return reserved_after_texture_1d[(offset - kLegacyBytes - kTexture1DDescriptorLaneBytes) /
                                     sizeof(std::uint32_t)];
  }

 private:
  void store(std::size_t offset, std::uint32_t value) {
    if (offset % sizeof(value) || offset + sizeof(value) > kLegacyBytes)
      throw std::out_of_range("Legacy sampler descriptor offset");
    legacy_words[offset / sizeof(value)] = value;
  }
};

static_assert(offsetof(SharedConstants1D, texture_1d_descriptor_indices) == 288);
static_assert(sizeof(SharedConstants1D) == SharedConstants1D::kSharedConstantsBytes);
static_assert(SharedConstants1D::kSamplerDescriptorLaneOffset + 4 * 4 == 208);
static_assert(SharedConstants1D::kSamplerDescriptorLaneOffset + 5 * 4 == 212);
static_assert(SharedConstants1D::kTexture1DDescriptorLaneOffset + 4 * 4 == 304);
static_assert(SharedConstants1D::kTexture1DDescriptorLaneOffset + 5 * 4 == 308);
