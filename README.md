# IP Inventory REST API (C++ / PostgreSQL)

A small IP inventory service written in C++17:

- REST API (Crow)
- PostgreSQL backend (SOCI), with a connection pool
- Advisory locks and retried transactions around reserve / assign / terminate
- Swagger UI for the API description
- Docker Compose setup and a GitHub Actions pipeline

The project is a work in progress. Known gaps (input validation, error handling
for some edge cases, test coverage) are being worked on.

## Features

- Add IPv4 / IPv6 addresses to the pool
- Reserve an IP for a serviceId
- Assign a reserved IP
- Terminate an assignment
- Query IPs by serviceId
- Reservations older than 15 minutes are released by a background job (runs every minute)

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
src/                 C++ sources (main, ip_service, db, config)
static/              swagger.html, swagger.yaml
sql/schema.sql       database schema
tests/smoke.sh       end-to-end smoke test (bash, curl, jq)
Dockerfile           multi-stage image build
docker-compose.yml   app + PostgreSQL 16
.github/workflows/   CI
CMakeLists.txt
```

## API endpoints

| Operation | Endpoint |
|---|---|
| Add IPs to pool | `POST /ip-inventory/ip-pool` |
| Reserve IP | `POST /ip-inventory/reserve-ip` |
| Assign IP | `POST /ip-inventory/assign-ip-serviceId` |
| Terminate IP | `POST /ip-inventory/terminate-ip-serviceId` |
| Get IPs by serviceId | `GET /ip-inventory/serviceId?serviceId=xxx` |
| Health | `GET /health` |
| Swagger UI / YAML | `GET /swagger`, `GET /swagger.yaml` |

IPv6 addresses are currently accepted only in the full eight-group form
(for example `2001:db8:0:0:0:0:0:1`).

## Configuration

Environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `DB_CONN` | `dbname=ipinventory user=ipinventory password=ipinventory host=localhost port=5432` | libpq connection string |
| `PORT` | `18080` | HTTP port |
| `STATIC_DIR` | `./static` | directory with swagger.html / swagger.yaml |

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

`tests/smoke.sh` runs the main flow against a running instance
(`BASE_URL`, default `http://localhost:18080`): add pool, reserve, assign,
query, terminate.

GitHub Actions (`.github/workflows/ci.yml`) builds the project on
ubuntu-24.04, starts PostgreSQL 16, applies the schema, runs the smoke test,
and checks that `docker compose build` works.

## Concurrency

- PostgreSQL advisory locks (`pg_advisory_xact_lock`) serialize changes per IP
  and a global lock is used for reservation and for the cleanup job.
- Each operation takes its own session from a pool of 8 connections.
- Transactions are retried up to 3 times on failure.

## Example flow

1. Add IPs: `POST /ip-inventory/ip-pool`
2. Reserve: `POST /ip-inventory/reserve-ip`
3. Assign: `POST /ip-inventory/assign-ip-serviceId`
4. Query: `GET /ip-inventory/serviceId?serviceId=abc`
5. Terminate: `POST /ip-inventory/terminate-ip-serviceId`
