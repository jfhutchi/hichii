// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"
#include "shader_recompiler/ir_opt/passes.h"
#include "video_core/renderer_vulkan/fragment_color_mask.h"

namespace {
using namespace Shader;

class FragmentEnvironment final : public Environment {
public:
    FragmentEnvironment() {
        stage = Stage::Fragment;
        sph = {};
    }
    void Outputs(u32 colors, bool depth = false, bool sample_mask = false) {
        sph.ps.omap.target = colors;
        sph.ps.omap.depth.Assign(depth);
        sph.ps.omap.sample_mask.Assign(sample_mask);
    }
    u64 ReadInstruction(u32) override {
        throw LogicError("Unexpected instruction read");
    }
    u32 ReadCbufValue(u32, u32) override {
        throw LogicError("Unexpected cbuf read");
    }
    TextureType ReadTextureType(u32) override {
        throw LogicError("Unexpected texture read");
    }
    TexturePixelFormat ReadTexturePixelFormat(u32) override {
        throw LogicError("Unexpected format read");
    }
    bool IsTexturePixelFormatInteger(u32) override {
        return false;
    }
    u32 ReadViewportTransformState() override {
        return 1;
    }
    u32 TextureBoundBuffer() const override {
        return 0;
    }
    u32 LocalMemorySize() const override {
        return 0;
    }
    u32 SharedMemorySize() const override {
        return 0;
    }
    std::array<u32, 3> WorkgroupSize() const override {
        return {1, 1, 1};
    }
    bool HasHLEMacroState() const override {
        return false;
    }
    std::optional<ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}
};

struct FragmentProgram {
    ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    FragmentEnvironment env;

    FragmentProgram() {
        program.stage = Stage::Fragment;
        program.blocks = {&block};
        program.post_order_blocks = {&block};
        program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                               {{}, IR::AbstractSyntaxNode::Type::Return}};
    }
    void Exit(u32 colors, bool depth = false, bool sample_mask = false) {
        env.Outputs(colors, depth, sample_mask);
        Maxwell::TranslatorVisitor{env, block}.EXIT(0);
    }
    void Collect() {
        Optimization::CollectShaderInfoPass(env, program);
    }
};
} // namespace

TEST_CASE("Fragment EXIT preserves every render target component in shader metadata",
          "[video_core][fragment_color_mask]") {
    for (u32 target = 0; target < 8; ++target) {
        for (u32 mask = 0; mask < 16; ++mask) {
            CAPTURE(target, mask);
            FragmentProgram fixture;
            fixture.Exit(mask << (target * 4));
            fixture.Collect();
            for (u32 rt = 0; rt < 8; ++rt) {
                CHECK(fixture.program.info.frag_color_components[rt] == (rt == target ? mask : 0));
                CHECK(fixture.program.info.stores_frag_color[rt] == (rt == target && mask != 0));
            }
            CHECK(Vulkan::PackFragmentColorOutputMask(fixture.program.info.frag_color_components) ==
                  mask << (target * 4));
        }
    }
    FragmentProgram mixed;
    mixed.Exit(0xFA538421);
    mixed.Collect();
    const std::array<u8, 8> expected{1, 2, 4, 8, 3, 5, 10, 15};
    CHECK(mixed.program.info.frag_color_components == expected);
    CHECK(Vulkan::PackFragmentColorOutputMask(expected) == 0xFA538421);
}

TEST_CASE("Fragment depth and sample exports do not enable color writes",
          "[video_core][fragment_color_mask]") {
    for (bool depth : {false, true}) {
        for (bool sample_mask : {false, true}) {
            FragmentProgram fixture;
            fixture.Exit(0, depth, sample_mask);
            fixture.Collect();
            CHECK(Vulkan::PackFragmentColorOutputMask(fixture.program.info.frag_color_components) ==
                  0);
            CHECK(fixture.program.info.stores_frag_depth == depth);
            CHECK(fixture.program.info.stores_sample_mask == sample_mask);
        }
    }
    FragmentProgram exits;
    exits.Exit(0x10000001, true);
    exits.Exit(0x80000002, false, true);
    exits.Collect();
    CHECK(Vulkan::PackFragmentColorOutputMask(exits.program.info.frag_color_components) ==
          0x90000003);
    CHECK(exits.program.info.stores_frag_depth);
    CHECK(exits.program.info.stores_sample_mask);
}

TEST_CASE("Fragment color writes intersect guest masks independently for all attachments",
          "[video_core][fragment_color_mask]") {
    const Info absent_fragment{};
    const u32 absent_mask =
        Vulkan::PackFragmentColorOutputMask(absent_fragment.frag_color_components);
    CHECK(absent_mask == 0);
    for (size_t rt = 0; rt < 8; ++rt) {
        for (u32 guest = 0; guest < 16; ++guest) {
            CAPTURE(rt, guest);
            CHECK(Vulkan::FragmentColorWriteMask(guest, absent_mask, rt) == 0);
            CHECK(Vulkan::FragmentColorWriteMask(guest, ~0U, rt) == guest);
            for (u32 output = 0; output < 16; ++output) {
                CAPTURE(output);
                const u32 packed = (~(15U << (rt * 4))) | (output << (rt * 4));
                CHECK(Vulkan::FragmentColorWriteMask(guest, packed, rt) == (guest & output));
            }
        }
    }
}

TEST_CASE("Fragment masks reemit after real command invalidation and pipeline transitions",
          "[video_core][fragment_color_mask]") {
    Vulkan::StateTracker tracker;
    Vulkan::FragmentColorMaskState masks;
    u32 emissions{};
    const auto update = [&](u32 output, bool expected) {
        const auto result = masks.Update(output, tracker);
        CHECK(result.blending == expected);
        CHECK(result.color_mask == expected);
        if (result.color_mask) {
            ++emissions;
            CHECK(Vulkan::FragmentColorWriteMask(15, output, 0) == (output & 15));
        }
        CHECK_FALSE(tracker.TouchBlending());
        CHECK_FALSE(tracker.TouchColorMask());
    };
    tracker.InvalidateCommandBufferState();
    update(0, true);
    update(0, false);
    tracker.InvalidateCommandBufferState();
    update(0, true);
    CHECK(emissions == 2);
    update(0, false);
    update(5, true);
    update(5, false);
    update(~0U, true);
    update(5, true);
    update(5, false);
    CHECK(emissions == 5);
    // Color-mask bookkeeping must leave other command-buffer state dirty.
    CHECK(tracker.TouchDepthTestEnable());
    CHECK(tracker.TouchDepthWriteEnable());
    CHECK(tracker.TouchStencilOp());
    CHECK(tracker.TouchStencilWriteMask());
    CHECK(tracker.TouchBlendEnable());
    CHECK(tracker.TouchBlendEquations());
    CHECK_FALSE(tracker.TouchDepthTestEnable());
    CHECK_FALSE(tracker.TouchStencilOp());
}

TEST_CASE("Fragment mask gates preserve blending and color-mask short circuit behavior",
          "[video_core][fragment_color_mask]") {
    Vulkan::StateTracker tracker;
    Vulkan::FragmentColorMaskState masks;
    tracker.InvalidateCommandBufferState();
    REQUIRE(tracker.TouchBlending());
    const auto skipped = masks.Update(0, tracker);
    CHECK_FALSE(skipped.blending);
    CHECK_FALSE(skipped.color_mask);
    CHECK(tracker.TouchColorMask());
    tracker.InvalidateCommandBufferState();
    REQUIRE(tracker.TouchColorMask());
    const auto blending_only = masks.Update(0, tracker);
    CHECK(blending_only.blending);
    CHECK_FALSE(blending_only.color_mask);
    const auto changed = masks.Update(1, tracker);
    CHECK(changed.blending);
    CHECK(changed.color_mask);
}

TEST_CASE("Dual source SPIR-V keeps primary and secondary fragment metadata distinct",
          "[video_core][fragment_color_mask][spirv]") {
    FragmentProgram fixture;
    IR::IREmitter ir{fixture.block};
    ir.SetFragColor(0, 0, ir.Imm32(0.25f));
    ir.SetFragColor(1, 3, ir.Imm32(0.75f));
    fixture.Collect();
    const auto components = fixture.program.info.frag_color_components;
    REQUIRE(components[0] == 1);
    REQUIRE(components[1] == 8);
    Profile profile{};
    profile.supported_spirv = 0x00010500;
    RuntimeInfo runtime{};
    runtime.dual_source_blend = true;
    Backend::Bindings bindings;
    const auto code = Backend::SPIRV::EmitSPIRV(profile, runtime, fixture.program, bindings);
    std::map<u32, u32> locations;
    std::map<u32, u32> indices;
    for (size_t offset = 5; offset < code.size();) {
        const u32 count = code[offset] >> 16;
        REQUIRE(count > 0);
        REQUIRE(offset + count <= code.size());
        if ((code[offset] & 0xffff) == 71 && count == 4) { // OpDecorate
            if (code[offset + 2] == 30) {                  // Location
                locations[code[offset + 1]] = code[offset + 3];
            } else if (code[offset + 2] == 32) { // Index
                indices[code[offset + 1]] = code[offset + 3];
            }
        }
        offset += count;
    }
    REQUIRE(indices.size() == 2);
    std::array<bool, 2> found{};
    for (const auto& [id, index] : indices) {
        REQUIRE(index < found.size());
        REQUIRE(locations.contains(id));
        CHECK(locations.at(id) == 0);
        found[index] = true;
    }
    CHECK(found[0]);
    CHECK(found[1]);
    CHECK(fixture.program.info.frag_color_components == components);
    CHECK(Vulkan::FragmentColorWriteMask(15, Vulkan::PackFragmentColorOutputMask(components), 0) ==
          1);
    if (const char* directory = std::getenv("EDEN_FRAG_TEST_OUTPUT")) {
        std::filesystem::create_directories(directory);
        std::ofstream file{std::filesystem::path{directory} / "dual-source.spv", std::ios::binary};
        file.write(reinterpret_cast<const char*>(code.data()),
                   static_cast<std::streamsize>(code.size() * sizeof(u32)));
        REQUIRE(file.good());
    }
}
