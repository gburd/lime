# PostgreSQL Lexer Analysis (scan.l)

Analysis of `src/backend/parser/scan.l` from PostgreSQL 18 (2026) for conversion
to a standalone tokenizer compatible with the Lime parser generator.

## 1. Lexer States

The PostgreSQL lexer uses 11 exclusive flex states plus the default `INITIAL`
state. Each state handles a specific kind of quoted/comment context.

| State    | Purpose                                       | Enters From     | Returns To       |
|----------|-----------------------------------------------|-----------------|------------------|
| INITIAL  | Default state; all top-level rules apply       | (start)         | N/A              |
| `xb`     | Bit string literal: `B'...'`                   | `{xbstart}`     | via `xqs`        |
| `xc`     | C-style block comments: `/* ... */`            | `{xcstart}`     | INITIAL           |
| `xd`     | Delimited (double-quoted) identifiers          | `{xdstart}`     | INITIAL           |
| `xh`     | Hexadecimal byte string: `X'...'`              | `{xhstart}`     | via `xqs`        |
| `xq`     | Standard single-quoted string                  | `{xqstart}`     | via `xqs`        |
| `xqs`    | Quote-stop lookahead (string continuation)     | any string state | prev or INITIAL  |
| `xe`     | Extended string with backslash escapes: `E'...'`| `{xestart}`    | via `xqs`        |
| `xdolq`  | Dollar-quoted string: `$tag$...$tag$`          | `{dolqdelim}`   | INITIAL           |
| `xui`    | Unicode-escape identifier: `U&"..."`           | `{xuistart}`    | INITIAL           |
| `xus`    | Unicode-escape string: `U&'...'`               | `{xusstart}`    | via `xqs`        |
| `xeu`    | Unicode surrogate pair continuation in `xe`    | from `xe`       | `xe`             |

### State Transition Diagram

```
INITIAL ─┬─> xb ──> xqs ──> INITIAL (returns BCONST)
          ├─> xh ──> xqs ──> INITIAL (returns XCONST)
          ├─> xq ──> xqs ──> INITIAL (returns SCONST)
          ├─> xe ──> xqs ──> INITIAL (returns SCONST)
          │    └──> xeu ──> xe  (surrogate pairs)
          ├─> xus ─> xqs ──> INITIAL (returns USCONST)
          ├─> xd ──────────> INITIAL (returns IDENT)
          ├─> xui ─────────> INITIAL (returns UIDENT)
          ├─> xdolq ───────> INITIAL (returns SCONST)
          └─> xc ──────────> INITIAL (comment; no token)
```

### The `xqs` Continuation Mechanism

When a single-quote is encountered inside any quoted-string state (`xb`, `xh`,
`xq`, `xe`, `xus`), the lexer does not immediately end the string. Instead it
transitions to state `xqs` to look ahead for a **quote continuation** -- a
newline-separated quote that continues the string literal per the SQL standard:

```sql
SELECT 'hello '
       'world';   -- This is one string: 'hello world'
```

If continuation is found (`{quotecontinue}`), it returns to the original string
state. Otherwise, it throws back all lookahead and emits the completed token.

### Nested Comments

Block comments (`xc`) support nesting via the `xcdepth` counter. Each `/*`
increments the depth, each `*/` decrements it. The comment only ends when depth
returns to zero.

## 2. Token Types Returned by the Lexer

### Literal Value Tokens

| Token    | Type Tag     | Description                           | Flex Pattern              |
|----------|-------------|---------------------------------------|---------------------------|
| `ICONST` | `<ival>`    | Integer constant (fits int32)         | `{decinteger}`, `{hexinteger}`, etc. |
| `FCONST` | `<str>`     | Float/numeric constant (or integer overflow) | `{numeric}`, `{real}`, or int overflow |
| `SCONST` | `<str>`     | String constant                       | `'...'`, `E'...'`, `$$...$$` |
| `BCONST` | `<str>`     | Bit string constant                   | `B'...'`                  |
| `XCONST` | `<str>`     | Hex string constant                   | `X'...'`                  |
| `USCONST`| `<str>`     | Unicode-escape string                 | `U&'...'`                 |
| `PARAM`  | `<ival>`    | Positional parameter                  | `$1`, `$2`, etc.          |

### Identifier Tokens

| Token    | Type Tag     | Description                           |
|----------|-------------|---------------------------------------|
| `IDENT`  | `<str>`     | Regular identifier (lowercased) or double-quoted identifier |
| `UIDENT` | `<str>`     | Unicode-escape identifier: `U&"..."` |

### Operator / Punctuation Tokens

| Token              | Description                              |
|--------------------|------------------------------------------|
| `TYPECAST`         | `::`                                     |
| `DOT_DOT`          | `..`                                     |
| `COLON_EQUALS`     | `:=`                                     |
| `EQUALS_GREATER`   | `=>`                                     |
| `LESS_EQUALS`      | `<=`                                     |
| `GREATER_EQUALS`   | `>=`                                     |
| `NOT_EQUALS`       | `<>` or `!=`                             |
| `RIGHT_ARROW`      | `->`                                     |
| `Op`               | Generic user-defined operator (`<str>`)  |
| Single-char tokens | `,` `(` `)` `[` `]` `.` `;` `:` `\|` `+` `-` `*` `/` `%` `^` `<` `>` `=` |

### Keyword Tokens

Keywords (~475 tokens) are recognized by the `{identifier}` rule. When an
identifier matches an entry in the keyword hash table (`ScanKeywordLookup`),
the corresponding token value from `ScanKeywordTokens[]` is returned instead
of `IDENT`.

### Injected Tokens (Not From Lexer)

These tokens are created by `parser.c` (the wrapper around the raw scanner),
not by `scan.l` itself:

| Token                    | Purpose                                    |
|--------------------------|--------------------------------------------|
| `FORMAT_LA`              | Lookahead-resolved: FORMAT in JSON context  |
| `NOT_LA`                 | Lookahead-resolved: NOT before LIKE/SIMILAR |
| `NULLS_LA`               | Lookahead-resolved: NULLS in window frame   |
| `WITH_LA`                | Lookahead-resolved: WITH before specific kw  |
| `WITHOUT_LA`             | Lookahead-resolved: WITHOUT in JSON context  |
| `MODE_TYPE_NAME`         | Injected to parse type name expressions      |
| `MODE_PLPGSQL_EXPR`      | Injected for PL/pgSQL expression parsing     |
| `MODE_PLPGSQL_ASSIGN1-3` | Injected for PL/pgSQL assignment parsing     |

## 3. Keyword Table Structure

### ScanKeywordTokens Array

The keyword system uses a two-part design:

1. **`ScanKeywordList`** (in `src/include/common/kwlookup.h`): A compact
   structure containing all keyword strings packed into a single `char[]`
   buffer with a parallel offsets array. Lookup is via binary search or
   perfect hash.

2. **`ScanKeywordTokens[]`** (defined in `scan.l`): A `uint16` array
   generated via the X-macro pattern from `kwlist.h`:
   ```c
   #define PG_KEYWORD(kwname, value, category, collabel) value,
   const uint16 ScanKeywordTokens[] = {
   #include "parser/kwlist.h"
   };
   #undef PG_KEYWORD
   ```

Each entry in `kwlist.h` has the form:
```c
PG_KEYWORD("abort", ABORT_P, UNRESERVED_KEYWORD, BARE_LABEL)
PG_KEYWORD("all", ALL, RESERVED_KEYWORD, BARE_LABEL)
...
```

### Keyword Categories (from gram.y)

Keywords are classified into four reservedness categories:

| Category                 | Count (approx) | Usage                                              |
|--------------------------|----------------|----------------------------------------------------|
| `unreserved_keyword`     | ~350           | Can be used as any kind of name                     |
| `col_name_keyword`       | ~75            | Can be column names, but not function/type names    |
| `type_func_name_keyword` | ~25            | Can be function/type names, but not column names    |
| `reserved_keyword`       | ~80            | Cannot be used as names (only as ColLabel with AS)  |

Additionally, `bare_label_keyword` lists keywords that can appear as column
labels without `AS` prefix.

### Identifier and Keyword Processing

When the `{identifier}` pattern matches:
1. `ScanKeywordLookup` is called with the original-case text
2. If found, the keyword's token value is returned and `yylval->keyword`
   is set to the canonical (lowercase) keyword string
3. If not found, the identifier is lowercased via `downcase_truncate_identifier`
   (which also truncates at `NAMEDATALEN = 64` bytes), and `IDENT` is returned

## 4. String Literal Handling

### Standard Strings (`xq`)

- Entered on bare single-quote `'`
- Content is accumulated in a literal buffer (`literalbuf`) via `addlit()`
- Embedded quotes are doubled: `''` -> `'`
- No backslash escapes (per SQL standard, unless `standard_conforming_strings = off`)

### Extended Strings (`xe`)

- Entered on `E'` or `e'`
- Supports backslash escape sequences:
  - `\b`, `\f`, `\n`, `\r`, `\t`, `\v` - standard C escapes
  - `\NNN` - octal escape (1-3 digits)
  - `\xNN` - hex escape (1-2 digits)
  - `\uXXXX` - Unicode escape (4 hex digits)
  - `\UXXXXXXXX` - Unicode escape (8 hex digits)
  - `\'` - escaped quote (subject to `backslash_quote` GUC)
  - `\\` - literal backslash
- Unicode surrogate pairs handled via `xeu` substate
- After completion, validates multibyte encoding if non-ASCII was seen

### Dollar-Quoted Strings (`xdolq`)

- Entered on `$tag$` pattern (tag is optional identifier)
- Completely opaque: no escape processing at all
- Ends on matching `$tag$`
- Returns `SCONST`

### Bit Strings (`xb`) and Hex Strings (`xh`)

- `B'...'` -> `BCONST` (prefixed with `'b'` marker in literal)
- `X'...'` -> `XCONST` (prefixed with `'x'` marker in literal)
- Content is passed through without validation (validated later)

### Unicode Strings (`xus`) and Identifiers (`xui`)

- `U&'...'` -> `USCONST`
- `U&"..."` -> `UIDENT`
- Escapes are processed post-lexing by `parser.c`

### National Character (`N'...'`)

- Special case: `N` is consumed and looked up as keyword `NCHAR`
- The quote is pushed back for normal string lexing
- Returns the keyword token for `NCHAR` followed by the string

### String Continuation (SQL Standard)

String literals separated by whitespace containing at least one newline are
automatically concatenated:
```sql
SELECT 'foo'
       'bar';  -- equivalent to SELECT 'foobar';
```

This is handled by the `xqs` state mechanism described above.

## 5. Comment Handling

### SQL-Style Line Comments

- Pattern: `--` followed by anything to end of line
- Handled as part of `{whitespace}` pattern
- Completely consumed; no token emitted

### C-Style Block Comments

- Pattern: `/* ... */` with nesting support
- Entered via `{xcstart}` pattern: `/*{op_chars}*`
  - The extra `{op_chars}*` prevents the `{operator}` rule from getting
    a longer match (tie-breaking ensures `xcstart` wins)
  - Excess characters after `/*` are pushed back with `yyless(2)`
- Nesting tracked via `xcdepth` counter
- Unterminated comment at EOF is an error
- No token emitted

### Comment/Operator Interaction

The operator rule must check for embedded `/*` or `--` sequences, since those
are comment starts. If found, the operator is truncated at that point and the
remainder is pushed back for re-scanning.

## 6. Numeric Literal Handling

### Integer Literals

| Pattern         | Base | Example     |
|-----------------|------|-------------|
| `{decinteger}`  | 10   | `42`, `1_000` |
| `{hexinteger}`  | 16   | `0xFF`, `0x1_00` |
| `{octinteger}`  | 8    | `0o77`      |
| `{bininteger}`  | 2    | `0b1010`    |

- Underscores allowed as visual separators (not at start/end of digit groups)
- Parsed via `process_integer_literal`:
  - If fits in int32 -> `ICONST` with `yylval->ival`
  - If overflow -> `FCONST` with `yylval->str` (treated as numeric)

### Floating Point / Numeric

| Pattern     | Example        | Token   |
|-------------|----------------|---------|
| `{numeric}` | `3.14`, `.5`   | `FCONST`|
| `{real}`    | `1e10`, `3.14e-2` | `FCONST`|

### Junk Detection

Patterns like `{integer_junk}` catch ambiguous sequences like `0x1234abc`
where a number is immediately followed by identifier characters. These produce
an error rather than silently splitting.

### Special: `numericfail`

The pattern `{decinteger}..` is caught to correctly lex `1..10` as three
tokens: `1`, `..`, `10` rather than `1.`, `.10`.

## 7. Operator Handling

### Self Characters (Single-Char Tokens)

Characters in the `self` set are returned as their ASCII value:
```
, ( ) [ ] . ; : | + - * / % ^ < > =
```

### Multi-Character Operators

Named multi-char operators have dedicated tokens:
`::`, `..`, `:=`, `=>`, `<=`, `>=`, `<>`, `!=`, `->`

### User-Defined Operators (`Op`)

The `{operator}` rule matches sequences of:
```
~ ! @ # ^ & | ` ? + - * / % < > =
```

SQL compatibility constraint: `+` and `-` cannot end a multi-char operator
unless the operator also contains one of `~ ! @ # ^ & | ` ? %`. This ensures
`=-` lexes as `=` followed by `-` (not as operator `=-`).

If truncation reduces to a single `self` character or a known two-char token,
the appropriate specific token is returned instead of `Op`.

Operators are limited to `NAMEDATALEN` (63) characters.

## 8. Location Tracking

- `SET_YYLLOC()` macro: `*(yylloc) = yytext - yyextra->scanbuf`
  - Records byte offset from start of input string
  - Must be called in the first rule of multi-rule token processing
- `ADVANCE_YYLLOC(delta)`: Moves location forward by delta bytes
- `PUSH_YYLLOC()` / `POP_YYLLOC()`: Save/restore location around
  sub-token error reporting (e.g., bad escape in string literal)
- Byte offsets are converted to character positions by `scanner_errposition()`
  using `pg_mbstrlen_with_len()` for multibyte encoding support

## 9. Scanner Infrastructure

### Reentrant Design

- `%option reentrant` / `%option bison-bridge` / `%option bison-locations`
- All state stored in `core_yy_extra_type` (accessed via `yyextra`)
- Thread-safe; multiple scanners can operate concurrently

### Extra State (`core_yy_extra_type`)

Key fields (inferred from usage in scan.l):
- `scanbuf` / `scanbuflen` - Input buffer
- `literalbuf` / `literallen` / `literalalloc` - String literal accumulator
- `xcdepth` - Nested comment depth counter
- `state_before_str_stop` - Saved state for `xqs` mechanism
- `dolqstart` - Dollar-quote delimiter string
- `saw_non_ascii` - Flag for multibyte validation
- `utf16_first_part` - For surrogate pair processing
- `save_yylloc` - Saved location for PUSH/POP_YYLLOC
- `keywordlist` - Keyword lookup structure
- `keyword_tokens` - Token value array
- `backslash_quote` - GUC value snapshot

### Memory Management

- Uses PostgreSQL `palloc`/`pfree` for all allocations
- Custom flex allocators: `core_yyalloc`, `core_yyrealloc`, `core_yyfree`
- Literal buffer starts at 1024 bytes, grows by powers of 2
- Scan buffer and literal buffer freed if >= 8KB; smaller leaked until
  memory context reset

### Initialization / Cleanup

- `scanner_init()`: Creates scan buffer (with double NUL termination for
  flex), initializes literal buffer, copies GUC values
- `scanner_finish()`: Frees large buffers; does not call `yylex_destroy()`

## 10. Conversion Approach Recommendation

### Recommended: Hand-Written C Tokenizer

For the Lime parser generator, the recommended approach is a **hand-written
tokenizer in C** rather than using flex or SIMD. Rationale:

#### Why Not Flex Wrapper

- Flex is a build-time dependency that may not be available
- The flex-generated scanner is ~3500 lines of table-driven code
- Lime's design philosophy favors self-contained, embeddable C code
- The flex reentrant API is complex and PostgreSQL-specific
- We need a clean interface that matches Lime's token-passing conventions

#### Why Not SIMD

- SIMD is effective for bulk tokenization of simple patterns (e.g., JSON
  field scanning), but SQL lexing requires complex state management
- Dollar quoting, nested comments, string continuation, and escape sequences
  are fundamentally sequential operations
- SIMD could help for fast whitespace/identifier scanning on hot paths, but
  the overall complexity cost is not worth it for a reference implementation
- Could be added as an optimization layer later if profiling shows need

#### Hand-Written Tokenizer Design

A manual tokenizer offers the best balance:

1. **Single-file, zero-dependency**: Matches Lime's philosophy
2. **Direct control**: Can implement the `xqs` continuation logic cleanly
3. **Performance**: Switch-based dispatch is fast; identifier/keyword lookup
   can use a perfect hash or trie
4. **Maintainability**: Direct mapping from scan.l rules to C code
5. **Token interface**: Clean function signature:
   ```c
   int pg_scan(pg_scan_state *state, YYSTYPE *lval, YYLTYPE *lloc);
   ```

#### Implementation Plan for Task #10

The tokenizer should implement:

1. **Core scanner loop**: Character-by-character with switch on first char
2. **String literal handlers**: Functions for each string type, implementing
   the accumulation and escape logic
3. **Comment skipper**: Nested block comment counter, line comment to EOL
4. **Number parser**: Decimal/hex/octal/binary with underscore support and
   the numericfail/realfail disambiguation
5. **Identifier/keyword recognizer**: Case-insensitive identifier scan +
   keyword hash lookup
6. **Operator parser**: Multi-char operator assembly with the comment-embed
   and trailing +/- SQL compatibility checks
7. **Location tracking**: Byte offset recording matching `SET_YYLLOC()` semantics
8. **Literal buffer**: Growing buffer for string accumulation

#### Keyword Table Strategy

For the standalone tokenizer, keywords should be handled via:

- A **generated perfect hash** (like gperf) or a **sorted array with binary
  search** -- the PostgreSQL approach uses the latter
- The keyword list should be extracted from `kwlist.h` and embedded directly
- Token values must match the Lime grammar's `%token` declarations

#### Lookahead Tokens

The five lookahead tokens (`FORMAT_LA`, `NOT_LA`, `NULLS_LA`, `WITH_LA`,
`WITHOUT_LA`) are created by PostgreSQL's `parser.c` wrapper, not the scanner.
For Lime, these should either be:
- Handled in the tokenizer with one-token lookahead, or
- Eliminated by grammar refactoring (preferred if feasible without
  introducing conflicts)

The `MODE_*` injection tokens are not needed for a standalone SQL parser.

## 11. Key Files Reference

| File | Role |
|------|------|
| `src/backend/parser/scan.l` | Flex lexer definition (analyzed here) |
| `src/backend/parser/gram.y` | Bison grammar (token consumer) |
| `src/backend/parser/parser.c` | Wrapper: lookahead token injection (not in checkout) |
| `src/include/parser/kwlist.h` | Keyword definitions via X-macro (not in checkout) |
| `src/include/parser/gramparse.h` | `core_yy_extra_type` definition (not in checkout) |
| `src/include/common/kwlookup.h` | `ScanKeywordList` structure (not in checkout) |
| `src/include/parser/scansup.h` | `downcase_truncate_identifier` and helpers (not in checkout) |
