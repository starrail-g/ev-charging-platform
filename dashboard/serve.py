#!/usr/bin/env python3
"""本地离线演示 HTTP 服务与安全运行时配置注入。

- GET /runtime-config.js  -> 注入 window.__EV_CONFIG__（含可选 GL 底图 key）
- GET /api/*              -> 反向代理到 EV_ANALYTICS_API_BASE_URL（analytics.api.app），
                             分析模式（?source=analytics）与静态页同源接线；后端不可达 → 结构化 503
- 其余路径从 dashboard/ 静态提供（本地 ECharts / 数据 / 页面）
- 底图密钥只读自环境变量 TENCENT_MAP_JS_KEY（优先）或 config/local.env（被 gitignore）；
  服务日志绝不打印 config、响应体或 key。
- --check 不启动服务，只校验答辩必需资产是否存在（构建验收用）。
"""
import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

DASHBOARD_DIR = Path(__file__).resolve().parent
REPO_ROOT = DASHBOARD_DIR.parent
LOCAL_ENV = REPO_ROOT / "config" / "local.env"

REQUIRED_ASSETS = [
    "dashboard/index.html",
    "dashboard/vendor/echarts.min.js",
    "dashboard/js/app.js",
    "dashboard/data/demo.json",
]


def load_runtime_config(local_env: Path) -> dict:
    """读取本地 env 文件（不存在则跳过），环境变量优先于文件。"""
    values = {}
    if local_env.exists():
        for raw in local_env.read_text(encoding="utf-8").splitlines():
            line = raw.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()
    return {
        # The server-side WebService key must never be sent to this browser
        # origin.  Dashboard only needs the separately scoped GL visual key.
        "tencentMapJsKey": os.environ.get(
            "TENCENT_MAP_JS_KEY", values.get("TENCENT_MAP_JS_KEY", "")
        ),
        "analysisApiBaseUrl": os.environ.get(
            "EV_ANALYSIS_API_BASE_URL", values.get("EV_ANALYSIS_API_BASE_URL", "")
        ),
        "dashboardApiBaseUrl": os.environ.get(
            "EV_DASHBOARD_API_BASE_URL", values.get("EV_DASHBOARD_API_BASE_URL", "")
        ),
        "demo": False,
    }


def render_runtime_config_js(config: dict) -> str:
    return "window.__EV_CONFIG__ = " + json.dumps(config, ensure_ascii=False) + ";\n"


def check_assets(root: Path) -> list:
    """返回缺失资产相对路径列表（空列表 = 全部存在）。"""
    missing = []
    for relative in REQUIRED_ASSETS:
        if not (root / relative).exists():
            missing.append(relative)
    return missing


class DemoRequestHandler(SimpleHTTPRequestHandler):
    """静态服务 + /runtime-config.js 注入 + /api/* 分析代理；日志不含任何配置内容。"""

    def __init__(self, *args, local_env: Path = LOCAL_ENV, analytics_api_base: str = "", **kwargs):
        self._local_env = local_env
        self._analytics_api_base = analytics_api_base
        super().__init__(*args, directory=str(DASHBOARD_DIR), **kwargs)

    def _send_json(self, status: int, payload: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _proxy_analytics(self) -> None:
        """把 /api/* 原样转发给分析快照服务（分析模式与静态页同源接线的反向代理）。"""
        if not self._analytics_api_base:
            payload = json.dumps({
                "status": "error",
                "error": {"code": "analytics_api_unreachable",
                          "message": "未配置 EV_ANALYTICS_API_BASE_URL，分析接口未接通"},
            }, ensure_ascii=False).encode("utf-8")
            self._send_json(503, payload)
            return
        request = urllib.request.Request(self._analytics_api_base + self.path,
                                         headers={"Accept": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=15) as response:
                body = response.read()
                status = response.status
                content_type = response.headers.get("Content-Type", "application/json")
        except urllib.error.HTTPError as error:      # 结构化业务错误按原状态码透传
            body = error.read()
            status = error.code
            content_type = error.headers.get("Content-Type", "application/json")
        except Exception as error:                    # 连接失败等 → 结构化 503（前端可识别）
            payload = json.dumps({
                "status": "error",
                "error": {"code": "analytics_api_unreachable",
                          "message": f"分析接口不可达：{error}"},
            }, ensure_ascii=False).encode("utf-8")
            self._send_json(503, payload)
            return
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path_only = self.path.split("?", 1)[0]
        if path_only == "/runtime-config.js":
            config = load_runtime_config(self._local_env)
            payload = render_runtime_config_js(config).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/javascript; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return
        if path_only == "/api" or path_only.startswith("/api/"):
            return self._proxy_analytics()
        return super().do_GET()

    def log_message(self, fmt, *args):
        # 只记录请求行本身（方法+路径），不记录查询串以外的任何内容；
        # 本服务不产生响应体日志，密钥从不进入日志。
        sys.stderr.write("%s %s\n" % (self.address_string(), self.path))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="只校验资产存在，不启动服务")
    parser.add_argument(
        "--port", type=int, default=int(os.environ.get("DASHBOARD_PORT", "8080"))
    )
    args = parser.parse_args()

    if args.check:
        missing = check_assets(REPO_ROOT)
        for relative in missing:
            print(relative, file=sys.stderr)
        return 1 if missing else 0

    analytics_api_base = os.environ.get("EV_ANALYTICS_API_BASE_URL", "").rstrip("/")
    server = ThreadingHTTPServer(
        ("127.0.0.1", args.port),
        partial(DemoRequestHandler, local_env=LOCAL_ENV, analytics_api_base=analytics_api_base),
    )
    print(f"dashboard demo server: http://127.0.0.1:{args.port}/ (Ctrl+C to stop)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("shutting down")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
