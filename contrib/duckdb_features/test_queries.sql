-- DuckDB Features Extension -- Test Queries
--
-- These queries demonstrate DuckDB-specific SQL extensions for
-- analytics workloads.  Each section exercises a different feature.

-- ================================================================
-- 1. LIST Literals
--
-- DuckDB LIST type uses bracket syntax with commas.
-- Lists are ordered, variable-length sequences of a single type.
-- ================================================================

-- Integer list
SELECT [1, 2, 3, 4, 5] AS numbers;

-- String list
SELECT ['apple', 'banana', 'cherry'] AS fruits;

-- Nested lists
SELECT [[1, 2], [3, 4], [5, 6]] AS matrix;

-- Empty list
SELECT [] AS empty_list;

-- List in WHERE clause
SELECT * FROM products WHERE tags = ['sale', 'featured'];

-- List column creation
CREATE TABLE inventory AS
SELECT 'widget' AS name, [10, 20, 30, 40] AS quarterly_sales;

-- ================================================================
-- 2. STRUCT Literals
--
-- DuckDB STRUCTs are typed named tuples, similar to JSON objects
-- but with a fixed schema determined at query time.
-- ================================================================

-- Simple struct
SELECT {'name': 'Alice', 'age': 30} AS person;

-- Struct with mixed types
SELECT {'x': 1.5, 'y': 2.7, 'label': 'point_a'} AS coordinate;

-- Nested struct
SELECT {
    'name': 'Bob',
    'address': {
        'street': '123 Main St',
        'city': 'Portland',
        'state': 'OR'
    }
} AS employee;

-- Struct with list fields
SELECT {
    'name': 'Project Alpha',
    'tags': ['urgent', 'backend'],
    'scores': [95, 87, 92]
} AS project;

-- Empty struct
SELECT {} AS empty_struct;

-- ================================================================
-- 3. Struct Access (Dot Notation)
--
-- Struct fields are accessed using dot notation, similar to
-- object property access in programming languages.
-- ================================================================

-- Simple field access
SELECT person.name, person.age
FROM (SELECT {'name': 'Alice', 'age': 30} AS person);

-- Nested field access
SELECT addr.city, addr.state
FROM (SELECT {'city': 'Portland', 'state': 'OR'} AS addr);

-- Chained access
SELECT employee.address.city
FROM employees;

-- ================================================================
-- 4. List Subscript Access
--
-- List elements accessed with 1-based bracket indexing.
-- ================================================================

-- Single element access
SELECT my_list[1] AS first, my_list[3] AS third
FROM (SELECT [10, 20, 30, 40] AS my_list);

-- Nested list access
SELECT matrix[1][2] AS element
FROM (SELECT [[1, 2], [3, 4]] AS matrix);

-- Struct field via bracket with string key
SELECT data['name'] AS name
FROM (SELECT {'name': 'Alice', 'age': 30} AS data);

-- ================================================================
-- 5. Lambda Functions
--
-- DuckDB supports lambda expressions for list operations.
-- Syntax: param -> body  or  (param1, param2) -> body
-- ================================================================

-- list_transform: apply a function to each element
SELECT list_transform([1, 2, 3, 4, 5], x -> x * 2) AS doubled;

-- list_transform with string operations
SELECT list_transform(['hello', 'world'], x -> x || '!') AS excited;

-- list_filter: keep elements matching a predicate
SELECT list_filter([1, 2, 3, 4, 5, 6, 7, 8], x -> x > 4) AS big_numbers;

-- list_filter with modulo
SELECT list_filter([1, 2, 3, 4, 5, 6], x -> x % 2 = 0) AS evens;

-- list_reduce: fold a list into a single value
SELECT list_reduce([1, 2, 3, 4, 5], (x, y) -> x + y) AS total;

-- list_reduce for product
SELECT list_reduce([2, 3, 4], (x, y) -> x * y) AS product;

-- Chained list operations
SELECT list_transform(
    list_filter([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], x -> x % 2 = 0),
    x -> x * x
) AS even_squares;

-- ================================================================
-- 6. List Utility Functions
--
-- DuckDB provides many built-in list functions beyond lambdas.
-- ================================================================

-- list_contains: check membership
SELECT list_contains([1, 2, 3, 4, 5], 3) AS has_three;
SELECT list_contains(['a', 'b', 'c'], 'd') AS has_d;

-- list_extract: get element by index
SELECT list_extract([10, 20, 30, 40], 2) AS second;

-- list_sort: sort a list
SELECT list_sort([5, 3, 1, 4, 2]) AS sorted;

-- list_distinct: remove duplicates
SELECT list_distinct([1, 1, 2, 2, 3, 3, 4]) AS unique_vals;

-- flatten: flatten nested lists
SELECT flatten([[1, 2], [3, 4], [5, 6]]) AS flat;

-- list aggregation
SELECT dept, list(salary) AS all_salaries
FROM employees
GROUP BY dept;

-- ================================================================
-- 7. EXCLUDE Clause
--
-- SELECT * EXCLUDE removes specified columns from the result.
-- Useful for wide tables where you want most but not all columns.
-- ================================================================

-- Exclude single column
SELECT * EXCLUDE (password) FROM users;

-- Exclude multiple columns
SELECT * EXCLUDE (ssn, salary, internal_notes) FROM employees;

-- Exclude with other clauses
SELECT * EXCLUDE (created_at, updated_at)
FROM orders
WHERE status = 'pending';

-- ================================================================
-- 8. REPLACE Clause
--
-- SELECT * REPLACE modifies specific columns in the output while
-- keeping all others unchanged.
-- ================================================================

-- Replace with transformation
SELECT * REPLACE (upper(name) AS name) FROM users;

-- Replace multiple columns
SELECT * REPLACE (
    salary * 1.1 AS salary,
    upper(department) AS department
) FROM employees;

-- Combine EXCLUDE and REPLACE
SELECT * EXCLUDE (internal_id) REPLACE (lower(email) AS email)
FROM contacts;

-- ================================================================
-- 9. QUALIFY Clause
--
-- QUALIFY filters rows after window functions are evaluated,
-- analogous to HAVING for aggregate functions.
-- ================================================================

-- Top employee per department
SELECT *, row_number() OVER (PARTITION BY dept ORDER BY salary DESC) AS rn
FROM employees
QUALIFY rn = 1;

-- Most recent order per customer
SELECT *
FROM orders
QUALIFY row_number() OVER (
    PARTITION BY customer_id ORDER BY order_date DESC
) = 1;

-- Dense rank filtering
SELECT product, region, revenue,
    dense_rank() OVER (PARTITION BY region ORDER BY revenue DESC) AS dr
FROM sales
QUALIFY dr <= 3;

-- ================================================================
-- 10. SAMPLE Clause
--
-- DuckDB supports various sampling methods directly in queries.
-- ================================================================

-- Sample by percentage
SELECT * FROM large_table USING SAMPLE 10%;

-- Sample by row count
SELECT * FROM events USING SAMPLE 1000 ROWS;

-- Bernoulli sampling (row-level, slower but unbiased)
SELECT * FROM measurements USING SAMPLE 5% (bernoulli);

-- System sampling (block-level, faster)
SELECT * FROM logs USING SAMPLE 1% (system);

-- Reservoir sampling (exact count, streaming)
SELECT * FROM stream_data USING SAMPLE 500 ROWS (reservoir);

-- ================================================================
-- 11. PIVOT
--
-- PIVOT transforms rows into columns, useful for creating
-- cross-tabulation reports.
-- ================================================================

-- Basic PIVOT: population by year
PIVOT cities ON year IN (2000, 2010, 2020) USING sum(population);

-- PIVOT with aliases
PIVOT sales ON quarter IN (
    1 AS q1,
    2 AS q2,
    3 AS q3,
    4 AS q4
) USING sum(revenue);

-- ================================================================
-- 12. COLUMNS Expression
--
-- COLUMNS() dynamically selects columns by name pattern or regex.
-- ================================================================

-- Select columns matching a regex pattern
SELECT COLUMNS('price_.*') FROM products;

-- Apply function to all columns
SELECT min(COLUMNS(*)) FROM numeric_data;

-- ================================================================
-- 13. Complex Analytical Queries
--
-- Combining multiple DuckDB features in realistic analytics queries.
-- ================================================================

-- Sales analysis with structs and lists
SELECT
    region,
    {'total': sum(revenue), 'avg': avg(revenue), 'count': count(*)} AS stats,
    list(product_name) AS products_sold
FROM sales
GROUP BY region;

-- Time-series with list operations
SELECT
    sensor_id,
    list_transform(readings, x -> x - baseline) AS normalized,
    list_filter(readings, x -> x > threshold) AS anomalies
FROM sensor_data;

-- Nested struct construction for JSON export
SELECT {
    'id': id,
    'name': name,
    'metrics': {
        'score': score,
        'rank': rank,
        'percentile': percentile
    },
    'tags': list_distinct(list(tag))
} AS record
FROM results
GROUP BY id, name, score, rank, percentile;

-- Window function with QUALIFY and EXCLUDE
SELECT * EXCLUDE (rn)
FROM (
    SELECT *,
        row_number() OVER (PARTITION BY category ORDER BY score DESC) AS rn
    FROM items
)
QUALIFY rn <= 5;
