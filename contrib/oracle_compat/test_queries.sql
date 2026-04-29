-- Oracle SQL Compatibility Extension -- Test Queries
--
-- Each query demonstrates a specific Oracle SQL feature handled by the
-- oracle_compat grammar extension.  Comments explain what feature is
-- being tested and how it differs from standard PostgreSQL.

-- =====================================================================
-- 1. ROWNUM pseudo-column
--
-- Oracle uses ROWNUM to limit result sets.  PostgreSQL uses LIMIT.
-- ROWNUM is a pseudo-column that numbers rows as they are returned.
-- =====================================================================

-- Basic ROWNUM usage
SELECT * FROM employees WHERE ROWNUM <= 10;

-- ROWNUM in select list
SELECT ROWNUM, employee_name FROM employees WHERE ROWNUM <= 10;

-- ROWNUM with alias
SELECT ROWNUM AS rn, employee_name FROM employees;

-- =====================================================================
-- 2. SYSDATE / SYSTIMESTAMP
--
-- Oracle's SYSDATE returns the current date and time.
-- PostgreSQL uses CURRENT_TIMESTAMP or NOW().
-- SYSTIMESTAMP includes fractional seconds and timezone.
-- =====================================================================

-- Basic SYSDATE
SELECT SYSDATE FROM DUAL;

-- Date arithmetic with SYSDATE
SELECT * FROM orders WHERE order_date > SYSDATE - 7;

-- SYSTIMESTAMP
SELECT SYSTIMESTAMP FROM DUAL;

-- =====================================================================
-- 3. CONNECT BY hierarchical queries
--
-- Oracle's tree-walking syntax.  PostgreSQL uses recursive CTEs instead.
--
-- CONNECT BY PRIOR establishes parent-child relationships.
-- START WITH defines the root of the hierarchy.
-- LEVEL is a pseudo-column indicating depth.
-- NOCYCLE prevents infinite loops in cyclic data.
-- ORDER SIBLINGS BY preserves order within each hierarchy level.
-- =====================================================================

-- Basic hierarchical query
SELECT employee_id, manager_id, LEVEL
FROM employees
START WITH manager_id IS NULL
CONNECT BY PRIOR employee_id = manager_id;

-- Hierarchical query with NOCYCLE
SELECT employee_id, LEVEL
FROM employees
START WITH manager_id IS NULL
CONNECT BY NOCYCLE PRIOR employee_id = manager_id;

-- CONNECT BY without START WITH (all roots)
SELECT department_id, parent_dept_id, LEVEL
FROM departments
CONNECT BY PRIOR department_id = parent_dept_id;

-- ORDER SIBLINGS BY (preserve hierarchy ordering)
SELECT employee_id, employee_name, LEVEL
FROM employees
START WITH manager_id IS NULL
CONNECT BY PRIOR employee_id = manager_id
ORDER SIBLINGS BY employee_name;

-- Reversed clause order (START WITH before CONNECT BY)
SELECT employee_id, LEVEL
FROM employees
CONNECT BY PRIOR employee_id = manager_id
START WITH manager_id IS NULL;

-- =====================================================================
-- 4. DECODE function
--
-- Oracle's DECODE is equivalent to a simple CASE expression.
-- DECODE(expr, search1, result1, search2, result2, ..., default)
-- =====================================================================

-- Basic DECODE
SELECT DECODE(status, 1, 'Active', 2, 'Inactive', 'Unknown') FROM users;

-- DECODE with multiple pairs
SELECT employee_name,
       DECODE(department_id, 10, 'Finance', 20, 'Engineering',
              30, 'Sales', 'Other')
FROM employees;

-- DECODE without default
SELECT DECODE(day_of_week, 1, 'Monday', 7, 'Sunday') FROM calendar;

-- Nested DECODE
SELECT DECODE(status, 1, DECODE(role, 'A', 'Admin Active', 'User Active'),
              0, 'Inactive')
FROM accounts;

-- =====================================================================
-- 5. NVL / NVL2 null-handling functions
--
-- NVL(expr1, expr2): returns expr2 if expr1 is NULL
-- NVL2(expr1, expr2, expr3): returns expr2 if NOT NULL, expr3 if NULL
-- PostgreSQL equivalents: COALESCE and CASE WHEN ... IS NOT NULL
-- =====================================================================

-- NVL basic usage
SELECT NVL(commission_pct, 0) FROM employees;

-- NVL with string
SELECT employee_name, NVL(department_name, 'Unassigned') FROM employees;

-- NVL2 three-argument form
SELECT NVL2(manager_id, 'Has Manager', 'Top Level') FROM employees;

-- Nested NVL
SELECT NVL(phone, NVL(mobile, 'No Contact')) FROM contacts;

-- =====================================================================
-- 6. (+) Outer join syntax
--
-- Oracle's legacy outer join operator.  The (+) appears on the
-- "deficient" side -- the side that may have no matching rows.
-- PostgreSQL uses explicit LEFT/RIGHT OUTER JOIN syntax.
-- =====================================================================

-- Left outer join using (+)
SELECT e.employee_name, d.department_name
FROM employees e, departments d
WHERE e.department_id = d.department_id(+);

-- Right outer join using (+)
SELECT e.employee_name, d.department_name
FROM employees e, departments d
WHERE e.department_id(+) = d.department_id;

-- =====================================================================
-- 7. Sequence references
--
-- Oracle: sequence_name.NEXTVAL / sequence_name.CURRVAL
-- PostgreSQL: nextval('sequence_name') / currval('sequence_name')
-- =====================================================================

-- Get next sequence value
SELECT emp_seq.NEXTVAL FROM DUAL;

-- Get current sequence value
SELECT emp_seq.CURRVAL FROM DUAL;

-- Use sequence in expression
SELECT order_seq.NEXTVAL, customer_name FROM new_orders;

-- =====================================================================
-- 8. MINUS set operator
--
-- Oracle uses MINUS where the SQL standard (and PostgreSQL) uses EXCEPT.
-- Returns rows from the first query that are not in the second.
-- =====================================================================

-- Basic MINUS
SELECT employee_id FROM all_employees
MINUS
SELECT employee_id FROM terminated_employees;

-- =====================================================================
-- 9. DUAL pseudo-table
--
-- Oracle's dummy table for SELECT expressions that don't need a
-- real table.  PostgreSQL allows SELECT without FROM.
-- =====================================================================

-- Expression evaluation
SELECT 1 + 1 FROM DUAL;

-- String function
SELECT SYSDATE FROM DUAL;

-- Sequence access
SELECT order_seq.NEXTVAL FROM DUAL;

-- =====================================================================
-- 10. ROWID pseudo-column
--
-- Oracle's physical row identifier.  PostgreSQL has ctid as a
-- similar concept but different syntax.
-- =====================================================================

-- Select ROWID
SELECT ROWID, employee_name FROM employees WHERE ROWNUM <= 5;

-- =====================================================================
-- 11. Combined Oracle features
--
-- Queries that use multiple Oracle-specific features together.
-- These demonstrate the parser handling several extensions at once.
-- =====================================================================

-- Hierarchical query with DECODE and NVL
SELECT LEVEL,
       DECODE(LEVEL, 1, 'Root', 'Child') AS node_type,
       NVL(manager_name, 'None') AS manager
FROM employees
START WITH manager_id IS NULL
CONNECT BY PRIOR employee_id = manager_id
ORDER SIBLINGS BY employee_name;

-- ROWNUM with SYSDATE and DECODE
SELECT ROWNUM,
       employee_name,
       DECODE(status, 1, 'Active', 'Inactive') AS status_text,
       NVL2(termination_date, 'Terminated', 'Current') AS employment
FROM employees
WHERE hire_date > SYSDATE - 365
  AND ROWNUM <= 20;

-- Outer join with NVL
SELECT e.employee_name,
       NVL(d.department_name, 'No Department') AS dept
FROM employees e, departments d
WHERE e.department_id = d.department_id(+);

-- MINUS with ROWNUM
SELECT employee_id FROM employees WHERE ROWNUM <= 100
MINUS
SELECT employee_id FROM contractors;
