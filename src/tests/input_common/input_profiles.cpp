// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <filesystem>
#include <random>
#include <QCoreApplication>
#include <QProcess>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "qt_common/config/qt_config.h"
#include "yuzu/configuration/input_profiles.h"

namespace {

class ConfigDirectory {
public:
    ConfigDirectory()
        : original{Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir)},
          path{std::filesystem::temp_directory_path() /
               fmt::format("lichii-input-profiles-{}", std::random_device{}())},
          global_players{Settings::values.players.GetValue(true)},
          using_global{Settings::values.players.UsingGlobal()},
          configuring_global{Settings::IsConfiguringGlobal()} {
        REQUIRE(std::filesystem::create_directory(path));
        Common::FS::SetEdenPath(Common::FS::EdenPath::ConfigDir, path);
        Settings::values.players.SetGlobal(false);
        custom_players = Settings::values.players.GetValue();
        Settings::values.players.GetValue(true) = {};
        Settings::values.players.GetValue() = {};
        Settings::values.players.SetGlobal(true);
        Settings::SetConfiguringGlobal(false);
    }

    ~ConfigDirectory() {
        Settings::values.players.GetValue(true) = global_players;
        Settings::values.players.SetGlobal(false);
        Settings::values.players.GetValue() = custom_players;
        Settings::values.players.SetGlobal(using_global);
        Settings::SetConfiguringGlobal(configuring_global);
        Common::FS::SetEdenPath(Common::FS::EdenPath::ConfigDir, original);
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    const std::filesystem::path original;
    const std::filesystem::path path;
    const std::array<Settings::PlayerInput, 10> global_players;
    std::array<Settings::PlayerInput, 10> custom_players;
    const bool using_global;
    const bool configuring_global;
};

void SetBindings(Settings::PlayerInput& player, int key) {
    player.connected = true;
    player.controller_type = Settings::ControllerType::RightJoycon;
    player.buttons.fill(fmt::format("engine:keyboard,code:{}", key));
    player.buttons.front() = "engine:sdl,guid:030000005e0400008e02000014010000,port:2,button:0";
    player.analogs.fill("engine:sdl,guid:030000005e0400008e02000014010000,port:2,axis_x:0,"
                        "axis_y:1,deadzone:0.2,range:0.9");
    player.motions.fill("engine:sdl,guid:030000005e0400008e02000014010000,port:2,motion:0");
    player.buttons.back() = "[empty]";
    player.analogs.back() = "[empty]";
    player.motions.back() = "[empty]";
}

void CheckBindings(const Settings::PlayerInput& player, int key) {
    Settings::PlayerInput expected{};
    SetBindings(expected, key);
    CHECK(player.connected == expected.connected);
    CHECK(player.controller_type == expected.controller_type);
    CHECK(player.buttons == expected.buttons);
    CHECK(player.analogs == expected.analogs);
    CHECK(player.motions == expected.motions);
}

int RestartStage(const std::string& stage, const std::filesystem::path& directory) {
    // The parent passes a newly created temporary folder before any config is constructed.
    Common::FS::SetEdenPath(Common::FS::EdenPath::ConfigDir, directory);
    QtConfig config;
    InputProfiles profiles;
    auto& players = Settings::values.players.GetValue();
    if (stage == "--write-profiles") {
        SetBindings(players[0], 71);
        SetBindings(players[8], 72);
        if (!profiles.CreateProfile("desktop", 0) || !profiles.CreateProfile("portable", 8)) {
            return 1;
        }
        config.SaveAllValues();
        return 0;
    }
    if (players[0].profile_name != "desktop" || players[8].profile_name != "portable") {
        return 2;
    }
    Settings::PlayerInput expected{};
    SetBindings(expected, 71);
    if (players[0].buttons != expected.buttons || players[0].analogs != expected.analogs ||
        players[0].motions != expected.motions || players[0].connected != expected.connected ||
        players[0].controller_type != expected.controller_type) {
        return 3;
    }
    SetBindings(expected, 72);
    if (players[8].buttons != expected.buttons || players[8].analogs != expected.analogs ||
        players[8].motions != expected.motions || players[8].connected != expected.connected ||
        players[8].controller_type != expected.controller_type) {
        return 4;
    }
    SetBindings(players[8], 99);
    if (!profiles.LoadProfile("portable", 8) || players[8].profile_name != "portable" ||
        players[8].buttons != expected.buttons || players[8].analogs != expected.analogs ||
        players[8].motions != expected.motions) {
        return 5;
    }
    return 0;
}

} // namespace

TEST_CASE("Profile operations remember the selected identity after success", "[input_profiles]") {
    ConfigDirectory directory;
    InputProfiles profiles;
    auto& players = Settings::values.players.GetValue();
    SetBindings(players[8], 65);
    REQUIRE(profiles.CreateProfile("portable", 8));
    CHECK(players[8].profile_name == "portable");
    CHECK(players[0].profile_name.empty());

    players[8].profile_name = "previous";
    REQUIRE(profiles.SaveProfile("portable", 8));
    CHECK(players[8].profile_name == "portable");

    players[8].profile_name = "previous";
    SetBindings(players[8], 99);
    REQUIRE(profiles.LoadProfile("portable", 8));
    CHECK(players[8].profile_name == "portable");
    CheckBindings(players[8], 65);

    CHECK_FALSE(profiles.CreateProfile("portable", 8));
    CHECK_FALSE(profiles.LoadProfile("absent", 8));
    CHECK_FALSE(profiles.SaveProfile("absent", 8));
    CHECK(players[8].profile_name == "portable");
    CheckBindings(players[8], 65);
}

TEST_CASE("Profile operations resolve handheld separately from player one", "[input_profiles]") {
    ConfigDirectory directory;
    InputProfiles profiles;
    auto& players = Settings::values.players.GetValue();
    SetBindings(players[0], 65);
    SetBindings(players[8], 66);
    const auto desktop = InputProfiles::GetPlayerIndex(0, false);
    const auto handheld = InputProfiles::GetPlayerIndex(0, true);
    REQUIRE(desktop == 0);
    REQUIRE(handheld == 8);
    CHECK(InputProfiles::GetPlayerIndex(1, true) == 1);
    CHECK(InputProfiles::GetPlayerIndex(7, false) == 7);
    REQUIRE(profiles.CreateProfile("desktop", desktop));
    REQUIRE(profiles.CreateProfile("portable", handheld));
    CHECK(players[desktop].profile_name == "desktop");
    CHECK(players[handheld].profile_name == "portable");
    SetBindings(players[handheld], 67);
    REQUIRE(profiles.SaveProfile("portable", handheld));
    SetBindings(players[handheld], 99);
    REQUIRE(profiles.LoadProfile("portable", handheld));
    CheckBindings(players[handheld], 67);
    CheckBindings(players[desktop], 65);
    CHECK(players[desktop].profile_name == "desktop");

    // Reopening the profile list must preserve each controller's own selection.
    InputProfiles reopened;
    const auto names = reopened.GetInputProfileNames();
    CHECK(std::ranges::find(names, players[handheld].profile_name) != names.end());
    CHECK(std::ranges::find(names, players[desktop].profile_name) != names.end());
}

TEST_CASE("Global handheld profile loads retain handheld style for single Joy-Con profiles",
          "[input_profiles]") {
    ConfigDirectory directory;
    InputProfiles profiles;
    auto style = Settings::ControllerType::LeftJoycon;
    SECTION("Left Joy-Con") {}
    SECTION("Right Joy-Con") {
        style = Settings::ControllerType::RightJoycon;
    }

    Settings::SetConfiguringGlobal(true);
    auto& players = Settings::values.players.GetValue();
    SetBindings(players[0], 65);
    players[0].controller_type = style;
    REQUIRE(profiles.CreateProfile("single-joycon", 0));
    const auto saved = players[0];

    SetBindings(players[0], 99);
    players[0].controller_type = Settings::ControllerType::ProController;
    players[0].profile_name = "desktop";
    const auto desktop = players[0];
    SetBindings(players[8], 66);
    players[8].controller_type = Settings::ControllerType::Handheld;
    REQUIRE(profiles.LoadProfile("single-joycon", InputProfiles::GetPlayerIndex(0, true)));
    CHECK(players[8].controller_type == Settings::ControllerType::Handheld);
    CHECK(players[8].connected);
    CHECK(players[8].profile_name == "single-joycon");
    CHECK(players[8].buttons == saved.buttons);
    CHECK(players[8].analogs == saved.analogs);
    CHECK(players[8].motions == saved.motions);
    CHECK(players[0].profile_name == desktop.profile_name);
    CHECK(players[0].controller_type == desktop.controller_type);
    CHECK(players[0].connected == desktop.connected);
    CHECK(players[0].buttons == desktop.buttons);
    CHECK(players[0].analogs == desktop.analogs);
    CHECK(players[0].motions == desktop.motions);

    REQUIRE(profiles.LoadProfile("single-joycon", 0));
    CHECK(players[0].controller_type == style);
    CHECK(players[0].buttons == saved.buttons);

    Settings::SetConfiguringGlobal(false);
    Settings::values.players.SetGlobal(false);
    auto& custom = Settings::values.players.GetValue()[8];
    custom.controller_type = Settings::ControllerType::Handheld;
    REQUIRE(profiles.LoadProfile("single-joycon", 8));
    CHECK(custom.controller_type == style);
    CHECK(custom.buttons == saved.buttons);
    CHECK(custom.profile_name == "single-joycon");
}

TEST_CASE("Deleting a profile clears matching identities while preserving bindings",
          "[input_profiles]") {
    ConfigDirectory directory;
    InputProfiles profiles;
    auto& players = Settings::values.players.GetValue(true);
    SetBindings(players[0], 65);
    REQUIRE(profiles.CreateProfile("shared", 0));
    players[0].profile_name = "shared";
    players[8] = players[0];
    players[1].profile_name = "other";
    Settings::values.players.SetGlobal(false);
    auto& custom = Settings::values.players.GetValue();
    custom[2] = players[0];
    CHECK_FALSE(profiles.DeleteProfile("absent"));
    CHECK(players[0].profile_name == "shared");
    REQUIRE(profiles.DeleteProfile("shared"));
    CHECK_FALSE(Settings::values.players.UsingGlobal());
    CHECK(players[0].profile_name.empty());
    CHECK(players[8].profile_name.empty());
    CHECK(custom[2].profile_name.empty());
    CHECK(players[1].profile_name == "other");
    CheckBindings(players[0], 65);
    CheckBindings(players[8], 65);
    CheckBindings(custom[2], 65);
}

TEST_CASE("Profile identity and all mappings survive a fresh process", "[input_profiles]") {
    ConfigDirectory directory;
    for (const auto* stage : {"--write-profiles", "--read-profiles"}) {
        QProcess child;
        child.start(
            QCoreApplication::applicationFilePath(),
            {QString::fromLatin1(stage), QString::fromStdWString(directory.path.wstring())});
        REQUIRE(child.waitForFinished(30000));
        INFO(child.readAllStandardError().toStdString());
        REQUIRE(child.exitStatus() == QProcess::NormalExit);
        REQUIRE(child.exitCode() == 0);
    }
}

TEST_CASE("A failed profile write preserves the previous selected identity", "[input_profiles]") {
    ConfigDirectory directory;
    InputProfiles profiles;
    auto& player = Settings::values.players.GetValue()[8];
    SetBindings(player, 65);
    REQUIRE(profiles.CreateProfile("portable", 8));
    player.profile_name = "previous";
    const auto profile_path = directory.path / "input" / "portable.ini";
    const auto saved_path = directory.path / "portable-saved.ini";
    std::filesystem::rename(profile_path, saved_path);
    REQUIRE(std::filesystem::create_directory(directory.path / "input" / "portable.ini"));
    SetBindings(player, 66);
    CHECK_FALSE(profiles.SaveProfile("portable", 8));
    CHECK(player.profile_name == "previous");
    CheckBindings(player, 66);
    CHECK_FALSE(profiles.LoadProfile("portable", 8));
    CHECK(player.profile_name == "previous");
    CheckBindings(player, 66);
    CHECK_FALSE(profiles.DeleteProfile("portable"));
    CHECK(player.profile_name == "previous");
    REQUIRE(std::filesystem::remove(profile_path));
    std::filesystem::rename(saved_path, profile_path);
    REQUIRE(profiles.LoadProfile("portable", 8));
    CheckBindings(player, 65);
    player.profile_name = "previous";

    REQUIRE(std::filesystem::create_directory(directory.path / "input" / "blocked.ini"));
    CHECK_FALSE(profiles.CreateProfile("blocked", 8));
    const auto names = profiles.GetInputProfileNames();
    CHECK(std::ranges::find(names, "blocked") == names.end());
    CHECK(player.profile_name == "previous");
}

TEST_CASE("Per-game profile bindings override only the selected player", "[input_profiles]") {
    ConfigDirectory directory;
    QtConfig global;
    InputProfiles profiles;
    auto& global_players = Settings::values.players.GetValue(true);
    SetBindings(global_players[0], 65);
    SetBindings(global_players[8], 66);
    global.SaveAllValues();
    {
        QtConfig custom{"game", Config::ConfigType::PerGameConfig};
        auto& custom_players = Settings::values.players.GetValue();
        SetBindings(custom_players[8], 67);
        REQUIRE(profiles.CreateProfile("game-portable", 8));
        CHECK_FALSE(Settings::values.players.UsingGlobal());
        custom.SaveAllValues();
        custom_players = {};
        custom.ReloadAllValues();
        CHECK(custom_players[8].profile_name == "game-portable");
        CheckBindings(custom_players[8], 67);
        CHECK(custom_players[0].profile_name.empty());
        CheckBindings(custom_players[0], 65);

        custom.ClearControlPlayerValues();
        custom_players[8].profile_name.clear();
        custom.SaveAllValues();
        CHECK_FALSE(custom.Exists("Controls", "player_8_profile_name"));
        CHECK_FALSE(custom.Exists("Controls",
                                  fmt::format("player_8_{}", Settings::NativeButton::mapping[0])));
        custom_players = {};
    }
    QtConfig inherited{"game", Config::ConfigType::PerGameConfig};
    const auto& inherited_players = Settings::values.players.GetValue();
    CHECK(inherited_players[8].profile_name.empty());
    CheckBindings(inherited_players[0], 65);
    CheckBindings(inherited_players[8], 66);
    CheckBindings(global_players[0], 65);
    CheckBindings(global_players[8], 66);
}

TEST_CASE("A failed custom profile load preserves identity bindings and connection state",
          "[input_profiles]") {
    ConfigDirectory directory;
    InputProfiles profiles;
    auto& global = Settings::values.players.GetValue(true)[2];
    SetBindings(global, 90);
    global.profile_name = "global";
    Settings::values.players.SetGlobal(false);
    auto& custom = Settings::values.players.GetValue()[2];
    SetBindings(custom, 65);
    REQUIRE(profiles.CreateProfile("portable", 2));
    custom.profile_name = "previous";
    custom.connected = false;
    custom.controller_type = Settings::ControllerType::LeftJoycon;
    const auto previous = custom;
    const auto profile_path = directory.path / "input" / "portable.ini";
    REQUIRE(std::filesystem::remove(profile_path));
    REQUIRE(std::filesystem::create_directory(profile_path));

    CHECK_FALSE(profiles.LoadProfile("portable", 2));
    CHECK_FALSE(Settings::values.players.UsingGlobal());
    CHECK(custom.profile_name == previous.profile_name);
    CHECK(custom.connected == previous.connected);
    CHECK(custom.controller_type == previous.controller_type);
    CHECK(custom.buttons == previous.buttons);
    CHECK(custom.analogs == previous.analogs);
    CHECK(custom.motions == previous.motions);
    CHECK(global.profile_name == "global");
    CheckBindings(global, 90);
}

int main(int argc, char* argv[]) {
    QCoreApplication app{argc, argv};
    if (argc == 3 && (std::string_view{argv[1]} == "--write-profiles" ||
                      std::string_view{argv[1]} == "--read-profiles")) {
        return RestartStage(argv[1], std::filesystem::path{argv[2]});
    }
    return Catch::Session().run(argc, argv);
}
