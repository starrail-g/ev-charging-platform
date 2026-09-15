import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from dashboard.serve import check_assets, load_runtime_config, render_runtime_config_js


class RuntimeConfigTest(unittest.TestCase):
    def test_dashboard_uses_separate_visual_key_without_leaking_server_key(self):
        with patch.dict(os.environ, {"TENCENT_MAP_KEY": "server-secret", "TENCENT_MAP_JS_KEY": "gl-secret"}, clear=False):
            config = load_runtime_config(Path("missing.env"))
        self.assertEqual(config["tencentMapJsKey"], "gl-secret")
        self.assertNotIn("tencentMapKey", config)
        rendered = render_runtime_config_js(config)
        self.assertIn('"tencentMapJsKey": "gl-secret"', rendered)
        self.assertNotIn("server-secret", rendered)

    def test_missing_key_returns_empty_string(self):
        with patch.dict(os.environ, {}, clear=True):
            config = load_runtime_config(Path("missing.env"))
        self.assertEqual(config["tencentMapJsKey"], "")

    def test_analysis_endpoint_is_runtime_configurable(self):
        with patch.dict(os.environ, {"EV_ANALYSIS_API_BASE_URL": "http://127.0.0.1:61501"}, clear=False):
            config = load_runtime_config(Path("missing.env"))
        self.assertEqual(config["analysisApiBaseUrl"], "http://127.0.0.1:61501")

    def test_check_assets_reports_missing_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            missing = check_assets(Path(tmp))
        self.assertIn("dashboard/index.html", missing)
        self.assertIn("dashboard/vendor/echarts.min.js", missing)


if __name__ == "__main__":
    unittest.main()
