cmake_minimum_required(VERSION 3.25)
project(fable_native_texture_1d LANGUAGES CXX)

set(Vulkan_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty")
find_package(Vulkan REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)
find_program(SPIRV_VAL spirv-val REQUIRED)

set(SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(VERT_SPV "${SHADER_DIR}/texture1d.vert.spv")
set(FRAG_SPV "${SHADER_DIR}/texture1d.frag.spv")
set(EMBEDDED_SPV "${CMAKE_CURRENT_BINARY_DIR}/texture1d_shaders.hpp")
add_custom_command(OUTPUT "${VERT_SPV}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_DIR}"
  COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.2 -S vert
          "${CMAKE_CURRENT_SOURCE_DIR}/texture1d.vert" -o "${VERT_SPV}"
  COMMAND "${SPIRV_VAL}" --target-env vulkan1.2 "${VERT_SPV}"
  DEPENDS texture1d.vert VERBATIM)
add_custom_command(OUTPUT "${FRAG_SPV}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_DIR}"
  COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.2 -S frag
          "${CMAKE_CURRENT_SOURCE_DIR}/texture1d.frag" -o "${FRAG_SPV}"
  COMMAND "${SPIRV_VAL}" --target-env vulkan1.2 "${FRAG_SPV}"
  DEPENDS texture1d.frag VERBATIM)
add_custom_command(OUTPUT "${EMBEDDED_SPV}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/deck-build/vulkan-smoke/embed_spirv.py"
          "${EMBEDDED_SPV}" "${VERT_SPV}" "${FRAG_SPV}"
  DEPENDS "${VERT_SPV}" "${FRAG_SPV}"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/deck-build/vulkan-smoke/embed_spirv.py"
  VERBATIM)

add_executable(native_texture_1d main.cpp "${EMBEDDED_SPV}")
target_include_directories(native_texture_1d PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../native_mesh")
target_compile_features(native_texture_1d PRIVATE cxx_std_17)
target_compile_options(native_texture_1d PRIVATE -march=x86-64-v2 -O2 -Wall -Wextra -Wpedantic
  -Wno-missing-field-initializers)
target_link_libraries(native_texture_1d PRIVATE Vulkan::Vulkan)
