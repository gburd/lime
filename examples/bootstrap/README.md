# PostgreSQL Bootstrap (BKI) Parser for Lime

This directory contains a conversion of the PostgreSQL bootstrap parser from
its original Bison/Flex format (`bootparse.y` + `bootscanner.l`) into Lime
parser generator format.

## Background

The BKI (Backend Interface) format is used by PostgreSQL's `initdb` to
initialize the system catalogs during database cluster creation. The bootstrap
parser reads BKI files that contain commands to create relations, insert
tuples, declare indexes, and build index structures.

## Source Files

| File | Description |
|------|-------------|
| `boot_grammar.lime` | BKI grammar in Lime format (from `bootparse.y`) |
| `boot_gram_defs.h` | Type definitions and function declarations |
| `boot_tokenize.c` | Hand-written lexer (from `bootscanner.l`) |
| `boot_actions.c` | Semantic action helpers (from grammar actions) |
| `main.c` | Standalone driver program |
| `Makefile` | Build system |
| `tests/*.bki` | Test BKI files |

## Original Sources

| PostgreSQL File | Lines | Description |
|-----------------|-------|-------------|
| `src/backend/bootstrap/bootparse.y` | 499 | Bison grammar |
| `src/backend/bootstrap/bootscanner.l` | 165 | Flex scanner |

## BKI Commands

The BKI format supports these commands:

```
open <relname>
close <relname>
create <relname> <oid> [bootstrap] [shared_relation] [rowtype_oid <oid>]
       (<colname> = <typename> [FORCE NOT NULL | FORCE NULL], ...)
insert (<value> | _null_ , ...)
declare [unique] index <idxname> <oid> on <relname> using <amname>
        (<colname> <opclass>, ...)
declare toast <oid> <oid> on <relname>
build indices
```

## Building

From this directory:

```sh
make          # Build the parser (also builds lime if needed)
make test     # Run tests
make clean    # Remove object files and executable
make distclean # Also remove generated parser source
```

## Conversion Notes

### Key Differences from Bison Version

1. **No mid-rule actions**: The original Bison grammar uses mid-rule actions
   (e.g., in `Boot_CreateStmt` where `do_start()` is called between the
   opening `LPAREN` and the column list). Lime does not support mid-rule
   actions, so these are restructured into the final reduction action.

2. **Named parameters**: Bison's `$1`, `$2`, etc. are replaced with Lime's
   named parameters (`A`, `B`, `C`, ...).

3. **No %union**: Bison's `%union` is replaced with per-nonterminal `%type`
   declarations.

4. **Extra argument**: Bison's `%parse-param` and `%lex-param` are replaced
   with Lime's `%extra_argument` directive, which passes a `BootParseState`
   pointer.

5. **Hand-written tokenizer**: The Flex scanner is replaced with a hand-written
   C tokenizer that implements the same token patterns.

6. **Semantic actions factored out**: Grammar actions are moved to
   `boot_actions.c` helper functions, keeping the grammar file focused on
   structure.

### Token Mapping

| Bison Token | Lime Token | Pattern |
|-------------|------------|---------|
| `ID` | `ID` | `[-A-Za-z0-9_]+` or `'...'` |
| `NULLVAL` | `NULLVAL` | `_null_` (reserved) |
| `COMMA` | `COMMA` | `,` |
| `EQUALS` | `EQUALS` | `=` |
| `LPAREN` | `LPAREN` | `(` |
| `RPAREN` | `RPAREN` | `)` |
| `OPEN` | `OPEN` | `open` |
| `XCLOSE` | `XCLOSE` | `close` |
| `XCREATE` | `XCREATE` | `create` |
| `INSERT_TUPLE` | `INSERT_TUPLE` | `insert` |
| ... | ... | ... |

All keywords except `_null_` are unreserved and can be used as identifiers
through the `boot_ident` production.
