#include "context.h"
#include "descriptor_array.h"
#include "shared_constants_1d.h"
#include "texture1d_shaders.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kWidth = 4;
constexpr std::size_t kColorBytes = kWidth * 4 * sizeof(float);
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kTargetFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

struct PushConstants {
  std::uint64_t vertex_constants;
  std::uint64_t pixel_constants;
  std::uint64_t shared_constants;
};
static_assert(sizeof(PushConstants) == 24);

VkDescriptorSetLayout placeholder_layout(Context& context, VkDescriptorType type) {
  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = type;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  info.bindingCount = 1;
  info.pBindings = &binding;
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  check(vkCreateDescriptorSetLayout(context.device, &info, nullptr, &layout));
  context.retire.push_back([device = context.device, layout] {
    vkDestroyDescriptorSetLayout(device, layout, nullptr);
  });
  return layout;
}

VkDescriptorSetLayout empty_layout(Context& context) {
  VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  check(vkCreateDescriptorSetLayout(context.device, &info, nullptr, &layout));
  context.retire.push_back([device = context.device, layout] {
    vkDestroyDescriptorSetLayout(device, layout, nullptr);
  });
  return layout;
}

VkShaderModule create_shader(Context& context, const std::uint32_t* words, std::size_t byte_count) {
  if (!words || byte_count < 20 || byte_count % sizeof(std::uint32_t))
    throw std::runtime_error("Invalid embedded SPIR-V module");
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = byte_count;
  info.pCode = words;
  VkShaderModule module = VK_NULL_HANDLE;
  check(vkCreateShaderModule(context.device, &info, nullptr, &module));
  context.retire.push_back([device = context.device, module] {
    vkDestroyShaderModule(device, module, nullptr);
  });
  return module;
}

void upload_1d(Context& context, Image image, const std::uint8_t* data, std::size_t bytes) {
  Buffer staging = context.buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  context.upload(staging, data, bytes);
  context.begin();
  VkImageMemoryBarrier to_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_transfer.image = image.image;
  to_transfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(context.command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                       1, &to_transfer);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {kWidth, 1, 1};
  vkCmdCopyBufferToImage(context.command, staging.buffer, image.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  VkImageMemoryBarrier to_sample{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  to_sample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_sample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  to_sample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_sample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_sample.image = image.image;
  to_sample.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  to_sample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  to_sample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(context.command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &to_sample);
  context.submit();
}

VkSampler make_sampler(Context& context, VkFilter filter) {
  VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  info.magFilter = filter;
  info.minFilter = filter;
  info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.minLod = 0.0f;
  info.maxLod = 0.0f;
  VkSampler sampler = VK_NULL_HANDLE;
  check(vkCreateSampler(context.device, &info, nullptr, &sampler));
  context.retire.push_back([device = context.device, sampler] {
    vkDestroySampler(device, sampler, nullptr);
  });
  return sampler;
}

VkRenderPass make_render_pass(Context& context) {
  VkAttachmentDescription attachment{};
  attachment.format = kTargetFormat;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;
  std::array<VkSubpassDependency, 2> dependencies{};
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  info.attachmentCount = 1;
  info.pAttachments = &attachment;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
  info.pDependencies = dependencies.data();
  VkRenderPass render_pass = VK_NULL_HANDLE;
  check(vkCreateRenderPass(context.device, &info, nullptr, &render_pass));
  context.retire.push_back([device = context.device, render_pass] {
    vkDestroyRenderPass(device, render_pass, nullptr);
  });
  return render_pass;
}

VkPipeline make_pipeline(Context& context, VkRenderPass render_pass,
                         VkPipelineLayout layout) {
  const auto vertex = create_shader(context, texture1d_shader_spirv::vertex.data(),
                                    texture1d_shader_spirv::vertex.size() * sizeof(std::uint32_t));
  const auto fragment = create_shader(context, texture1d_shader_spirv::fragment.data(),
                                      texture1d_shader_spirv::fragment.size() * sizeof(std::uint32_t));
  std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
  stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertex;
  stages[0].pName = "main";
  stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragment;
  stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport viewport{0.0f, 0.0f, static_cast<float>(kWidth), 1.0f, 0.0f, 1.0f};
  VkRect2D scissor{{0, 0}, {kWidth, 1}};
  VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;
  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;
  VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.stageCount = static_cast<std::uint32_t>(stages.size());
  info.pStages = stages.data();
  info.pVertexInputState = &vertex_input;
  info.pInputAssemblyState = &assembly;
  info.pViewportState = &viewport_state;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &multisample;
  info.pColorBlendState = &blend;
  info.layout = layout;
  info.renderPass = render_pass;
  info.subpass = 0;
  VkPipeline pipeline = VK_NULL_HANDLE;
  check(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));
  context.retire.push_back([device = context.device, pipeline] {
    vkDestroyPipeline(device, pipeline, nullptr);
  });
  return pipeline;
}

void run() {
  Context context;
  context.init(true, false);

  VkFormatProperties texture_format{};
  vkGetPhysicalDeviceFormatProperties(context.physical, kTextureFormat, &texture_format);
  const VkFormatFeatureFlags texture_features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  if ((texture_format.optimalTilingFeatures & texture_features) != texture_features)
    throw std::runtime_error("1D texture format lacks sampled/linear/transfer features");
  VkImageFormatProperties image1d_properties{};
  check(vkGetPhysicalDeviceImageFormatProperties(context.physical, kTextureFormat,
      VK_IMAGE_TYPE_1D, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0,
      &image1d_properties));
  VkFormatProperties target_format{};
  vkGetPhysicalDeviceFormatProperties(context.physical, kTargetFormat, &target_format);
  const VkFormatFeatureFlags target_features = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  if ((target_format.optimalTilingFeatures & target_features) != target_features)
    throw std::runtime_error("Float render target format lacks required color/transfer/sample support");

  const std::array<std::uint8_t, 16> extinction_pixels = {
      0, 10, 50, 255, 0, 20, 60, 255, 0, 30, 70, 255, 0, 40, 80, 255};
  const std::array<std::uint8_t, 16> in_scattering_pixels = {
      0, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255};
  const Image extinction = context.image_1d(kWidth, kTextureFormat);
  const Image in_scattering = context.image_1d(kWidth, kTextureFormat);
  upload_1d(context, extinction, extinction_pixels.data(), extinction_pixels.size());
  upload_1d(context, in_scattering, in_scattering_pixels.data(), in_scattering_pixels.size());

  SharedConstants1D shared{};
  shared.set_sampler_descriptor_index(SharedConstants1D::kInScatteringSamplerRegister, 1);
  shared.set_sampler_descriptor_index(SharedConstants1D::kExtinctionSamplerRegister, 0);
  shared.set_texture_1d_descriptor_index(SharedConstants1D::kInScatteringSamplerRegister, 1);
  shared.set_texture_1d_descriptor_index(SharedConstants1D::kExtinctionSamplerRegister, 0);
  if (shared.at_byte_offset(SharedConstants1D::kInScatteringTexture1DOffset) != 1 ||
      shared.at_byte_offset(SharedConstants1D::kExtinctionTexture1DOffset) != 0 ||
      shared.at_byte_offset(SharedConstants1D::kInScatteringSamplerOffset) != 1 ||
      shared.at_byte_offset(SharedConstants1D::kExtinctionSamplerOffset) != 0)
    throw std::runtime_error("SharedConstants descriptor index ABI mismatch");
  Buffer shared_buffer = context.buffer(sizeof(shared), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  context.upload(shared_buffer, &shared, sizeof(shared));

  const VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2},
      {VK_DESCRIPTOR_TYPE_SAMPLER, 2},
  };
  VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.maxSets = 2;
  pool_info.poolSizeCount = 2;
  pool_info.pPoolSizes = pool_sizes;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  check(vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool));
  context.retire.push_back([device = context.device, descriptor_pool] {
    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
  });
  VariableDescriptorArray sampler_heap(context, descriptor_pool, VK_DESCRIPTOR_TYPE_SAMPLER,
                                       2, VK_SHADER_STAGE_FRAGMENT_BIT);
  VariableDescriptorArray texture_1d_heap(context, descriptor_pool, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                          2, VK_SHADER_STAGE_FRAGMENT_BIT);
  texture_1d_heap.write_sampled_image(0, extinction.view);
  texture_1d_heap.write_sampled_image(1, in_scattering.view);
  sampler_heap.write_sampler(0, make_sampler(context, VK_FILTER_NEAREST));
  sampler_heap.write_sampler(1, make_sampler(context, VK_FILTER_LINEAR));

  // Keep the translated Xenos set numbers: 2D=0, 3D=1, Cube=2,
  // Sampler=3, reserved=4, and the 1D extension=5.
  std::array<VkDescriptorSetLayout, 6> set_layouts{};
  set_layouts[0] = placeholder_layout(context, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
  set_layouts[1] = placeholder_layout(context, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
  set_layouts[2] = placeholder_layout(context, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
  set_layouts[3] = sampler_heap.layout();
  set_layouts[4] = empty_layout(context);
  set_layouts[5] = texture_1d_heap.layout();
  VkPushConstantRange push_range{};
  push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  push_range.offset = 0;
  push_range.size = sizeof(PushConstants);
  VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.setLayoutCount = static_cast<std::uint32_t>(set_layouts.size());
  layout_info.pSetLayouts = set_layouts.data();
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push_range;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  check(vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout));
  context.retire.push_back([device = context.device, pipeline_layout] {
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
  });

  const VkRenderPass render_pass = make_render_pass(context);
  const Image target = context.image(kWidth, 1, kTargetFormat);
  VkFramebufferCreateInfo framebuffer_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebuffer_info.renderPass = render_pass;
  framebuffer_info.attachmentCount = 1;
  framebuffer_info.pAttachments = &target.view;
  framebuffer_info.width = kWidth;
  framebuffer_info.height = 1;
  framebuffer_info.layers = 1;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  check(vkCreateFramebuffer(context.device, &framebuffer_info, nullptr, &framebuffer));
  context.retire.push_back([device = context.device, framebuffer] {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
  });
  const VkPipeline pipeline = make_pipeline(context, render_pass, pipeline_layout);

  Buffer readback = context.buffer(kColorBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  context.begin();
  VkClearValue clear{};
  clear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
  VkRenderPassBeginInfo render_begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  render_begin.renderPass = render_pass;
  render_begin.framebuffer = framebuffer;
  render_begin.renderArea = {{0, 0}, {kWidth, 1}};
  render_begin.clearValueCount = 1;
  render_begin.pClearValues = &clear;
  vkCmdBeginRenderPass(context.command, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  const VkDescriptorSet sampler_set = sampler_heap.set();
  vkCmdBindDescriptorSets(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
                          3, 1, &sampler_set, 0, nullptr);
  const VkDescriptorSet texture_set = texture_1d_heap.set();
  vkCmdBindDescriptorSets(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
                          5, 1, &texture_set, 0, nullptr);
  const PushConstants push{0, 0, context.address(shared_buffer)};
  vkCmdPushConstants(context.command, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(push), &push);
  vkCmdDraw(context.command, 3, 1, 0, 0);
  vkCmdEndRenderPass(context.command);

  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {kWidth, 1, 1};
  vkCmdCopyImageToBuffer(context.command, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         readback.buffer, 1, &copy);
  VkBufferMemoryBarrier host_barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  host_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  host_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  host_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  host_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  host_barrier.buffer = readback.buffer;
  host_barrier.offset = 0;
  host_barrier.size = kColorBytes;
  vkCmdPipelineBarrier(context.command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host_barrier,
                       0, nullptr);
  context.submit();

  void* mapped = nullptr;
  check(vkMapMemory(context.device, readback.memory, 0, kColorBytes, 0, &mapped));
  const auto* actual = static_cast<const float*>(mapped);
  std::array<float, kWidth * 4> expected{};
  for (std::uint32_t x = 0; x < kWidth; ++x) {
    expected[x * 4 + 0] = 0.5f;
    expected[x * 4 + 1] = static_cast<float>(10 + x * 10) / 255.0f;
    expected[x * 4 + 2] = static_cast<float>(50 + x * 10) / 255.0f;
    expected[x * 4 + 3] = 1.0f;
  }
  float max_error = 0.0f;
  std::array<float, kWidth * 4> actual_values{};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    actual_values[i] = actual[i];
    const float error = std::fabs(actual_values[i] - expected[i]);
    if (error > max_error) max_error = error;
  }
  vkUnmapMemory(context.device, readback.memory);
  if (max_error > 0.005f)
    throw std::runtime_error("Rendered 1D sample/readback mismatch; max error=" + std::to_string(max_error));

  VkPhysicalDeviceProperties device_properties{};
  vkGetPhysicalDeviceProperties(context.physical, &device_properties);
  VkPhysicalDeviceVulkan12Properties device12_properties{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
  VkPhysicalDeviceProperties2 device_properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  device_properties2.pNext = &device12_properties;
  vkGetPhysicalDeviceProperties2(context.physical, &device_properties2);
  VkPhysicalDeviceVulkan12Features supported12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  supported.pNext = &supported12;
  vkGetPhysicalDeviceFeatures2(context.physical, &supported);
  std::cout << "{\n"
            << "  \"result\": \"pass\",\n"
            << "  \"device\": \"" << device_properties.deviceName << "\",\n"
            << "  \"api_version\": " << device_properties.apiVersion << ",\n"
            << "  \"driver_name\": \"" << device12_properties.driverName << "\",\n"
            << "  \"driver_info\": \"" << device12_properties.driverInfo << "\",\n"
            << "  \"descriptor_sets\": {\"sampler_heap\": 3, \"texture_1d_heap\": 5},\n"
            << "  \"descriptor_bindings\": {\"sampler_heap\": 0, \"texture_1d_heap\": 0},\n"
            << "  \"shared_constants\": {\"size\": " << sizeof(shared)
            << ", \"1d_lane_offset\": " << SharedConstants1D::kTexture1DDescriptorLaneOffset
            << ", \"in_scattering_index_offset\": " << SharedConstants1D::kInScatteringTexture1DOffset
            << ", \"extinction_index_offset\": " << SharedConstants1D::kExtinctionTexture1DOffset
            << ", \"in_scattering_index\": " << shared.at_byte_offset(304)
            << ", \"extinction_index\": " << shared.at_byte_offset(308)
            << ", \"in_scattering_sampler_offset\": " << SharedConstants1D::kInScatteringSamplerOffset
            << ", \"extinction_sampler_offset\": " << SharedConstants1D::kExtinctionSamplerOffset << "},\n"
            << "  \"descriptor_indexing\": " << (supported12.descriptorIndexing ? "true" : "false") << ",\n"
            << "  \"runtime_descriptor_array\": " << (supported12.runtimeDescriptorArray ? "true" : "false") << ",\n"
            << "  \"variable_descriptor_count\": " << (supported12.descriptorBindingVariableDescriptorCount ? "true" : "false") << ",\n"
            << "  \"descriptor_binding_partially_bound\": " << (supported12.descriptorBindingPartiallyBound ? "true" : "false") << ",\n"
            << "  \"sampled_image_array_nonuniform_indexing\": " << (supported12.shaderSampledImageArrayNonUniformIndexing ? "true" : "false") << ",\n"
            << "  \"sampled_image_update_after_bind\": " << (supported12.descriptorBindingSampledImageUpdateAfterBind ? "true" : "false") << ",\n"
            << "  \"sampled_image_array_dynamic_indexing\": "
            << (supported.features.shaderSampledImageArrayDynamicIndexing ? "true" : "false") << ",\n"
            << "  \"1d_texture_format\": \"R8G8B8A8_UNORM\",\n"
            << "  \"extent\": [4, 1],\n"
            << "  \"actual_rgba_f32\": [";
  for (std::size_t i = 0; i < actual_values.size(); ++i) {
    if (i) std::cout << ", ";
    std::cout << actual_values[i];
  }
  std::cout << "],\n  \"expected_rgba_f32\": [";
  for (std::size_t i = 0; i < expected.size(); ++i) {
    if (i) std::cout << ", ";
    std::cout << expected[i];
  }
  std::cout << "],\n"
            << "  \"linear_midpoint_red\": " << actual_values[0] << ",\n"
            << "  \"max_readback_error\": " << max_error << ",\n"
            << "  \"vertex_spirv_sha256\": \"" << texture1d_shader_spirv::vertex_sha256 << "\",\n"
            << "  \"fragment_spirv_sha256\": \"" << texture1d_shader_spirv::fragment_sha256 << "\"\n"
            << "}\n";
}

}  // namespace

int main() {
  try {
    run();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "native_texture_1d: " << error.what() << '\n';
    return 1;
  }
}
