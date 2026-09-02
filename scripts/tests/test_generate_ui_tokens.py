import unittest
from pathlib import Path

from scripts.generate_ui_tokens import contrast_ratio, load_and_validate, render_outputs


class TokenGenerationTest(unittest.TestCase):
    def test_day_and_night_state_colors_meet_aa(self):
        tokens = load_and_validate(Path("libs/common/ui/design-tokens.json"))
        for theme in ("day", "night"):
            background = tokens["themes"][theme]["background"]
            for state, color in tokens["themes"][theme]["states"].items():
                self.assertGreaterEqual(
                    contrast_ratio(color, background),
                    4.5,
                    f"{theme}.{state} must meet 4.5:1",
                )

    def test_information_topology_line_meets_ui_contrast(self):
        tokens = load_and_validate(Path("libs/common/ui/design-tokens.json"))
        for theme in ("day", "night"):
            values = tokens["themes"][theme]
            self.assertGreaterEqual(
                contrast_ratio(values["topologyLine"], values["background"]), 3.0
            )

    def test_rendered_outputs_are_deterministic(self):
        tokens = load_and_validate(Path("libs/common/ui/design-tokens.json"))
        first = render_outputs(tokens, Path("."))
        second = render_outputs(tokens, Path("."))
        self.assertEqual(first, second)

    def test_qmake_uses_platform_specific_python_for_pre_link_check(self):
        project = Path("apps/admin-client/src/src.pro").read_text(encoding="utf-8")
        self.assertIn("win32:UI_TOKEN_PYTHON = python", project)
        self.assertIn("unix:UI_TOKEN_PYTHON = python3", project)
        self.assertIn("QMAKE_PRE_LINK", project)
        self.assertIn("generate_ui_tokens.py", project)
        self.assertIn("--check", project)

    def test_local_map_key_file_is_ignored(self):
        ignored = Path(".gitignore").read_text(encoding="utf-8")
        self.assertIn("config/local.env", ignored)

    def test_motion_duration_reaches_cpp_header(self):
        # motion.aurora 必须进 C++ 头（Qt 端 AuroraBackdrop 读 kMotionAuroraMs，
        # 禁止 C++ 手写与 design-tokens.json 重复的时长字面量）
        header = Path("apps/admin-client/src/theme/generated/theme_tokens.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("kMotionAuroraMs{11000}", header)


if __name__ == "__main__":
    unittest.main()
