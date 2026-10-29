#!/usr/bin/env python3
"""Minimal yacc -> Lime converter, written to MEASURE the gap, not to ship.

Handles: %token/%left/%right/%nonassoc/%start, `a : b | c ;`, '%prec',
char literals ('x' -> CHAR_<code>), %empty, a wrapper start rule when the
start symbol is on some RHS.  Lower-case terminals / Upper-case
nonterminals are renamed (t_ / n_ prefixes).  Actions are DROPPED (so
only accept/reject can be compared).  Prints the char-code map on stderr
as `CHARMAP <code> <LIMENAME>` lines for the differential driver.
usage: y2lime_min.py in.y > out.lime
"""
import re, sys

def strip_comments(t):
    return re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: '\n' * m.group(0).count('\n'), t, flags=re.S)

def skip_braces(t, i):
    d = 0
    while i < len(t):
        c = t[i]
        if c in '"\'':
            q = c; i += 1
            while i < len(t) and t[i] != q:
                i += 2 if t[i] == '\\' else 1
        elif c == '{': d += 1
        elif c == '}':
            d -= 1
            if d == 0: return i + 1
        i += 1
    return i

def main():
    src = strip_comments(open(sys.argv[1]).read())
    parts = re.split(r'^%%[^\n]*$', src, maxsplit=2, flags=re.M)
    decl, rules = parts[0], parts[1]
    decl = re.sub(r'%\{.*?%\}', '', decl, flags=re.S)
    terms, prec, start = set(), [], None
    for m in re.finditer(r'^%(token|left|right|nonassoc|precedence|start|type)\b([^\n]*)', decl, re.M):
        kind, rest = m.group(1), re.sub(r'<[\w\s*]*>', '', m.group(2))  # a <tag>, not '<' '>'
        names = re.findall(r"'(?:\\.|[^'\\])'|[A-Za-z_][\w.]*", rest)
        if kind == 'start': start = names[0]; continue
        if kind == 'type': continue
        terms.update(n for n in names if not n.startswith("'"))
        if kind != 'token': prec.append((kind if kind != 'precedence' else 'nonassoc', names))
    # tokenise rules
    toks, i = [], 0
    while i < len(rules):
        c = rules[i]
        if c.isspace(): i += 1; continue
        if c == '{': i = skip_braces(rules, i); continue
        if c == "'":
            j = rules.index("'", i + 2 if rules[i+1] == '\\' else i + 1)
            if rules[i+1] == '\\': j = rules.index("'", i + 3)
            toks.append(rules[i:j+1]); i = j + 1; continue
        m = re.match(r'%prec|%empty|[A-Za-z_][\w.]*|[:|;]', rules[i:])
        if m: toks.append(m.group(0)); i += len(m.group(0)); continue
        i += 1
    chars = {}
    def charname(lit):
        body = lit[1:-1]
        code = ord(bytes(body, 'ascii').decode('unicode_escape'))
        chars[code] = 'CHAR_%d' % code
        return chars[code]
    def sym(s):
        if s.startswith("'"): return charname(s)
        if s in terms: return s if s[0].isupper() else 'T_' + s.replace('.', '_')
        return s if s[0].islower() else 'n_' + s
    out, k, lhs_seen = [], 0, []
    while k < len(toks):
        if k + 1 < len(toks) and toks[k+1] == ':':
            lhs = toks[k]; lhs_seen.append(lhs); k += 2; alt = []; pr = None
            while k < len(toks):
                t = toks[k]
                if t in ('|', ';'):
                    out.append((lhs, alt, pr)); alt, pr = [], None; k += 1
                    if t == ';': break
                    continue
                if k + 1 < len(toks) and toks[k+1] == ':' and t not in ('%prec',):
                    out.append((lhs, alt, pr)); break   # missing ';'
                if t == '%prec': pr = toks[k+1]; k += 2; continue
                if t != '%empty': alt.append(t)
                k += 1
            else:
                out.append((lhs, alt, pr))
        else:
            k += 1
    start = start or lhs_seen[0]
    rhs_syms = {s for _, a, _ in out for s in a}
    print('%name Y')
    print('%token_prefix TK_')
    print('%syntax_error { (void)yymajor; (void)yyminor; ++*(int*)yy_err_counter(); }')
    print('%include { int *yy_err_counter(void); }')
    for kind, names in prec:
        print('%%%s %s.' % (kind, ' '.join(sym(n) for n in names)))
    lines = []
    if start in rhs_syms:
        lines.append('yacc_start ::= %s.' % sym(start)); s0 = 'yacc_start'
    else:
        s0 = sym(start)
    for lhs, alt, pr in out:
        lines.append('%s ::= %s.%s' % (sym(lhs), ' '.join(sym(a) for a in alt), (' [%s]' % sym(pr)) if pr else ''))
    print('%%start_symbol %s' % s0)
    used = {sym(t) for t in terms} | set(chars.values())
    print('%%token %s.' % ' '.join(sorted(used)))
    print('\n'.join(lines))
    for code, name in sorted(chars.items()):
        sys.stderr.write('CHARMAP %d %s\n' % (code, name))
    for t in sorted(terms):
        sys.stderr.write('TOKMAP %s %s\n' % (t, sym(t)))

if __name__ == '__main__':
    main()
