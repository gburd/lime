-- ======================================================================
-- SQLite Compatibility Extension - Test Queries
--
-- These queries exercise the SQLite-specific syntax supported by the
-- sqlite_compat grammar extension.  Each section corresponds to a
-- feature area with both valid and (commented) invalid examples.
-- ======================================================================

-- ======================================================================
-- 1. WITHOUT ROWID tables
-- ======================================================================

-- Basic WITHOUT ROWID table
CREATE TABLE config (
    key TEXT PRIMARY KEY,
    value TEXT
) WITHOUT ROWID;

-- WITHOUT ROWID with composite primary key
CREATE TABLE lookup (
    category TEXT,
    name TEXT,
    value BLOB,
    PRIMARY KEY (category, name)
) WITHOUT ROWID;

-- STRICT table (SQLite 3.37+)
CREATE TABLE measurements (
    id INTEGER PRIMARY KEY,
    sensor TEXT NOT NULL,
    reading REAL NOT NULL,
    timestamp TEXT NOT NULL
) STRICT;

-- Combined STRICT + WITHOUT ROWID
CREATE TABLE settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
) STRICT, WITHOUT ROWID;

-- ======================================================================
-- 2. AUTOINCREMENT
-- ======================================================================

-- INTEGER PRIMARY KEY AUTOINCREMENT (monotonically increasing rowids)
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT UNIQUE
);

-- AUTOINCREMENT with additional columns
CREATE TABLE events (
    event_id INTEGER PRIMARY KEY AUTOINCREMENT,
    event_type TEXT NOT NULL,
    payload BLOB,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP
);

-- Note: AUTOINCREMENT is only valid on INTEGER PRIMARY KEY
-- The following would be an error:
-- CREATE TABLE bad (id TEXT PRIMARY KEY AUTOINCREMENT, name TEXT);

-- ======================================================================
-- 3. ON CONFLICT (UPSERT)
-- ======================================================================

-- Basic upsert: update on conflict
INSERT INTO users (id, name, email) VALUES (1, 'Alice', 'alice@example.com')
ON CONFLICT(id) DO UPDATE SET name = excluded.name, email = excluded.email;

-- Upsert with DO NOTHING (ignore conflicts)
INSERT INTO users (id, name) VALUES (1, 'Alice')
ON CONFLICT(id) DO NOTHING;

-- Upsert on UNIQUE constraint
INSERT INTO users (name, email) VALUES ('Bob', 'bob@example.com')
ON CONFLICT(email) DO UPDATE SET name = excluded.name;

-- INSERT OR REPLACE (older SQLite syntax)
INSERT OR REPLACE INTO config (key, value) VALUES ('theme', 'dark');

-- INSERT OR IGNORE
INSERT OR IGNORE INTO config (key, value) VALUES ('theme', 'light');

-- INSERT OR ROLLBACK
INSERT OR ROLLBACK INTO users (name, email) VALUES ('Charlie', 'charlie@example.com');

-- INSERT OR ABORT (default behavior)
INSERT OR ABORT INTO users (name, email) VALUES ('Dave', 'dave@example.com');

-- INSERT OR FAIL
INSERT OR FAIL INTO users (name, email) VALUES ('Eve', 'eve@example.com');

-- ======================================================================
-- 4. PRAGMA statements
-- ======================================================================

-- Query a PRAGMA value
PRAGMA foreign_keys;

-- Set a PRAGMA value (= syntax)
PRAGMA foreign_keys = ON;

-- Set a PRAGMA value (function-call syntax)
PRAGMA foreign_keys(ON);

-- Common PRAGMAs
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA cache_size = -64000;
PRAGMA temp_store = MEMORY;
PRAGMA mmap_size = 268435456;

-- Schema-qualified PRAGMA
PRAGMA main.cache_size;
PRAGMA main.page_count;

-- Information PRAGMAs (function-call form)
PRAGMA table_info(users);
PRAGMA table_xinfo(users);
PRAGMA index_list(users);
PRAGMA index_info(idx_users_email);
PRAGMA foreign_key_list(users);

-- Integrity checks
PRAGMA integrity_check;
PRAGMA quick_check;

-- ======================================================================
-- 5. SQLite type affinity
-- ======================================================================

-- INTEGER affinity (contains "INT")
CREATE TABLE int_types (
    a INTEGER,
    b INT,
    c TINYINT,
    d SMALLINT,
    e MEDIUMINT,
    f BIGINT,
    g INT2,
    h INT8
);

-- TEXT affinity (contains "CHAR", "CLOB", or "TEXT")
CREATE TABLE text_types (
    a TEXT,
    b CHARACTER(20),
    c VARCHAR(255),
    d NCHAR(55),
    e NVARCHAR(100),
    f CLOB
);

-- REAL affinity (contains "REAL", "FLOA", or "DOUB")
CREATE TABLE real_types (
    a REAL,
    b DOUBLE,
    c DOUBLE PRECISION,
    d FLOAT
);

-- NUMERIC affinity (catch-all)
CREATE TABLE numeric_types (
    a NUMERIC,
    b DECIMAL(10,5),
    c BOOLEAN,
    d DATE,
    e DATETIME
);

-- BLOB affinity (no type specified or "BLOB")
CREATE TABLE blob_types (
    a BLOB,
    b
);

-- ======================================================================
-- 6. JSON functions (SQLite 3.38+)
-- ======================================================================

-- JSON extraction with function syntax
SELECT json_extract(data, '$.name') FROM documents;
SELECT json_extract(data, '$.address.city') FROM documents;

-- JSON extraction with arrow operators (SQLite 3.38+)
SELECT data -> '$.name' FROM documents;
SELECT data ->> '$.name' FROM documents;

-- JSON construction
SELECT json_array(1, 2, 3, 4);
SELECT json_object('name', 'Alice', 'age', 30);

-- JSON modification
SELECT json_insert('{"a":1}', '$.b', 2);
SELECT json_replace('{"a":1}', '$.a', 99);
SELECT json_set('{"a":1}', '$.b', 2);
SELECT json_remove('{"a":1,"b":2}', '$.b');
SELECT json_patch('{"a":1}', '{"b":2}');

-- JSON validation and type checking
SELECT json_valid('{"name":"Alice"}');
SELECT json_type('{"name":"Alice"}', '$.name');
SELECT json_quote('hello');

-- JSON aggregate functions
SELECT json_group_array(name) FROM users;
SELECT json_group_object(key, value) FROM config;

-- JSON table-valued functions
SELECT * FROM json_each('[1,2,3,4]');
SELECT * FROM json_tree('{"a":{"b":1},"c":2}');
SELECT je.key, je.value FROM documents, json_each(documents.data) AS je;

-- ======================================================================
-- 7. ATTACH / DETACH database
-- ======================================================================

-- Attach a database file
ATTACH DATABASE 'analytics.db' AS analytics;
ATTACH 'cache.db' AS cache;

-- Query across attached databases
SELECT u.name, a.event
FROM main.users u
JOIN analytics.events a ON u.id = a.user_id;

-- Detach
DETACH DATABASE analytics;
DETACH cache;

-- ======================================================================
-- 8. VACUUM
-- ======================================================================

-- Basic VACUUM (rebuilds the entire database)
VACUUM;

-- VACUUM a specific schema
VACUUM main;

-- VACUUM INTO a new file (SQLite 3.27+)
VACUUM INTO 'backup.db';

-- VACUUM schema INTO file
VACUUM main INTO 'main_backup.db';

-- ======================================================================
-- 9. EXPLAIN / EXPLAIN QUERY PLAN
-- ======================================================================

-- EXPLAIN shows the VDBE opcodes
EXPLAIN SELECT * FROM users WHERE name = 'Alice';

-- EXPLAIN QUERY PLAN shows the high-level query plan
EXPLAIN QUERY PLAN SELECT * FROM users WHERE name = 'Alice';

EXPLAIN QUERY PLAN
SELECT u.name, COUNT(e.event_id) as event_count
FROM users u
LEFT JOIN events e ON u.id = e.user_id
GROUP BY u.name
ORDER BY event_count DESC;

-- ======================================================================
-- 10. GLOB operator
-- ======================================================================

-- GLOB uses Unix file-globbing rules (case-sensitive)
SELECT * FROM files WHERE name GLOB '*.txt';
SELECT * FROM files WHERE name GLOB '[A-Z]*';
SELECT * FROM files WHERE path GLOB '/usr/*/bin/*';

-- NOT GLOB
SELECT * FROM files WHERE name NOT GLOB '.*';

-- ======================================================================
-- 11. INDEXED BY / NOT INDEXED hints
-- ======================================================================

-- Force use of a specific index
SELECT * FROM users INDEXED BY idx_users_email WHERE email = 'alice@example.com';

-- Disable index usage (full table scan)
SELECT * FROM users NOT INDEXED WHERE name = 'Alice';

-- ======================================================================
-- 12. REINDEX
-- ======================================================================

-- Rebuild all indices
REINDEX;

-- Rebuild indices for a specific table/index
REINDEX users;
REINDEX idx_users_email;

-- Schema-qualified REINDEX
REINDEX main.users;

-- ======================================================================
-- 13. SQLite-specific expressions
-- ======================================================================

-- ISNULL / NOTNULL postfix operators
SELECT * FROM users WHERE email ISNULL;
SELECT * FROM users WHERE email NOTNULL;
SELECT * FROM users WHERE email IS NOT NULL;

-- CAST with SQLite type affinity
SELECT CAST(value AS INTEGER) FROM config;
SELECT CAST(amount AS REAL) FROM transactions;
SELECT CAST(data AS TEXT) FROM binary_data;

-- Date/time constants
SELECT CURRENT_TIMESTAMP;
SELECT CURRENT_DATE;
SELECT CURRENT_TIME;
INSERT INTO events (event_type, created_at) VALUES ('login', CURRENT_TIMESTAMP);
