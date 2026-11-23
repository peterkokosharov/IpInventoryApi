# HOME-RUN: build, run and verify IpInventoryApi on a Linux Mint laptop

This sheet is for an AI coding agent (for example opencode). You execute every step yourself.

## Rules for the agent

- You run every command yourself. Do not hand commands back to the human.
- After each step compare the output with the "Expected" line. On failure: diagnose, retry, at most 2 retries,
  then stop and report the exact error output.
- Do not change any source file. Do not push (no `git push`). Do not use `sudo` except in the install part of
  step 0; if a password is needed, ask the human once.
- Finish with the report in the format at the bottom.

Facts used below (taken from the repo): app port 18080, database `ipinventory` / user `ipinventory` /
password `ipinventory`, PostgreSQL published on localhost:5432, compose services `db` and `app`.

## Step 0: preconditions

```
git --version; docker --version; docker compose version; curl --version | head -1; jq --version; psql --version
id -nG
```

Expected: every command prints a version; `id -nG` contains `docker`. `psql` is optional but needed for the
database checks of the integration suite (without it they are skipped; the suite does not fail).

If something is missing (this is the only place where `sudo` is allowed):

```
sudo apt-get update
sudo apt-get install -y git curl jq postgresql-client docker.io docker-compose-v2
sudo usermod -aG docker "$USER"
```

Note: after `usermod` a re-login is needed for the group to apply. If you cannot re-login, run the following
docker commands with `sg docker -c "<command>"`, or tell the human to log out and in once. If `docker.io` /
`docker-compose-v2` are not available, use Docker CE from docs.docker.com instead.
If port 5432 or 18080 is already used on the laptop, stop the other service (report it) before step 2.

## Step 1: get the code

```
cd ~ && if [ -d IpInventoryApi/.git ]; then cd IpInventoryApi && git pull; else git clone https://github.com/peterkokosharov/IpInventoryApi.git && cd IpInventoryApi; fi
git log -1 --oneline
```

Expected: the last command prints one commit line (hash + subject). Put it in the report. All following
commands run in `~/IpInventoryApi`.

## Step 2: start the stack

```
docker compose up -d --build
docker compose ps
```

Expected: services `db` and `app` are both running (`db` shows `healthy`). The first build compiles C++ and can
take several minutes. The `app` service has no healthcheck of its own, so "healthy" for `app` means State
`running`; step 3 proves it. If `app` is restarting, run `docker compose logs app` and report.

## Step 3: health check

```
for i in $(seq 1 30); do curl -sf http://localhost:18080/health && break; sleep 1; done; echo "exit=$?"
curl -s -o /dev/null -w '%{http_code}\n' http://localhost:18080/swagger
```

Expected: first command ends with HTTP 200 (curl -f succeeds); second prints `200`.
Swagger UI: http://localhost:18080/swagger (the human can open it in a browser; you only check the 200).

## Step 4: integration suite

Compose passes `RESERVATION_TTL_SECONDS` (default 900) and `CLEANUP_INTERVAL_SECONDS` (default 60) through to the
`app` service. With the defaults the expiry cases are SKIPPED. That is not an error. To run the expiry cases too,
recreate the app with short values (no repo edit):

```
RESERVATION_TTL_SECONDS=10 CLEANUP_INTERVAL_SECONDS=2 docker compose up -d --build
until curl -sf http://localhost:18080/health >/dev/null; do sleep 1; done
```

Run the suite (needs host `psql`; port 5432 is published by compose; the script's defaults already use
`localhost`, user and password `ipinventory`). `RESET_DB=1` truncates the tables: fine on this throwaway stack.

```
RESET_DB=1 RESERVATION_TTL_SECONDS=10 BASE_URL=http://localhost:18080 bash tests/integration.sh; echo "exit=$?"
```

(If you skipped the short values, omit `RESERVATION_TTL_SECONDS=10`; the expiry case then prints SKIP.)

Expected: only `PASS:` lines, last lines `== N passed, 0 failed, S skipped` and `INTEGRATION TESTS PASSED`,
`exit=0`. With the override and psql there should be no SKIP lines. Without host psql there are SKIPs for the
database checks, and concurrency reserve is skipped. Install `postgresql-client` (step 0) and rerun instead of
accepting that. On any `FAIL:` line, copy it verbatim into the report.

## Step 5: manual demo (5 + 1 calls)

Run on the stack after step 4. Pool may hold leftovers from the suite; use the returned IPs, do not assume them.

```
H='Content-Type: application/json'; U=http://localhost:18080/ip-inventory
curl -s -X POST -H "$H" $U/ip-pool -d '{"ipAddresses":[{"ip":"10.99.0.1","ipType":"IPv4"},{"ip":"2001:db8:99::1","ipType":"IPv6"}]}'
```
Expected: `{"statusCode":"0","statusMessage":"Successful operation. OK"}`

```
R=$(curl -s -X POST -H "$H" $U/reserve-ip -d '{"serviceId":"demo-1","ipType":"Both"}'); echo "$R"
```
Expected: `{"ipAddresses":[{"ip":"<an IPv4>","ipType":"IPv4"},{"ip":"<an IPv6, full form>","ipType":"IPv6"}]}`
(two entries, one per type; IPv6 is returned uncompressed, e.g. `2001:0db8:0099:0000:0000:0000:0000:0001`).

```
IPS=$(echo "$R" | jq -c '[.ipAddresses[] | {ip}]')
curl -s -X POST -H "$H" $U/assign-ip-serviceId -d "{\"serviceId\":\"demo-1\",\"ipAddresses\":$IPS}"
curl -s "$U/serviceId?serviceId=demo-1"
```
Expected: first the OK status JSON above, then the same two IPs as in the reserve response.

```
curl -s -X POST -H "$H" $U/serviceId-change -d '{"serviceIdOld":"demo-1","serviceId":"demo-2"}'
curl -s "$U/serviceId?serviceId=demo-2"
curl -s -X POST -H "$H" $U/terminate-ip-serviceId -d "{\"serviceId\":\"demo-2\",\"ipAddresses\":$IPS}"
curl -s "$U/serviceId?serviceId=demo-2"
```
Expected: OK status JSON; the two IPs now under demo-2; OK status JSON; finally `{"ipAddresses":[]}`.

## Step 6: clean up

```
docker compose down -v
docker compose ps
```

Expected: containers and the `pgdata` volume are removed; `docker compose ps` lists nothing. 

## Report format

Reply with this table (one evidence line per step), then the summary line, then errors:

```
| step | result PASS/FAIL | evidence (one line) |
|---|---|---|
| 0 preconditions | | versions, docker group yes/no |
| 1 clone/pull | | HEAD commit line |
| 2 compose up | | `docker compose ps` states |
| 3 health | | /health 200, /swagger 200 |
| 4 integration | | exit code, TTL override used yes/no |
| 5 demo | | final GET returned {"ipAddresses":[]} yes/no |
| 6 cleanup | | compose ps empty yes/no |

Integration summary: <the "== N passed, F failed, S skipped" line>
Errors (verbatim): <exact output of anything that failed, or "none">
```
