# Replication Protocol Parser (Lime Conversion)

Converted from PostgreSQL's `src/backend/replication/repl_gram.y` (448 lines)
and `src/backend/replication/repl_scanner.l` (350 lines).

## Commands Supported

| Command | Description |
|---------|-------------|
| `IDENTIFY_SYSTEM` | Request system identification info |
| `BASE_BACKUP [(options)]` | Start a base backup |
| `START_REPLICATION [SLOT s] [PHYSICAL] lsn [TIMELINE t]` | Start physical streaming |
| `START_REPLICATION SLOT s LOGICAL lsn [(options)]` | Start logical streaming |
| `CREATE_REPLICATION_SLOT s [TEMPORARY] PHYSICAL\|LOGICAL plugin [options]` | Create slot |
| `DROP_REPLICATION_SLOT s [WAIT]` | Drop a replication slot |
| `ALTER_REPLICATION_SLOT s (options)` | Alter slot options |
| `READ_REPLICATION_SLOT s` | Read slot info |
| `TIMELINE_HISTORY t` | Request timeline history |
| `SHOW setting` | Show a configuration setting |
| `UPLOAD_MANIFEST` | Upload an incremental backup manifest |

## Building

```bash
# Build Lime first (if not already done)
make -C ../.. lime

# Generate parser and build
make

# Run tests
make test
```

## Files

| File | Description |
|------|-------------|
| `repl_gram.lime` | Lime grammar (converted from repl_gram.y) |
| `repl_defs.h` | Type definitions and function declarations |
| `repl_tokenize.c` | Hand-written scanner (converted from repl_scanner.l) |
| `repl_actions.c` | Constructor helpers and parser state management |
| `main.c` | Standalone driver program |
| `Makefile` | Build system |
| `tests/*.repl` | Test cases for each command type |

## Conversion Notes

### Grammar (repl_gram.y -> repl_gram.lime)

- Bison `%union` replaced with `%token_type {ReplToken}` and per-nonterminal `%type`
- Bison `%parse-param` replaced with `%extra_argument {ReplParseState *pstate}`
- Bison `$$` replaced with named result variable (R)
- Bison `$N` replaced with named positional parameters (A, B, C, ...)
- Single-character tokens (`'('`, `')'`, etc.) replaced with named tokens (LPAREN, RPAREN, etc.)
- Empty alternatives use `nt(R) ::= .` syntax
- All PostgreSQL-specific node types (e.g., `Node *`, `StartReplicationCmd`) replaced with
  standalone equivalents (`ReplCommand`, `ReplDefElem`)

### Scanner (repl_scanner.l -> repl_tokenize.c)

- Flex-generated scanner replaced with hand-written C scanner
- Reentrant scanner state moved into `ReplParseState`
- Exclusive states `<xq>` and `<xd>` for quoted strings handled inline
- Keyword matching is case-insensitive via binary search
- Identifiers are downcased (matching PostgreSQL behavior)
- RECPTR token (hex/hex WAL location format) recognized inline
- Push-back token mechanism from original scanner removed (not needed with Lime)

### Memory Management

- PostgreSQL's `palloc`/`pfree` replaced with standard `malloc`/`free`
- `repl_free_command()` provided for cleanup
