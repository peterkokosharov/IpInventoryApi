# IP Inventory REST API (C++ / PostgreSQL)

A small IP inventory service written in C++17:

- REST API (Crow)
- PostgreSQL backend (SOCI), with a connection pool
- Advisory locks and retried transactions around reserve / assign / terminate
- Swagger UI for the API description
- Docker Compose setup and a GitHub Actions pipeline

Task: see `IP_inventory_task.pdf`. See "Limitations" below for what the service does not do.

## Features

- Add IPv4 / IPv6 addresses to the pool
- Reserve an IP for a serviceId
- Reserve one IPv4, one IPv6 or both (`Both`) for a serviceId; repeated calls return the same IPs
- Assign reserved IPs, terminate an assignment, move assignments to a new serviceId
- Query IPs by serviceId
- Reservations that are not assigned in time (default 15 minutes) are released by a background job

## Architecture

```
Swagger UI
    |
Crow REST API (C++)
    |
IpService (business logic)
    |
Db layer (SOCI, connection pool)
    |
PostgreSQL
```

## Project structure

```
src/                 C++ sources (main, ip_service, db, ip_utils, config)
static/              swagger.html, swagger.yaml
sql/schema.sql       database schema
tests/integration.sh integration test suite (bash, curl, jq, optional psql)
tests/test_ip_utils.cpp  unit test for the IP helpers (ctest)
Dockerfile           multi-stage image build
docker-compose.yml   app + PostgreSQL 16
.github/workflows/   CI
CMakeLists.txt
```

## API endpoints

| Operation | Endpoint | Success | Errors |
|---|---|---|---|
| Add IPs to pool | `POST /ip-inventory/ip-pool` | 200 `{statusCode, statusMessage}` | 400 |
| Reserve IP (IPv4 / IPv6 / Both) | `POST /ip-inventory/reserve-ip` | 200 `{ipAddresses:[{ip, ipType}]}` | 400, 409 no free IP |
| Assign IPs | `POST /ip-inventory/assign-ip-serviceId` | 200 status | 400, 404 IP not in pool, 409 not reserved / expired / already assigned |
| Terminate assignment | `POST /ip-inventory/terminate-ip-serviceId` | 200 status | 400, 404 IP not in pool, 409 not assigned to this serviceId |
| Change serviceId | `POST /ip-inventory/serviceId-change` | 200 status | 400, 404 nothing for serviceIdOld |
| IPs of a serviceId | `GET /ip-inventory/serviceId?serviceId=xxx` | 200 `{ipAddresses:[...]}` (empty array if none) | 400 |
| Health | `GET /health` | 200 | |
| Swagger UI / YAML | `GET /swagger`, `GET /swagger.yaml` | | |

Success status body: `{"statusCode":"0","statusMessage":"Successful operation. OK"}`.
Error body: `{"statusCode":"<code>","statusMessage":"Bad request|Conflict|Not found|Internal error","details":"..."}`
(codes are listed in `static/swagger.yaml`). Unexpected failures give 500.

IPs: IPv4 and IPv6 are validated with `inet_pton`; `ipType` must match the address.
IPv6 may be sent compressed (`2001:db8::1`); it is stored and returned in the
full form `2001:0db8:0000:0000:0000:0000:0000:0001`.

## Configuration

Environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `DB_CONN` | `dbname=ipinventory user=ipinventory password=ipinventory host=localhost port=5432` | libpq connection string |
| `PORT` | `18080` | HTTP port |
| `STATIC_DIR` | `./static` | directory with swagger.html / swagger.yaml |
| `RESERVATION_TTL_SECONDS` | `900` | a reservation not assigned within this time expires |
| `CLEANUP_INTERVAL_SECONDS` | `60` | how often the cleaner job releases expired reservations |

At startup the application retries the database connection for about 30 seconds.

## Run with Docker Compose

```
docker compose up --build
```

- API: http://localhost:18080
- Swagger UI: http://localhost:18080/swagger
- PostgreSQL: localhost:5432 (database, user and password `ipinventory`)

The schema in `sql/schema.sql` is applied automatically when the database
volume is created. Stop with `docker compose down` (add `-v` to drop the data).

## Build locally (Linux)

Dependencies on Ubuntu 24.04:

```
sudo apt-get install build-essential cmake libasio-dev libsoci-dev libpq-dev libboost-dev
```

Crow is downloaded by CMake (pinned release v1.2.0).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
psql -U ipinventory -d ipinventory -f sql/schema.sql   # once, on an empty database
./build/IPInventoryApi
```

## Database

Tables `ip_pool`, `ip_reservations`, `ip_assignments` are defined in
`sql/schema.sql`, including primary keys, CHECK constraints on status and
IP type, a unique index that allows at most one active assignment per IP,
and indexes on `service_id`.

## Tests and CI

Unit test (no database), after building:

```
ctest --test-dir build --output-on-failure
```

Integration suite against a running instance (needs bash, curl, jq; psql is
optional and enables the database checks):

```
RESET_DB=1 BASE_URL=http://localhost:18080 tests/integration.sh
```

It prints PASS / FAIL / SKIP per case and exits with 1 on any FAIL. It covers every
endpoint, `Both`, idempotent reserve, serviceId-change, exact error codes (400 / 404 /
409), all-or-nothing requests, expiry and concurrency (20 parallel reserves ->
exactly as many winners as free IPs, no IP given twice, checked in the database).
`RESET_DB=1` truncates all tables first (use it only on a test database); without it the
cases that need an empty pool are skipped. For the expiry case start the server with a
short `RESERVATION_TTL_SECONDS` (<= 30, e.g. 10) and `CLEANUP_INTERVAL_SECONDS=2`, and
export the same `RESERVATION_TTL_SECONDS` for the script.

GitHub Actions (`.github/workflows/ci.yml`) builds the project on ubuntu-24.04, runs
ctest, starts PostgreSQL 16, applies the schema, runs the integration suite with a
short reservation TTL, and checks that `docker compose build` works.

## Concurrency

- Every request that changes data is ONE transaction (all or nothing).
- All such transactions take the same PostgreSQL advisory lock first
  (`pg_advisory_xact_lock`), and the state is re-checked inside it. This also keeps the
  cleaner job from racing with reserve / assign. It is simple and safe, but it
  serializes writes (see Limitations).
- Each operation takes its own session from a pool of 8 connections.
- Only serialization failures and deadlocks (SQLSTATE 40001 / 40P01) are retried (up to 3
  times). Business errors (no free IP, not reserved, ...) are never retried.

## Limitations

- Writes are serialized by one global lock: fine for an inventory of this kind, not for
  very high write rates or many application instances behind a load balancer
  (it works across instances, but only one writer at a time).
- No authentication or TLS; no pagination; the pool cannot remove IPs.
- `GET /ip-inventory/serviceId` returns assigned IPs only, not reserved ones.
- Reservation expiry uses the database clock and a periodic job; an expired but not yet
  released reservation is already refused by assign and reclaimed by the next reserve.
- The `ip-pool` GUI from the task description ("nice to have") is not implemented; use
  Swagger UI.

## Example flow

1. Add IPs: `POST /ip-inventory/ip-pool`
2. Reserve: `POST /ip-inventory/reserve-ip`
3. Assign: `POST /ip-inventory/assign-ip-serviceId`
4. Query: `GET /ip-inventory/serviceId?serviceId=abc`
5. Terminate: `POST /ip-inventory/terminate-ip-serviceId`
