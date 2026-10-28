#pragma once

#include <string_view>

#include <rex/graphics/vulkan/deferred_command_buffer.h>

namespace fable2::nativevk {

// A deliberately tiny, device-independent shader pair: the vertex shader
// derives three positions from gl_VertexIndex; the fragment shader writes a
// solid magenta marker. The pipeline targets ReXGlue's final guest-output
// render pass, after Xenos rendering and gamma processing.
inline constexpr std::string_view kTriangleVertexShader = R"glsl(
#version 450
void main() {
  const vec2 p[3] = vec2[3](
      vec2(-0.96, -0.96),
      vec2(-0.68, -0.96),
      vec2(-0.96, -0.68));
  gl_Position = vec4(p[gl_VertexIndex], 0.0, 1.0);
}
)glsl";

inline constexpr std::string_view kTriangleFragmentShader = R"glsl(
#version 450
layout(location = 0) out vec4 out_color;
void main() {
  out_color = vec4(1.0, 0.0, 1.0, 1.0);
}
)glsl";

inline void RecordTriangleDraw(
    rex::graphics::vulkan::DeferredCommandBuffer& command_buffer) {
  command_buffer.CmdVkDraw(3, 1, 0, 0);
}

}  // namespace fable2::nativevk
