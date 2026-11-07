// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fmt/ranges.h>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "frontend_common/config.h"
#include "yuzu/configuration/input_profiles.h"

namespace FS = Common::FS;

namespace {

bool ProfileExistsInFilesystem(std::string_view profile_name) {
    return FS::Exists(FS::GetEdenPath(FS::EdenPath::ConfigDir) / "input" /
                      fmt::format("{}.ini", profile_name));
}

bool IsINI(const std::filesystem::path& filename) {
    return filename.extension() == ".ini";
}

std::filesystem::path GetNameWithoutExtension(std::filesystem::path filename) {
    return filename.replace_extension();
}

} // namespace

InputProfiles::InputProfiles() {
    const auto input_profile_loc = FS::GetEdenPath(FS::EdenPath::ConfigDir) / "input";

    if (!FS::IsDir(input_profile_loc)) {
        return;
    }

    FS::IterateDirEntries(
        input_profile_loc,
        [this](const std::filesystem::path& full_path) {
            const auto filename = full_path.filename();
            const auto name_without_ext =
                Common::FS::PathToUTF8String(GetNameWithoutExtension(filename));

            if (IsINI(filename) && IsProfileNameValid(name_without_ext)) {
                map_profiles.insert_or_assign(
                    name_without_ext,
                    std::make_unique<QtConfig>(name_without_ext, Config::ConfigType::InputProfile));
            }

            return true;
        },
        FS::DirEntryFilter::File);
}

InputProfiles::~InputProfiles() = default;

std::vector<std::string> InputProfiles::GetInputProfileNames() {
    std::vector<std::string> profile_names;
    profile_names.reserve(map_profiles.size());

    auto it = map_profiles.cbegin();
    while (it != map_profiles.cend()) {
        const auto& [profile_name, config] = *it;
        if (!ProfileExistsInFilesystem(profile_name)) {
            it = map_profiles.erase(it);
            continue;
        }

        profile_names.push_back(profile_name);
        ++it;
    }

    std::stable_sort(profile_names.begin(), profile_names.end());

    return profile_names;
}

bool InputProfiles::IsProfileNameValid(std::string_view profile_name) {
    return profile_name.find_first_of("<>:;\"/\\|,.!?*") == std::string::npos;
}

std::size_t InputProfiles::GetPlayerIndex(std::size_t player_index, bool is_handheld) {
    constexpr std::size_t handheld_index = 8;
    return player_index == 0 && is_handheld ? handheld_index : player_index;
}

bool InputProfiles::CreateProfile(const std::string& profile_name, std::size_t player_index) {
    if (ProfileExistsInMap(profile_name)) {
        return false;
    }

    map_profiles.insert_or_assign(
        profile_name, std::make_unique<QtConfig>(profile_name, Config::ConfigType::InputProfile));

    if (!SaveProfile(profile_name, player_index)) {
        map_profiles.erase(profile_name);
        return false;
    }
    return true;
}

bool InputProfiles::DeleteProfile(const std::string& profile_name) {
    if (!ProfileExistsInMap(profile_name)) {
        return false;
    }

    if (!ProfileExistsInFilesystem(profile_name) ||
        FS::RemoveFile(map_profiles[profile_name]->GetConfigFilePath())) {
        map_profiles.erase(profile_name);
    }

    if (ProfileExistsInMap(profile_name) || ProfileExistsInFilesystem(profile_name)) {
        return false;
    }

    auto& players = Settings::values.players;
    const bool using_global = players.UsingGlobal();
    for (const bool global : {false, true}) {
        players.SetGlobal(global);
        for (auto& player : players.GetValue()) {
            if (player.profile_name == profile_name) {
                player.profile_name.clear();
            }
        }
    }
    players.SetGlobal(using_global);
    return true;
}

bool InputProfiles::LoadProfile(const std::string& profile_name, std::size_t player_index) {
    if (!ProfileExistsInMap(profile_name)) {
        return false;
    }

    if (!ProfileExistsInFilesystem(profile_name)) {
        map_profiles.erase(profile_name);
        return false;
    }

    LOG_INFO(Config, "Loading input profile `{}`", profile_name);

    if (!map_profiles[profile_name]->ReadQtControlPlayerValues(player_index)) {
        return false;
    }
    Settings::values.players.GetValue()[player_index].profile_name = profile_name;
    return true;
}

bool InputProfiles::SaveProfile(const std::string& profile_name, std::size_t player_index) {
    if (!ProfileExistsInMap(profile_name)) {
        return false;
    }

    if (!map_profiles[profile_name]->SaveQtControlPlayerValues(player_index)) {
        return false;
    }
    Settings::values.players.GetValue()[player_index].profile_name = profile_name;
    return true;
}

bool InputProfiles::ProfileExistsInMap(const std::string& profile_name) const {
    return map_profiles.find(profile_name) != map_profiles.end();
}
