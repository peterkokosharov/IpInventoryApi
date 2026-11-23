#!/usr/bin/env bash
# Smoke test for a running IPInventoryApi. Needs bash, curl, jq.
# Usage: BASE_URL=http://localhost:18080 tests/smoke.sh
set -u

BASE_URL="${BASE_URL:-http://localhost:18080}"
SVC="smoke-$(date +%s)-$$"
FAILED=0
BODY=""
CODE=""

fail() { echo "FAIL: $*"; FAILED=1; }

# call METHOD PATH [JSON]  -> sets CODE and BODY
call() {
    local method="$1" path="$2" data="${3:-}" out
    if [ -n "$data" ]; then
        out=$(curl -s -w '\n%{http_code}' -X "$method" -H 'Content-Type: application/json' \
              -d "$data" "$BASE_URL$path")
    else
        out=$(curl -s -w '\n%{http_code}' -X "$method" "$BASE_URL$path")
    fi
    CODE=$(printf '%s\n' "$out" | tail -n1)
    BODY=$(printf '%s\n' "$out" | sed '$d')
}

expect_code() {
    local want="$1" what="$2"
    if [ "$CODE" = "$want" ]; then echo "ok:   $what ($CODE)"; else fail "$what: expected HTTP $want, got $CODE, body: $BODY"; fi
}

expect_json() {
    local filter="$1" want="$2" what="$3" got
    got=$(printf '%s' "$BODY" | jq -r "$filter" 2>/dev/null)
    if [ "$got" = "$want" ]; then echo "ok:   $what"; else fail "$what: '$filter' expected '$want', got '$got', body: $BODY"; fi
}

call GET /health
expect_code 200 "health"

call POST /ip-inventory/ip-pool '{"ipAddresses":[
  {"ip":"10.77.0.1","ipType":"IPv4"},
  {"ip":"10.77.0.2","ipType":"IPv4"},
  {"ip":"2001:db8:0:0:0:0:0:1","ipType":"IPv6"}]}'
expect_code 200 "add pool"
expect_json '.statusCode' "0" "add pool statusCode"

call POST /ip-inventory/ip-pool '{"ipAddresses":[{"ip":"999.1.1.1","ipType":"IPv4"}]}'
expect_code 400 "invalid IP rejected"
expect_json '.statusCode' "1001" "invalid IP statusCode"

call POST /ip-inventory/reserve-ip "{\"serviceId\":\"$SVC\",\"ipType\":\"IPv4\"}"
expect_code 200 "reserve IPv4"
expect_json '.ipAddresses[0].ipType' "IPv4" "reserved type"
IP=$(printf '%s' "$BODY" | jq -r '.ipAddresses[0].ip')
case "$IP" in 10.77.0.1|10.77.0.2) echo "ok:   reserved $IP" ;; *) fail "unexpected reserved ip '$IP'" ;; esac

call POST /ip-inventory/assign-ip-serviceId "{\"serviceId\":\"other-$SVC\",\"ipAddresses\":[{\"ip\":\"$IP\"}]}"
expect_code 400 "assign by other service rejected"
expect_json '.statusCode' "3001" "assign by other service statusCode"

call POST /ip-inventory/assign-ip-serviceId "{\"serviceId\":\"$SVC\",\"ipAddresses\":[{\"ip\":\"$IP\"}]}"
expect_code 200 "assign"
expect_json '.statusCode' "0" "assign statusCode"

call GET "/ip-inventory/serviceId?serviceId=$SVC"
expect_code 200 "get by serviceId"
expect_json '.ipAddresses[0].ip' "$IP" "get returns assigned ip"
expect_json '.ipAddresses[0].ipType' "IPv4" "get returns type"

call GET "/ip-inventory/serviceId"
expect_code 400 "get without serviceId"
expect_json '.statusCode' "5001" "get without serviceId statusCode"

call POST /ip-inventory/reserve-ip "{\"serviceId\":\"$SVC-v6\",\"ipType\":\"IPv6\"}"
expect_code 200 "reserve IPv6"
expect_json '.ipAddresses[0].ip' "2001:db8:0:0:0:0:0:1" "reserved IPv6 address"

call POST /ip-inventory/terminate-ip-serviceId "{\"serviceId\":\"$SVC\",\"ipAddresses\":[{\"ip\":\"$IP\"}]}"
expect_code 200 "terminate"
expect_json '.statusCode' "0" "terminate statusCode"

call POST /ip-inventory/terminate-ip-serviceId "{\"serviceId\":\"$SVC\",\"ipAddresses\":[{\"ip\":\"$IP\"}]}"
expect_code 400 "second terminate rejected"
expect_json '.statusCode' "4001" "second terminate statusCode"

call GET "/ip-inventory/serviceId?serviceId=$SVC"
expect_code 200 "get after terminate"

if [ "$FAILED" -ne 0 ]; then echo "SMOKE FAILED"; exit 1; fi
echo "SMOKE PASSED"
