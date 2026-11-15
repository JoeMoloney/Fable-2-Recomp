cmake_minimum_required(VERSION 3.25)
project(localization_probe LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
if(NOT DEFINED SDK)
  message(FATAL_ERROR "Pass -DSDK=<vendored SDK>")
endif()
set(CODEGEN "${SDK}/src/codegen")
add_executable(emitter
  emitter.cpp adapters.cpp
  "${CODEGEN}/config.cpp"
  "${CODEGEN}/manifest.cpp"
  "${CODEGEN}/function_graph.cpp"
  "${CODEGEN}/binary_view.cpp"
  "${CODEGEN}/instruction_dispatch.cpp"
  "${CODEGEN}/ppc/disasm.cpp"
  "${SDK}/src/system/module.cpp"
  "${SDK}/src/system/guest_path.cpp"
  "${SDK}/src/core/utf8.cpp"
  "${SDK}/thirdparty/disasm/disasm.c"
  "${SDK}/thirdparty/disasm/ppc-dis.c"
)
foreach(builder context comparison arithmetic logical control_flow floating_point memory system vector)
  target_sources(emitter PRIVATE "${CODEGEN}/builders/${builder}.cpp")
endforeach()
target_compile_definitions(emitter PRIVATE FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL)
target_compile_options(emitter PRIVATE -O1 -ffunction-sections -fdata-sections -msse4.1)
target_link_options(emitter PRIVATE -Wl,--gc-sections)
target_include_directories(emitter PRIVATE
  "${SDK}/include" "${CODEGEN}" "${CODEGEN}/builders"
  "${SDK}/thirdparty/disasm" "${SDK}/thirdparty/fmt/include"
  "${SDK}/thirdparty/spdlog/include" "${SDK}/thirdparty/simde"
  "${SDK}/thirdparty/tomlplusplus/include"
  "${SDK}/thirdparty/utfcpp/source"
)
