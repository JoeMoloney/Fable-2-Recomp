#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kWordCount = 16;
constexpr std::uint32_t kPattern = 0x6d2b79f5;
constexpr VkDeviceSize kBufferBytes = kWordCount * sizeof(std::uint32_t);

void check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                             std::to_string(static_cast<int>(result)));
  }
}

std::string quote(const char* value) {
  std::string result;
  for (const unsigned char c : std::string(value)) {
    if (c == '"' || c == '\\') {
      result.push_back('\\');
      result.push_back(static_cast<char>(c));
    } else if (c == '\n') {
      result += "\\n";
    } else if (c >= 0x20) {
      result.push_back(static_cast<char>(c));
    }
  }
  return result;
}

std::string version(std::uint32_t value) {
  return std::to_string(VK_API_VERSION_MAJOR(value)) + "." +
         std::to_string(VK_API_VERSION_MINOR(value)) + "." +
         std::to_string(VK_API_VERSION_PATCH(value));
}

struct Api {
  void* library = nullptr;
  PFN_vkGetInstanceProcAddr gipa = nullptr;
  PFN_vkGetDeviceProcAddr gdpa = nullptr;
  PFN_vkCreateInstance create_instance = nullptr;
  PFN_vkEnumerateInstanceVersion enumerate_instance_version = nullptr;
  PFN_vkDestroyInstance destroy_instance = nullptr;
  PFN_vkEnumeratePhysicalDevices enumerate_physical_devices = nullptr;
  PFN_vkGetPhysicalDeviceProperties get_properties = nullptr;
  PFN_vkGetPhysicalDeviceProperties2 get_properties2 = nullptr;
  PFN_vkGetPhysicalDeviceFeatures2 get_features2 = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties get_queue_families = nullptr;
  PFN_vkEnumerateDeviceExtensionProperties enumerate_extensions = nullptr;
  PFN_vkGetPhysicalDeviceFormatProperties get_format_properties = nullptr;
  PFN_vkGetPhysicalDeviceImageFormatProperties get_image_format_properties = nullptr;
  PFN_vkGetPhysicalDeviceMemoryProperties get_memory_properties = nullptr;
  PFN_vkCreateDevice create_device = nullptr;
  PFN_vkDestroyDevice destroy_device = nullptr;
  PFN_vkDeviceWaitIdle device_wait_idle = nullptr;
  PFN_vkGetDeviceQueue get_device_queue = nullptr;
  PFN_vkCreateBuffer create_buffer = nullptr;
  PFN_vkDestroyBuffer destroy_buffer = nullptr;
  PFN_vkGetBufferMemoryRequirements get_buffer_requirements = nullptr;
  PFN_vkAllocateMemory allocate_memory = nullptr;
  PFN_vkFreeMemory free_memory = nullptr;
  PFN_vkBindBufferMemory bind_buffer_memory = nullptr;
  PFN_vkMapMemory map_memory = nullptr;
  PFN_vkUnmapMemory unmap_memory = nullptr;
  PFN_vkInvalidateMappedMemoryRanges invalidate_ranges = nullptr;
  PFN_vkCreateCommandPool create_command_pool = nullptr;
  PFN_vkDestroyCommandPool destroy_command_pool = nullptr;
  PFN_vkAllocateCommandBuffers allocate_command_buffers = nullptr;
  PFN_vkBeginCommandBuffer begin_command_buffer = nullptr;
  PFN_vkEndCommandBuffer end_command_buffer = nullptr;
  PFN_vkCmdFillBuffer cmd_fill_buffer = nullptr;
  PFN_vkCmdPipelineBarrier cmd_pipeline_barrier = nullptr;
  PFN_vkCreateFence create_fence = nullptr;
  PFN_vkDestroyFence destroy_fence = nullptr;
  PFN_vkQueueSubmit queue_submit = nullptr;
  PFN_vkWaitForFences wait_for_fences = nullptr;

  ~Api() {
    if (library) dlclose(library);
  }

  template <typename T>
  T global(const char* name) const {
    auto function = reinterpret_cast<T>(gipa(VK_NULL_HANDLE, name));
    if (!function) throw std::runtime_error(std::string("Missing Vulkan function: ") + name);
    return function;
  }

  template <typename T>
  T instance(VkInstance handle, const char* name) const {
    auto function = reinterpret_cast<T>(gipa(handle, name));
    if (!function) throw std::runtime_error(std::string("Missing Vulkan function: ") + name);
    return function;
  }

  template <typename T>
  T device(VkDevice handle, const char* name) const {
    auto function = reinterpret_cast<T>(gdpa(handle, name));
    if (!function) throw std::runtime_error(std::string("Missing Vulkan function: ") + name);
    return function;
  }

  void load_global() {
    library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library) throw std::runtime_error(std::string("dlopen libvulkan.so.1: ") + dlerror());
    gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
    if (!gipa) throw std::runtime_error("Vulkan loader lacks vkGetInstanceProcAddr");
    create_instance = global<PFN_vkCreateInstance>("vkCreateInstance");
    enumerate_instance_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        gipa(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
  }

  void load_instance(VkInstance handle) {
#define LOAD_I(member, type, name) member = instance<type>(handle, name)
    LOAD_I(destroy_instance, PFN_vkDestroyInstance, "vkDestroyInstance");
    LOAD_I(enumerate_physical_devices, PFN_vkEnumeratePhysicalDevices, "vkEnumeratePhysicalDevices");
    LOAD_I(get_properties, PFN_vkGetPhysicalDeviceProperties, "vkGetPhysicalDeviceProperties");
    LOAD_I(get_properties2, PFN_vkGetPhysicalDeviceProperties2, "vkGetPhysicalDeviceProperties2");
    LOAD_I(get_features2, PFN_vkGetPhysicalDeviceFeatures2, "vkGetPhysicalDeviceFeatures2");
    LOAD_I(get_queue_families, PFN_vkGetPhysicalDeviceQueueFamilyProperties, "vkGetPhysicalDeviceQueueFamilyProperties");
    LOAD_I(enumerate_extensions, PFN_vkEnumerateDeviceExtensionProperties, "vkEnumerateDeviceExtensionProperties");
    LOAD_I(get_format_properties, PFN_vkGetPhysicalDeviceFormatProperties, "vkGetPhysicalDeviceFormatProperties");
    LOAD_I(get_image_format_properties, PFN_vkGetPhysicalDeviceImageFormatProperties, "vkGetPhysicalDeviceImageFormatProperties");
    LOAD_I(get_memory_properties, PFN_vkGetPhysicalDeviceMemoryProperties, "vkGetPhysicalDeviceMemoryProperties");
    create_device = instance<PFN_vkCreateDevice>(handle, "vkCreateDevice");
    gdpa = instance<PFN_vkGetDeviceProcAddr>(handle, "vkGetDeviceProcAddr");
#undef LOAD_I
  }

  void load_device(VkDevice handle) {
#define LOAD_D(member, type, name) member = device<type>(handle, name)
    LOAD_D(destroy_device, PFN_vkDestroyDevice, "vkDestroyDevice");
    LOAD_D(device_wait_idle, PFN_vkDeviceWaitIdle, "vkDeviceWaitIdle");
    LOAD_D(get_device_queue, PFN_vkGetDeviceQueue, "vkGetDeviceQueue");
    LOAD_D(create_buffer, PFN_vkCreateBuffer, "vkCreateBuffer");
    LOAD_D(destroy_buffer, PFN_vkDestroyBuffer, "vkDestroyBuffer");
    LOAD_D(get_buffer_requirements, PFN_vkGetBufferMemoryRequirements, "vkGetBufferMemoryRequirements");
    LOAD_D(allocate_memory, PFN_vkAllocateMemory, "vkAllocateMemory");
    LOAD_D(free_memory, PFN_vkFreeMemory, "vkFreeMemory");
    LOAD_D(bind_buffer_memory, PFN_vkBindBufferMemory, "vkBindBufferMemory");
    LOAD_D(map_memory, PFN_vkMapMemory, "vkMapMemory");
    LOAD_D(unmap_memory, PFN_vkUnmapMemory, "vkUnmapMemory");
    LOAD_D(invalidate_ranges, PFN_vkInvalidateMappedMemoryRanges, "vkInvalidateMappedMemoryRanges");
    LOAD_D(create_command_pool, PFN_vkCreateCommandPool, "vkCreateCommandPool");
    LOAD_D(destroy_command_pool, PFN_vkDestroyCommandPool, "vkDestroyCommandPool");
    LOAD_D(allocate_command_buffers, PFN_vkAllocateCommandBuffers, "vkAllocateCommandBuffers");
    LOAD_D(begin_command_buffer, PFN_vkBeginCommandBuffer, "vkBeginCommandBuffer");
    LOAD_D(end_command_buffer, PFN_vkEndCommandBuffer, "vkEndCommandBuffer");
    LOAD_D(cmd_fill_buffer, PFN_vkCmdFillBuffer, "vkCmdFillBuffer");
    LOAD_D(cmd_pipeline_barrier, PFN_vkCmdPipelineBarrier, "vkCmdPipelineBarrier");
    LOAD_D(create_fence, PFN_vkCreateFence, "vkCreateFence");
    LOAD_D(destroy_fence, PFN_vkDestroyFence, "vkDestroyFence");
    LOAD_D(queue_submit, PFN_vkQueueSubmit, "vkQueueSubmit");
    LOAD_D(wait_for_fences, PFN_vkWaitForFences, "vkWaitForFences");
#undef LOAD_D
  }
};

struct Resources {
  Api api;
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;

  ~Resources() {
    if (device) api.device_wait_idle(device);
    if (fence && device) api.destroy_fence(device, fence, nullptr);
    if (command_pool && device) api.destroy_command_pool(device, command_pool, nullptr);
    if (buffer && device) api.destroy_buffer(device, buffer, nullptr);
    if (memory && device) api.free_memory(device, memory, nullptr);
    if (device) api.destroy_device(device, nullptr);
    if (instance) api.destroy_instance(instance, nullptr);
  }
};

bool extension_present(const std::vector<VkExtensionProperties>& extensions, const char* name) {
  for (const auto& item : extensions) {
    if (std::strcmp(item.extensionName, name) == 0) return true;
  }
  return false;
}

std::uint32_t transfer_queue(Api& api, VkPhysicalDevice physical) {
  std::uint32_t count = 0;
  api.get_queue_families(physical, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  api.get_queue_families(physical, &count, families.data());
  for (std::uint32_t i = 0; i < count; ++i) {
    if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_TRANSFER_BIT)) return i;
  }
  throw std::runtime_error("No transfer-capable queue family");
}

std::uint32_t host_visible_type(Api& api, VkPhysicalDevice physical,
                               std::uint32_t compatible_bits,
                               VkMemoryPropertyFlags& selected_flags) {
  VkPhysicalDeviceMemoryProperties properties{};
  api.get_memory_properties(physical, &properties);
  std::uint32_t fallback = UINT32_MAX;
  VkMemoryPropertyFlags fallback_flags = 0;
  for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
    if (!(compatible_bits & (1u << i))) continue;
    const auto flags = properties.memoryTypes[i].propertyFlags;
    if (!(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
    if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
      selected_flags = flags;
      return i;
    }
    if (fallback == UINT32_MAX) {
      fallback = i;
      fallback_flags = flags;
    }
  }
  if (fallback != UINT32_MAX) {
    selected_flags = fallback_flags;
    return fallback;
  }
  throw std::runtime_error("No host-visible readback memory type");
}

std::string execute_gpu_fill(Resources& resources, VkPhysicalDevice physical,
                            std::uint32_t family) {
  Api& api = resources.api;
  VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  buffer_info.size = kBufferBytes;
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(api.create_buffer(resources.device, &buffer_info, nullptr, &resources.buffer), "vkCreateBuffer");

  VkMemoryRequirements requirements{};
  api.get_buffer_requirements(resources.device, resources.buffer, &requirements);
  VkMemoryPropertyFlags memory_flags = 0;
  VkMemoryAllocateInfo memory_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  memory_info.memoryTypeIndex = host_visible_type(api, physical, requirements.memoryTypeBits, memory_flags);
  memory_info.allocationSize = requirements.size;
  check(api.allocate_memory(resources.device, &memory_info, nullptr, &resources.memory), "vkAllocateMemory");
  check(api.bind_buffer_memory(resources.device, resources.buffer, resources.memory, 0), "vkBindBufferMemory");

  VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  pool_info.queueFamilyIndex = family;
  check(api.create_command_pool(resources.device, &pool_info, nullptr, &resources.command_pool), "vkCreateCommandPool");
  VkCommandBuffer command = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  command_info.commandPool = resources.command_pool;
  command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_info.commandBufferCount = 1;
  check(api.allocate_command_buffers(resources.device, &command_info, &command), "vkAllocateCommandBuffers");

  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(api.begin_command_buffer(command, &begin), "vkBeginCommandBuffer");
  api.cmd_fill_buffer(command, resources.buffer, 0, kBufferBytes, kPattern);
  VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = resources.buffer;
  barrier.offset = 0;
  barrier.size = kBufferBytes;
  api.cmd_pipeline_barrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                           0, 0, nullptr, 1, &barrier, 0, nullptr);
  check(api.end_command_buffer(command), "vkEndCommandBuffer");

  VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  check(api.create_fence(resources.device, &fence_info, nullptr, &resources.fence), "vkCreateFence");
  VkQueue queue = VK_NULL_HANDLE;
  api.get_device_queue(resources.device, family, 0, &queue);
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command;
  check(api.queue_submit(queue, 1, &submit, resources.fence), "vkQueueSubmit");
  check(api.wait_for_fences(resources.device, 1, &resources.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

  void* mapped = nullptr;
  check(api.map_memory(resources.device, resources.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
  if (!(memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = resources.memory;
    range.offset = 0;
    range.size = VK_WHOLE_SIZE;
    check(api.invalidate_ranges(resources.device, 1, &range), "vkInvalidateMappedMemoryRanges");
  }
  const auto* words = static_cast<const std::uint32_t*>(mapped);
  for (std::uint32_t i = 0; i < kWordCount; ++i) {
    if (words[i] != kPattern) {
      api.unmap_memory(resources.device, resources.memory);
      throw std::runtime_error("GPU fill/readback mismatch at word " + std::to_string(i));
    }
  }
  api.unmap_memory(resources.device, resources.memory);
  return (memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
             ? "host_coherent_memory"
             : "noncoherent_memory_invalidated";
}

void run() {
  Resources r;
  Api& a = r.api;
  a.library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!a.library) throw std::runtime_error(std::string("dlopen libvulkan.so.1: ") + dlerror());
  a.gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(a.library, "vkGetInstanceProcAddr"));
  if (!a.gipa) throw std::runtime_error("Vulkan loader lacks vkGetInstanceProcAddr");
  a.create_instance = a.global<PFN_vkCreateInstance>("vkCreateInstance");
  a.enumerate_instance_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
      a.gipa(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));

  std::uint32_t loader_version = VK_API_VERSION_1_0;
  if (a.enumerate_instance_version) check(a.enumerate_instance_version(&loader_version), "vkEnumerateInstanceVersion");
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "Fable Deck Vulkan smoke";
  app.applicationVersion = 1;
  app.pEngineName = "standalone-radv-compatibility-test";
  app.engineVersion = 1;
  app.apiVersion = loader_version < VK_API_VERSION_1_2 ? loader_version : VK_API_VERSION_1_2;
  VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  instance_info.pApplicationInfo = &app;
  check(a.create_instance(&instance_info, nullptr, &r.instance), "vkCreateInstance");
  a.load_instance(r.instance);

  std::uint32_t device_count = 0;
  check(a.enumerate_physical_devices(r.instance, &device_count, nullptr), "vkEnumeratePhysicalDevices(count)");
  if (!device_count) throw std::runtime_error("No Vulkan physical devices");
  std::vector<VkPhysicalDevice> devices(device_count);
  check(a.enumerate_physical_devices(r.instance, &device_count, devices.data()), "vkEnumeratePhysicalDevices(list)");
  const VkPhysicalDevice physical = devices.front();
  VkPhysicalDeviceProperties properties{};
  a.get_properties(physical, &properties);
  if (properties.apiVersion < VK_API_VERSION_1_2) throw std::runtime_error("Physical device lacks Vulkan 1.2");
  const auto family = transfer_queue(a, physical);

  VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  f2.pNext = &f12;
  a.get_features2(physical, &f2);
  VkPhysicalDeviceVulkan12Properties p12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
  VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  p2.pNext = &p12;
  a.get_properties2(physical, &p2);

  std::uint32_t extension_count = 0;
  check(a.enumerate_extensions(physical, nullptr, &extension_count, nullptr), "vkEnumerateDeviceExtensionProperties(count)");
  std::vector<VkExtensionProperties> extensions(extension_count);
  check(a.enumerate_extensions(physical, nullptr, &extension_count, extensions.data()), "vkEnumerateDeviceExtensionProperties(list)");

  VkFormatProperties format{};
  a.get_format_properties(physical, VK_FORMAT_R8G8B8A8_UNORM, &format);
  VkImageFormatProperties image_1d{};
  const VkResult image_1d_result = a.get_image_format_properties(
      physical, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_1D, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_SAMPLED_BIT, 0, &image_1d);

  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queue_info.queueFamilyIndex = family;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;
  VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  check(a.create_device(physical, &device_info, nullptr, &r.device), "vkCreateDevice");
  a.load_device(r.device);
  const std::string memory_mode = execute_gpu_fill(r, physical, family);

  std::cout << "{\n"
            << "  \"schema\": 1,\n"
            << "  \"result\": \"pass\",\n"
            << "  \"loader_api\": \"" << version(loader_version) << "\",\n"
            << "  \"device_api\": \"" << version(properties.apiVersion) << "\",\n"
            << "  \"device_name\": \"" << quote(properties.deviceName) << "\",\n"
            << "  \"vendor_id\": " << properties.vendorID << ",\n"
            << "  \"device_id\": " << properties.deviceID << ",\n"
            << "  \"driver_id\": " << static_cast<std::uint32_t>(p12.driverID) << ",\n"
            << "  \"driver_name\": \"" << quote(p12.driverName) << "\",\n"
            << "  \"driver_info\": \"" << quote(p12.driverInfo) << "\",\n"
            << "  \"gpu_operation\": \"vkCmdFillBuffer\",\n"
            << "  \"synchronization\": \"transfer-to-host barrier + fence wait\",\n"
            << "  \"readback\": \"" << memory_mode << ",16_words_verified\",\n"
            << "  \"sampled_1d_image_r8g8b8a8\": " << (image_1d_result == VK_SUCCESS ? "true" : "false") << ",\n"
            << "  \"r8g8b8a8_sampled_format\": "
            << ((format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ? "true" : "false") << ",\n"
            << "  \"r8g8b8a8_linear_filter\": "
            << ((format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ? "true" : "false") << ",\n"
            << "  \"descriptor_indexing_extension_advertised\": "
            << (extension_present(extensions, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) ? "true" : "false") << ",\n"
            << "  \"features\": {\n"
            << "    \"shaderSampledImageArrayDynamicIndexing\": " << (f2.features.shaderSampledImageArrayDynamicIndexing ? "true" : "false") << ",\n"
            << "    \"descriptorIndexing\": " << (f12.descriptorIndexing ? "true" : "false") << ",\n"
            << "    \"runtimeDescriptorArray\": " << (f12.runtimeDescriptorArray ? "true" : "false") << ",\n"
            << "    \"descriptorBindingVariableDescriptorCount\": " << (f12.descriptorBindingVariableDescriptorCount ? "true" : "false") << ",\n"
            << "    \"descriptorBindingPartiallyBound\": " << (f12.descriptorBindingPartiallyBound ? "true" : "false") << ",\n"
            << "    \"shaderSampledImageArrayNonUniformIndexing\": " << (f12.shaderSampledImageArrayNonUniformIndexing ? "true" : "false") << ",\n"
            << "    \"descriptorBindingSampledImageUpdateAfterBind\": " << (f12.descriptorBindingSampledImageUpdateAfterBind ? "true" : "false") << ",\n"
            << "    \"descriptorBindingUpdateUnusedWhilePending\": " << (f12.descriptorBindingUpdateUnusedWhilePending ? "true" : "false") << "\n"
            << "  },\n"
            << "  \"limits\": {\n"
            << "    \"maxPerStageDescriptorUpdateAfterBindSampledImages\": " << p12.maxPerStageDescriptorUpdateAfterBindSampledImages << ",\n"
            << "    \"maxDescriptorSetUpdateAfterBindSampledImages\": " << p12.maxDescriptorSetUpdateAfterBindSampledImages << "\n"
            << "  }\n"
            << "}\n";
}

}  // namespace

int main() {
  try {
    run();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "vulkan_smoke: " << error.what() << '\n';
    return 1;
  }
}
