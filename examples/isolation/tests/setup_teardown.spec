# Test with setup and teardown blocks

setup { CREATE TABLE test_iso (id int, val text); }
setup { INSERT INTO test_iso VALUES (1, 'a'), (2, 'b'); }

teardown { DROP TABLE test_iso; }

session s1
setup { BEGIN; }
step s1_update { UPDATE test_iso SET val = 'x' WHERE id = 1; }
teardown { COMMIT; }

session s2
setup { BEGIN; }
step s2_update { UPDATE test_iso SET val = 'y' WHERE id = 1; }
teardown { COMMIT; }
