#!/usr/bin/env python3
"""
Robust converter: PostgreSQL gram.y (Bison) -> Lime format.

Strategy: Parse the grammar into (nonterminal, alternatives) using a
state machine that properly tracks brace nesting for C actions.
"""
import re, sys

CHAR_MAP = {
    "'('": "LPAREN", "')'": "RPAREN",
    "'['": "LBRACKET", "']'": "RBRACKET",
    "'<'": "LT", "'>'": "GT", "'='": "EQ",
    "'+'": "PLUS", "'-'": "MINUS",
    "'*'": "STAR", "'/'": "SLASH", "'%'": "PERCENT",
    "'^'": "CARET", "'|'": "PIPE",
    "'.'": "DOT", "','": "COMMA",
    "';'": "SEMICOLON", "':'": "COLON",
    "'#'": "HASH",
}

# Bison token names that don't match the ALL_CAPS Lime convention
TOKEN_REMAP = {
    "Op": "OP",
}

def strip_c_comments(text):
    """Remove C-style block comments, preserving newlines."""
    result = []
    i = 0
    in_string = False
    in_char = False
    while i < len(text):
        if not in_string and not in_char:
            if text[i:i+2] == '/*':
                j = text.find('*/', i+2)
                if j == -1:
                    break
                # Preserve newlines in the comment
                nl_count = text[i:j+2].count('\n')
                result.append('\n' * nl_count)
                i = j + 2
                continue
            if text[i:i+2] == '//':
                j = text.find('\n', i)
                if j == -1:
                    break
                i = j
                continue
        if text[i] == '"' and not in_char:
            in_string = not in_string
        elif text[i] == "'" and not in_string:
            in_char = not in_char
        elif text[i] == '\\' and (in_string or in_char):
            result.append(text[i])
            i += 1
            if i < len(text):
                result.append(text[i])
                i += 1
            continue
        result.append(text[i])
        i += 1
    return ''.join(result)

def extract_grammar_section(filename):
    """Extract text between first %% and second %%."""
    with open(filename) as f:
        text = f.read()
    
    markers = [m.start() for m in re.finditer(r'^%%\s*$', text, re.MULTILINE)]
    if len(markers) < 2:
        print("ERROR: could not find two %% markers", file=sys.stderr)
        sys.exit(1)
    
    return text[markers[0]+3:markers[1]]

def tokenize_grammar(text):
    """Tokenize the grammar section into a stream of tokens.
    
    Returns list of (type, value, line) tuples where type is:
    - 'SYMBOL': identifier or keyword
    - 'CHAR': character literal like '('
    - 'ACTION': { ... } block (with brace balancing)
    - 'COLON': :
    - 'PIPE': |
    - 'SEMI': ;
    - 'PREC': %prec SYMBOL
    """
    tokens = []
    i = 0
    line = 1
    
    while i < len(text):
        c = text[i]
        
        # Track newlines
        if c == '\n':
            line += 1
            i += 1
            continue
        
        # Skip whitespace
        if c in ' \t\r':
            i += 1
            continue
        
        # Character literal
        if c == "'" and i+2 < len(text) and text[i+2] == "'":
            tokens.append(('CHAR', text[i:i+3], line))
            i += 3
            continue
        
        # Action block
        if c == '{':
            depth = 1
            j = i + 1
            in_str = False
            in_chr = False
            while j < len(text) and depth > 0:
                ch = text[j]
                if ch == '\n':
                    line += 1
                if not in_str and not in_chr:
                    if ch == '{': depth += 1
                    elif ch == '}': depth -= 1
                    elif ch == '"': in_str = True
                    elif ch == "'": in_chr = True
                elif in_str:
                    if ch == '\\': j += 1
                    elif ch == '"': in_str = False
                elif in_chr:
                    if ch == '\\': j += 1
                    elif ch == "'": in_chr = False
                j += 1
            action_text = text[i+1:j-1]  # Strip outer braces
            tokens.append(('ACTION', action_text, line))
            i = j
            continue
        
        # Colon
        if c == ':':
            tokens.append(('COLON', ':', line))
            i += 1
            continue
        
        # Pipe
        if c == '|':
            tokens.append(('PIPE', '|', line))
            i += 1
            continue
        
        # Semicolon
        if c == ';':
            tokens.append(('SEMI', ';', line))
            i += 1
            continue
        
        # %prec
        if text[i:i+5] == '%prec':
            j = i + 5
            while j < len(text) and text[j] in ' \t':
                j += 1
            k = j
            while k < len(text) and (text[k].isalnum() or text[k] == '_'):
                k += 1
            prec_sym = text[j:k]
            tokens.append(('PREC', prec_sym, line))
            i = k
            continue
        
        # Symbol (identifier or keyword)
        if c.isalpha() or c == '_':
            j = i
            while j < len(text) and (text[j].isalnum() or text[j] == '_'):
                j += 1
            tokens.append(('SYMBOL', text[i:j], line))
            i = j
            continue
        
        # Skip anything else
        i += 1
    
    return tokens

def parse_rules(tokens):
    """Parse token stream into rules.
    
    Returns list of (nt_name, [Alternative, ...]) where
    Alternative = (rhs_symbols, action_text, prec_token)
    """
    rules = []
    i = 0
    
    while i < len(tokens):
        # Look for SYMBOL COLON pattern (start of rule)
        if (i + 1 < len(tokens) and 
            tokens[i][0] == 'SYMBOL' and 
            tokens[i+1][0] == 'COLON'):
            
            nt_name = tokens[i][1]
            i += 2  # Skip name and colon
            
            alternatives = []
            
            # Parse alternatives until SEMI
            while i < len(tokens) and tokens[i][0] != 'SEMI':
                # Parse one alternative
                rhs = []
                action = ''
                prec = None
                
                while i < len(tokens) and tokens[i][0] not in ('PIPE', 'SEMI'):
                    tt, tv, tl = tokens[i]
                    if tt == 'SYMBOL' or tt == 'CHAR':
                        rhs.append(tv)
                    elif tt == 'ACTION':
                        action = tv
                    elif tt == 'PREC':
                        prec = tv
                    i += 1
                
                alternatives.append((rhs, action.strip(), prec))
                
                # Skip PIPE if present
                if i < len(tokens) and tokens[i][0] == 'PIPE':
                    i += 1
            
            # Skip SEMI
            if i < len(tokens) and tokens[i][0] == 'SEMI':
                i += 1
            
            rules.append((nt_name, alternatives))
        else:
            i += 1
    
    return rules

def is_terminal(sym):
    if sym.startswith("'"):
        return True
    return sym == sym.upper() and sym.replace('_','').isalnum() and len(sym) > 0

def convert_action(action, rhs_symbols):
    """Convert Bison action to Lime action."""
    if not action:
        return ''
    
    result = action
    
    # Build param name map
    params = []
    for idx in range(len(rhs_symbols)):
        params.append(chr(ord('B') + idx) if idx < 24 else f'P{idx}')
    
    # Replace $$-> with A->
    result = result.replace('$$->', 'A->')
    # Replace $$ with A  
    result = re.sub(r'\$\$', 'A', result)
    
    # Replace $N
    def repl_dollar(m):
        n = int(m.group(1))
        if 1 <= n <= len(params):
            return params[n-1]
        return m.group(0)
    result = re.sub(r'\$(\d+)', repl_dollar, result)
    
    # Replace @N with LOC(param)
    def repl_at(m):
        n = int(m.group(1))
        if 1 <= n <= len(params):
            return f'LOC({params[n-1]})'
        return m.group(0)
    result = re.sub(r'@(\d+)', repl_at, result)
    
    return result

def format_lime_rule(nt, rhs, action, prec):
    """Format one Lime production rule."""
    mapped_rhs = []
    params = []
    for idx, sym in enumerate(rhs):
        mapped = CHAR_MAP.get(sym, TOKEN_REMAP.get(sym, sym))
        param = chr(ord('B') + idx) if idx < 24 else f'P{idx}'
        params.append(param)

        if is_terminal(mapped):
            mapped_rhs.append(mapped)
        else:
            mapped_rhs.append(f"{mapped}({param})")
    
    rhs_str = ' '.join(mapped_rhs)
    prec_mapped = TOKEN_REMAP.get(prec, prec) if prec else None
    prec_str = f' [{prec_mapped}]' if prec_mapped else ''
    
    if action:
        conv = convert_action(action, rhs)
        # Indent action lines
        lines = conv.split('\n')
        indented = '\n'.join('    ' + l if l.strip() else '' for l in lines)
        return f"{nt}(A) ::= {rhs_str}.{prec_str} {{\n{indented}\n}}\n"
    else:
        if rhs_str:
            return f"{nt}(A) ::= {rhs_str}.{prec_str}\n"
        else:
            return f"{nt}(A) ::=.{prec_str}  /* empty */\n"


def main():
    fn = sys.argv[1]
    
    # Extract grammar section
    gram_text = extract_grammar_section(fn)
    
    # Strip comments 
    gram_clean = strip_c_comments(gram_text)
    
    # Tokenize
    tokens = tokenize_grammar(gram_clean)
    print(f"Tokenized: {len(tokens)} tokens", file=sys.stderr)
    
    # Parse
    rules = parse_rules(tokens)
    total_alts = sum(len(a) for _, a in rules)
    print(f"Parsed: {len(rules)} non-terminals, {total_alts} alternatives", file=sys.stderr)
    
    # Output header
    print("/*")
    print(" * PostgreSQL Grammar for Lime Parser Generator")
    print(f" * Converted from gram.y: {len(rules)} non-terminals, {total_alts} alternatives.")
    print(" *")
    print(" * Conversion conventions:")
    print(" *   Bison $$  -> A    (LHS result)")
    print(" *   Bison $N  -> B,C,D,...  (RHS params, 1-indexed)")
    print(" *   Bison @N  -> LOC(param) (source location)")
    print(" *   Bison %prec TOKEN -> [TOKEN]")
    print(" *   Bison 'x' -> named tokens (LPAREN, COMMA, etc.)")
    print(" *   Empty alt -> nt(A) ::=.  (empty RHS)")
    print(" */")
    print()
    print('%include "tokens.lime"')
    print()
    print("%include {")
    print('#include "postgres.h"')
    print('#include <ctype.h>')
    print('#include <limits.h>')
    print('#include "catalog/index.h"')
    print('#include "catalog/namespace.h"')
    print('#include "catalog/pg_am.h"')
    print('#include "catalog/pg_trigger.h"')
    print('#include "commands/defrem.h"')
    print('#include "commands/trigger.h"')
    print('#include "gramparse.h"')
    print('#include "nodes/makefuncs.h"')
    print('#include "nodes/nodeFuncs.h"')
    print('#include "parser/parser.h"')
    print('#include "utils/datetime.h"')
    print('#include "utils/xml.h"')
    print('#include "pg_gram_helpers.h"')
    print()
    print('#define LOC(tok) ((tok).location)')
    print('#define yyscanner (pstate->scanner)')
    print("}")
    print()
    print("%syntax_error { parser_yyerror(\"syntax error\"); }")
    print("%parse_failure { parser_yyerror(\"parse failure\"); }")
    print()
    print("%start_symbol parse_toplevel")
    print()
    
    # Output rules grouped
    for nt, alts in rules:
        print(f"/* ----- {nt} ----- */")
        for rhs, action, prec in alts:
            print(format_lime_rule(nt, rhs, action, prec))
    
    print(f"\n/* End of grammar: {len(rules)} non-terminals, {total_alts} alternatives */")

if __name__ == '__main__':
    main()
