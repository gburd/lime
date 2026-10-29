#!/usr/bin/env python3
"""Count, per yacc/flex feature, how many corpus files use it.
usage: census.py CORPUS_DIR   (file lists are shared with run_corpus.py)"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_corpus import Y, L

def uncomment(text):
    return re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)

def split(text):
    text = uncomment(text)
    m = [x.start() for x in re.finditer(r'^%%', text, re.M)]
    decl = text[:m[0]] if m else text
    rules = text[m[0]:m[1]] if len(m) > 1 else (text[m[0]:] if m else '')
    return decl, rules

def strip_code(rules):
    # crude: drop {...} action bodies (brace-balanced, skipping strings/chars)
    out, d, i, n = [], 0, 0, len(rules)
    while i < n:
        c = rules[i]
        if d == 0 and c == '{': d = 1; out.append(' {ACT} '); i += 1; continue
        if d > 0:
            if c in '"\'':
                q = c; i += 1
                while i < n and rules[i] != q:
                    i += 2 if rules[i] == '\\' else 1
            elif c == '{': d += 1
            elif c == '}': d -= 1
            i += 1; continue
        out.append(c); i += 1
    return ''.join(out)

def midrule(rules_nocode):
    # an {ACT} followed by a grammar symbol before | or ;
    return len(re.findall(r'\{ACT\}\s*(?:/\*.*?\*/\s*)*[A-Za-z_\'"]', rules_nocode, re.S))

YF = [
 ('%{ %} prologue',            lambda d,r,c: '%{' in d),
 ('%% sections / a: b | c ;',  lambda d,r,c: True),
 ('$$ / $N in actions',        lambda d,r,c: bool(re.search(r'\$\$|\$[0-9]', r))),
 ('char literal token \'+\'',  lambda d,r,c: bool(re.search(r"'(\\.|[^'\\])'", c))),
 ('string alias token "+"',    lambda d,r,c: bool(re.search(r'%token\s*(<[^>]*>)?\s*\w+\s+"', d)) or bool(re.search(r'"[^"\n]+"', re.sub(r"'(\\.|[^'\\])'", ' ', c)))),
 ('%union',                    lambda d,r,c: '%union' in d),
 ('%token/%type <tag>',        lambda d,r,c: bool(re.search(r'%(token|type|nterm|left|right|nonassoc)\s*<', d))),
 ('$<type>N',                  lambda d,r,c: bool(re.search(r'\$<\w+>', r))),
 ('mid-rule action',           lambda d,r,c: midrule(c) > 0),
 ('error token',               lambda d,r,c: bool(re.search(r'\berror\b', c))),
 ('yyerrok / yyclearin',       lambda d,r,c: bool(re.search(r'\byyerrok\b|\byyclearin\b', r))),
 ('YYABORT/YYACCEPT/YYERROR',  lambda d,r,c: bool(re.search(r'\bYY(ABORT|ACCEPT|ERROR)\b', r))),
 ('@N / @$ / %locations',      lambda d,r,c: bool(re.search(r'@[0-9$]', r)) or '%locations' in d),
 ('%define',                   lambda d,r,c: '%define' in d),
 ('%parse-param/%lex-param/%param', lambda d,r,c: bool(re.search(r'%(parse|lex)-param|%param\b', d))),
 ('%pure-parser',              lambda d,r,c: '%pure-parser' in d or 'api.pure' in d),
 ('%expect N',                 lambda d,r,c: bool(re.search(r'%expect\s+\d', d))),
 ('%expect-rr',                lambda d,r,c: '%expect-rr' in d),
 ('%precedence',               lambda d,r,c: '%precedence' in d),
 ('%prec in rules',            lambda d,r,c: '%prec' in c),
 ('%empty',                    lambda d,r,c: '%empty' in c),
 ('%code',                     lambda d,r,c: '%code' in d),
 ('%printer / %destructor',    lambda d,r,c: bool(re.search(r'%printer|%destructor', d))),
 ('%name-prefix / api.prefix', lambda d,r,c: bool(re.search(r'%name-prefix|api\.prefix', d))),
 ('%glr-parser',               lambda d,r,c: '%glr-parser' in d),
 ('yychar / yylval in actions',lambda d,r,c: bool(re.search(r'\byychar\b|\byylval\b', r))),
 ('start symbol on its own RHS (needs augmentation)', None),
 ('PascalCase / lower-case token names', None),
]

def start_on_rhs(decl, nocode):
    m = re.search(r'%start\s+(\w+)', decl)
    rules = re.findall(r'^\s*([A-Za-z_][\w.]*)\s*:(?!:)(.*?)(?=^\s*[A-Za-z_][\w.]*\s*:(?!:)|\Z)', nocode, re.S | re.M)
    if not rules: return False
    start = m.group(1) if m else rules[0][0]
    return any(re.search(r'(?<![\w.])%s(?![\w.])' % re.escape(start), body) for lhs, body in rules)

def case_clash(decl, nocode):
    # terminals that aren't Upper-first, or nonterminals that are Upper-first
    toks = set()
    for m in re.finditer(r'^%(token|left|right|nonassoc|precedence)\b([^\n%]*)', decl, re.M):
        for w in re.findall(r'(?<![<\w"\'])([A-Za-z_]\w*)(?![\w>])', re.sub(r'<[^>]*>|"[^"]*"', ' ', m.group(2))):
            toks.add(w)
    lhs = set(re.findall(r'^\s*([A-Za-z_]\w*)\s*:(?!:)', nocode, re.M))
    return any(not t[0].isupper() for t in toks) or any(n[0].isupper() for n in lhs)

def lex_rules(t):
    m = [x.start() for x in re.finditer(r'^%%', t, re.M)]
    return t[m[0]:m[1]] if len(m) > 1 else (t[m[0]:] if m else '')

def pattern_of(line):
    # the pattern is the leading non-blank run, honouring "..", [..] and \x
    i, n, q, b = 0, len(line), False, False
    while i < n:
        c = line[i]
        if c == '\\': i += 2; continue
        if q:
            if c == '"': q = False
        elif b:
            if c == ']': b = False
        elif c == '"': q = True
        elif c == '[': b = True
        elif c in ' \t': break
        elif c == '/': return True
        i += 1
    return False

def trailing_ctx(t):
    for ln in lex_rules(t).splitlines():
        if not ln or ln[0] in ' \t%{}' or ln.startswith('/*'): continue
        ln = re.sub(r'^<[^>]*>', '', ln)
        if pattern_of(ln): return True
    return False

LF = [
 ('%{ %} / definitions / %%',   lambda t: True),
 ('%option',                    lambda t: '%option' in t),
 ('%x / %s start conditions',   lambda t: bool(re.search(r'^%[xs]\b', t, re.M))),
 ('BEGIN',                      lambda t: 'BEGIN' in t),
 ('yy_push_state / pop',        lambda t: 'yy_push_state' in t or 'yy_pop_state' in t),
 ('yytext / yyleng',            lambda t: 'yytext' in t or 'yyleng' in t),
 ('yylineno',                   lambda t: 'yylineno' in t),
 ('yymore',                     lambda t: bool(re.search(r'\byymore\s*\(', t))),
 ('REJECT',                     lambda t: bool(re.search(r'\bREJECT\b', t))),
 ('yyless',                     lambda t: 'yyless' in t),
 ('unput / input()',            lambda t: bool(re.search(r'\bunput\s*\(|\binput\s*\(\s*\)|yyinput', uncomment(t)))),
 ('ECHO',                       lambda t: bool(re.search(r'\bECHO\b', t))),
 ('<<EOF>>',                    lambda t: '<<EOF>>' in t),
 ('trailing context r/s',       lambda t: trailing_ctx(t)),
 ('yylval / yylloc / bison-bridge', lambda t: bool(re.search(r'yylval|yylloc|bison-bridge|bison-locations', t))),
 ('reentrant / yyscanner / yyextra', lambda t: bool(re.search(r'reentrant|yyextra|yyscanner', t))),
 ('yywrap',                     lambda t: 'yywrap' in t),
 ('YY_INPUT / yy_scan_*',       lambda t: bool(re.search(r'YY_INPUT|yy_scan_|yy_create_buffer|yy_switch_to_buffer', t))),
 ('case-insensitive',           lambda t: bool(re.search(r'case-insensitive|caseless|\(\?i', t))),
]

def main():
    corpus = sys.argv[1]
    res = {}
    for name, fn in YF: res[name] = []
    for label, rel in Y:
        if label.startswith('CONTROL'): continue
        t = open(os.path.join(corpus, rel), errors='replace').read()
        d, r = split(t); c = strip_code(r)
        for name, fn in YF:
            if name.startswith('start symbol'): hit = start_on_rhs(d, c)
            elif name.startswith('PascalCase'): hit = case_clash(d, c)
            else: hit = fn(d, r, c)
            if hit: res[name].append(label)
    ny = len([1 for l, _ in Y if not l.startswith('CONTROL')])
    print('| yacc/bison feature | grammars using it (of %d) | which |' % ny)
    print('|---|---|---|')
    for name, _ in YF:
        print('| %s | %d | %s |' % (name, len(res[name]), ', '.join(res[name]) if len(res[name]) < ny else 'all'))
    lres = {name: [] for name, _ in LF}
    for label, rel in L:
        t = open(os.path.join(corpus, rel), errors='replace').read()
        for name, fn in LF:
            if fn(t): lres[name].append(label)
    print()
    print('| flex feature | scanners using it (of %d) | which |' % len(L))
    print('|---|---|---|')
    for name, _ in LF:
        print('| %s | %d | %s |' % (name, len(lres[name]), ', '.join(lres[name]) if len(lres[name]) < len(L) else 'all'))

if __name__ == '__main__' and '--tiers' not in sys.argv:
    main()

def tiers(corpus):
    """Per-file feature sets, then: how many files does each nested tier cover?"""
    feats = {}
    for label, rel in Y:
        if label.startswith('CONTROL'): continue
        t = open(os.path.join(corpus, rel), errors='replace').read()
        d, r = split(t); c = strip_code(r); s = set()
        for name, fn in YF:
            if name.startswith('start symbol'): hit = start_on_rhs(d, c)
            elif name.startswith('PascalCase'): hit = case_clash(d, c)
            else: hit = fn(d, r, c)
            if hit: s.add(name)
        feats[label] = s
    return feats

YACC_TIERS = [
 ('T1 POSIX-yacc front end', {'%{ %} prologue', '%% sections / a: b | c ;', '$$ / $N in actions',
   "char literal token '+'", '%union', '%token/%type <tag>', '$<type>N', 'mid-rule action',
   'error token', 'yyerrok / yyclearin', 'YYABORT/YYACCEPT/YYERROR', '%prec in rules',
   'start symbol on its own RHS (needs augmentation)', 'PascalCase / lower-case token names'}),
 ('T2 + common bison/byacc decls', {'%expect N', '%pure-parser', '%parse-param/%lex-param/%param',
   '%name-prefix / api.prefix', '@N / @$ / %locations', 'yychar / yylval in actions'}),
 ('T3 + bison-3 extensions', {'%define', '%empty', '%code', '%printer / %destructor', '%precedence',
   '%expect-rr', 'string alias token "+"'}),
 ('T4 + GLR', {'%glr-parser'}),
]


if __name__ == '__main__' and len(sys.argv) > 2 and sys.argv[2] == '--tiers':
    feats = tiers(sys.argv[1]); have = set()
    print('| tier (cumulative) | grammars fully covered (of %d) | still blocked by |' % len(feats))
    print('|---|---|---|')
    for name, add in YACC_TIERS:
        have |= add
        ok = [l for l, s in feats.items() if s <= have]
        miss = {}
        for l, s in feats.items():
            for f in s - have: miss[f] = miss.get(f, 0) + 1
        print('| %s | %d | %s |' % (name, len(ok), ', '.join('%s (%d)' % kv for kv in sorted(miss.items(), key=lambda x: -x[1]))))
