#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Structural coverage for Vulkan callers that require a device to instantiate.

The Catch2 fragment_color_mask tests exercise the actual compiler and state policy.
These checks constrain the remaining caller wiring and the raw-view blit's ordering.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
VULKAN = ROOT / "src/video_core/renderer_vulkan"


def code_only(source):
    tokens = r'R"([^ ()\\\t\r\n]*)\(.*?\)\1"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/'
    return re.sub(tokens, lambda match: " " * len(match[0]), source, flags=re.S)


def closing(source, opening, left, right):
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == left:
            depth += 1
        elif source[index] == right:
            depth -= 1
            if depth == 0:
                return index
    raise AssertionError(f"Unclosed {left}")


def function(source, name, parameter_pattern=None):
    for match in re.finditer(re.escape(name) + r"\s*\(", source):
        end = closing(source, match.end() - 1, "(", ")")
        if parameter_pattern and not re.fullmatch(
            parameter_pattern, source[match.end():end], flags=re.S
        ):
            continue
        body = re.match(r"\s*(?:const\s*)?\{", source[end + 1:])
        if body:
            opening = end + body.end()
            return source[opening + 1:closing(source, opening, "{", "}")]
    raise AssertionError(f"Missing function {name}")


def top_level_statements(body):
    braces = parentheses = brackets = 0
    start = 0
    for index, token in enumerate(body):
        braces += (token == "{") - (token == "}")
        parentheses += (token == "(") - (token == ")")
        brackets += (token == "[") - (token == "]")
        if token == ";" and braces == parentheses == brackets == 0:
            yield "".join(body[start:index + 1].split())
            start = index + 1
    if braces or parentheses or brackets:
        raise AssertionError("Unbalanced function body")


def check_raw_blit(source):
    body = function(
        code_only(source), "BlitImageHelper::BlitColor",
        r"\s*const\s+Framebuffer\s*\*\s*dst_framebuffer\s*,\s*"
        r"VkImageView\s+src_image_view\s*,.*",
    )
    statements = list(top_level_statements(body))
    records = [i for i, stmt in enumerate(statements) if stmt.startswith("scheduler.Record(")]
    invalidations = [i for i, stmt in enumerate(statements)
                     if stmt == "scheduler.InvalidateState();"]
    if not records or not any(index > max(records) for index in invalidations):
        raise AssertionError("Raw VkImageView blit must invalidate after its top-level Record")


class FragmentMaskWiring(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blit = (VULKAN / "blit_image.cpp").read_text(encoding="utf-8")
        cls.rasterizer = code_only((VULKAN / "vk_rasterizer.cpp").read_text(encoding="utf-8"))
        cls.pipeline = code_only((VULKAN / "vk_graphics_pipeline.cpp").read_text(encoding="utf-8"))

    def test_raw_blit_invalidates_after_record(self):
        check_raw_blit(self.blit)

    def test_blit_checker_rejects_mutations(self):
        raw = "void BlitImageHelper::BlitColor(const Framebuffer* dst_framebuffer, " \
              "VkImageView src_image_view, VkImage image) { %s }"
        wrong = "void BlitImageHelper::BlitColor(const Framebuffer* dst_framebuffer, " \
                "const ImageView& src_image_view) { scheduler.InvalidateState(); }"
        record = "scheduler.Record([](auto cb) { cb.Draw(); });"
        mutations = {
            "missing": record,
            "inside worker": "scheduler.Record([](auto cb) { scheduler.InvalidateState(); });",
            "wrong overload": record,
            "before record": "scheduler.InvalidateState();" + record,
            "comment": record + "/* scheduler.InvalidateState(); */",
            "string": record + 'const char* s = "scheduler.InvalidateState();";',
            "raw string": record + 'const char* s = R"tag(scheduler.InvalidateState();)tag";',
            "nested block": record + "{ scheduler.InvalidateState(); }",
        }
        for name, body in mutations.items():
            with self.subTest(name=name), self.assertRaises(AssertionError):
                check_raw_blit((wrong if name == "wrong overload" else "") + raw % body)
        check_raw_blit(raw % (record + "scheduler.InvalidateState();"))

    def test_actual_blit_rejects_missing_worker_and_wrong_overload_invalidation(self):
        source = code_only(self.blit)
        body = function(source, "BlitImageHelper::BlitColor",
                        r"\s*const\s+Framebuffer\s*\*\s*dst_framebuffer\s*,\s*"
                        r"VkImageView\s+src_image_view\s*,.*")
        call = "scheduler.InvalidateState();"
        self.assertEqual(body.count(call), 1)
        missing = source.replace(body, body.replace(call, ""), 1)
        worker = source.replace(body, body.replace(call, "").replace(
            "cmdbuf.EndRenderPass();", "cmdbuf.EndRenderPass();" + call), 1)
        wrong_body = function(missing, "BlitImageHelper::BlitColor",
                              r"\s*const\s+Framebuffer\s*\*\s*dst_framebuffer\s*,\s*"
                              r"const\s+ImageView\s*&\s*src_image_view\s*,.*")
        wrong = missing.replace(wrong_body, wrong_body + call, 1)
        for name, mutation in (("missing", missing), ("worker", worker), ("wrong overload", wrong)):
            with self.subTest(name=name), self.assertRaises(AssertionError):
                check_raw_blit(mutation)

    def test_static_and_dynamic_masks_use_shared_policy(self):
        pipeline = "".join(self.pipeline.split())
        self.assertIn("PackFragmentColorOutputMask(stage_infos[NUM_STAGES-1].frag_color_components)",
                      pipeline)
        self.assertIn("write_mask=FragmentColorWriteMask(write_mask,color_output_mask,index);",
                      pipeline)
        body = "".join(function(self.rasterizer, "RasterizerVulkan::UpdateBlending").split())
        self.assertIn("pipeline?pipeline->ColorOutputMask():~0U", body)
        self.assertIn("fragment_color_mask_state.Update(color_output_mask,state_tracker)", body)
        self.assertIn("if(!update.blending){return;}", body)
        self.assertIn("if(update.color_mask){", body)
        self.assertIn("current=FragmentColorWriteMask(current,color_output_mask,index);", body)
        self.assertNotIn("CurrentGraphicsPipeline", body)

    def test_selected_draw_pipeline_and_internal_draw_are_distinct(self):
        dynamic = "".join(function(self.rasterizer, "RasterizerVulkan::UpdateDynamicStates").split())
        self.assertIn("UpdateBlending(regs,pipeline);", dynamic)
        texture = "".join(function(self.rasterizer, "RasterizerVulkan::DrawTexture").split())
        self.assertIn("UpdateDynamicStates(nullptr);", texture)
        draw = "".join(function(self.rasterizer, "RasterizerVulkan::PrepareDraw").split())
        self.assertIn("UpdateDynamicStates(pipeline);", draw)


if __name__ == "__main__":
    unittest.main()
