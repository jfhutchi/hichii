// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>

#include "common/common_types.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"

namespace Vulkan {

inline u32 PackFragmentColorOutputMask(std::span<const u8, 8> components) {
    u32 result{};
    for (size_t rt = 0; rt < components.size(); ++rt) {
        result |= u32{components[rt]} << (rt * 4);
    }
    return result;
}

inline u32 FragmentColorWriteMask(u32 guest_mask, u32 output_mask, size_t render_target) {
    return guest_mask & ((output_mask >> (render_target * 4)) & 0xF);
}

class FragmentColorMaskState {
public:
    struct UpdateResult {
        bool blending{};
        bool color_mask{};
    };

    UpdateResult Update(u32 output_mask, StateTracker& state_tracker) {
        const bool output_changed = output_mask != last_output_mask;
        if (!state_tracker.TouchBlending() && !output_changed) {
            return {};
        }
        const bool color_mask = state_tracker.TouchColorMask() || output_changed;
        if (color_mask) {
            last_output_mask = output_mask;
        }
        return {true, color_mask};
    }

private:
    u32 last_output_mask{};
};

} // namespace Vulkan
