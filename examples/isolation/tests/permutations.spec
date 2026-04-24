# Test with explicit permutations

session s1
step s1a { SELECT 1; }
step s1b { SELECT 2; }

session s2
step s2a { SELECT 3; }
step s2b { SELECT 4; }

permutation s1a s2a s1b s2b
permutation s2a s1a s2b s1b
permutation s1a s1b s2a s2b
