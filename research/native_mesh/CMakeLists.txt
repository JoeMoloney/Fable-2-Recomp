cmake_minimum_required(VERSION 3.20)
project(fable_native_mesh LANGUAGES CXX)

# Standalone research renderer. Use the repository's Vulkan C headers and the
# system loader; this target is intentionally not linked into the game/plugin.
set(Vulkan_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty")
find_package(Vulkan REQUIRED)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

foreach(target IN ITEMS native_mesh native_event362)
  if(target STREQUAL "native_mesh")
    set(source render_mesh.cpp)
  else()
    set(source render_event362.cpp)
  endif()
  add_executable(${target} ${source})
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
  target_link_libraries(${target} PRIVATE Vulkan::Vulkan)
endforeach()

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/flat.frag.spv"
  COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.2
          "${CMAKE_CURRENT_SOURCE_DIR}/flat.frag" -o "${CMAKE_CURRENT_BINARY_DIR}/flat.frag.spv"
  DEPENDS flat.frag VERBATIM)
add_custom_target(flat_shader ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/flat.frag.spv")
add_dependencies(native_mesh flat_shader)
