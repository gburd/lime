# Test with multiple blockers on a single step

session s1
step s1a { SELECT 1; }

session s2
step s2a { SELECT 2; }

session s3
step s3a { SELECT 3; }

permutation s1a s2a s3a(s1a, s2a)
