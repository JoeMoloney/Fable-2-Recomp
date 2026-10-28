#pragma once

#include "context.h"

// Reusable variable-count descriptor-array helper. Caller owns the descriptor
// pool; Context retires the layout after later-created pipeline layouts.
class VariableDescriptorArray {
 public:
  VariableDescriptorArray(Context& context, VkDescriptorPool pool,
                          VkDescriptorType descriptor_type, std::uint32_t count,
                          VkShaderStageFlags stages)
      : context_(context), descriptor_type_(descriptor_type), count_(count) {
    if (!count_) throw std::runtime_error("Variable descriptor array cannot be empty");

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = descriptor_type_;
    binding.descriptorCount = count_;
    binding.stageFlags = stages;

    const VkDescriptorBindingFlags binding_flags = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flags_info.bindingCount = 1;
    flags_info.pBindingFlags = &binding_flags;
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.pNext = &flags_info;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(context_.device, &layout_info, nullptr, &layout_));
    const auto layout = layout_;
    context_.retire.push_back([device = context_.device, layout] {
      vkDestroyDescriptorSetLayout(device, layout, nullptr);
    });

    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_count_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
    variable_count_info.descriptorSetCount = 1;
    variable_count_info.pDescriptorCounts = &count_;
    VkDescriptorSetAllocateInfo allocate_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate_info.pNext = &variable_count_info;
    allocate_info.descriptorPool = pool;
    allocate_info.descriptorSetCount = 1;
    allocate_info.pSetLayouts = &layout_;
    check(vkAllocateDescriptorSets(context_.device, &allocate_info, &set_));
  }

  VkDescriptorSetLayout layout() const { return layout_; }
  VkDescriptorSet set() const { return set_; }
  std::uint32_t count() const { return count_; }

  void write_sampled_image(std::uint32_t index, VkImageView view,
                           VkImageLayout image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    require_type(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, index);
    VkDescriptorImageInfo image_info{};
    image_info.imageView = view;
    image_info.imageLayout = image_layout;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set_;
    write.dstBinding = 0;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = descriptor_type_;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(context_.device, 1, &write, 0, nullptr);
  }

  void write_sampler(std::uint32_t index, VkSampler sampler) {
    require_type(VK_DESCRIPTOR_TYPE_SAMPLER, index);
    VkDescriptorImageInfo image_info{};
    image_info.sampler = sampler;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set_;
    write.dstBinding = 0;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = descriptor_type_;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(context_.device, 1, &write, 0, nullptr);
  }

 private:
  void require_type(VkDescriptorType expected, std::uint32_t index) const {
    if (descriptor_type_ != expected || index >= count_)
      throw std::out_of_range("Descriptor array write type or index");
  }

  Context& context_;
  VkDescriptorType descriptor_type_;
  std::uint32_t count_;
  VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
  VkDescriptorSet set_ = VK_NULL_HANDLE;
};
