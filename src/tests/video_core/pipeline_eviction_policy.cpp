// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "common/container/unordered_map.h"
#include "video_core/renderer_vulkan/pipeline_eviction_policy.h"

TEST_CASE("Switch 2 retains stale pipelines within its budget", "[video_core][pipeline_eviction]") {
    CHECK(Vulkan::PipelineEvictionQuota(true, 2999, 3000, 20) == 0);
    CHECK(Vulkan::PipelineEvictionQuota(true, 3000, 3000, 20) == 0);
}

TEST_CASE("Pipeline eviction keeps the strict stale age floor", "[video_core][pipeline_eviction]") {
    CHECK_FALSE(Vulkan::IsPipelineEvictionCandidate(0, 0, true, false, true));
    CHECK_FALSE(Vulkan::IsPipelineEvictionCandidate(8999, 0, true, false, true));
    CHECK_FALSE(Vulkan::IsPipelineEvictionCandidate(9000, 0, true, false, true));
    CHECK(Vulkan::IsPipelineEvictionCandidate(9001, 0, true, false, true));
    CHECK_FALSE(Vulkan::IsPipelineEvictionCandidate(9001, 1, true, false, true));
    CHECK(Vulkan::PipelineEvictionQuota(false, 3000, 3000, 20) == 20);
}

TEST_CASE("Switch 2 pipeline budget follows physical RAM with a boot-limit floor",
          "[video_core][pipeline_eviction]") {
    constexpr u64 gib = u64{1} << 30;
    CHECK(Vulkan::Switch2PipelineBudgetFromBytes(0) == 3000);
    CHECK(Vulkan::Switch2PipelineBudgetFromBytes(1) == 3000);
    CHECK(Vulkan::Switch2PipelineBudgetFromBytes(8 * gib) == 3000);
    CHECK(Vulkan::Switch2PipelineBudgetFromBytes(16 * gib) == 4660);
    CHECK(Vulkan::Switch2PipelineBudgetFromBytes(32 * gib) == 9320);
    constexpr u64 boundary = u64{3001} * 8 * 450 * 1024;
    CHECK(Vulkan::Switch2PipelineBudgetFromBytes(boundary - 1) == 3000);
    CHECK(Vulkan::Switch2PipelineBudgetFromBytes(boundary) == 3001);
    CHECK(Vulkan::Switch2PipelineBudgetFromBytes(boundary + 1) == 3001);
    CHECK(Vulkan::Switch2PipelineBudgetFromBytes(std::numeric_limits<u64>::max()) ==
          5003999585967ULL);
}

TEST_CASE("Pipeline eviction quota caps Switch 2 excess but preserves Switch 1",
          "[video_core][pipeline_eviction]") {
    CHECK(Vulkan::PipelineEvictionQuota(true, 3001, 3000, 20) == 1);
    CHECK(Vulkan::PipelineEvictionQuota(true, 3100, 3000, 20) == 20);
    CHECK(Vulkan::PipelineEvictionQuota(true, 3010, 3000, 20) == 10);
    CHECK(Vulkan::PipelineEvictionQuota(true, 3100, 3000, 0) == 0);
    CHECK(Vulkan::PipelineEvictionQuota(true, 0, 0, 0) == 0);
    CHECK(Vulkan::PipelineEvictionQuota(false, 20, 3000, 20) == 20);
    CHECK(Vulkan::PipelineEvictionQuota(false, 3100, 3000, 20) == 20);
}

TEST_CASE("Pipeline eviction protects null current and building pipelines",
          "[video_core][pipeline_eviction]") {
    CHECK_FALSE(Vulkan::IsPipelineEvictionCandidate(20000, 0, false, false, true));
    CHECK_FALSE(Vulkan::IsPipelineEvictionCandidate(20000, 0, true, true, true));
    CHECK_FALSE(Vulkan::IsPipelineEvictionCandidate(20000, 0, true, false, false));
    CHECK(Vulkan::IsPipelineEvictionCandidate(20000, 0, true, false, true));
}

TEST_CASE("Pipeline eviction age arithmetic handles maximum frames",
          "[video_core][pipeline_eviction]") {
    constexpr u64 maximum = std::numeric_limits<u64>::max();
    CHECK(Vulkan::IsPipelineEvictionCandidate(maximum, maximum - 9001, true, false, true));
    for (const u64 last_use : {maximum - 9000, maximum - 1, maximum}) {
        CHECK_FALSE(Vulkan::IsPipelineEvictionCandidate(maximum, last_use, true, false, true));
    }
}

namespace {

void CheckOldest(std::span<const u64> construction, std::span<const u64> traversal, size_t count) {
    std::vector<u64> candidates(construction.begin(), construction.end());
    Vulkan::OldestPipelineSelection selection{candidates, count};
    std::vector<u64> evicted;
    for (const u64 last_use : traversal) {
        if (selection.Take(last_use)) {
            evicted.push_back(last_use);
        }
    }
    std::vector<u64> expected(construction.begin(), construction.end());
    std::ranges::sort(expected);
    expected.resize(std::min(count, expected.size()));
    std::ranges::sort(evicted);
    CHECK(evicted == expected);
}

void CheckEveryOrder(std::vector<u64> last_uses) {
    std::ranges::sort(last_uses);
    auto construction = last_uses;
    do {
        auto traversal = last_uses;
        do {
            for (size_t count = 0; count <= last_uses.size() + 1; ++count) {
                CAPTURE(construction, traversal, count);
                CheckOldest(construction, traversal, count);
            }
        } while (std::ranges::next_permutation(traversal).found);
    } while (std::ranges::next_permutation(construction).found);
}

} // namespace

TEST_CASE("Pipeline eviction preserves oldest ordering across cutoff ties in every order",
          "[video_core][pipeline_eviction]") {
    CheckEveryOrder({100, 200, 200});
    CheckEveryOrder({300, 100, 200, 100});
}

TEST_CASE("Pipeline eviction selection handles empty equal and maximum frames",
          "[video_core][pipeline_eviction]") {
    CheckEveryOrder({});
    CheckEveryOrder({0, 0, 0, 0});
    constexpr u64 maximum = std::numeric_limits<u64>::max();
    CheckEveryOrder({maximum, maximum, maximum - 1});
}

TEST_CASE("Pipeline retention applies production policy to mixed lookup entries",
          "[video_core][pipeline_eviction]") {
    struct Entry {
        u64 last_use;
        bool present = true;
        bool current = false;
        bool built = true;
    };
    const std::array entries{
        Entry{200},   Entry{0, false}, Entry{0, true, true}, Entry{0, true, false, false},
        Entry{11000}, Entry{100},      Entry{200},           Entry{19999},
    };
    const auto eligible = [](const Entry& entry) {
        return Vulkan::IsPipelineEvictionCandidate(20000, entry.last_use, entry.present,
                                                   entry.current, entry.built);
    };
    std::vector<u64> last_uses;
    for (const auto& entry : entries) {
        if (eligible(entry)) {
            last_uses.push_back(entry.last_use);
        }
    }
    REQUIRE(last_uses.size() == 3);
    for (const bool switch2 : {false, true}) {
        const auto quota =
            Vulkan::PipelineEvictionQuota(switch2, entries.size(), 6, last_uses.size());
        Vulkan::OldestPipelineSelection selection{last_uses, quota};
        std::vector<u64> retired;
        for (const auto& entry : entries) {
            if (eligible(entry) && selection.Take(entry.last_use)) {
                retired.push_back(entry.last_use);
            }
        }
        std::ranges::sort(retired);
        CHECK(retired == (switch2 ? std::vector<u64>{100, 200} : std::vector<u64>{100, 200, 200}));
    }
}

TEST_CASE("Pipeline eviction freezes identities before asynchronous build completion",
          "[video_core][pipeline_eviction][pipeline_eviction_snapshot]") {
    struct Entry {
        u64 last_use;
        bool built;
    };
    // The middle entry becomes eligible after the snapshot. Its age must never
    // replace either collected candidate, including at or below the cutoff.
    for (const u64 completed_age : {150ULL, 200ULL, 50ULL}) {
        for (const size_t quota : {size_t{1}, size_t{2}}) {
            CAPTURE(completed_age, quota);
            Common::unordered_map<int, Entry> entries{
                {0, {200, true}}, {1, {completed_age, false}}, {2, {100, true}}};
            std::vector<Vulkan::PipelineEvictionCandidate<decltype(entries)::iterator>> candidates;
            for (const int id : {0, 1, 2}) {
                const auto it = entries.find(id);
                const auto& entry = it->second;
                if (Vulkan::IsPipelineEvictionCandidate(20000, entry.last_use, true, false,
                                                        entry.built)) {
                    candidates.push_back({it, entry.last_use});
                }
            }
            REQUIRE(candidates.size() == 2);
            entries.at(1).built = true;
            // Captured last-use frames also remain the selection input.
            entries.at(0).last_use = 0;
            Vulkan::SelectOldestPipelineCandidates(candidates, quota);
            REQUIRE(candidates.size() == quota);
            std::vector<int> selected;
            for (const auto& candidate : candidates) {
                selected.push_back(candidate.entry->first);
                entries.erase(candidate.entry);
            }
            std::ranges::sort(selected);
            CHECK(selected == (quota == 1 ? std::vector<int>{2} : std::vector<int>{0, 2}));
            CHECK(entries.contains(1));
        }
    }
}
