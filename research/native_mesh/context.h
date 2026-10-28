#pragma once
// Reused offline Context, adapted for native mesh shader addresses and rectangular targets.
// Original isolated research implementation. No upstream application code.
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static void check(VkResult r) {
  if (r != VK_SUCCESS) throw std::runtime_error("Vulkan result " + std::to_string(r));
}
static std::vector<uint8_t> read(const fs::path& p) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f) throw std::runtime_error("Cannot read " + p.string());
  auto length = f.tellg();
  if (length < 0 || length > 16 * 1024 * 1024) throw std::runtime_error("Input size limit");
  std::vector<uint8_t> b(static_cast<size_t>(length));
  f.seekg(0); f.read(reinterpret_cast<char*>(b.data()), b.size());
  if (!f) throw std::runtime_error("Short read");
  return b;
}
struct Image { VkImage image; VkImageView view; uint32_t width, height; VkFormat format; };
struct Buffer { VkBuffer buffer; VkDeviceMemory memory; };
struct Context {
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t family = 0;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer command = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  std::vector<std::function<void()>> retire;
  ~Context() {
    if (device) vkDeviceWaitIdle(device);
    for (auto i = retire.rbegin(); i != retire.rend(); ++i) (*i)();
    if (device) vkDestroyDevice(device, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
  }
  uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties m; vkGetPhysicalDeviceMemoryProperties(physical, &m);
    for (uint32_t i = 0; i < m.memoryTypeCount; ++i)
      if ((bits & (1u << i)) && (m.memoryTypes[i].propertyFlags & flags) == flags) return i;
    throw std::runtime_error("Required memory type unavailable");
  }
  void init() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Fable isolated native mesh"; app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo = &app;
    check(vkCreateInstance(&ci, nullptr, &instance));
    uint32_t n = 0; check(vkEnumeratePhysicalDevices(instance, &n, nullptr));
    std::vector<VkPhysicalDevice> devices(n); check(vkEnumeratePhysicalDevices(instance, &n, devices.data()));
    for (auto p : devices) {
      VkPhysicalDeviceProperties candidate; vkGetPhysicalDeviceProperties(p, &candidate);
      const char* requested = std::getenv("NATIVE_VULKAN_DEVICE");
      if (requested && std::string(candidate.deviceName).find(requested) == std::string::npos) continue;
      if (candidate.apiVersion < VK_API_VERSION_1_2) continue;
      VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
      VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; features.pNext=&features12;
      vkGetPhysicalDeviceFeatures2(p,&features);
       if (!features.features.shaderInt64 || !features.features.shaderSampledImageArrayDynamicIndexing ||
           !features.features.depthClamp || !features.features.textureCompressionBC ||
           !features.features.samplerAnisotropy || !features.features.sampleRateShading ||
           !features12.bufferDeviceAddress || !features12.runtimeDescriptorArray ||
           !features12.scalarBlockLayout) continue;
      VkFormatProperties format; vkGetPhysicalDeviceFormatProperties(p, VK_FORMAT_R8G8B8A8_UNORM, &format);
      const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
          VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
      if ((format.optimalTilingFeatures & needed) != needed) continue;
      uint32_t count; vkGetPhysicalDeviceQueueFamilyProperties(p, &count, nullptr);
      std::vector<VkQueueFamilyProperties> queues(count);
      vkGetPhysicalDeviceQueueFamilyProperties(p, &count, queues.data());
      for (uint32_t i = 0; i < count; ++i) if ((queues[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
        physical = p; family = i; break;
      }
      if (physical) break;
    }
    if (!physical) throw std::runtime_error("No compatible graphics device");
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physical, &props);
    std::cout << "device=" << props.deviceName << " vendor=" << props.vendorID
              << " device_id=" << props.deviceID << " driver=" << props.driverVersion
              << " api=" << props.apiVersion << '\n';
    float priority = 1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family; qi.queueCount = 1; qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
     VkPhysicalDeviceFeatures features{}; features.shaderInt64=VK_TRUE; features.shaderSampledImageArrayDynamicIndexing=VK_TRUE;
     features.depthClamp=VK_TRUE; features.textureCompressionBC=VK_TRUE; features.samplerAnisotropy=VK_TRUE;
     features.sampleRateShading=VK_TRUE;
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
     features12.bufferDeviceAddress=VK_TRUE; features12.runtimeDescriptorArray=VK_TRUE; features12.scalarBlockLayout=VK_TRUE;
    di.pEnabledFeatures=&features; di.pNext=&features12;
    check(vkCreateDevice(physical, &di, nullptr, &device)); vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = family; pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(device, &pi, nullptr, &pool));
    retire.push_back([this]{ vkDestroyCommandPool(device, pool, nullptr); });
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device, &ai, &command));
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; check(vkCreateFence(device, &fi, nullptr, &fence));
    retire.push_back([this]{ vkDestroyFence(device, fence, nullptr); });
  }
  Buffer buffer(VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer b{};
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = size; bi.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    check(vkCreateBuffer(device, &bi, nullptr, &b.buffer));
    // Register each allocation immediately, including partial-construction failures.
    auto handle = b.buffer;
    // Memory must outlive its buffer; install memory callback before buffer callback below.
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device, b.buffer, &req);
    VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mi.allocationSize = req.size;
    VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT; mi.pNext=&flags;
    try {
      mi.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      check(vkAllocateMemory(device, &mi, nullptr, &b.memory));
    } catch (...) { vkDestroyBuffer(device, b.buffer, nullptr); throw; }
    auto memory = b.memory;
    retire.push_back([this, memory]{ vkFreeMemory(device, memory, nullptr); });
    retire.push_back([this, handle]{ vkDestroyBuffer(device, handle, nullptr); });
    check(vkBindBufferMemory(device, b.buffer, b.memory, 0));
    return b;
  }
  VkDeviceAddress address(Buffer b) {
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; info.buffer=b.buffer;
    return vkGetBufferDeviceAddress(device,&info);
  }
  Image image(uint32_t width, uint32_t height, VkFormat format=VK_FORMAT_R8G8B8A8_UNORM, bool swizzle=false,
              VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT) {
    Image im{}; im.width=width; im.height=height; im.format=format;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format; ci.extent = {width, height, 1}; ci.mipLevels = 1; ci.arrayLayers = 1;
    ci.samples = samples; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (format==VK_FORMAT_D32_SFLOAT)
      ci.usage=(ci.usage & ~VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    check(vkCreateImage(device, &ci, nullptr, &im.image));
    auto handle = im.image;
    VkMemoryRequirements req; vkGetImageMemoryRequirements(device, im.image, &req);
    VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mi.allocationSize = req.size;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    try {
      mi.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      check(vkAllocateMemory(device, &mi, nullptr, &memory));
    } catch (...) { vkDestroyImage(device, handle, nullptr); throw; }
    retire.push_back([this, memory]{ vkFreeMemory(device, memory, nullptr); });
    retire.push_back([this, handle]{ vkDestroyImage(device, handle, nullptr); });
    check(vkBindImageMemory(device, im.image, memory, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = im.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = ci.format;
    vi.subresourceRange = {format==VK_FORMAT_D32_SFLOAT?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (swizzle) vi.components = {VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE};
    check(vkCreateImageView(device, &vi, nullptr, &im.view)); auto view = im.view;
    retire.push_back([this, view]{ vkDestroyImageView(device, view, nullptr); });
    return im;
  }
  void upload(Buffer b, const void* data, size_t bytes) {
    void* mapped; check(vkMapMemory(device, b.memory, 0, bytes, 0, &mapped));
    std::memcpy(mapped, data, bytes); vkUnmapMemory(device, b.memory);
  }
  void begin() {
    check(vkResetCommandBuffer(command, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &bi));
  }
  void submit() {
    check(vkEndCommandBuffer(command)); check(vkResetFences(device, 1, &fence));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &command;
    check(vkQueueSubmit(queue, 1, &si, fence));
    check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
  }
  void barrier(Image im, VkImageLayout old_layout, VkImageLayout new_layout,
               VkAccessFlags src, VkAccessFlags dst, VkPipelineStageFlags from, VkPipelineStageFlags to) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.image = im.image;
    b.oldLayout = old_layout; b.newLayout = new_layout; b.srcAccessMask = src; b.dstAccessMask = dst;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = {im.format==VK_FORMAT_D32_SFLOAT?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, from, to, 0, 0, nullptr, 0, nullptr, 1, &b);
  }
  VkShaderModule shader(const fs::path& path) {
    auto bytes = read(path);
    if (bytes.empty() || bytes.size() % 4) throw std::runtime_error("Invalid SPIR-V size");
    std::vector<uint32_t> words(bytes.size()/4); std::memcpy(words.data(), bytes.data(), bytes.size());
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize = bytes.size(); ci.pCode = words.data();
    VkShaderModule module; check(vkCreateShaderModule(device, &ci, nullptr, &module));
    retire.push_back([this,module]{ vkDestroyShaderModule(device, module, nullptr); }); return module;
  }
};
