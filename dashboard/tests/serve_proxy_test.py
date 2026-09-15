"""serve.py /api/* 反向代理测试（分析模式同源接线）。

- 后端可用：/api/* 原样透传（含结构化错误状态码）；
- 后端不可达/未配置：返回结构化 503（code=analytics_api_unreachable），前端可识别而非裸 404。
"""
from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import threading
import time
import unittest
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SERVE = ROOT / "dashboard" / "serve.py"


class _StubAnalyticsHandler(BaseHTTPRequestHandler):
    def do_GET(self):  # noqa: N802
        if self.path.startswith("/api/dashboard"):
            payload = json.dumps({"status": "ok", "meta": {"batch_id": "stub-batch"}}).encode("utf-8")
            self.send_response(200)
        else:
            payload = json.dumps({"status": "error",
                                  "error": {"code": "not_found", "message": self.path}}).encode("utf-8")
            self.send_response(404)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *args):  # 静默
        pass


def _free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


class ServeProxyTest(unittest.TestCase):
    def _start_serve(self, port: int, analytics_base: str) -> subprocess.Popen:
        env = {**os.environ, "EV_ANALYTICS_API_BASE_URL": analytics_base}
        return subprocess.Popen([sys.executable, str(SERVE), "--port", str(port)],
                                cwd=ROOT, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    @staticmethod
    def _wait_ready(port: int, timeout: float = 10.0) -> None:
        """就绪探测走静态路径（不经后端），避免把"后端连接慢"误判成服务未起。"""
        deadline = time.time() + timeout
        last_error: Exception | None = None
        while time.time() < deadline:
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{port}/runtime-config.js", timeout=2).close()
                return
            except Exception as error:
                last_error = error
                time.sleep(0.2)
        raise AssertionError(f"serve.py 未就绪：{last_error}")

    @classmethod
    def _request(cls, port: int, path: str, timeout: float = 25.0):
        """真实请求：不可达后端的代理在部分平台上要等到连接超时，故给足宽限。"""
        cls._wait_ready(port)
        try:
            return urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=timeout)
        except urllib.error.HTTPError as error:       # 4xx/5xx 也要拿回响应体
            return error

    def test_proxies_api_and_keeps_static_serving(self) -> None:
        backend_port = _free_port()
        backend = ThreadingHTTPServer(("127.0.0.1", backend_port), _StubAnalyticsHandler)
        threading.Thread(target=backend.serve_forever, daemon=True).start()
        port = _free_port()
        proc = self._start_serve(port, f"http://127.0.0.1:{backend_port}")
        try:
            response = self._request(port, "/api/dashboard?start=2026-06-17&end=2026-09-14")
            self.assertEqual(response.status, 200)
            payload = json.loads(response.read().decode("utf-8"))
            self.assertEqual(payload["meta"]["batch_id"], "stub-batch")
            response.close()
            # 后端 404 结构化错误按原状态码透传
            not_found = self._request(port, "/api/unknown")
            self.assertEqual(not_found.status, 404)
            not_found.close()
            # 静态资源不受影响
            static = self._request(port, "/index.html")
            self.assertEqual(static.status, 200)
            static.close()
        finally:
            proc.terminate()
            proc.wait(timeout=10)
            backend.shutdown()
            backend.server_close()

    def test_unreachable_backend_returns_structured_503(self) -> None:
        port = _free_port()
        dead_port = _free_port()      # 空闲端口：无人监听
        proc = self._start_serve(port, f"http://127.0.0.1:{dead_port}")
        try:
            response = self._request(port, "/api/dashboard")
            self.assertEqual(response.status, 503)
            payload = json.loads(response.read().decode("utf-8"))
            self.assertEqual(payload["status"], "error")
            self.assertEqual(payload["error"]["code"], "analytics_api_unreachable")
            response.close()
        finally:
            proc.terminate()
            proc.wait(timeout=10)


if __name__ == "__main__":
    unittest.main()
