# Test with quoted identifiers

session "session one"
step "step-1" { SELECT 1; }

session "session two"
step "step-2" { SELECT 2; }

permutation "step-1" "step-2"
