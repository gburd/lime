/*
** Cross-feature integration grammar for the M3.4 + M3.5 merge
** bridge.  Verifies that LEX_PUSHBACK from an action body
** correctly drives M3.5's buffer-stack runtime.
*/
%name_prefix M3i.
rule abcd matches /abcd/  { LEX_PUSHBACK(2); LEX_EMIT(M3I_RULE_ABCD); }
rule cd   matches /cd/    { /* auto-emit */ }
rule any  matches /[a-z]/ { /* auto-emit */ }
