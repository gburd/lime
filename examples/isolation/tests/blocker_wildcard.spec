# Test with wildcard blocker (*)

session s1
step s1_begin { BEGIN; }
step s1_update { UPDATE t SET v = 1; }
step s1_commit { COMMIT; }

session s2
step s2_begin { BEGIN; }
step s2_update { UPDATE t SET v = 2; }
step s2_commit { COMMIT; }

permutation s1_begin s1_update s2_begin s2_update(*) s1_commit s2_update s2_commit
