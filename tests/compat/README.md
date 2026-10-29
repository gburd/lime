# tests/compat -- yacc/bison and lex/flex compatibility measurement

These scripts measure how far `lime` is from reading real yacc/bison and
lex/flex sources. They are not meson tests, because the corpus (bison 3.8.2,
flex 2.6.4, byacc, gawk, one-true-awk, bc, PostgreSQL REL_18_0, the C11
grammar, sqlite's parse.y) is downloaded, not vendored. Point them at a
directory laid out like `/tmp/compat-dl` (the file list is in
`run_corpus.py`).

| script | what it answers |
|---|---|
| `run_corpus.py LIME LIMPAR CORPUS [--byacc Y] [--conv2 CONV]` | Per .y: do bison, byacc, lime as-is, lime after `examples/pg/convert_gram.py`, and lime after an optional second converter accept it? Per .l: do flex and `lime -X` accept it? First error for each. |
| `census.py CORPUS [--tiers]` | Which yacc/flex features each file uses; `--tiers` gives how many grammars each nested feature tier would fully cover. |
| `y2lime_min.py in.y` | Throwaway yacc-to-Lime syntax converter (actions dropped), used only to measure the engine. |
| `run_ymin.py LIME LIMPAR CORPUS` | lime's LALR conflict count vs bison's on every .y that `y2lime_min.py` converts. |
| `diff/run_diff.sh LIME LIMPAR BISON_EXAMPLES_C` | bison-built vs Lime-built calc / rpcalc / mfcalc (hand ports in `diff/*.lime`) on 40 inputs: same stdout, exit status, error count. |
| `diff/run_c11.sh LIME LIMPAR c11.y c11.l INPUT...` | One flex scanner feeding bison's and Lime's C11 parsers: same ACCEPT/REJECT and the same error token index. `C11_MUTATE='sed-expr'` is the negative-control hook. |

`tests/test_bison_diagnostics.sh` (a meson test) is the regression gate for
the diagnostics that lime gives on yacc syntax.
