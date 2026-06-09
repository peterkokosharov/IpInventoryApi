IP Inventory REST API (C++ / PostgreSQL / Docker)

A production-grade IP inventory management system written in modern C++ with:

REST API (Crow)
PostgreSQL backend (SOCI ORM)
Distributed locking (advisory locks)
Retry-safe transactions
Swagger UI documentation
Dockerized deployment (Linux / Windows compatible)
🚀 Features
Core IP Lifecycle
Add IPs to pool
Reserve IPs for serviceId
Assign reserved IPs
Terminate IP assignments
Query IPs by serviceId
Production-grade capabilities
✅ Distributed locking (prevents double assignment)
✅ Retry-safe DB transactions
✅ Automatic reservation expiration (15 min)
✅ Idempotent database operations
✅ Multi-instance safe (Docker / scaling ready)
🧱 Architecture
Swagger UI
    ↓
Crow REST API (C++)
    ↓
IpService (Business Logic)
    ↓
Db Layer (SOCI ORM)
    ↓
PostgreSQL
📁 Project Structure
IPInventory/
├── src/
│   ├── main.cpp
│   ├── ip_service.h
│   ├── ip_service.cpp
│   ├── db.h
│   ├── db.cpp
│   ├── models.h
├── static/
│   ├── swagger.html
│   ├── swagger.yaml
├── docker-compose.yml
├── Dockerfile
├── CMakeLists.txt
└── README.md
⚙️ API Endpoints
📌 Add IPs to pool
POST /ip-inventory/ip-pool
📌 Reserve IP
POST /ip-inventory/reserve-ip
📌 Assign IP
POST /ip-inventory/assign-ip-serviceId
📌 Terminate IP
POST /ip-inventory/terminate-ip-serviceId
📌 Get IPs by serviceId
GET /ip-inventory/serviceId?serviceId=xxx
🌐 Swagger UI

After running the server:

http://localhost:18080/swagger

Swagger YAML:

http://localhost:18080/swagger.yaml
🐘 Database Setup (PostgreSQL)

Create database:

CREATE DATABASE ip_inventory;

Tables:

CREATE TABLE ip_pool (
    ip TEXT PRIMARY KEY,
    ip_type TEXT,
    status TEXT DEFAULT 'FREE'
);

CREATE TABLE ip_reservations (
    ip TEXT,
    service_id TEXT,
    reserved_at TIMESTAMP DEFAULT NOW()
);

CREATE TABLE ip_assignments (
    ip TEXT,
    service_id TEXT,
    assigned_at TIMESTAMP DEFAULT NOW(),
    active BOOLEAN DEFAULT TRUE
);
🐳 Docker Deployment
Build and run
docker-compose build
docker-compose up -d
Services
Service	URL
API	http://localhost:18080
Swagger UI	http://localhost:18080/swagger
PostgreSQL	localhost:5432
Stop system
docker-compose down
🔐 Concurrency Safety

This system prevents race conditions using:

✔ PostgreSQL advisory locks
pg_advisory_xact_lock(hashtext(ip))
✔ Global cleanup lock
Prevents multiple nodes from expiring reservations simultaneously
✔ Retry-safe transactions
Automatic retry on deadlocks or transient failures
⏱ Reservation Expiration
Reserved IP expires after 15 minutes
Automatically returned to FREE pool
Background cleanup job runs every minute
🧠 Key Design Principles
No shared mutable state in application layer
Database is source of truth
All operations are idempotent
Safe for horizontal scaling
Stateless API nodes
🛠 Build (Local)
mkdir build
cd build
cmake ..
cmake --build . --config Release

Run:

./IPInventoryApi
🚀 Example Flow
1. Add IPs
POST /ip-inventory/ip-pool
2. Reserve IP
POST /ip-inventory/reserve-ip
3. Assign IP
POST /ip-inventory/assign-ip-serviceId
4. Query
GET /ip-inventory/serviceId?serviceId=abc