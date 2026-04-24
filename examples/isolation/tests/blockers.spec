# Test with blocker annotations

session s1
step s1_lock { SELECT pg_advisory_lock(1); }
step s1_unlock { SELECT pg_advisory_unlock(1); }

session s2
step s2_lock { SELECT pg_advisory_lock(1); }

permutation s1_lock s2_lock(s1_lock) s1_unlock s2_lock
