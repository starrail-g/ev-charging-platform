#!/usr/bin/env bash

# Safe Tencent Maps POI probe for the Linux VM.
# The key is read from the environment and is never printed or written to a file.

if [ -z "${TENCENT_MAP_KEY:-}" ]; then
  echo "FAIL: TENCENT_MAP_KEY is not set"
  exit 2
fi

probe_file="$(mktemp)"
trap 'rm -f "$probe_file"' EXIT

if ! curl --silent --show-error --max-time 15 --get \
  'https://apis.map.qq.com/ws/place/v1/search' \
  --data-urlencode 'keyword=充电站' \
  --data-urlencode 'boundary=nearby(22.530,113.930,5000)' \
  --data-urlencode 'orderby=distance' \
  --data-urlencode 'page_size=10' \
  --data-urlencode 'output=json' \
  --data-urlencode "key=${TENCENT_MAP_KEY}" \
  -o "$probe_file"; then
  echo "FAIL: Tencent Maps request failed or timed out"
  exit 1
fi

if command -v jq >/dev/null 2>&1; then
  jq -r 'if .status == 0 then "PASS: Tencent POI response status=0 count=\(.count // (.data | length) // 0)" else "FAIL: Tencent POI status=\(.status // "unknown") message=\(.message // "unknown")" end' "$probe_file"
  jq -r 'if .status == 0 then (.data[0] // {} | "FIRST_POI: title=\(.title // "") address=\(.address // "") lat=\(.location.lat // "") lng=\(.location.lng // "")") else empty end' "$probe_file"
else
  response_status="$(sed -n 's/.*"status"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$probe_file" | head -n 1)"
  response_message="$(sed -n 's/.*"message"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$probe_file" | head -n 1)"
  if [ "$response_status" = "0" ]; then
    echo "PASS: Tencent POI response status=0 (jq unavailable; result count not parsed)"
  else
    echo "FAIL: Tencent POI status=${response_status:-unknown} message=${response_message:-unknown}"
  fi
fi
