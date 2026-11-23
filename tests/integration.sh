#!/usr/bin/env bash
# Integration test suite for a running IPInventoryApi. Needs bash, curl, jq.
# psql is optional but enables the database checks (e.g. "no IP given twice").
#
# Usage:
#   BASE_URL=http://localhost:18080 tests/integration.sh
#
# Environment:
#   BASE_URL                  default http://localhost:18080
#   RESET_DB=1                TRUNCATE all tables first (needs psql). Recommended:
#                             a few cases (pool exhaustion, exactly 5 winners)
#                             need an empty pool and are skipped otherwise.
#   RESERVATION_TTL_SECONDS   must match the server (default 900); the expiry
#                             case runs only when it is <= 30.
#   PSQL_CMD                  default: psql -h localhost -U ipinventory -d ipinventory -v ON_ERROR_STOP=1 -tA
#   PGPASSWORD                default: ipinventory
#
# Output: one PASS / FAIL / SKIP line per case, a summary, exit code 1 on any FAIL.
set -u

BASE_URL="${BASE_URL:-http://localhost:18080}"
RESET_DB="${RESET_DB:-0}"
TTL="${RESERVATION_TTL_SECONDS:-900}"
export PGPASSWORD="${PGPASSWORD:-ipinventory}"
PSQL_CMD="${PSQL_CMD:-psql -h localhost -U ipinventory -d ipinventory -v ON_ERROR_STOP=1 -tA}"

RUN="t$(date +%s)$$"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASSED=0
FAILED=0
SKIPPED=0
BODY=""
CODE=""
OKMSG="Successful operation. OK"

HAVE_PSQL=0
if command -v psql >/dev/null 2>&1; then HAVE_PSQL=1; fi

# ---------------------------------------------------------------- helpers
pass() { PASSED=$((PASSED + 1)); echo "PASS: $1"; }
fail() { FAILED=$((FAILED + 1)); echo "FAIL: $1"; }
skip() { SKIPPED=$((SKIPPED + 1)); echo "SKIP: $1"; }

# call METHOD PATH [RAW_BODY]  -> sets CODE and BODY
call() {
    local method="$1" path="$2" data="${3:-}" out
    if [ "$method" = "POST" ]; then
        out=$(curl -s -w '\n%{http_code}' -X "$method" -H 'Content-Type: application/json' \
              --data-binary "$data" "$BASE_URL$path")
    else
        out=$(curl -s -w '\n%{http_code}' -X "$method" "$BASE_URL$path")
    fi
    CODE=$(printf '%s\n' "$out" | tail -n1)
    BODY=$(printf '%s\n' "$out" | sed '$d')
}

db() { $PSQL_CMD -c "$1" 2>/dev/null; }

expect_code() {
    if [ "$CODE" = "$1" ]; then pass "$2 (HTTP $CODE)"; else fail "$2: expected HTTP $1, got $CODE, body: $BODY"; fi
}

expect_json() {
    local got
    got=$(printf '%s' "$BODY" | jq -r "$1" 2>/dev/null)
    if [ "$got" = "$2" ]; then pass "$3"; else fail "$3: '$1' expected '$2', got '$got', body: $BODY"; fi
}

expect_eq() {
    if [ "$1" = "$2" ]; then pass "$3"; else fail "$3: expected '$2', got '$1'"; fi
}

# ips_of TYPE -> IPs of that type in the last BODY
ips_of() { printf '%s' "$BODY" | jq -r --arg t "$1" '.ipAddresses[]? | select(.ipType==$t) | .ip'; }

# Short forms: ok_status / reserve / assign / terminate
reserve()   { call POST /ip-inventory/reserve-ip "{\"serviceId\":\"$1\",\"ipType\":\"$2\"}"; }
assign()    { call POST /ip-inventory/assign-ip-serviceId "{\"serviceId\":\"$1\",\"ipAddresses\":[$2]}"; }
terminate() { call POST /ip-inventory/terminate-ip-serviceId "{\"serviceId\":\"$1\",\"ipAddresses\":[$2]}"; }
ipobj()     { local out="" ip; for ip in "$@"; do out="$out{\"ip\":\"$ip\"},"; done; printf '%s' "${out%,}"; }

V6_1="2001:0db8:0000:0000:0000:0000:0000:0001"
V6_2="2001:0db8:0000:0000:0000:0000:0000:0002"

# ---------------------------------------------------------------- start
echo "== IPInventoryApi integration tests against $BASE_URL (run $RUN)"

CLEAN=0
if [ "$HAVE_PSQL" = 1 ]; then
    if [ "$RESET_DB" = 1 ]; then
        db "TRUNCATE ip_assignments, ip_reservations, ip_pool" >/dev/null && CLEAN=1
    elif [ "$(db 'SELECT COUNT(*) FROM ip_pool')" = "0" ]; then
        CLEAN=1
    fi
else
    echo "note: psql not found, database checks are skipped"
fi
[ "$CLEAN" = 1 ] || echo "note: pool is not known to be empty, exhaustion cases are skipped (use RESET_DB=1)"

# ---------------------------------------------------------------- basics
call GET /health
expect_code 200 "health"

call GET /swagger.yaml
if printf '%s' "$BODY" | grep -q 'serviceId-change'; then pass "swagger.yaml documents serviceId-change"; else fail "swagger.yaml documents serviceId-change"; fi

# ---------------------------------------------------------------- ip-pool
POOL='{"ipAddresses":[
  {"ip":"10.77.0.1","ipType":"IPv4"},
  {"ip":"10.77.0.2","ipType":"IPv4"},
  {"ip":"10.77.0.3","ipType":"IPv4"},
  {"ip":"2001:db8::1","ipType":"IPv6"},
  {"ip":"2001:0db8:0:0:0:0:0:2","ipType":"IPv6"}]}'
call POST /ip-inventory/ip-pool "$POOL"
expect_code 200 "pool: add 3 IPv4 + 2 IPv6 (one compressed IPv6)"
expect_json '.statusCode' "0" "pool: statusCode"
expect_json '.statusMessage' "$OKMSG" "pool: exact statusMessage"

call POST /ip-inventory/ip-pool "$POOL"
expect_code 200 "pool: adding the same IPs again is accepted"

if [ "$HAVE_PSQL" = 1 ]; then
    expect_eq "$(db "SELECT COUNT(*) FROM ip_pool WHERE ip='$V6_1'")" "1" "pool: compressed IPv6 stored in normalized full form"
    expect_eq "$(db "SELECT COUNT(*) FROM ip_pool WHERE ip IN ('10.77.0.1','10.77.0.2','10.77.0.3','$V6_1','$V6_2')")" "5" "pool: duplicate add created no duplicates"
else
    skip "pool: normalized storage (no psql)"
fi

call POST /ip-inventory/ip-pool '{"ipAddresses":[{"ip":"999.1.1.1","ipType":"IPv4"}]}'
expect_code 400 "pool: invalid IPv4 rejected"
expect_json '.statusCode' "1001" "pool: invalid IP statusCode 1001"

call POST /ip-inventory/ip-pool '{"ipAddresses":[{"ip":"2001:db8::1::2","ipType":"IPv6"}]}'
expect_code 400 "pool: invalid IPv6 rejected"

call POST /ip-inventory/ip-pool '{"ipAddresses":[{"ip":"10.123.0.1","ipType":"IPv4"},{"ip":"nonsense","ipType":"IPv4"}]}'
expect_code 400 "pool: batch with one bad entry rejected"
if [ "$HAVE_PSQL" = 1 ]; then
    expect_eq "$(db "SELECT COUNT(*) FROM ip_pool WHERE ip='10.123.0.1'")" "0" "pool: rejected batch stored nothing"
fi

call POST /ip-inventory/ip-pool '{"ipAddresses":[{"ip":"10.123.0.2","ipType":"IPv6"}]}'
expect_code 400 "pool: ipType does not match address (IPv4 address as IPv6)"
expect_json '.statusCode' "1003" "pool: mismatch statusCode 1003"

call POST /ip-inventory/ip-pool '{"ipAddresses":[{"ip":"2001:db8::9","ipType":"IPv4"}]}'
expect_code 400 "pool: ipType does not match address (IPv6 address as IPv4)"

call POST /ip-inventory/ip-pool '{"ipAddresses":[{"ip":"10.123.0.3","ipType":"Both"}]}'
expect_code 400 "pool: ipType Both not allowed"

call POST /ip-inventory/ip-pool '{"ipAddresses":[{"ip":"10.123.0.3"}]}'
expect_code 400 "pool: missing ipType"

call POST /ip-inventory/ip-pool '{"ipAddresses":[{"ipType":"IPv4"}]}'
expect_code 400 "pool: missing ip"

call POST /ip-inventory/ip-pool '{"ipAddresses":[]}'
expect_code 400 "pool: empty list"

call POST /ip-inventory/ip-pool '{"ipAddresses":"10.0.0.1"}'
expect_code 400 "pool: ipAddresses is not an array"

call POST /ip-inventory/ip-pool '{"ipAddresses":[{"ip":5,"ipType":"IPv4"}]}'
expect_code 400 "pool: ip has wrong type (number)"

call POST /ip-inventory/ip-pool '{"foo":1}'
expect_code 400 "pool: missing ipAddresses"

call POST /ip-inventory/ip-pool 'this is not json'
expect_code 400 "pool: invalid JSON"

call POST /ip-inventory/ip-pool ''
expect_code 400 "pool: empty body"

call POST /ip-inventory/ip-pool '[1,2]'
expect_code 400 "pool: body is an array, not an object"

# ---------------------------------------------------------------- reserve
SA="$RUN-A"; SB="$RUN-B"; SC="$RUN-C"; SD="$RUN-D"; SE="$RUN-E"

reserve "$SA" IPv4
expect_code 200 "reserve: IPv4"
expect_json '.ipAddresses | length' "1" "reserve: exactly one address"
expect_json '.ipAddresses[0].ipType' "IPv4" "reserve: type IPv4"
A4=$(ips_of IPv4)
case "$A4" in 10.77.0.[123]) pass "reserve: IPv4 comes from the pool ($A4)" ;; *) fail "reserve: unexpected IPv4 '$A4'" ;; esac

reserve "$SA" IPv4
expect_code 200 "reserve: repeated call"
expect_eq "$(ips_of IPv4)" "$A4" "reserve: idempotent, same IP returned"

reserve "$SB" Both
expect_code 200 "reserve: Both"
expect_json '.ipAddresses | length' "2" "reserve: Both returns two addresses"
B4=$(ips_of IPv4); B6=$(ips_of IPv6)
if [ -n "$B4" ] && [ -n "$B6" ]; then pass "reserve: Both returns one IPv4 and one IPv6"; else fail "reserve: Both returns one IPv4 and one IPv6 (got '$B4' '$B6')"; fi
if [ "$B4" != "$A4" ]; then pass "reserve: another service gets another IPv4"; else fail "reserve: IPv4 given twice ($A4)"; fi

reserve "$SB" Both
expect_eq "$(ips_of IPv4)/$(ips_of IPv6)" "$B4/$B6" "reserve: Both is idempotent (same pair)"

reserve "$SC" IPv6
expect_code 200 "reserve: IPv6"
C6=$(ips_of IPv6)
if [ -n "$C6" ] && [ "$C6" != "$B6" ]; then pass "reserve: IPv6 differs from the one reserved by B"; else fail "reserve: IPv6 '$C6' vs '$B6'"; fi

if [ "$CLEAN" = 1 ]; then
    reserve "$SD" IPv6
    expect_code 409 "reserve: no free IPv6 left"
    expect_json '.statusCode' "2001" "reserve: no free IP statusCode 2001"

    reserve "$SD" Both
    expect_code 409 "reserve: Both fails when IPv6 is exhausted"

    reserve "$SE" IPv4
    expect_code 200 "reserve: failed Both did not consume the last IPv4 (all or nothing)"
else
    skip "reserve: exhaustion cases (pool not empty)"
    reserve "$SE" IPv4
    expect_code 200 "reserve: IPv4 for service E"
fi
E4=$(ips_of IPv4)

reserve "$SD" Foo
expect_code 400 "reserve: unknown ipType"
reserve "$SD" ipv4
expect_code 400 "reserve: ipType is case sensitive"
call POST /ip-inventory/reserve-ip '{"ipType":"IPv4"}'
expect_code 400 "reserve: missing serviceId"
call POST /ip-inventory/reserve-ip '{"serviceId":"x"}'
expect_code 400 "reserve: missing ipType"
call POST /ip-inventory/reserve-ip '{"serviceId":123,"ipType":"IPv4"}'
expect_code 400 "reserve: serviceId has wrong type (number)"
call POST /ip-inventory/reserve-ip '{"serviceId":"","ipType":"IPv4"}'
expect_code 400 "reserve: empty serviceId"
call POST /ip-inventory/reserve-ip ''
expect_code 400 "reserve: empty body"
call POST /ip-inventory/reserve-ip '{broken'
expect_code 400 "reserve: invalid JSON"

# ---------------------------------------------------------------- assign
assign "$SE" "$(ipobj "$A4")"
expect_code 409 "assign: IP reserved by another service"
expect_json '.statusCode' "3001" "assign: statusCode 3001"

assign "$SA" "$(ipobj 10.250.0.1)"
expect_code 404 "assign: IP not in the pool"
expect_json '.statusCode' "3002" "assign: statusCode 3002"

assign "$SA" "$(ipobj 999.0.0.1)"
expect_code 400 "assign: invalid IP"
call POST /ip-inventory/assign-ip-serviceId "{\"serviceId\":\"$SA\"}"
expect_code 400 "assign: missing ipAddresses"
call POST /ip-inventory/assign-ip-serviceId "{\"ipAddresses\":[{\"ip\":\"$A4\"}]}"
expect_code 400 "assign: missing serviceId"
call POST /ip-inventory/assign-ip-serviceId "{\"serviceId\":\"$SA\",\"ipAddresses\":[{}]}"
expect_code 400 "assign: entry without ip"
call POST /ip-inventory/assign-ip-serviceId ''
expect_code 400 "assign: empty body"

# all or nothing: E's own IP plus an IP reserved for B
assign "$SE" "$(ipobj "$E4" "$B4")"
expect_code 409 "assign: request with one foreign IP fails as a whole"
assign "$SE" "$(ipobj "$E4")"
expect_code 200 "assign: E's own IP still reserved after the failed request (rollback)"
expect_json '.statusMessage' "$OKMSG" "assign: exact statusMessage"

assign "$SE" "$(ipobj "$E4")"
expect_code 409 "assign: already assigned IP"

assign "$SB" "$(ipobj "$B4" "$B6")"
expect_code 200 "assign: both IPs of B in one request"

call GET "/ip-inventory/serviceId?serviceId=$SB"
expect_code 200 "get: B after assign"
expect_json '.ipAddresses | length' "2" "get: B has two IPs"
expect_eq "$(ips_of IPv4)/$(ips_of IPv6)" "$B4/$B6" "get: B returns its IPv4 and IPv6"
expect_json '.ipAddresses[0] | keys | join(",")' "ip,ipType" "get: items have exactly ip and ipType"

assign "$SA" "$(ipobj "$A4")"
expect_code 200 "assign: A"

# ---------------------------------------------------------------- get
call GET "/ip-inventory/serviceId?serviceId=$SA"
expect_code 200 "get: A"
expect_json '.ipAddresses[0].ip' "$A4" "get: A returns its IP"
expect_json '.ipAddresses[0].ipType' "IPv4" "get: A type"

call GET "/ip-inventory/serviceId?serviceId=$RUN-unknown"
expect_code 200 "get: unknown service"
expect_json '.ipAddresses | type' "array" "get: unknown service returns an array, not null"
expect_json '.ipAddresses | length' "0" "get: unknown service returns empty list"
if [ "$BODY" = '{"ipAddresses":[]}' ]; then pass "get: empty body is exactly {\"ipAddresses\":[]}"; else fail "get: empty body, got $BODY"; fi

call GET "/ip-inventory/serviceId"
expect_code 400 "get: missing serviceId"
expect_json '.statusCode' "5001" "get: statusCode 5001"
call GET "/ip-inventory/serviceId?serviceId="
expect_code 400 "get: empty serviceId"

# reserved-only IPs are not "associated": C only reserved
call GET "/ip-inventory/serviceId?serviceId=$SC"
expect_json '.ipAddresses | length' "0" "get: reserved but not assigned IP is not returned"

# ---------------------------------------------------------------- terminate
terminate "$SE" "$(ipobj "$A4")"
expect_code 409 "terminate: IP assigned to another service"
expect_json '.statusCode' "4001" "terminate: statusCode 4001"

terminate "$SA" "$(ipobj 10.250.0.1)"
expect_code 404 "terminate: IP not in the pool"

terminate "$SA" "$(ipobj abc)"
expect_code 400 "terminate: invalid IP"
call POST /ip-inventory/terminate-ip-serviceId "{\"serviceId\":\"$SA\"}"
expect_code 400 "terminate: missing ipAddresses"
call POST /ip-inventory/terminate-ip-serviceId ''
expect_code 400 "terminate: empty body"

# all or nothing: A's IP plus B's IP
terminate "$SA" "$(ipobj "$A4" "$B4")"
expect_code 409 "terminate: request with one foreign IP fails as a whole"
call GET "/ip-inventory/serviceId?serviceId=$SA"
expect_json '.ipAddresses | length' "1" "terminate: A still has its IP after the failed request (rollback)"

terminate "$SA" "$(ipobj "$A4")"
expect_code 200 "terminate: A"
expect_json '.statusMessage' "$OKMSG" "terminate: exact statusMessage"

terminate "$SA" "$(ipobj "$A4")"
expect_code 409 "terminate: second terminate rejected"

terminate "$SB" "$(ipobj "$B4" "$B6")"
expect_code 200 "terminate: both IPs of B"
call GET "/ip-inventory/serviceId?serviceId=$SB"
expect_json '.ipAddresses | length' "0" "get: B has no IPs after terminate"

# freed IPs can be reserved again
SF="$RUN-F"
reserve "$SF" IPv4
expect_code 200 "reserve: IP freed by terminate is available again"
F4=$(ips_of IPv4)

# ---------------------------------------------------------------- serviceId-change
call POST /ip-inventory/serviceId-change "{\"serviceIdOld\":\"$SE\",\"serviceId\":\"$RUN-E2\"}"
expect_code 200 "change: assigned IP moves to the new serviceId"
expect_json '.statusCode' "0" "change: statusCode 0"
expect_json '.statusMessage' "$OKMSG" "change: exact statusMessage"

call GET "/ip-inventory/serviceId?serviceId=$SE"
expect_json '.ipAddresses | length' "0" "change: old serviceId has no IPs"
call GET "/ip-inventory/serviceId?serviceId=$RUN-E2"
expect_json '.ipAddresses[0].ip' "$E4" "change: new serviceId has the IP"

call POST /ip-inventory/serviceId-change "{\"serviceIdOld\":\"$SE\",\"serviceId\":\"$RUN-E3\"}"
expect_code 404 "change: old serviceId without IPs"
expect_json '.statusCode' "6001" "change: statusCode 6001"

call POST /ip-inventory/serviceId-change "{\"serviceIdOld\":\"$RUN-E2\"}"
expect_code 400 "change: missing serviceId"
call POST /ip-inventory/serviceId-change "{\"serviceId\":\"$RUN-E2\"}"
expect_code 400 "change: missing serviceIdOld"
call POST /ip-inventory/serviceId-change "{\"serviceIdOld\":\"$RUN-E2\",\"serviceId\":\"$RUN-E2\"}"
expect_code 400 "change: same old and new serviceId"
call POST /ip-inventory/serviceId-change "{\"serviceIdOld\":1,\"serviceId\":\"x\"}"
expect_code 400 "change: wrong type"
call POST /ip-inventory/serviceId-change ''
expect_code 400 "change: empty body"

terminate "$SE" "$(ipobj "$E4")"
expect_code 409 "change: old serviceId can no longer terminate"
terminate "$RUN-E2" "$(ipobj "$E4")"
expect_code 200 "change: new serviceId can terminate"

# a reservation moves with the serviceId, too
call POST /ip-inventory/serviceId-change "{\"serviceIdOld\":\"$SF\",\"serviceId\":\"$RUN-F2\"}"
expect_code 200 "change: reservation moves to the new serviceId"
reserve "$RUN-F2" IPv4
expect_eq "$(ips_of IPv4)" "$F4" "change: new serviceId sees the moved reservation (same IP)"
assign "$RUN-F2" "$(ipobj "$F4")"
expect_code 200 "change: new serviceId can assign the moved reservation"
terminate "$RUN-F2" "$(ipobj "$F4")"
expect_code 200 "change: cleanup of F2"

# ---------------------------------------------------------------- unknown route / method
call GET /ip-inventory/nothing
expect_code 404 "unknown route returns 404"

# ---------------------------------------------------------------- expiry
if [ "$TTL" -le 30 ] 2>/dev/null; then
    SX="$RUN-X"
    reserve "$SX" IPv4
    X4=$(ips_of IPv4)
    sleep $((TTL + 3))
    assign "$SX" "$(ipobj "$X4")"
    expect_code 409 "expiry: assign after the reservation expired is rejected"
    expect_json '.statusCode' "3001" "expiry: statusCode 3001"
    if [ "$HAVE_PSQL" = 1 ]; then
        expect_eq "$(db "SELECT status FROM ip_pool WHERE ip='$X4'")" "FREE" "expiry: IP is FREE again (cleaner job)"
        expect_eq "$(db "SELECT COUNT(*) FROM ip_reservations WHERE service_id='$SX'")" "0" "expiry: reservation row removed"
    else
        skip "expiry: database state (no psql)"
    fi
    reserve "$RUN-X2" IPv4
    expect_code 200 "expiry: IP can be reserved by someone else afterwards"
else
    skip "expiry: needs RESERVATION_TTL_SECONDS <= 30 (is $TTL)"
fi

# ---------------------------------------------------------------- concurrent assign of one IP
call POST /ip-inventory/ip-pool '{"ipAddresses":[{"ip":"10.77.2.1","ipType":"IPv4"}]}'
SZ="$RUN-Z"
reserve "$SZ" IPv4
Z4=$(ips_of IPv4)
for i in 1 2 3 4 5; do
    ( curl -s -o /dev/null -w '%{http_code}\n' -X POST -H 'Content-Type: application/json' \
        -d "{\"serviceId\":\"$SZ\",\"ipAddresses\":[{\"ip\":\"$Z4\"}]}" \
        "$BASE_URL/ip-inventory/assign-ip-serviceId" > "$TMP/pa$i" ) &
done
wait
OKS=$(cat "$TMP"/pa? | grep -c '^200$')
CONFLICTS=$(cat "$TMP"/pa? | grep -c '^409$')
expect_eq "$OKS/$CONFLICTS" "1/4" "concurrency: 5 parallel assigns of one IP -> 1 x 200, 4 x 409"
call GET "/ip-inventory/serviceId?serviceId=$SZ"
expect_json '.ipAddresses | length' "1" "concurrency: IP assigned exactly once"
terminate "$SZ" "$(ipobj "$Z4")"
expect_code 200 "concurrency: cleanup"

# ---------------------------------------------------------------- concurrent reserve (must run last: exhausts IPv4)
if [ "$HAVE_PSQL" = 1 ]; then
    if [ "$CLEAN" = 1 ]; then
        db "TRUNCATE ip_assignments, ip_reservations, ip_pool" >/dev/null
    fi
    call POST /ip-inventory/ip-pool '{"ipAddresses":[
      {"ip":"10.88.0.1","ipType":"IPv4"},{"ip":"10.88.0.2","ipType":"IPv4"},
      {"ip":"10.88.0.3","ipType":"IPv4"},{"ip":"10.88.0.4","ipType":"IPv4"},
      {"ip":"10.88.0.5","ipType":"IPv4"}]}'
    expect_code 200 "concurrency: pool prepared"

    FREE=$(db "SELECT COUNT(*) FROM ip_pool WHERE status='FREE' AND ip_type='IPv4'")
    N=20
    if [ "$FREE" -lt "$N" ]; then WANT=$FREE; else WANT=$N; fi
    if [ "$CLEAN" = 1 ]; then expect_eq "$FREE" "5" "concurrency: exactly 5 free IPv4 before the race"; fi

    for i in $(seq 1 $N); do
        ( curl -s -o "$TMP/r$i.body" -w '%{http_code}\n' -X POST -H 'Content-Type: application/json' \
            -d "{\"serviceId\":\"$RUN-c$i\",\"ipType\":\"IPv4\"}" \
            "$BASE_URL/ip-inventory/reserve-ip" > "$TMP/r$i.code" ) &
    done
    wait

    OKS=$(cat "$TMP"/r*.code | grep -c '^200$')
    CONFLICTS=$(cat "$TMP"/r*.code | grep -c '^409$')
    expect_eq "$OKS" "$WANT" "concurrency: $N parallel reserves, $FREE free IPs -> $WANT succeed"
    expect_eq "$CONFLICTS" "$((N - WANT))" "concurrency: all others get 409"

    GIVEN=$(for i in $(seq 1 $N); do
                if [ "$(cat "$TMP/r$i.code")" = "200" ]; then jq -r '.ipAddresses[0].ip' "$TMP/r$i.body"; fi
            done)
    DUPS=$(printf '%s\n' "$GIVEN" | grep -v '^$' | sort | uniq -d | wc -l | tr -d ' ')
    expect_eq "$DUPS" "0" "concurrency: no IP in more than one response"

    expect_eq "$(db "SELECT COUNT(DISTINCT ip) FROM ip_reservations WHERE service_id LIKE '$RUN-c%'")" "$WANT" "concurrency (db): $WANT distinct IPs reserved"
    expect_eq "$(db "SELECT COUNT(*) FROM (SELECT ip FROM ip_reservations GROUP BY ip HAVING COUNT(*) > 1) d")" "0" "concurrency (db): no IP reserved for two services"
    expect_eq "$(db "SELECT COUNT(*) FROM ip_pool WHERE status='RESERVED' AND ip_type='IPv4'")" "$WANT" "concurrency (db): pool status matches"
else
    skip "concurrency: parallel reserve needs psql for the database checks"
fi

# ---------------------------------------------------------------- summary
echo
echo "== $PASSED passed, $FAILED failed, $SKIPPED skipped"
if [ "$FAILED" -ne 0 ]; then echo "INTEGRATION TESTS FAILED"; exit 1; fi
echo "INTEGRATION TESTS PASSED"
