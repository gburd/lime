-- EDN Literals Extension -- Test Queries
--
-- These queries demonstrate EDN (Extensible Data Notation) literals
-- used as first-class syntax in SQL.  Each section exercises a
-- different EDN feature.

-- ================================================================
-- 1. EDN Keywords as Values
--
-- Keywords (identifiers prefixed with :) can appear directly in
-- SQL expressions.  They are treated as enum-like string values.
-- ================================================================

-- Simple keyword comparison
SELECT * FROM users WHERE status = :active;

-- Multiple keywords in an IN list
SELECT * FROM config WHERE key IN (:debug :verbose :trace);

-- Namespaced keywords
SELECT * FROM entities WHERE type = :db/user;

-- Keyword in SELECT list (as literal value)
SELECT id, name, :active AS default_status FROM users;

-- ================================================================
-- 2. EDN Maps
--
-- Maps use {:key value} syntax.  Keys are typically keywords.
-- Maps are converted to jsonb in PostgreSQL.
-- ================================================================

-- Simple map literal in WHERE clause
SELECT * FROM users
WHERE preferences = {:theme "dark" :lang "en" :notifications true};

-- Map in INSERT VALUES
INSERT INTO config (key, data) VALUES
('ui_defaults', {:theme "dark" :font-size 14 :sidebar true});

-- Map with integer values
INSERT INTO config (key, data) VALUES
('limits', {:timeout 30 :retries 3 :max-connections 100});

-- Map with mixed value types
INSERT INTO settings (name, value) VALUES
('app_config', {:debug false :version "2.1.0" :max-retries 5 :ratio 0.75});

-- Empty map
INSERT INTO config (key, data) VALUES ('empty', {});

-- ================================================================
-- 3. EDN Vectors
--
-- Vectors use [elem1 elem2 ...] syntax.  They map to PostgreSQL
-- array types or jsonb arrays.
-- ================================================================

-- Vector of keywords (tag matching)
SELECT * FROM posts WHERE tags @> [:postgres :sql :database];

-- Vector of integers (score comparison)
SELECT id, scores FROM results WHERE scores = [95 87 92 88];

-- Vector of strings
SELECT * FROM documents WHERE categories @> ["tech" "database" "open-source"];

-- Vector in INSERT
INSERT INTO users (name, roles) VALUES ('admin', [:admin :editor :viewer]);

-- Empty vector
INSERT INTO lists (name, items) VALUES ('empty_list', []);

-- ================================================================
-- 4. EDN Sets
--
-- Sets use #{elem1 elem2 ...} syntax.  They represent unordered
-- collections of unique values, mapped to arrays with the &&
-- (overlap) operator.
-- ================================================================

-- Set of keywords with overlap operator
SELECT * FROM roles WHERE permissions && #{:read :write :delete};

-- Set of strings
SELECT * FROM users WHERE groups && #{"admin" "staff" "moderator"};

-- Set of integers
SELECT * FROM lottery WHERE numbers && #{7 13 42 99};

-- Set in INSERT (deduplicated by definition)
INSERT INTO acl (resource, allowed) VALUES
('api_endpoint', #{:get :post :put :delete});

-- ================================================================
-- 5. Nested Structures
--
-- EDN values can be nested: maps containing vectors, maps within
-- maps, vectors of maps, etc.
-- ================================================================

-- Deeply nested map (user preferences)
INSERT INTO config (key, value) VALUES
('user_prefs', {
    :display {:theme "dark" :font-size 14 :line-height 1.5}
    :editor {:tab-size 4 :auto-save true :word-wrap false}
    :shortcuts [:save :run :debug :format]
});

-- Map containing a vector
INSERT INTO projects (name, metadata) VALUES
('lime', {:tags [:parser :compiler :c] :version "1.0.0" :active true});

-- Vector of maps
INSERT INTO batch_config (name, steps) VALUES
('pipeline', [
    {:name "build" :timeout 300 :retries 2}
    {:name "test" :timeout 600 :retries 1}
    {:name "deploy" :timeout 120 :retries 3}
]);

-- Map with set values
INSERT INTO security (name, config) VALUES
('role_admin', {
    :permissions #{:read :write :delete :admin}
    :resources #{:users :config :logs}
    :ip-whitelist ["10.0.0.0/8" "192.168.0.0/16"]
});

-- ================================================================
-- 6. EDN in Complex SQL Expressions
--
-- EDN literals can participate in more complex SQL expressions
-- alongside standard SQL operators and functions.
-- ================================================================

-- EDN in CASE expression
SELECT id, name,
    CASE status
        WHEN :active THEN 'Active'
        WHEN :pending THEN 'Pending'
        WHEN :suspended THEN 'Suspended'
        ELSE 'Unknown'
    END AS status_label
FROM users;

-- EDN with JOIN conditions
SELECT u.name, r.name AS role_name
FROM users u
JOIN user_roles ur ON u.id = ur.user_id
JOIN roles r ON r.id = ur.role_id
WHERE u.status = :active
  AND r.permissions && #{:admin :superuser};

-- EDN in subquery
SELECT * FROM users
WHERE id IN (
    SELECT user_id FROM audit_log
    WHERE action = :login
      AND metadata @> {:success true}
);

-- EDN with aggregation
SELECT status, COUNT(*) as cnt
FROM users
WHERE preferences @> {:notifications true}
GROUP BY status
HAVING status IN (:active :pending);

-- ================================================================
-- 7. EDN nil and Boolean Values
--
-- nil maps to SQL NULL; true/false map to boolean literals.
-- ================================================================

-- nil comparison (maps to IS NULL)
SELECT * FROM users WHERE deleted_at = nil;

-- Boolean values
SELECT * FROM features WHERE enabled = true;
SELECT * FROM config WHERE value = false;

-- Boolean in map
UPDATE users SET preferences = {:dark-mode true :beta-features false}
WHERE id = 42;

-- ================================================================
-- 8. Keyword Arithmetic / Comparisons
--
-- Keywords can be used in places where string enum values are
-- expected, enabling clean comparison syntax.
-- ================================================================

-- Ordering with keywords
SELECT * FROM tasks
WHERE priority IN (:critical :high :medium)
ORDER BY
    CASE priority
        WHEN :critical THEN 1
        WHEN :high THEN 2
        WHEN :medium THEN 3
    END;

-- Range-style queries with keyword ordering
SELECT * FROM incidents
WHERE severity >= :warning
  AND category = :security;
