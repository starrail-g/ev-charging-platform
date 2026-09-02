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


if __name__ == "__main__":
    unittest.main()
