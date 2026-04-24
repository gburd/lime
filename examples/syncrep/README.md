# PostgreSQL Synchronous Replication Config Parser for Lime

This directory contains a conversion of the PostgreSQL synchronous replication
configuration parser from its original Bison/Flex format into Lime parser
generator format.

## Background

PostgreSQL's `synchronous_standby_names` GUC parameter specifies which standby
servers to use for synchronous replication and how many must confirm receipt of
WAL data. The parameter supports several formats:

```
standby1, standby2                  -- priority, num_sync=1
3 (s1, s2, s3, s4)                  -- priority, num_sync=3
ANY 2 (s1, s2, s3)                  -- quorum, num_sync=2
FIRST 3 (s1, s2, s3, s4)           -- priority (explicit), num_sync=3
```

## Source Files

| File | Description |
|------|-------------|
| `syncrep_gram.lime` | Grammar in Lime format (from `syncrep_gram.y`) |
| `syncrep_defs.h` | Type definitions and function declarations |
| `syncrep_tokenize.c` | Hand-written lexer (from `syncrep_scanner.l`) |
| `syncrep_actions.c` | Semantic action helpers |
| `main.c` | Standalone driver program |
| `Makefile` | Build system |
| `tests/*.conf` | Test configuration strings |

## Original Sources

| PostgreSQL File | Lines | Description |
|-----------------|-------|-------------|
| `src/backend/replication/syncrep_gram.y` | 119 | Bison grammar |
| `src/backend/replication/syncrep_scanner.l` | 220 | Flex scanner |

## Building

```sh
make          # Build the parser
make test     # Run tests
make clean    # Remove object files
make distclean # Also remove generated files
```

## Usage

```sh
./syncrep_parser "s1, s2"
./syncrep_parser "ANY 2 (s1, s2, s3)"
./syncrep_parser -q "FIRST 3 (s1, s2, s3, s4)"  # validate only
echo "3 (a, b, c, d)" | ./syncrep_parser
```

## Conversion Notes

The syncrep grammar is small (4 production rules) and maps cleanly to Lime:

1. Keywords `ANY` and `FIRST` are case-insensitive (handled in tokenizer)
2. Numbers used as standby names (e.g., `2 (123, abc)`) are valid
3. Double-quoted identifiers with `""` escaping are supported
4. The wildcard `*` is treated as a regular NAME token
5. No precedence declarations are needed (no ambiguity)
