#!/usr/bin/env python3
"""Read-only Flask API for generated ADS forecasts and operational analysis."""

from __future__ import annotations

import json
import os
from datetime import datetime, timezone
from pathlib import Path

from flask import Flask, jsonify, request


def utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def create_app(artifact_dir: str | os.PathLike[str] | None = None) -> Flask:
    app = Flask(__name__)
    root = Path(artifact_dir or os.environ.get("EV_ANALYSIS_ARTIFACT_DIR", "../build/stage2-data/ads"))

    @app.after_request
    def add_cors_headers(response):
        # Dashboard is a separately served static origin. Keep the service
        # read-only and expose only the configured origin (wildcard by default
        # because the API contains no credentials or user data).
        response.headers["Access-Control-Allow-Origin"] = os.environ.get(
            "EV_ANALYSIS_ALLOWED_ORIGIN", "*"
        )
        response.headers["Access-Control-Allow-Headers"] = "Content-Type"
        response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
        return response

    def load(name: str) -> dict:
        path = root / name
        if not path.exists():
            raise FileNotFoundError(name)
        return json.loads(path.read_text(encoding="utf-8"))

    @app.get("/api/v1/analysis/health")
    def health():
        metadata_path = root / "model_metadata.json"
        metadata = None
        if metadata_path.exists():
            try:
                metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                metadata = None
        return jsonify({"status": "ok", "service": "analysis", "artifact_available": metadata_path.exists(), "metadata": metadata, "checked_at": utc_now()})

    @app.get("/api/v1/dashboard/snapshot")
    def dashboard_snapshot():
        try:
            return jsonify(load("dashboard.json"))
        except FileNotFoundError:
            return jsonify({"status": "degraded", "code": "DASHBOARD_SNAPSHOT_UNAVAILABLE", "message": "业务快照暂不可用"}), 503

    @app.post("/api/v1/analysis/forecast")
    def forecast():
        body = request.get_json(silent=True) or {}
        horizons = body.get("horizons_hours", [1, 6, 24])
        if not isinstance(horizons, list) or any(h not in (1, 6, 24) for h in horizons):
            return jsonify({"status": "error", "code": "INVALID_REQUEST", "message": "horizons_hours must contain 1, 6 or 24"}), 400
        try:
            payload = load("forecast.json")
        except FileNotFoundError:
            return jsonify({"status": "degraded", "code": "MODEL_UNAVAILABLE", "message": "分析结果暂不可用", "generated_at": utc_now(), "items": []}), 503
        selected = [item for item in payload.get("items", []) if item.get("horizon_hours") in horizons]
        return jsonify({**payload, "status": "ok", "items": selected})

    @app.get("/api/v1/analysis/recommendations")
    def recommendations():
        try:
            return jsonify(load("recommendations.json"))
        except FileNotFoundError:
            return jsonify({"status": "degraded", "code": "MODEL_UNAVAILABLE", "items": [], "generated_at": utc_now()}), 503

    @app.get("/api/v1/analysis/alerts")
    def alerts():
        try:
            return jsonify(load("alerts.json"))
        except FileNotFoundError:
            return jsonify({"status": "degraded", "code": "MODEL_UNAVAILABLE", "items": [], "generated_at": utc_now()}), 503

    return app


app = create_app()
