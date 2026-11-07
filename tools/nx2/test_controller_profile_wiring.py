#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Structural checks for controller profile UI callers; no QWidget is instantiated.

The Catch2 input_profile_tests cover actual profile/config behavior. These checks
cover the UI-to-policy wiring that those headless behavioral tests cannot invoke.
Alternate source paths allow regression checks against old files without rewriting
the production checkout.
"""

import argparse
from pathlib import Path
import re
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
PLAYER_SOURCE = ROOT / "src/yuzu/configuration/configure_input_player.cpp"
PER_GAME_SOURCE = ROOT / "src/yuzu/configuration/configure_input_per_game.cpp"


def code_only(source):
    tokens = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/'
    return re.sub(
        tokens, lambda match: '""' if match[0][0] in ('"', "'") else "", source, flags=re.S
    )


def block(source, opening):
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index], index + 1
    raise AssertionError("Unclosed source block")


def function(source, name):
    match = re.search(re.escape(name) + r"\([^)]*\)\s*(?:const\s*)?\{", source)
    if match is None:
        raise AssertionError(f"Missing production function: {name}")
    return block(source, match.end() - 1)[0]


def compact(source):
    return "".join(source.split())


class ControllerProfileWiring(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.player = code_only(PLAYER_SOURCE.read_text(encoding="utf-8"))
        cls.per_game = code_only(PER_GAME_SOURCE.read_text(encoding="utf-8"))

    def check_player_operation(self, operation):
        body = compact(function(self.player, f"ConfigureInputPlayer::{operation}"))
        self.assertIn(
            f"profiles->{operation}(profile_name.toStdString(),GetProfilePlayerIndex())", body
        )

    def test_create_uses_active_slot(self):
        self.check_player_operation("CreateProfile")

    def test_load_uses_active_slot(self):
        self.check_player_operation("LoadProfile")

    def test_save_uses_active_slot(self):
        self.check_player_operation("SaveProfile")

    def test_profile_selection_uses_active_slot(self):
        body = compact(function(self.player, "ConfigureInputPlayer::UpdateInputProfiles"))
        self.assertIn(
            "Settings::values.players.GetValue()[GetProfilePlayerIndex()].profile_name", body
        )

    def test_slot_resolver_reads_actual_emulated_controller(self):
        body = compact(function(self.player, "ConfigureInputPlayer::GetProfilePlayerIndex"))
        self.assertIn(
            "InputProfiles::GetPlayerIndex(player_index,"
            "emulated_controller->GetNpadIdType()==Core::HID::NpadIdType::Handheld)", body
        )

    def test_per_game_load_failure_stops_before_connection_or_reload(self):
        body = function(self.per_game, "ConfigureInputPerGame::LoadConfiguration")
        guard = re.search(
            r"if\s*\(\s*!profiles->LoadProfile\(profile_name,\s*player_index\)\s*\)\s*\{",
            body,
        )
        self.assertIsNotNone(guard, "Per-game caller must handle a failed profile load")
        failure, end = block(body, guard.end() - 1)
        failure = compact(failure)
        self.assertIn("LOG_ERROR(Config,", failure)
        self.assertIn("profile_name", failure)
        self.assertIn("player_index", failure)
        self.assertTrue(failure.endswith("continue;"), "Failure must skip settings/controller changes")
        before_guard = compact(body[body.index("const auto profile_name"):guard.start()])
        self.assertNotIn("player.connected=", before_guard)
        self.assertNotIn("emulated_controller->ReloadFromSettings(", before_guard)
        after_guard = compact(body[end:])
        self.assertIn("player.connected=true;", after_guard)
        self.assertIn("emulated_controller->ReloadFromSettings();", after_guard)

    def test_per_game_does_not_preassign_profile_identity(self):
        body = compact(function(self.per_game, "ConfigureInputPerGame::LoadConfiguration"))
        self.assertNotIn("player.profile_name=", body)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--player-source-file", type=Path, default=PLAYER_SOURCE)
    parser.add_argument("--per-game-source-file", type=Path, default=PER_GAME_SOURCE)
    options, unittest_arguments = parser.parse_known_args()
    PLAYER_SOURCE = options.player_source_file
    PER_GAME_SOURCE = options.per_game_source_file
    unittest.main(argv=[sys.argv[0], *unittest_arguments])
