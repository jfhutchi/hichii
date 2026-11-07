// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <span>

#include "common/common_types.h"

namespace Vulkan {

inline constexpr u64 PipelineEvictAfterFrames = 9000;
inline constexpr size_t MinSwitch2PipelineBudget = 3000;

constexpr size_t Switch2PipelineBudgetFromBytes(u64 physical_memory) {
    return std::max<u64>(MinSwitch2PipelineBudget, physical_memory / 8 / (450 * 1024));
}

constexpr bool IsPipelineEvictionCandidate(u64 frame, u64 last_use, bool present, bool current,
                                           bool built) {
    return present && !current && built && frame > PipelineEvictAfterFrames &&
           last_use < frame - PipelineEvictAfterFrames;
}

constexpr size_t PipelineEvictionQuota(bool switch2, size_t cache_size, size_t budget,
                                       size_t eligible) {
    if (!switch2) {
        return eligible;
    }
    return cache_size > budget ? std::min(cache_size - budget, eligible) : 0;
}

/// Picks the `count` oldest candidates. Visit each once, in any order; cutoff ties
/// share only the remaining slots so a newer pipeline never displaces an older one.
class OldestPipelineSelection {
public:
    /// Reorders last-use frames in place.
    OldestPipelineSelection(std::span<u64> last_uses, size_t count) {
        count = std::min(count, last_uses.size());
        if (count == 0) {
            return;
        }
        std::ranges::nth_element(last_uses, last_uses.begin() + (count - 1));
        cutoff = last_uses[count - 1];
        const size_t older =
            std::ranges::count_if(last_uses, [this](u64 last_use) { return last_use < cutoff; });
        equal_remaining = count - older;
    }

    bool Take(u64 last_use) {
        if (last_use < cutoff) {
            return true;
        }
        if (last_use == cutoff && equal_remaining != 0) {
            --equal_remaining;
            return true;
        }
        return false;
    }

private:
    u64 cutoff{};
    size_t equal_remaining{};
};

} // namespace Vulkan
