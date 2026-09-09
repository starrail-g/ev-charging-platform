#!/usr/bin/env bash

# Safe Tencent Maps POI probe for Ubuntu. The key is never printed or written.

if [ -z "${TENCENT_MAP_KEY:-}" ]; then
  echo "FAIL: interface=place.search reason=key_not_configured degraded=true"
  exit 2
fi

if [ "${TENCENT_MAP_ENABLED:-1}" = "0" ]; then
  echo "FAIL: interface=place.search reason=map_disabled degraded=true"
  exit 2
fi

probe_file="$(mktemp)"
trap 'rm -f "$probe_file"' EXIT

http_status="$(curl --silent --show-error --max-time 15 --get \
  'https://apis.map.qq.com/ws/place/v1/search' \
  --data-urlencode 'keyword=充电站' \
  --data-urlencode 'boundary=nearby(22.530,113.930,1000)' \
  --data-urlencode 'orderby=_distance' \
  --data-urlencode 'page_size=10' \
  --data-urlencode 'output=json' \
  --data-urlencode "key=${TENCENT_MAP_KEY}" \
  --output "$probe_file" \
  --write-out '%{http_code}')"
curl_status=$?

if [ "$curl_status" -ne 0 ]; then
  echo "FAIL: interface=place.search http_status=unavailable reason=network_or_timeout degraded=true"
  exit 1
fi

if [ "$http_status" -lt 200 ] || [ "$http_status" -ge 300 ]; then
  echo "FAIL: interface=place.search http_status=${http_status} reason=http_error degraded=true"
  exit 1
fi

if command -v jq >/dev/null 2>&1; then
  summary="$(jq -r --arg http "$http_status" '
    (.status // -1) as $status |
    (.data // []) as $data |
    ($data | length) as $count |
    ([ $data[]? | ((.id | type) == "string" and (.id | length) > 0 and
                    (.title | type) == "string" and (.title | length) > 0 and
                    (.address | type) == "string" and (.address | length) > 0 and
                    (.location.lat | type) == "number" and
                    (.location.lng | type) == "number") ] | all) as $complete |
    "interface=place.search http_status=\($http) tencent_status=\($status) result_count=\($count) fields_complete=\($complete) degraded=\($status != 0 or $count == 0 or ($complete | not))"
  ' "$probe_file" 2>/dev/null)"
else
  summary="$(python3 - "$probe_file" "$http_status" <<'PY'
import json
import sys

try:
    with open(sys.argv[1], encoding="utf-8") as source:
        payload = json.load(source)
    data = payload.get("data") or []
    complete = all(
        isinstance(item.get("id"), str) and bool(item.get("id"))
        and isinstance(item.get("title"), str) and bool(item.get("title"))
        and isinstance(item.get("address"), str) and bool(item.get("address"))
        and isinstance((item.get("location") or {}).get("lat"), (int, float))
        and isinstance((item.get("location") or {}).get("lng"), (int, float))
        for item in data
    )
    status = payload.get("status", -1)
    print(f"interface=place.search http_status={sys.argv[2]} tencent_status={status} "
          f"result_count={len(data)} fields_complete={str(complete).lower()} "
          f"degraded={str(status != 0 or not data or not complete).lower()}")
except Exception:
    print(f"interface=place.search http_status={sys.argv[2]} tencent_status=unparseable "
          "result_count=unknown fields_complete=false degraded=true")
PY
)"
fi

if [ -z "$summary" ]; then
  echo "FAIL: interface=place.search http_status=${http_status} reason=parse_error degraded=true"
  exit 1
fi

case "$summary" in
  *"tencent_status=0"*"fields_complete=true"*"degraded=false"*) echo "PASS: $summary" ;;
  *) echo "FAIL: $summary"; exit 1 ;;
esac
