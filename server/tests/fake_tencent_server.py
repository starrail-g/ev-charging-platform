#!/usr/bin/env python3
"""Loopback-only Tencent WebService fake for production-adapter integration tests."""

import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse


HOST = "127.0.0.1"
PORT = int(os.getenv("EV_FAKE_TENCENT_PORT", "45539"))


class Handler(BaseHTTPRequestHandler):
    def log_message(self, _format, *_args):
        pass

    def respond(self, payload, status=200):
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        request = urlparse(self.path)
        query = parse_qs(request.query)
        if query.get("key") != ["fake-integration-key"]:
            self.respond({"status": 311, "message": "permission denied"}, 403)
            return
        if request.path == "/ws/geocoder/v1/":
            self.respond({
                "status": 0,
                "message": "Success",
                "result": {"location": {"lat": 41.7192, "lng": 123.4315}},
            })
            return
        if request.path == "/ws/place/v1/search":
            if not query.get("boundary", [""])[0].startswith("nearby("):
                self.respond({"status": 348, "message": "invalid boundary"})
                return
            self.respond({
                "status": 0,
                "message": "Success",
                "data": [{
                    "id": "fake-live-poi-001",
                    "title": "腾讯适配器联调充电站",
                    "address": "沈阳市浑南区测试路1号",
                    "location": {"lat": 41.7202, "lng": 123.4335},
                    "distance": 200,
                }],
            })
            return
        if request.path in ("/ws/direction/v1/driving/", "/ws/direction/v1/walking/"):
            self.respond({
                "status": 0,
                "message": "Success",
                "result": {"routes": [{
                    "distance": 1234,
                    "duration": 7,
                    "polyline": [41.7192, 123.4315, 1000, 2000],
                }]},
            })
            return
        self.respond({"status": 404, "message": "not found"}, 404)


ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
