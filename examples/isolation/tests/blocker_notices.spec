# Test with notice-based blockers

session s1
step s1_notify { NOTIFY test_channel; }

session s2
step s2_listen { LISTEN test_channel; }
step s2_wait   { SELECT 1; }

permutation s2_listen s1_notify s2_wait(s1_notify notices 1)
