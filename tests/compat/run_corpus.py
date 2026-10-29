#!/usr/bin/env python3
"""Measure how much of a real-world yacc/bison and flex corpus Lime accepts.

usage: run_corpus.py LIME LIMPAR_C CORPUS_DIR [--byacc PATH] [--conv2 PATH]

For each .y: does bison accept it, byacc, lime as-is, lime after the
in-tree converter (examples/pg/convert_gram.py), lime after an optional
second converter (--conv2, e.g. the PG port's lime_convert_gram.py).
For each .l: does flex accept it, does `lime -X`.
Prints one markdown table row per file.  Not a pass/fail test: the
corpus is downloaded, not vendored (see COMPAT_REPORT.md for sources).
"""
import os, re, subprocess, sys, tempfile, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
CONV1 = os.path.join(HERE, '..', '..', 'examples', 'pg', 'convert_gram.py')

Y = [  # (label, path relative to corpus)
 ('bison calc', 'bison-3.8.2/examples/c/calc/calc.y'),
 ('bison rpcalc', 'bison-3.8.2/examples/c/rpcalc/rpcalc.y'),
 ('bison mfcalc', 'bison-3.8.2/examples/c/mfcalc/mfcalc.y'),
 ('bison pushcalc', 'bison-3.8.2/examples/c/pushcalc/calc.y'),
 ('bison reccalc', 'bison-3.8.2/examples/c/reccalc/parse.y'),
 ('bison lexcalc', 'bison-3.8.2/examples/c/lexcalc/parse.y'),
 ('bison bistromathic', 'bison-3.8.2/examples/c/bistromathic/parse.y'),
 ('bison glr c++-types', 'bison-3.8.2/examples/c/glr/c++-types.y'),
 ('bison parse-gram.y (bison itself)', 'bison-3.8.2/src/parse-gram.y'),
 ('flex parse.y (flex itself)', 'flex-2.6.4/src/parse.y'),
 ('flex manual expr.y', 'flex-2.6.4/examples/manual/expr.y'),
 ('flex manual front.y', 'flex-2.6.4/examples/manual/front.y'),
 ('byacc test calc.y', 'byacc-20260126/test/calc.y'),
 ('byacc test grammar.y (ANSI C decls)', 'byacc-20260126/test/grammar.y'),
 ('byacc test quote_calc.y', 'byacc-20260126/test/quote_calc.y'),
 ('gawk awkgram.y', 'gawk-5.3.1/awkgram.y'),
 ('gawk command.y', 'gawk-5.3.1/command.y'),
 ('one-true-awk awkgram.y', 'otawk-awkgram.y'),
 ('FreeBSD awk awkgram.y', 'freebsd-awkgram.y'),
 ('bc bc.y', 'bc-1.07.1/bc/bc.y'),
 ('ANSI C11 c11.y', 'c11.y'),
 ('PG gram.y', 'postgres-REL_18_0/src/backend/parser/gram.y'),
 ('PG pl_gram.y', 'postgres-REL_18_0/src/pl/plpgsql/src/pl_gram.y'),
 ('PG bootparse.y', 'postgres-REL_18_0/src/backend/bootstrap/bootparse.y'),
 ('PG repl_gram.y', 'postgres-REL_18_0/src/backend/replication/repl_gram.y'),
 ('PG syncrep_gram.y', 'postgres-REL_18_0/src/backend/replication/syncrep_gram.y'),
 ('PG jsonpath_gram.y', 'postgres-REL_18_0/src/backend/utils/adt/jsonpath_gram.y'),
 ('PG pgbench exprparse.y', 'postgres-REL_18_0/src/bin/pgbench/exprparse.y'),
 ('PG cube cubeparse.y', 'postgres-REL_18_0/contrib/cube/cubeparse.y'),
 ('PG seg segparse.y', 'postgres-REL_18_0/contrib/seg/segparse.y'),
 ('CONTROL sqlite parse.y (Lemon)', 'sqlite-parse.y'),
 ('CONTROL sqlite parse.y 3.50.4 (Lemon)', 'sqlite-parse-3.50.4.y'),
]

L = [
 ('flex manual wc.lex', 'flex-2.6.4/examples/manual/wc.lex'),
 ('flex manual numbers.lex', 'flex-2.6.4/examples/manual/numbers.lex'),
 ('flex manual string1.lex (input())', 'flex-2.6.4/examples/manual/string1.lex'),
 ('flex manual yymore.lex', 'flex-2.6.4/examples/manual/yymore.lex'),
 ('flex manual reject.lex', 'flex-2.6.4/examples/manual/reject.lex'),
 ('flex manual unput.lex', 'flex-2.6.4/examples/manual/unput.lex'),
 ('flex manual eof_rules.lex', 'flex-2.6.4/examples/manual/eof_rules.lex'),
 ('flex manual pascal.lex', 'flex-2.6.4/examples/manual/pascal.lex'),
 ('flex fastwc wc1.l', 'flex-2.6.4/examples/fastwc/wc1.l'),
 ('flex tests posix.l', 'flex-2.6.4/tests/posix.l'),
 ('flex tests bison_yylval_scanner.l', 'flex-2.6.4/tests/bison_yylval_scanner.l'),
 ('flex scan.l (flex itself)', 'flex-2.6.4/src/scan.l'),
 ('bison scan-gram.l', 'bison-3.8.2/src/scan-gram.l'),
 ('bison lexcalc scan.l', 'bison-3.8.2/examples/c/lexcalc/scan.l'),
 ('bc scan.l', 'bc-1.07.1/bc/scan.l'),
 ('ANSI C11 c11.l', 'c11.l'),
 ('PG scan.l', 'postgres-REL_18_0/src/backend/parser/scan.l'),
 ('PG bootscanner.l', 'postgres-REL_18_0/src/backend/bootstrap/bootscanner.l'),
 ('PG repl_scanner.l', 'postgres-REL_18_0/src/backend/replication/repl_scanner.l'),
 ('PG jsonpath_scan.l', 'postgres-REL_18_0/src/backend/utils/adt/jsonpath_scan.l'),
 ('PG ecpg pgc.l', 'postgres-REL_18_0/src/interfaces/ecpg/preproc/pgc.l'),
 ('PG psqlscan.l', 'postgres-REL_18_0/src/fe_utils/psqlscan.l'),
]

def run(cmd, cwd, inp=None):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, input=inp,
                       errors='replace', timeout=600)
    return p.returncode, p.stdout + p.stderr

def first_err(out):
    for ln in out.splitlines():
        if ln.startswith(('converted ', 'wrote ', 'Tokenized', 'Parsed')):
            continue
        if re.search(r'error|Illegal|should be|Unknown|Expected|unexpected|Missing|'
                     r'not a valid|Can.t open|Assertion|ERROR|mid-rule|positional', ln):
            return ln.strip()[:150]
    lines = [l for l in out.splitlines() if l.strip()]
    return (lines[0][:150] if lines else '')

def lime_verdict(lime, tpl, path, cwd, lex=False):
    if lex:
        rc, out = run([lime, '-X', os.path.basename(path)], cwd)
        return ('yes' if rc == 0 else 'NO'), ('' if rc == 0 else first_err(out))
    rc, out = run([lime, '-q', '-T' + tpl, os.path.basename(path)], cwd)
    if rc == 0:
        return 'yes', ''
    # lime exits with its error count (mod 256), so only a signal is a crash
    if rc < 0 or 'Assertion `' in out:
        return 'CRASH', first_err(out)
    m = re.search(r'(\d+) parsing conflicts', out)
    other = [l for l in out.splitlines() if l.strip() and 'parsing conflicts' not in l]
    if m and not other:
        return 'yes (%s conflicts)' % m.group(1), ''
    return 'NO', first_err(out)

def tool_verdict(cmd, cwd):
    rc, out = run(cmd, cwd)
    note = ''
    sr = re.search(r'(\d+) shift/reduce', out); rr = re.search(r'(\d+) reduce/reduce', out)
    if sr or rr:
        note = ' (%s s/r, %s r/r)' % (sr.group(1) if sr else 0, rr.group(1) if rr else 0)
    if rc == 0:
        return 'yes' + note, ''
    return 'NO' + note, first_err(out)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('lime'); ap.add_argument('tpl'); ap.add_argument('corpus')
    ap.add_argument('--byacc'); ap.add_argument('--conv2')
    ap.add_argument('--only', choices=['y', 'l'])
    a = ap.parse_args()
    lime = os.path.abspath(a.lime); tpl = os.path.abspath(a.tpl)
    if a.only != 'l':
        print('| grammar | lines | bison | byacc | lime as-is | lime via in-tree convert_gram.py | lime via PG-port lime_convert_gram.py | first lime error (as-is) | first error (in-tree conv) | first error (PG-port conv) |')
        print('|---|---|---|---|---|---|---|---|---|---|')
        for label, rel in Y:
            src = os.path.join(a.corpus, rel)
            if not os.path.exists(src):
                print('| %s | MISSING | | | | | | | | |' % label); continue
            w = tempfile.mkdtemp(prefix='cx-')
            dst = os.path.join(w, 'g.y'); open(dst, 'w').write(open(src, errors='replace').read())
            n = sum(1 for _ in open(dst))
            ctl = label.startswith('CONTROL')
            b = ('n/a (Lemon)', '') if ctl else tool_verdict(['bison', '-Wnone', '-d', '-o', 'b.c', 'g.y'], w)
            y = ('n/a', '') if (ctl or not a.byacc) else tool_verdict([os.path.abspath(a.byacc), '-d', '-o', 'y.c', 'g.y'], w)
            v0 = lime_verdict(lime, tpl, dst, w)
            v1, v2 = ('n/a', ''), ('n/a', '')
            if not ctl:
                rc, out = run([sys.executable, CONV1, 'g.y'], w)
                if rc != 0:
                    v1 = ('CONV FAIL', first_err(out))
                else:
                    # the converter writes stats to stderr; keep stdout only
                    p = subprocess.run([sys.executable, CONV1, 'g.y'], cwd=w, capture_output=True, text=True, errors='replace')
                    open(os.path.join(w, 'c1.lime'), 'w').write(p.stdout)
                    v1 = lime_verdict(lime, tpl, os.path.join(w, 'c1.lime'), w)
                if a.conv2:
                    rc, out = run([sys.executable, a.conv2, 'g.y', 'c2.lime', '--no-driver'], w)
                    if rc != 0:
                        v2 = ('CONV FAIL', first_err(out))
                    else:
                        v2 = lime_verdict(lime, tpl, os.path.join(w, 'c2.lime'), w)
            print('| %s | %d | %s | %s | %s | %s | %s | %s | %s | %s |' % (
                label, n, b[0], y[0], v0[0], v1[0], v2[0],
                v0[1].replace('|', '\\|'), v1[1].replace('|', '\\|'), v2[1].replace('|', '\\|')), flush=True)
    if a.only != 'y':
        print()
        print('| scanner | lines | flex | lime -X | first lime error |')
        print('|---|---|---|---|---|')
        for label, rel in L:
            src = os.path.join(a.corpus, rel)
            if not os.path.exists(src):
                print('| %s | MISSING | | | |' % label); continue
            w = tempfile.mkdtemp(prefix='cxl-')
            dst = os.path.join(w, 'g.l'); open(dst, 'w').write(open(src, errors='replace').read())
            n = sum(1 for _ in open(dst))
            f = tool_verdict(['flex', '-o', 'f.c', 'g.l'], w)
            v = lime_verdict(lime, tpl, dst, w, lex=True)
            print('| %s | %d | %s | %s | %s |' % (label, n, f[0], v[0], v[1].replace('|', '\\|')), flush=True)

if __name__ == '__main__':
    main()
