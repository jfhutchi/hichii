// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/ampere/program_address_map.h"
#include "shader_recompiler/frontend/maxwell/structured_control_flow.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "video_core/pipeline_cache_files.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/shader_environment.h"

namespace {

constexpr std::array<char, 8> Magic{'y', 'u', 'z', 'u', 'c', 'a', 'c', 'h'};
constexpr std::array<char, 8> OtherMagic{'y', 'u', 'z', 'u', 'v', 'k', 'c', 'h'};

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("eden-pipeline-cache-files-" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void WriteCache(const std::filesystem::path& file, const std::array<char, 8>& magic, u32 version,
                std::string_view body) {
    std::ofstream stream(file, std::ios::binary);
    stream.write(magic.data(), magic.size());
    stream.write(reinterpret_cast<const char*>(&version), sizeof(version));
    stream.write(body.data(), static_cast<std::streamsize>(body.size()));
}

std::string ReadAll(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

class SerializedEnvironment : public VideoCommon::GenericEnvironment {
public:
    explicit SerializedEnvironment(Shader::Stage shader_stage) {
        stage = shader_stage;
        code = {0x123456789abcdef0, 0xfedcba9876543210};
        cached_lowest = 0;
        cached_highest = sizeof(u64);
        mesh_registers = {0x12345678, 0x87654321, 0xabcdef01};
        std::memset(&mesh_primitive_header, 0x5a, sizeof(mesh_primitive_header));
        texture_bound = 2;
        cbuf_values.emplace((u64{1} << 32) | 16, 0x1357);
        texture_types.emplace(3, Shader::TextureType::Color2D);
    }

    u32 ReadCbufValue(u32, u32) override {
        return 0;
    }
    Shader::TextureType ReadTextureType(u32) override {
        return Shader::TextureType::Color2D;
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return {};
    }
    bool IsTexturePixelFormatInteger(u32) override {
        return false;
    }
    u32 ReadViewportTransformState() override {
        return 1;
    }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
};

class NativeMinMaxEnvironment final : public SerializedEnvironment {
public:
    NativeMinMaxEnvironment() : SerializedEnvironment{Shader::Stage::Compute} {
        // Synthetic native instructions, not a captured game shader. Dynamic invocation IDs
        // and shared stores retain both floating operations through optimization.
        using Shader::Ampere::Instruction;
        constexpr std::array instructions{
            Instruction{{0x00007919, 0, 0x00002100, 0}}, // S2R R0, TID.X
            Instruction{{0x00017919, 0, 0x00002200, 0}}, // S2R R1, TID.Y
            Instruction{{0x00047209, 1, 0x03800000, 0}}, // FMNMX R4, R0, R1, PT
            Instruction{{0x00057209, 1, 0x07800000, 0}}, // FMNMX R5, R0, R1, !PT
            Instruction{{0xff007388, 4, 0x00000800, 0}}, // STS.32 [RZ], R4
            Instruction{{0xff007388, 0x405, 0x00000800, 0}}, // STS.32 [RZ+4], R5
            Instruction{{0x0000794d, 0, 0x03800000, 0}}, // EXIT
        };
        const auto lowered = Shader::Ampere::LowerToMaxwell(instructions);
        INFO(lowered.error);
        REQUIRE(lowered);
        code.assign(sizeof(sph) / sizeof(u64), 0);
        for (const u64 instruction : lowered.instructions) {
            if ((code.size() * sizeof(u64)) % 32 == 0) {
                code.push_back(0);
            }
            code.push_back(instruction);
        }
        code.push_back(0);
        Shader::Ampere::AppendNativeAddressMap(code, lowered.source_instruction_offsets);
        initial_offset = sizeof(sph);
        cached_lowest = 0;
        cached_highest = static_cast<u32>((code.size() - 1) * sizeof(u64));
        workgroup_size = {32, 2, 1};
        shared_memory_size = 8;
    }
};

} // Anonymous namespace

TEST_CASE("Migrated v53 native compute environments retranslate to NaN-defined SPIR-V",
          "[pipeline_cache_files][nan_cache_retranslation]") {
    TempDir temp;
    const auto old = VideoCommon::VersionedPipelineCachePath(temp.path, "vulkan", 53);
    const auto current = VideoCommon::VersionedPipelineCachePath(
        temp.path, "vulkan", VideoCommon::VulkanPipelineCacheVersion);
    NativeMinMaxEnvironment source;
    const Vulkan::ComputePipelineCacheKey key{
        .unique_hash = 0x325,
        .shared_memory_size = 8,
        .workgroup_size = {32, 2, 1},
    };
    const std::array<const VideoCommon::GenericEnvironment*, 1> envs{&source};
    VideoCommon::SerializePipeline(key, envs, old, 53);
    const auto before = ReadAll(old);
    REQUIRE(VideoCommon::MigrateVulkanShaderCache(temp.path) == 53);
    REQUIRE(ReadAll(old) == before);
    REQUIRE(ReadAll(current).substr(12) == before.substr(12));
    size_t loaded = 0;
    VideoCommon::LoadPipelines(
        {}, current, VideoCommon::VulkanPipelineCacheVersion,
        [&](std::ifstream& file, VideoCommon::FileEnvironment restored) {
            ++loaded;
            Vulkan::ComputePipelineCacheKey restored_key{};
            file.read(reinterpret_cast<char*>(&restored_key), sizeof(restored_key));
            REQUIRE(file.good());
            CHECK(restored_key == key);
            CHECK(restored.ShaderStage() == Shader::Stage::Compute);
            CHECK(restored.IsNativeProgram());
            CHECK(restored.HasNativeInstructions());
            CHECK(restored.WorkgroupSize() == key.workgroup_size);
            CHECK(restored.SharedMemorySize() == key.shared_memory_size);
            for (u32 address = 0; address < 7 * 16; address += 16) {
                CHECK(restored.TranslateProgramAddress(address) ==
                      source.TranslateProgramAddress(address));
            }
            Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
            Shader::Maxwell::Flow::CFG cfg{restored, flow_pool, Shader::Maxwell::Location{0x50}};
            Shader::ObjectPool<Shader::IR::Inst> inst_pool;
            Shader::ObjectPool<Shader::IR::Block> block_pool;
            auto program = Shader::Maxwell::TranslateProgram(
                inst_pool, block_pool, restored, cfg, Shader::HostTranslateInfo{});
            size_t stores = 0;
            for (const auto* block : program.blocks) {
                for (const auto& inst : block->Instructions()) {
                    stores += inst.GetOpcode() == Shader::IR::Opcode::WriteSharedU32;
                }
            }
            REQUIRE(stores == 2);
            Shader::Profile profile{};
            profile.supported_spirv = 0x00010500;
            const auto code = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
            size_t nmin = 0;
            size_t nmax = 0;
            for (size_t offset = 5; offset < code.size();) {
                const u32 size = code[offset] >> 16;
                REQUIRE(size > 0);
                REQUIRE(offset + size <= code.size());
                if ((code[offset] & 0xffffU) == 12) { // OpExtInst, GLSL.std.450 only
                    REQUIRE(size >= 5);
                    const u32 operation = code[offset + 4];
                    CHECK(operation != 37); // FMin
                    CHECK(operation != 40); // FMax
                    CHECK(operation != 43); // FClamp
                    nmin += operation == 79;
                    nmax += operation == 80;
                }
                offset += size;
            }
            CHECK(nmin == 1);
            CHECK(nmax == 1);
            if (const char* directory = std::getenv("EDEN_NAN_TEST_OUTPUT")) {
                std::filesystem::create_directories(directory);
                std::ofstream output{std::filesystem::path{directory} / "cached-native.spv",
                                     std::ios::binary};
                output.write(reinterpret_cast<const char*>(code.data()),
                             static_cast<std::streamsize>(code.size() * sizeof(u32)));
                REQUIRE(output.good());
            }
        },
        [](std::ifstream&, std::vector<VideoCommon::FileEnvironment>) {
            FAIL("Unexpected graphics record");
        });
    REQUIRE(loaded == 1);
}

TEST_CASE("Shader cache migration selects the newest compatible environment and its used list",
          "[pipeline_cache_files][nan_cache_migration]") {
    TempDir temp;
    const auto path = [&](std::string_view stem, u32 version) {
        return VideoCommon::VersionedPipelineCachePath(temp.path, stem, version);
    };
    constexpr u32 current = VideoCommon::VulkanPipelineCacheVersion;
    std::optional<u32> expected = 53;
    WriteCache(path("vulkan", 52), Magic, 52, "older environment");
    WriteCache(path("vulkan", 53), Magic, 53, "newer environment");
    WriteCache(path("vulkan_used", 52), OtherMagic, 1, "older usage");
    WriteCache(path("vulkan_used", 53), OtherMagic, 1, "newer usage");
    bool missing_list = false;
    bool existing_environment = false;
    bool existing_list = false;
    SECTION("newest compatible pair") {}
    SECTION("v52 only") {
        std::filesystem::remove(path("vulkan", 53));
        expected = 52;
    }
    SECTION("invalid v53 falls back to v52") {
        WriteCache(path("vulkan", 53), OtherMagic, 53, "wrong magic");
        expected = 52;
    }
    SECTION("missing selected list does not import another generation") {
        std::filesystem::remove(path("vulkan_used", 53));
        missing_list = true;
    }
    SECTION("existing environment leaves both destinations alone") {
        WriteCache(path("vulkan", current), Magic, current, "current environment");
        existing_environment = true;
        expected.reset();
    }
    SECTION("existing environment and used list are both preserved") {
        WriteCache(path("vulkan", current), Magic, current, "current environment");
        WriteCache(path("vulkan_used", current), OtherMagic, 1, "current usage");
        existing_environment = true;
        existing_list = true;
        expected.reset();
    }
    SECTION("existing used list is preserved") {
        WriteCache(path("vulkan_used", current), OtherMagic, 1, "current usage");
        existing_list = true;
    }
    SECTION("no compatible environment leaves destinations absent") {
        std::filesystem::remove(path("vulkan", 52));
        std::filesystem::remove(path("vulkan", 53));
        expected.reset();
    }
    SECTION("interrupted temporary copy is replaced") {
        auto temporary = path("vulkan", current);
        temporary += ".migrating";
        std::ofstream{temporary} << "partial";
    }
    const auto old52 = ReadAll(path("vulkan", 52));
    const auto old53 = ReadAll(path("vulkan", 53));
    const auto used52 = ReadAll(path("vulkan_used", 52));
    const auto used53 = ReadAll(path("vulkan_used", 53));
    const auto before_env = ReadAll(path("vulkan", current));
    const auto before_used = ReadAll(path("vulkan_used", current));
    CHECK(VideoCommon::MigrateVulkanShaderCache(temp.path) == expected);
    CHECK(ReadAll(path("vulkan", 52)) == old52);
    CHECK(ReadAll(path("vulkan", 53)) == old53);
    CHECK(ReadAll(path("vulkan_used", 52)) == used52);
    CHECK(ReadAll(path("vulkan_used", 53)) == used53);
    if (expected) {
        CHECK(VideoCommon::PipelineCacheHeaderMatches(path("vulkan", current), Magic, current));
        CHECK(ReadAll(path("vulkan", current)).substr(12) ==
              (*expected == 53 ? old53 : old52).substr(12));
    } else {
        CHECK(ReadAll(path("vulkan", current)) == before_env);
        CHECK(std::filesystem::exists(path("vulkan", current)) == existing_environment);
    }
    if (!expected || missing_list || existing_list) {
        CHECK(ReadAll(path("vulkan_used", current)) == before_used);
        CHECK(std::filesystem::exists(path("vulkan_used", current)) == existing_list);
    } else {
        CHECK(ReadAll(path("vulkan_used", current)) == (*expected == 53 ? used53 : used52));
    }
    auto temporary = path("vulkan", current);
    temporary += ".migrating";
    CHECK_FALSE(std::filesystem::exists(temporary));
}

TEST_CASE("Opaque driver cache v52 copy preserves bytes without importing shader environments",
          "[pipeline_cache_files][nan_cache_migration]") {
    TempDir temp;
    const auto old = VideoCommon::VersionedPipelineCachePath(temp.path, "vulkan_pipelines", 52);
    const auto newer = VideoCommon::VersionedPipelineCachePath(temp.path, "vulkan_pipelines", 53);
    const auto destination = VideoCommon::VersionedPipelineCachePath(
        temp.path, "vulkan_pipelines", VideoCommon::VulkanPipelineCacheVersion);
    WriteCache(old, OtherMagic, 52, "opaque driver data");
    WriteCache(newer, OtherMagic, 53, "separate newer driver data");
    const auto before = ReadAll(old);
    const auto before_newer = ReadAll(newer);
    CHECK_FALSE(VideoCommon::MigrateVulkanShaderCache(temp.path));
    CHECK_FALSE(std::filesystem::exists(destination));
    // LoadDiskResources retains its separate explicit v52 driver-blob policy.
    CHECK_FALSE(VideoCommon::CopyCompatiblePipelineCache(
        old, destination, Magic, 52, VideoCommon::VulkanPipelineCacheVersion));
    REQUIRE(VideoCommon::CopyCompatiblePipelineCache(
        old, destination, OtherMagic, 52, VideoCommon::VulkanPipelineCacheVersion));
    CHECK(VideoCommon::PipelineCacheHeaderMatches(
        destination, OtherMagic, VideoCommon::VulkanPipelineCacheVersion));
    CHECK(ReadAll(destination).substr(12) == before.substr(12));
    CHECK(ReadAll(old) == before);
    CHECK(ReadAll(newer) == before_newer);
}

TEST_CASE("Task and mesh disk environments roundtrip all translation state and record boundaries",
          "[pipeline_cache_files][mesh_serialization]") {
    TempDir temp;
    const auto path = temp.path / "mesh.bin";
    constexpr u64 key = 0xabcdef;
    for (const auto stage : {Shader::Stage::Task, Shader::Stage::Mesh, Shader::Stage::Fragment}) {
        SerializedEnvironment source{stage};
        const std::array<const VideoCommon::GenericEnvironment*, 1> envs{&source};
        VideoCommon::SerializePipeline(key, envs, path, VideoCommon::VulkanPipelineCacheVersion);
    }
    size_t read = 0;
    VideoCommon::LoadPipelines(
        {}, path, VideoCommon::VulkanPipelineCacheVersion,
        [](std::ifstream&, VideoCommon::FileEnvironment) { FAIL("Unexpected compute record"); },
        [&](std::ifstream& file, std::vector<VideoCommon::FileEnvironment> envs) {
            const std::array stages{Shader::Stage::Task, Shader::Stage::Mesh,
                                    Shader::Stage::Fragment};
            REQUIRE(read < stages.size());
            REQUIRE(envs.size() == 1);
            auto& restored = envs.front();
            SerializedEnvironment source{stages[read++]};
            REQUIRE(restored.ShaderStage() == source.ShaderStage());
            REQUIRE(restored.ReadInstruction(0) == 0x123456789abcdef0);
            REQUIRE(restored.ReadInstruction(8) == 0xfedcba9876543210);
            REQUIRE(restored.ReadCbufValue(1, 16) == 0x1357);
            REQUIRE(restored.ReadTextureType(3) == Shader::TextureType::Color2D);
            REQUIRE(restored.TextureBoundBuffer() == 2);
            REQUIRE(restored.MemorySize() > sizeof(restored) + 2 * sizeof(u64));
            if (source.ShaderStage() != Shader::Stage::Fragment) {
                REQUIRE(restored.MeshRegisters() == source.MeshRegisters());
                REQUIRE(std::memcmp(&restored.MeshPrimitiveHeader(), &source.MeshPrimitiveHeader(),
                                    sizeof(Shader::ProgramHeader)) == 0);
            }
            u64 restored_key{};
            file.read(reinterpret_cast<char*>(&restored_key), sizeof(restored_key));
            REQUIRE(restored_key == key);
        });
    REQUIRE(read == 3);
}

TEST_CASE("Compatible v52 pipelines migrate without changing or discarding the old warm cache",
          "[pipeline_cache_files][mesh_serialization]") {
    TempDir temp;
    const auto old = VideoCommon::VersionedPipelineCachePath(temp.path, "vulkan", 52);
    const auto current = VideoCommon::VersionedPipelineCachePath(
        temp.path, "vulkan", VideoCommon::VulkanPipelineCacheVersion);
    SerializedEnvironment source{Shader::Stage::Fragment};
    const std::array<const VideoCommon::GenericEnvironment*, 1> envs{&source};
    constexpr u64 key = 42;
    VideoCommon::SerializePipeline(key, envs, old, 52);
    const auto before = ReadAll(old);
    REQUIRE(VideoCommon::CopyCompatiblePipelineCache(old, current, Magic, 52,
                                                     VideoCommon::VulkanPipelineCacheVersion));
    REQUIRE(ReadAll(old) == before);
    REQUIRE(ReadAll(current).substr(12) == before.substr(12));
    REQUIRE_FALSE(VideoCommon::CopyCompatiblePipelineCache(
        old, current, Magic, 52, VideoCommon::VulkanPipelineCacheVersion));
    bool loaded = false;
    VideoCommon::LoadPipelines(
        {}, current, VideoCommon::VulkanPipelineCacheVersion,
        [](std::ifstream&, VideoCommon::FileEnvironment) { FAIL("Unexpected compute record"); },
        [&](std::ifstream& file, std::vector<VideoCommon::FileEnvironment> restored) {
            REQUIRE(restored.size() == 1);
            REQUIRE(restored.front().ShaderStage() == Shader::Stage::Fragment);
            u64 restored_key{};
            file.read(reinterpret_cast<char*>(&restored_key), sizeof(restored_key));
            REQUIRE(restored_key == key);
            loaded = true;
        });
    REQUIRE(loaded);
    const auto other = temp.path / "wrong.bin";
    REQUIRE_FALSE(VideoCommon::CopyCompatiblePipelineCache(old, other, Magic, 51, 53));
    REQUIRE_FALSE(std::filesystem::exists(other));
}

TEST_CASE("Compatible pipeline cache migration retries after an interrupted temporary copy",
          "[pipeline_cache_files][mesh_serialization][migration_retry]") {
    TempDir temp;
    const auto old = VideoCommon::VersionedPipelineCachePath(temp.path, "vulkan", 52);
    const auto current = VideoCommon::VersionedPipelineCachePath(
        temp.path, "vulkan", VideoCommon::VulkanPipelineCacheVersion);
    auto temporary = current;
    temporary += ".migrating";
    WriteCache(old, Magic, 52, "complete pipeline records");
    const auto before = ReadAll(old);
    {
        std::ofstream stream(temporary, std::ios::binary);
        stream.write(before.data(), 4);
    }
    REQUIRE(ReadAll(temporary) == before.substr(0, 4));
    REQUIRE(VideoCommon::CopyCompatiblePipelineCache(old, current, Magic, 52,
                                                     VideoCommon::VulkanPipelineCacheVersion));
    REQUIRE(ReadAll(old) == before);
    REQUIRE(VideoCommon::PipelineCacheHeaderMatches(
        current, Magic, VideoCommon::VulkanPipelineCacheVersion));
    REQUIRE(ReadAll(current).substr(12) == before.substr(12));
    REQUIRE_FALSE(std::filesystem::exists(temporary));
    const auto migrated = ReadAll(current);
    REQUIRE_FALSE(VideoCommon::CopyCompatiblePipelineCache(
        old, current, Magic, 52, VideoCommon::VulkanPipelineCacheVersion));
    REQUIRE(ReadAll(current) == migrated);
    REQUIRE(ReadAll(old) == before);
}

TEST_CASE("Truncated mesh state is rejected before a pipeline can be queued",
          "[pipeline_cache_files][mesh_serialization]") {
    TempDir temp;
    const auto path = temp.path / "truncated-mesh.bin";
    SerializedEnvironment source{Shader::Stage::Mesh};
    const std::array<const VideoCommon::GenericEnvironment*, 1> envs{&source};
    constexpr u64 key = 42;
    VideoCommon::SerializePipeline(key, envs, path, VideoCommon::VulkanPipelineCacheVersion);
    std::filesystem::resize_file(path, std::filesystem::file_size(path) - sizeof(key) - 1);
    VideoCommon::LoadPipelines(
        {}, path, VideoCommon::VulkanPipelineCacheVersion,
        [](std::ifstream&, VideoCommon::FileEnvironment) { FAIL("Unexpected compute record"); },
        [](std::ifstream&, std::vector<VideoCommon::FileEnvironment>) {
            FAIL("Incomplete mesh state reached pipeline creation");
        });
    REQUIRE_FALSE(std::filesystem::exists(path));
}

TEST_CASE("Cancelled mesh loading preserves its disk cache and queues no work",
          "[pipeline_cache_files][mesh_serialization]") {
    TempDir temp;
    const auto path = temp.path / "cancelled-mesh.bin";
    SerializedEnvironment source{Shader::Stage::Mesh};
    const std::array<const VideoCommon::GenericEnvironment*, 1> envs{&source};
    constexpr u64 key = 42;
    VideoCommon::SerializePipeline(key, envs, path, VideoCommon::VulkanPipelineCacheVersion);
    const auto before = ReadAll(path);
    std::stop_source stop;
    stop.request_stop();
    VideoCommon::LoadPipelines(
        stop.get_token(), path, VideoCommon::VulkanPipelineCacheVersion,
        [](std::ifstream&, VideoCommon::FileEnvironment) { FAIL("Unexpected compute record"); },
        [](std::ifstream&, std::vector<VideoCommon::FileEnvironment>) {
            FAIL("Cancelled load reached pipeline creation");
        });
    REQUIRE(ReadAll(path) == before);
}

TEST_CASE("Pipeline cache names are version-suffixed per build", "[pipeline_cache_files]") {
    const std::filesystem::path dir{"shader"};
    CHECK(VideoCommon::VersionedPipelineCachePath(dir, "vulkan", 39).filename() ==
          "vulkan.fork-v39.bin");
    CHECK(VideoCommon::VersionedPipelineCachePath(dir, "vulkan_pipelines", 39).filename() ==
          "vulkan_pipelines.fork-v39.bin");
    CHECK(VideoCommon::VersionedPipelineCachePath(dir, "vulkan", 18) !=
          VideoCommon::VersionedPipelineCachePath(dir, "vulkan", 39));
}

TEST_CASE("A legacy cache of this version is adopted with its contents",
          "[pipeline_cache_files]") {
    TempDir temp;
    const auto legacy = temp.path / "vulkan.bin";
    const auto versioned = VideoCommon::VersionedPipelineCachePath(temp.path, "vulkan", 39);
    WriteCache(legacy, Magic, 39, "pipelines");
    const std::string before = ReadAll(legacy);
    REQUIRE(VideoCommon::AdoptLegacyPipelineCache(legacy, versioned, Magic, 39));
    CHECK_FALSE(std::filesystem::exists(legacy));
    CHECK(ReadAll(versioned) == before);
    // Idempotent: nothing left to adopt.
    CHECK_FALSE(VideoCommon::AdoptLegacyPipelineCache(legacy, versioned, Magic, 39));
}

TEST_CASE("Another build's legacy cache is never touched", "[pipeline_cache_files]") {
    TempDir temp;
    const auto legacy = temp.path / "vulkan.bin";
    const auto versioned = VideoCommon::VersionedPipelineCachePath(temp.path, "vulkan", 39);
    WriteCache(legacy, Magic, 18, "official");
    const std::string before = ReadAll(legacy);
    CHECK_FALSE(VideoCommon::AdoptLegacyPipelineCache(legacy, versioned, Magic, 39));
    CHECK(ReadAll(legacy) == before);
    CHECK_FALSE(std::filesystem::exists(versioned));

    WriteCache(legacy, OtherMagic, 39, "wrong magic");
    CHECK_FALSE(VideoCommon::AdoptLegacyPipelineCache(legacy, versioned, Magic, 39));
    CHECK(std::filesystem::exists(legacy));

    // Truncated header.
    {
        std::ofstream stream(legacy, std::ios::binary | std::ios::trunc);
        stream.write(Magic.data(), 4);
    }
    CHECK_FALSE(VideoCommon::AdoptLegacyPipelineCache(legacy, versioned, Magic, 39));
    CHECK(std::filesystem::exists(legacy));
}

TEST_CASE("An existing versioned cache is never overwritten by a legacy one",
          "[pipeline_cache_files]") {
    TempDir temp;
    const auto legacy = temp.path / "vulkan.bin";
    const auto versioned = VideoCommon::VersionedPipelineCachePath(temp.path, "vulkan", 39);
    WriteCache(legacy, Magic, 39, "older");
    WriteCache(versioned, Magic, 39, "current");
    const std::string current = ReadAll(versioned);
    CHECK_FALSE(VideoCommon::AdoptLegacyPipelineCache(legacy, versioned, Magic, 39));
    CHECK(ReadAll(versioned) == current);
    CHECK(std::filesystem::exists(legacy));
    CHECK_FALSE(VideoCommon::AdoptLegacyPipelineCache(temp.path / "missing.bin", versioned,
                                                      Magic, 39));
}

TEST_CASE("Cached compute failures retain their cause and the current dispatch address",
          "[pipeline_cache][diagnostics]") {
    const Vulkan::ComputePipelineCacheKey key{
        .unique_hash = 0x1234,
        .shared_memory_size = 1024,
        .workgroup_size = {32, 1, 1},
    };
    std::unordered_map<Vulkan::ComputePipelineCacheKey, Vulkan::ComputePipelineCacheEntry> cache;
    const std::string cause =
        "SPIR-V emission failed at IR opcode UndefU8: SPIR-V Instruction is not implemented";
    cache[key].failure = cause;
    for (const GPUVAddr address : {0x211ddd9000ULL, 0x21ebdfa000ULL}) {
        const auto [entry, inserted] = cache.try_emplace(key);
        REQUIRE_FALSE(inserted);
        CHECK(entry->second.Get(false, address, key) == nullptr);
        try {
            static_cast<void>(entry->second.Get(true, address, key));
            FAIL("Native compute failure was not propagated");
        } catch (const Shader::RuntimeError& exception) {
            const std::string message{exception.what()};
            CHECK(message.find(cause) != std::string::npos);
            CHECK(message.find(fmt::format("address={:#x}", address)) != std::string::npos);
            CHECK(message.find(fmt::format("shader={:#016x}", key.unique_hash)) != std::string::npos);
            CHECK(message.find(fmt::format("pipeline={:#016x}", key.Hash())) != std::string::npos);
        }
        CHECK(entry->second.failure == cause);
        CHECK_FALSE(entry->second.pipeline);
    }
    auto other = key;
    other.workgroup_size[0] = 64;
    const auto [entry, inserted] = cache.try_emplace(other);
    REQUIRE(inserted);
    CHECK(entry->second.failure.empty());
    CHECK(entry->second.Get(false, 0x2120633c00, other) == nullptr);
    REQUIRE_THROWS_AS(entry->second.Get(true, 0x2120633c00, other), Shader::RuntimeError);
}
