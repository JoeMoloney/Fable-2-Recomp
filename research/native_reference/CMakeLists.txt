cmake_minimum_required(VERSION 3.20)
project(fable_native_shader_reference LANGUAGES CXX)

# Isolated copied-converter experiment: no game, plugin, codegen or staging.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "Pinned XenosRecomp requires Clang's -fms-extensions")
endif()
find_package(fmt REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PATCHED_XENOS_RECOMPILER "${CMAKE_CURRENT_BINARY_DIR}/local-patch/shader_recompiler.cpp")
add_custom_command(OUTPUT "${PATCHED_XENOS_RECOMPILER}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/apply_1d_patch.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/upstream/xenosrecomp/XenosRecomp/shader_recompiler.cpp"
          "${PATCHED_XENOS_RECOMPILER}"
  DEPENDS apply_1d_patch.py upstream/xenosrecomp/XenosRecomp/shader_recompiler.cpp
  VERBATIM)
add_executable(emit_hlsl emit_hlsl.cpp "${PATCHED_XENOS_RECOMPILER}")
target_include_directories(emit_hlsl PRIVATE upstream/xenosrecomp/XenosRecomp)
target_compile_features(emit_hlsl PRIVATE cxx_std_20)
target_compile_options(emit_hlsl PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/converter_compat.h"
  -UNDEBUG -fno-strict-aliasing -fms-extensions -Wno-switch -Wno-unused-variable
  -Wno-pointer-arith -Wno-microsoft-anon-tag -Wno-null-arithmetic)
target_compile_definitions(emit_hlsl PRIVATE
  FABLE_XENOS_1D_COMMON_PATH="${CMAKE_CURRENT_SOURCE_DIR}/xenos_1d_common.h")
target_link_libraries(emit_hlsl PRIVATE fmt::fmt)

enable_testing()
add_test(NAME xenos_1d_patch_contract
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/test_1d_patch.py")
