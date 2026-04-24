# Minimal isolation test spec: two sessions, one step each

session s1
step s1_read { SELECT 1; }

session s2
step s2_read { SELECT 2; }
