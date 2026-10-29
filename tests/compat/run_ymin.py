#!/usr/bin/env python3
"""Front-end-only measurement: run every corpus .y through y2lime_min.py
(syntax only, actions dropped) and compare lime's conflict count to bison's.
Answers "if Lime had a yacc front end, would its LALR(1) engine agree?"
usage: run_ymin.py LIME LIMPAR_C CORPUS_DIR"""
import os, re, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from run_corpus import Y

def main():
    lime, tpl, corpus = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]), sys.argv[3]
    print('| grammar | y2lime_min | lime conflicts | bison s/r + r/r | agree | first lime error |')
    print('|---|---|---|---|---|---|')
    for label, rel in Y:
        if label.startswith('CONTROL'): continue
        src = os.path.join(corpus, rel); w = tempfile.mkdtemp(prefix='ym-')
        p = subprocess.run([sys.executable, os.path.join(HERE, 'y2lime_min.py'), src], capture_output=True, text=True)
        b = subprocess.run(['bison', '-Wno-other', '-Wno-deprecated', '-d', '-o', os.path.join(w, 'b.c'), src], capture_output=True, text=True, cwd=w)
        sr = re.search(r'(\d+) shift/reduce', b.stderr); rr = re.search(r'(\d+) reduce/reduce', b.stderr)
        bn = (int(sr.group(1)) if sr else 0) + (int(rr.group(1)) if rr else 0)
        if p.returncode:
            print('| %s | FAIL | | %d | | %s |' % (label, bn, p.stderr.strip().splitlines()[-1][:90])); continue
        open(os.path.join(w, 'g.lime'), 'w').write(p.stdout)
        q = subprocess.run([lime, '-q', '-s', '-T' + tpl, 'g.lime'], cwd=w, capture_output=True, text=True)
        out = q.stdout + q.stderr
        err = [l for l in out.splitlines() if 'g.lime:' in l]
        m = re.search(r'conflicts\.+\s+(\d+)', out)
        ln = int(m.group(1)) if m else None
        print('| %s | ok | %s | %d | %s | %s |' % (label, '-' if err else ln, bn,
              '' if err else ('yes' if ln == bn else 'NO'), err[0][:100].replace('|', '\\|') if err else ''))

if __name__ == '__main__':
    main()
