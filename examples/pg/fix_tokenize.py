#!/usr/bin/env python3
"""Fix tokenize.c to use Lime-generated token IDs from pg_grammar.h."""

import re

path = '/home/gburd/ws/lemon/examples/pg/tokenize.c'

with open(path) as f:
    content = f.read()

# 1. Replace TK_ prefixed token names with bare names (from pg_grammar.h)
# Longer names first to avoid partial matches
replacements = [
    ('TK_EQUALS_GREATER', 'EQUALS_GREATER'),
    ('TK_GREATER_EQUALS', 'GREATER_EQUALS'),
    ('TK_LESS_EQUALS', 'LESS_EQUALS'),
    ('TK_NOT_EQUALS', 'NOT_EQUALS'),
    ('TK_RIGHT_ARROW', 'RIGHT_ARROW'),
    ('TK_COLON_EQUALS', 'COLON_EQUALS'),
    ('TK_DOT_DOT', 'DOT_DOT'),
    ('TK_TYPECAST', 'TYPECAST'),
    ('TK_FORMAT_LA', 'FORMAT_LA'),
    ('TK_WITHOUT_LA', 'WITHOUT_LA'),
    ('TK_NULLS_LA', 'NULLS_LA'),
    ('TK_WITH_LA', 'WITH_LA'),
    ('TK_NOT_LA', 'NOT_LA'),
    ('TK_USCONST', 'USCONST'),
    ('TK_ICONST', 'ICONST'),
    ('TK_FCONST', 'FCONST'),
    ('TK_SCONST', 'SCONST'),
    ('TK_BCONST', 'BCONST'),
    ('TK_XCONST', 'XCONST'),
    ('TK_UIDENT', 'UIDENT'),
    ('TK_PARAM', 'PARAM'),
    ('TK_IDENT', 'IDENT'),
    ('TK_OP', 'OP'),
]

for old, new in replacements:
    content = content.replace(old, new)

# 2. Replace "return (int)c;" for self-char tokens
content = content.replace(
    'return (int)c;',
    'return self_to_token(c);'
)
content = content.replace(
    'return (int)(unsigned char)s->input[op_start];',
    'return self_to_token((unsigned char)s->input[op_start]);'
)

# 3. Fix is_keyword_token: remove TK_KEYWORD_BASE check
old_is_kw = """static bool is_keyword_token(int token, const char *kwname) {
    if (token < TK_KEYWORD_BASE) return false;
    const char *name = pg_keyword_name(token);
    if (!name) return false;
    return strcmp(name, kwname) == 0;
}"""
new_is_kw = """static bool is_keyword_token(int token, const char *kwname) {
    const char *name = pg_keyword_name(token);
    if (!name) return false;
    return strcmp(name, kwname) == 0;
}"""
content = content.replace(old_is_kw, new_is_kw)

# 4. Replace the needs_lookahead string comparison with direct token ID checks
old_needs = """    bool needs_lookahead = false;
    if (token >= TK_KEYWORD_BASE) {
        const char *name = pg_keyword_name(token);
        if (name) {
            needs_lookahead = (strcmp(name, "not") == 0 ||
                               strcmp(name, "nulls") == 0 ||
                               strcmp(name, "with") == 0 ||
                               strcmp(name, "without") == 0 ||
                               strcmp(name, "format") == 0);
        }
    }"""
new_needs = """    bool needs_lookahead = (token == NOT ||
                               token == NULLS_P ||
                               token == WITH ||
                               token == WITHOUT ||
                               token == FORMAT);"""
content = content.replace(old_needs, new_needs)

# 5. Replace the lookahead replacement checks with direct token ID comparisons
old_replace = """    const char *cur_name = pg_keyword_name(token);
    if (!cur_name) return token;

    if (strcmp(cur_name, "not") == 0) {
        if (is_keyword_token(next_token, "between") ||
            is_keyword_token(next_token, "in") ||
            is_keyword_token(next_token, "like") ||
            is_keyword_token(next_token, "ilike") ||
            is_keyword_token(next_token, "similar")) {
            return NOT_LA;
        }
    }
    else if (strcmp(cur_name, "nulls") == 0) {
        if (is_keyword_token(next_token, "first") ||
            is_keyword_token(next_token, "last")) {
            return NULLS_LA;
        }
    }
    else if (strcmp(cur_name, "with") == 0) {
        if (is_keyword_token(next_token, "time") ||
            is_keyword_token(next_token, "ordinality")) {
            return WITH_LA;
        }
    }
    else if (strcmp(cur_name, "without") == 0) {
        if (is_keyword_token(next_token, "time")) {
            return WITHOUT_LA;
        }
    }
    else if (strcmp(cur_name, "format") == 0) {
        if (is_keyword_token(next_token, "json")) {
            return FORMAT_LA;
        }
    }"""
new_replace = """    if (token == NOT) {
        if (next_token == BETWEEN || next_token == IN_P ||
            next_token == LIKE || next_token == ILIKE ||
            next_token == SIMILAR) {
            return NOT_LA;
        }
    }
    else if (token == NULLS_P) {
        if (next_token == FIRST_P || next_token == LAST_P) {
            return NULLS_LA;
        }
    }
    else if (token == WITH) {
        if (next_token == TIME || next_token == ORDINALITY) {
            return WITH_LA;
        }
    }
    else if (token == WITHOUT) {
        if (next_token == TIME) {
            return WITHOUT_LA;
        }
    }
    else if (token == FORMAT) {
        if (next_token == JSON) {
            return FORMAT_LA;
        }
    }"""
content = content.replace(old_replace, new_replace)

# 6. Fix pg_token_name default case
old_default = r"""        default:
            if (token >= TK_KEYWORD_BASE) {
                const char *name = pg_keyword_name(token);
                if (name) return name;
            }
            if (token > 0 && token < 128) {
                static char buf[4];
                buf[0] = '\'';
                buf[1] = (char)token;
                buf[2] = '\'';
                buf[3] = '\0';
                return buf;
            }
            return "???";"""
new_default = """        default: {
            const char *name = pg_keyword_name(token);
            if (name) return name;
            return "???";
        }"""
content = content.replace(old_default, new_default)

# 7. Fix the test driver keyword check
old_test = """        } else if (token >= TK_KEYWORD_BASE) {
            printf(" kw=\\"%s\\"", val.keyword);
        } else if (token > 0 && token < 128) {
            printf(" char='%c'", (char)token);"""
new_test = """        } else {
            const char *kwn = pg_keyword_name(token);
            if (kwn)
                printf(" kw=\\"%s\\"", val.keyword);"""
content = content.replace(old_test, new_test)

with open(path, 'w') as f:
    f.write(content)

# Verify
remaining = re.findall(r'\bTK_\w+', content)
if remaining:
    unique = sorted(set(remaining))
    print(f"WARNING: {len(remaining)} TK_ references remain ({len(unique)} unique):")
    for r in unique:
        count = remaining.count(r)
        print(f"  {r} ({count}x)")
else:
    print("All TK_ references successfully replaced.")

# Check for return (int)c patterns
int_c = re.findall(r'return \(int\)', content)
if int_c:
    print(f"WARNING: {len(int_c)} 'return (int)' patterns remain")
else:
    print("All (int)c returns replaced with self_to_token().")
