#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Structural Vulkan retirement checks; these do not execute GPU completion.

Reuse the comment/string-aware method parser used by fragment-mask wiring tests.
Mutation cases alter actual method sources in memory, never the checkout.
"""

from pathlib import Path
import unittest

from test_fragment_color_mask_wiring import code_only, closing, function


VULKAN = Path(__file__).resolve().parents[2] / "src/video_core/renderer_vulkan"


def compact(source):
    return "".join(source.split())


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def check_retirement(source, graphics):
    source = code_only(source)
    tick = compact(function(source, "PipelineCache::TickFrame"))
    guard = "while(!evicted_pipelines.empty()&&scheduler.IsFree(evicted_pipelines.front().first)){"
    require(guard in tick, "Retired ownership must wait for the front submission")
    opening = tick.index(guard) + len(guard) - 1
    end = closing(tick, opening, "{", "}")
    require(tick[opening + 1:end] == "evicted_pipelines.pop_front();",
            "Only the guarded loop can release retired ownership")
    require("evicted_pipelines.pop_front" not in tick[:opening] + tick[end + 1:],
            "Unguarded retirement release")
    require("++frame_count;" in tick and "constexpru64EvictCheckFrames=600;" in tick and
            "if(frame_count%EvictCheckFrames==0){EvictUnusedPipelines();}" in tick,
            "Preserve frame advancement and periodic eviction")

    body = compact(function(source, "PipelineCache::EvictUnusedPipelines"))
    require(body.startswith("if(device.IsMoltenVK()){return;}"), "MoltenVK must retain pipelines")
    require("constu64tick=scheduler.CurrentTick();" in body, "Use the current retirement tick")
    ownership = ("if(used_this_session.erase(pipeline)!=0){"
                 "used_and_evicted.insert(it->first.Hash());}"
                 "evicted_pipelines.emplace_back(tick,std::move(it->second));"
                 "graphics_cache.erase(it);++evicted;")
    require(ownership in body, "Keep bookkeeping, move ownership, then erase lookup")
    cleanup = ("if(evicted==0){return;}for(auto&[key,pipeline]:graphics_cache){"
               "if(pipeline){pipeline->ClearTransitions();}}")
    require(cleanup in body and body.index(cleanup) > body.index(ownership),
            "Clear remaining nonnull transitions after retirement")
    transitions = compact(function(code_only(graphics).replace(" noexcept", ""), "ClearTransitions"))
    require(transitions == "transition_keys.clear();transitions.clear();",
            "Both raw transition vectors must be cleared")


def check_policy(source):
    source = code_only(source)
    body = compact(function(source, "PipelineCache::EvictUnusedPipelines"))
    require("if(frame_count<=EvictAfterFrames){return;}" in body, "Retain frame floor guard")
    predicate = ("returnpipeline&&IsPipelineEvictionCandidate(frame_count,pipeline->LastUse(),true,"
                 "pipeline==current_pipeline,pipeline->IsBuilt());")
    require(predicate in body, "Production eligibility must short-circuit null before dereferencing")
    require("if(evictable(pipeline.get())){candidates.push_back({it,pipeline->LastUse()});}" in body,
            "Collect only eligible candidates")
    require("PipelineEvictionQuota(switch2,graphics_cache.size(),budget,candidates.size())" in body,
            "Quota must count every lookup entry and only eligible candidates")
    require("if(switch2){SelectOldestPipelineCandidates(candidates,max_evicted);}" in body,
            "Use oldest selection on the captured candidate snapshot")
    retirement = "for(constauto&candidate:candidates){autoit=candidate.entry;"
    require(retirement in body, "Retire only captured candidate identities")
    opening = body.index(retirement) + retirement.index("{")
    retired = body[opening + 1:closing(body, opening, "{", "}")]
    require("evictable(" not in retired and "LastUse(" not in retired and "IsBuilt(" not in retired,
            "Do not read changing eligibility or age during snapshot retirement")
    require(body.count("evictable(") == 1 and body.count("candidates.push_back(") == 1,
            "Only the original collection may add candidates")
    budget = compact(function(source, "Switch2PipelineBudget"))
    require("staticconstsize_tbudget=Switch2PipelineBudgetFromBytes("
            "Common::GetMemInfo().TotalPhysicalMemory);returnbudget;" == budget,
            "Cache the budget derived from physical RAM")


class PipelineEvictionWiring(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (VULKAN / "vk_pipeline_cache.cpp").read_text(encoding="utf-8")
        cls.graphics = (VULKAN / "vk_graphics_pipeline.h").read_text(encoding="utf-8")

    def test_retirement_lifetime_wiring(self):
        check_retirement(self.source, self.graphics)

    def test_production_policy_wiring(self):
        check_policy(self.source)

    def test_retirement_uses_frozen_candidate_identities(self):
        body = compact(function(code_only(self.source), "PipelineCache::EvictUnusedPipelines"))
        require("for(constauto&candidate:candidates){autoit=candidate.entry;" in body,
                "Retirement must traverse captured candidate identities, not rescan live build flags")

    def test_actual_retirement_mutations_are_rejected(self):
        mutations = {
            "missing completion": (" && scheduler.IsFree(evicted_pipelines.front().first)", ""),
            "negated completion": ("&& scheduler.IsFree", "&& !scheduler.IsFree"),
            "unconditional release": ("    ++frame_count;", "    evicted_pipelines.pop_front();\n    ++frame_count;"),
            "wrong tick": ("const u64 tick = scheduler.CurrentTick();", "const u64 tick = 0;"),
            "missing ownership": ("std::move(it->second)", "nullptr"),
            "erase before move": ("evicted_pipelines.emplace_back(tick, std::move(it->second));",
                                  "it = graphics_cache.erase(it);\n        evicted_pipelines.emplace_back(tick, std::move(it->second));"),
            "missing transitions": ("pipeline->ClearTransitions();", ""),
            "missing MoltenVK": ("if (device.IsMoltenVK())", "if (false)"),
        }
        source = code_only(self.source)
        for name, (old, new) in mutations.items():
            with self.subTest(name=name):
                method = "TickFrame" if name in {
                    "missing completion", "negated completion", "unconditional release"
                } else "EvictUnusedPipelines"
                body = function(source, "PipelineCache::" + method)
                self.assertIn(old, body)
                mutation = source.replace(body, body.replace(old, new, 1), 1)
                # Original text in comments, strings, and unrelated methods is not evidence.
                decoys = ('\n/* ' + old + ' */\nconst char* decoy = R"decoy(' +
                          old + ')decoy";\nvoid Unrelated() { ' + old + ' }')
                with self.assertRaises(AssertionError):
                    check_retirement(mutation + decoys, self.graphics)

    def test_actual_transition_vector_mutations_are_rejected(self):
        for call in ("transition_keys.clear();", "transitions.clear();"):
            with self.subTest(call=call), self.assertRaises(AssertionError):
                check_retirement(self.source, self.graphics.replace(call, "", 1))

    def test_actual_policy_mutations_are_rejected(self):
        mutations = {
            "null dereference": ("pipeline &&", ""),
            "different collection": ("if (evictable(pipeline.get()))", "if (pipeline)"),
            "live rescan": ("for (const auto& candidate : candidates) {\n        auto it = candidate.entry;",
                            "for (auto it = graphics_cache.begin(); it != graphics_cache.end(); ++it) {"),
            "quota bypass": ("PipelineEvictionQuota(switch2, graphics_cache.size(), budget, candidates.size())", "candidates.size()"),
        }
        source = code_only(self.source)
        original = function(source, "PipelineCache::EvictUnusedPipelines")
        body = compact(original)
        for name, (old, new) in mutations.items():
            with self.subTest(name=name):
                self.assertTrue(compact(old) in body, f"Missing mutation anchor: {name}")
                mutation = source.replace(original, body.replace(compact(old), compact(new), 1), 1)
                with self.assertRaises(AssertionError):
                    check_policy(mutation)


if __name__ == "__main__":
    unittest.main()
