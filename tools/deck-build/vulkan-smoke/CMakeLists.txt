cmake_minimum_required(VERSION 3.25)
project(fable_vulkan_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h
  PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../../thirdparty"
  NO_DEFAULT_PATH REQUIRED)

add_executable(vulkan_smoke main.cpp)
target_include_directories(vulkan_smoke PRIVATE "${VULKAN_INCLUDE_DIR}")
target_compile_definitions(vulkan_smoke PRIVATE VK_NO_PROTOTYPES=1)
target_compile_options(vulkan_smoke PRIVATE -march=x86-64-v2 -O2 -Wall -Wextra -Wpedantic
  -Wno-missing-field-initializers)
target_link_libraries(vulkan_smoke PRIVATE ${CMAKE_DL_LIBS})
