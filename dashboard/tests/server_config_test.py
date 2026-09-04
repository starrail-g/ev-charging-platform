import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from dashboard.serve import check_assets, load_runtime_config, render_runtime_config_js


class RuntimeConfigTest(unittest.TestCase):
    def test_environment_key_wins_without_logging(self):
        with patch.dict(os.environ, {"TENCENT_MAP_KEY": "dev-secret"}, clear=False):
            config = load_runtime_config(Path("missing.env"))
        self.assertEqual(config["tencentMapKey"], "dev-secret")
        rendered = render_runtime_config_js(config)
        self.assertIn('"tencentMapKey": "dev-secret"', rendered)

    def test_missing_key_returns_empty_string(self):
        with patch.dict(os.environ, {}, clear=True):
            config = load_runtime_config(Path("missing.env"))
        self.assertEqual(config["tencentMapKey"], "")

    def test_check_assets_reports_missing_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            missing = check_assets(Path(tmp))
        self.assertIn("dashboard/index.html", missing)
        self.assertIn("dashboard/vendor/echarts.min.js", missing)


if __name__ == "__main__":
    unittest.main()
