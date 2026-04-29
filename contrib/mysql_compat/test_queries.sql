-- MySQL SQL Compatibility Extension -- Test Queries
--
-- Each query demonstrates a specific MySQL SQL feature handled by the
-- mysql_compat grammar extension.  Comments explain what feature is
-- being tested and how it differs from standard PostgreSQL.

-- =====================================================================
-- 1. LIMIT clause variants
--
-- MySQL LIMIT supports three forms:
--   LIMIT count
--   LIMIT count OFFSET offset
--   LIMIT offset, count          (MySQL shorthand, note reversed order)
-- PostgreSQL only supports the first two forms.
-- =====================================================================

-- Basic LIMIT
SELECT * FROM employees LIMIT 10;

-- LIMIT with OFFSET
SELECT * FROM employees ORDER BY hire_date LIMIT 10 OFFSET 20;

-- MySQL shorthand: LIMIT offset, count (offset first!)
SELECT * FROM employees ORDER BY hire_date LIMIT 20, 10;

-- =====================================================================
-- 2. AUTO_INCREMENT
--
-- MySQL uses AUTO_INCREMENT on column definitions.
-- PostgreSQL uses SERIAL/BIGSERIAL or GENERATED ALWAYS AS IDENTITY.
-- =====================================================================

-- AUTO_INCREMENT column
CREATE TABLE users (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) NOT NULL,
    email VARCHAR(100) NOT NULL UNIQUE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- AUTO_INCREMENT with table option
CREATE TABLE orders (
    order_id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    customer_id INT NOT NULL,
    total DECIMAL(10,2),
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB AUTO_INCREMENT=1000;

-- =====================================================================
-- 3. ENGINE clause
--
-- MySQL tables have a storage engine.  PostgreSQL has no equivalent
-- (it uses a single storage engine).
-- =====================================================================

-- InnoDB (default, transactional)
CREATE TABLE products (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(200)
) ENGINE=InnoDB;

-- MyISAM (legacy, non-transactional)
CREATE TABLE logs (
    id INT AUTO_INCREMENT PRIMARY KEY,
    message TEXT
) ENGINE=MyISAM;

-- MEMORY (in-memory, temporary data)
CREATE TABLE session_cache (
    session_id VARCHAR(64) PRIMARY KEY,
    data TEXT
) ENGINE=MEMORY;

-- =====================================================================
-- 4. CHARSET and COLLATE
--
-- MySQL allows per-table and per-column character set specification.
-- PostgreSQL uses database-level encoding.
-- =====================================================================

-- Table-level charset
CREATE TABLE messages (
    id INT AUTO_INCREMENT PRIMARY KEY,
    body TEXT
) DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Column-level charset
CREATE TABLE multilingual (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name_en VARCHAR(100) CHARSET utf8,
    name_jp VARCHAR(100) CHARSET utf8mb4 COLLATE utf8mb4_unicode_ci
) ENGINE=InnoDB;

-- =====================================================================
-- 5. SHOW statements
--
-- MySQL's SHOW commands provide schema introspection.
-- PostgreSQL uses information_schema or \d commands.
-- =====================================================================

-- List tables
SHOW TABLES;

-- Filter tables
SHOW TABLES LIKE 'user%';

-- List tables from specific database
SHOW TABLES FROM production;

-- List databases
SHOW DATABASES;

-- Column information
SHOW COLUMNS FROM users;

-- Index information
SHOW INDEX FROM users;

-- Server status
SHOW STATUS;

-- Server variables
SHOW VARIABLES;

-- Filter variables
SHOW VARIABLES LIKE 'innodb%';

-- Grants
SHOW GRANTS;

-- Process list
SHOW PROCESSLIST;

-- Warnings and errors
SHOW WARNINGS;
SHOW ERRORS;

-- DDL for a table
SHOW CREATE TABLE users;

-- =====================================================================
-- 6. Backtick identifiers
--
-- MySQL uses backticks to quote identifiers.
-- PostgreSQL uses double quotes.
-- =====================================================================

-- Backtick-quoted table and column names
SELECT `user`.`id`, `user`.`name`
FROM `user`
WHERE `user`.`status` = 1;

-- Reserved words as identifiers
SELECT `select`, `from`, `where`
FROM `table`
LIMIT 10;

-- =====================================================================
-- 7. IFNULL and IF functions
--
-- MySQL IFNULL(a, b) is equivalent to PostgreSQL's COALESCE(a, b).
-- MySQL IF(cond, then, else) is equivalent to CASE WHEN cond THEN then ELSE else END.
-- =====================================================================

-- IFNULL
SELECT IFNULL(commission, 0) FROM employees;

-- Nested IFNULL
SELECT IFNULL(phone, IFNULL(mobile, 'N/A')) FROM contacts;

-- IF function
SELECT IF(status = 1, 'Active', 'Inactive') FROM users;

-- Nested IF
SELECT IF(score >= 90, 'A', IF(score >= 80, 'B', IF(score >= 70, 'C', 'F')))
FROM students;

-- =====================================================================
-- 8. ON DUPLICATE KEY UPDATE (UPSERT)
--
-- MySQL's upsert syntax.
-- PostgreSQL uses INSERT ... ON CONFLICT DO UPDATE.
-- =====================================================================

-- Basic upsert
INSERT INTO counters (name, count) VALUES ('page_views', 1)
ON DUPLICATE KEY UPDATE count = count + 1;

-- Multi-column upsert
INSERT INTO inventory (product_id, warehouse_id, quantity)
VALUES (100, 1, 50)
ON DUPLICATE KEY UPDATE quantity = quantity + 50;

-- Using VALUES() in update (deprecated but common)
INSERT INTO users (id, name, email) VALUES (1, 'Alice', 'alice@example.com')
ON DUPLICATE KEY UPDATE name = 'Alice', email = 'alice@example.com';

-- INSERT IGNORE (skip on duplicate)
INSERT IGNORE INTO tags (name) VALUES ('important');

-- =====================================================================
-- 9. DIV integer division and <=> null-safe equality
--
-- MySQL DIV performs integer division (truncating).
-- MySQL <=> returns 1 when both sides are NULL (unlike = which returns NULL).
-- PostgreSQL has no direct equivalents.
-- =====================================================================

-- DIV integer division
SELECT 17 DIV 5;
SELECT total_cents DIV 100 AS dollars FROM purchases;

-- <=> null-safe equality
SELECT * FROM users WHERE email <=> NULL;
SELECT * FROM t1, t2 WHERE t1.value <=> t2.value;

-- =====================================================================
-- 10. XOR operator and REGEXP
--
-- MySQL has a logical XOR operator.
-- MySQL REGEXP uses the POSIX regex syntax (similar to PostgreSQL's ~ operator).
-- =====================================================================

-- XOR
SELECT * FROM flags WHERE active XOR deleted;

-- REGEXP
SELECT * FROM products WHERE name REGEXP '^[A-Z]';
SELECT * FROM logs WHERE message REGEXP 'error|warning';

-- =====================================================================
-- 11. INTERVAL expressions
--
-- MySQL INTERVAL syntax for date arithmetic.
-- PostgreSQL also supports INTERVAL but with different syntax.
-- =====================================================================

-- Date arithmetic
SELECT * FROM orders WHERE created_at > NOW() - INTERVAL 7 DAY;
SELECT * FROM sessions WHERE last_active > NOW() - INTERVAL 30 MINUTE;
SELECT * FROM subscriptions WHERE expiry > NOW() + INTERVAL 1 YEAR;

-- =====================================================================
-- 12. Combined MySQL features
--
-- Queries using multiple MySQL-specific features together.
-- =====================================================================

-- Backtick identifiers + LIMIT + IFNULL
SELECT `user`.`name`, IFNULL(`user`.`email`, 'no email') AS email
FROM `user`
WHERE `user`.`active` = 1
ORDER BY `user`.`name`
LIMIT 10 OFFSET 0;

-- IF + DIV + LIMIT shorthand
SELECT IF(total DIV 100 > 0, 'Has dollars', 'Cents only') AS amount_type,
       total DIV 100 AS dollars
FROM purchases
LIMIT 0, 20;

-- SHOW + CREATE TABLE with engine and charset
CREATE TABLE IF NOT EXISTS audit_log (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    action VARCHAR(50) NOT NULL,
    details TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT 'When the action occurred'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Complex upsert
INSERT INTO daily_stats (date, page, views, unique_visitors)
VALUES ('2024-01-15', '/home', 1, 1)
ON DUPLICATE KEY UPDATE views = views + 1, unique_visitors = unique_visitors + 1;
