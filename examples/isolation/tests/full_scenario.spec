# Full scenario: simulates a realistic isolation test

# Global setup
setup { CREATE TABLE accounts (id int PRIMARY KEY, balance int); }
setup { INSERT INTO accounts VALUES (1, 100), (2, 200); }

# Global teardown
teardown { DROP TABLE accounts; }

# Session 1: transfer from account 1 to account 2
session s1
setup { BEGIN ISOLATION LEVEL SERIALIZABLE; }
step s1_read  { SELECT balance FROM accounts WHERE id = 1; }
step s1_write { UPDATE accounts SET balance = balance - 50 WHERE id = 1; }
step s1_write2 { UPDATE accounts SET balance = balance + 50 WHERE id = 2; }
teardown { COMMIT; }

# Session 2: transfer from account 2 to account 1
session s2
setup { BEGIN ISOLATION LEVEL SERIALIZABLE; }
step s2_read  { SELECT balance FROM accounts WHERE id = 2; }
step s2_write { UPDATE accounts SET balance = balance - 30 WHERE id = 2; }
step s2_write2 { UPDATE accounts SET balance = balance + 30 WHERE id = 1; }
teardown { COMMIT; }

# Test specific orderings
permutation s1_read s2_read s1_write s2_write(s1_write) s1_write2 s2_write s2_write2
permutation s1_read s1_write s1_write2 s2_read s2_write s2_write2
