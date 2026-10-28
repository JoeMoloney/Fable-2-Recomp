#pragma once

// Experimental, non-destructive proof that project-owned Vulkan code is
// reached from live guest draw processing. Re-record the backend's current
// scissor as an equivalent Vulkan dynamic-state command: no attachment,
// resource, synchronization, or guest state is changed.
#include <rex/graphics/vulkan/deferred_command_buffer.h>

namespace fable2::nativevk {

inline void RecordLiveDrawProbe(
    rex::graphics::vulkan::DeferredCommandBuffer& command_buffer,
    const VkRect2D& active_scissor) {
  command_buffer.CmdVkSetScissor(0, 1, &active_scissor);
}

}  // namespace fable2::nativevk
