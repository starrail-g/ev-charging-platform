from __future__ import annotations

import json
from pathlib import Path

from ml.service.app import create_app


def test_health_and_forecast_contract(tmp_path: Path) -> None:
    (tmp_path / "forecast.json").write_text(json.dumps({"generated_at": "2026-09-16T00:00:00Z", "model_version": "test", "items": [
        {"station_id": 101, "horizon_hours": 1, "target": "load_kwh", "predicted_value": 3.2, "unit": "kWh"},
        {"station_id": 101, "horizon_hours": 6, "target": "load_kwh", "predicted_value": 4.1, "unit": "kWh"},
    ]}), encoding="utf-8")
    client = create_app(tmp_path).test_client()
    assert client.get("/api/v1/analysis/health").status_code == 200
    response = client.post("/api/v1/analysis/forecast", json={"horizons_hours": [1]})
    assert response.status_code == 200
    assert len(response.get_json()["items"]) == 1
    assert response.headers["Access-Control-Allow-Origin"] == "*"


def test_missing_artifacts_degrade_without_exception(tmp_path: Path) -> None:
    client = create_app(tmp_path).test_client()
    response = client.post("/api/v1/analysis/forecast", json={"horizons_hours": [24]})
    assert response.status_code == 503
    assert response.get_json()["code"] == "MODEL_UNAVAILABLE"
    assert client.post("/api/v1/analysis/forecast", json={"horizons_hours": [2]}).status_code == 400


def test_dashboard_snapshot_contract_exposes_analytics(tmp_path: Path) -> None:
    (tmp_path / "dashboard.json").write_text(json.dumps({
        "meta": {"source": "validated SQLite Schema v0.4"},
        "updatedAt": "2026-09-14T00:00:00Z",
        "overview": {"revenueCents": 100},
        "stations": [], "piles": [], "stationUtilization": [],
        "revenue7dCents": [], "revenue30dCents": [], "demoSeries": {"points": []},
        "analytics": {"users": {"total": 1}, "orders": {"total": 0}},
    }), encoding="utf-8")
    response = create_app(tmp_path).test_client().get("/api/v1/dashboard/snapshot")
    assert response.status_code == 200
    assert response.get_json()["analytics"]["users"]["total"] == 1
