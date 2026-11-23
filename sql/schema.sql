-- IP inventory schema (PostgreSQL 16)

CREATE TABLE IF NOT EXISTS ip_pool (
    ip       TEXT PRIMARY KEY,
    ip_type  TEXT NOT NULL CHECK (ip_type IN ('IPv4', 'IPv6')),
    status   TEXT NOT NULL DEFAULT 'FREE'
             CHECK (status IN ('FREE', 'RESERVED', 'ASSIGNED'))
);

CREATE TABLE IF NOT EXISTS ip_reservations (
    ip           TEXT NOT NULL REFERENCES ip_pool(ip),
    service_id   TEXT NOT NULL,
    reserved_at  TIMESTAMP NOT NULL DEFAULT NOW(),
    PRIMARY KEY (ip, service_id)
);

CREATE TABLE IF NOT EXISTS ip_assignments (
    id           BIGSERIAL PRIMARY KEY,
    ip           TEXT NOT NULL REFERENCES ip_pool(ip),
    service_id   TEXT NOT NULL,
    assigned_at  TIMESTAMP NOT NULL DEFAULT NOW(),
    active       BOOLEAN NOT NULL DEFAULT TRUE
);

-- One IP has at most one active assignment.
CREATE UNIQUE INDEX IF NOT EXISTS ux_ip_assignments_active_ip
    ON ip_assignments (ip) WHERE active;

CREATE INDEX IF NOT EXISTS ix_ip_reservations_service_id
    ON ip_reservations (service_id);

CREATE INDEX IF NOT EXISTS ix_ip_assignments_service_id
    ON ip_assignments (service_id);

CREATE INDEX IF NOT EXISTS ix_ip_pool_status_type
    ON ip_pool (status, ip_type);
