#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer SharedConstantsRef {
  uint words[];
};

layout(push_constant, std430) uniform XenosPushConstants {
  uint64_t VertexShaderConstants;
  uint64_t PixelShaderConstants;
  uint64_t SharedConstants;
} g_PushConstants;

// Preserve the existing sampler heap at set 3 and add Texture1D at set 5.
// The target PS's 1D fetches use shared byte offsets 304/308 and sampler
// descriptor indices at offsets 208/212 respectively.
layout(set = 3, binding = 0) uniform sampler g_SamplerDescriptorHeap[];
layout(set = 5, binding = 0) uniform texture1D g_Texture1DDescriptorHeap[];

layout(location = 0) out vec4 color;

void main() {
  SharedConstantsRef constants_ref = SharedConstantsRef(g_PushConstants.SharedConstants);
  const uint inScatteringTextureIndex = constants_ref.words[304 / 4];
  const uint extinctionTextureIndex = constants_ref.words[308 / 4];
  const uint inScatteringSamplerIndex = constants_ref.words[208 / 4];
  const uint extinctionSamplerIndex = constants_ref.words[212 / 4];

  // Linear sample halfway between texels 0 and 1 in the InScattering image.
  const vec4 inScattering = textureLod(
      sampler1D(g_Texture1DDescriptorHeap[inScatteringTextureIndex],
                g_SamplerDescriptorHeap[inScatteringSamplerIndex]),
      0.25, 0.0);

  // Sample one Extinction texel center per output pixel with the nearest sampler.
  const float coordinate = gl_FragCoord.x / 4.0;
  const vec4 extinction = textureLod(
      sampler1D(g_Texture1DDescriptorHeap[extinctionTextureIndex],
                g_SamplerDescriptorHeap[extinctionSamplerIndex]),
      coordinate, 0.0);
  color = vec4(inScattering.r, extinction.g, extinction.b, extinction.a);
}
