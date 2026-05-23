# COBOL Parser Example

A substantial Lime grammar for ANSI/ISO COBOL, covering the four
divisions (IDENTIFICATION, ENVIRONMENT, DATA, PROCEDURE) and the core
verb set most COBOL programs actually use.

This is **substantial, not complete.**  Real-world COBOL parsers
(GnuCOBOL, IBM Enterprise COBOL, Micro Focus) implement thousands of
grammar rules to cover decades of dialect divergence.  This example
covers the common subset that compiles the COBOL you would actually
encounter in a typical batch program.

It also demonstrates Lime's `%symbol_prefix` directive: every internal
`yy*` symbol and `YY_*` macro emitted by the generator is renamed
with a `CB_INTERNAL_` prefix so the parser .o is namespace-clean.
Run `nm cobol_parser.o | head -20` after building to see the
prefixed symbols.

## Building

```sh
cd examples/cobol
make
```

This produces `cobol_parser`, a CLI driver:

```sh
./cobol_parser tests/hello.cob
./cobol_parser tests/payroll.cob
```

## What is covered

### IDENTIFICATION DIVISION
- `PROGRAM-ID. name.`
- Optional `AUTHOR.`, `DATE-WRITTEN.`, `INSTALLATION.`,
  `DATE-COMPILED.`, `SECURITY.` paragraphs
- Free-form text in those optional paragraphs (consumed and
  discarded as documentation)

### ENVIRONMENT DIVISION
- `CONFIGURATION SECTION` with `SOURCE-COMPUTER` and
  `OBJECT-COMPUTER`
- `INPUT-OUTPUT SECTION` (when present) with `FILE-CONTROL`
  paragraphs (`SELECT name ASSIGN TO ...`)

### DATA DIVISION
- `WORKING-STORAGE SECTION`, `LINKAGE SECTION`
- Level numbers `01`-`49`, condition-name level `88`, FILLER level `77`
- `PIC` clauses: `X(n)`, `A(n)`, `9(n)`, `9(n)V9(n)`, `S9(n)`,
  edited pictures (`Z`, `*`, `,`, `.`, `B`, `0`)
- `USAGE`: `DISPLAY` (default), `COMP`, `COMP-3`,
  `COMP-5`, `BINARY`, `PACKED-DECIMAL`, `POINTER`
- `VALUE` clauses: numeric literals, alphanumeric literals,
  figurative constants (`ZERO`, `SPACES`, `LOW-VALUES`,
  `HIGH-VALUES`, `QUOTES`, `NULL`)
- `REDEFINES`
- `OCCURS n TIMES` and `OCCURS m TO n TIMES DEPENDING ON identifier`
- `INDEXED BY index-name`

### PROCEDURE DIVISION
- Paragraphs and optional `SECTION` headers
- `MOVE ... TO ...` (with `CORRESPONDING` form `MOVE CORR a TO b`)
- `IF condition [THEN] ... [ELSE ...] END-IF`
  (and the older period-terminated form)
- `PERFORM paragraph-name`
- `PERFORM paragraph-name THRU other-name`
- `PERFORM ... TIMES`
- `PERFORM ... UNTIL condition`
- `PERFORM ... VARYING identifier FROM x BY y UNTIL condition`
- `COMPUTE identifier = expression` (full arithmetic with
  precedence: `+ -`, `* /`, `**`, parentheses)
- Arithmetic verbs: `ADD ... TO ...`, `SUBTRACT ... FROM ...`,
  `MULTIPLY ... BY ...`, `DIVIDE ... BY/INTO ...`
- `EVALUATE ... WHEN ... [WHEN OTHER] END-EVALUATE`
- `CALL "name" [USING arg-list]`
- `DISPLAY ... [UPON CONSOLE]`
- `ACCEPT identifier [FROM source]`
- `INITIALIZE identifier`
- `SET identifier TO TRUE/FALSE/value`
- `INSPECT identifier TALLYING/REPLACING ...` (basic)
- `STRING ... DELIMITED BY ... INTO ... END-STRING`
- `UNSTRING ... INTO ... END-UNSTRING`
- `GO TO paragraph-name`
- `CONTINUE`
- `EXIT [PROGRAM]`
- `STOP RUN`

### Conditions
- Relational: `=`, `<`, `>`, `<=`, `>=`, `NOT =`, `IS GREATER THAN`,
  etc.
- Class: `IS NUMERIC`, `IS ALPHABETIC`, `IS POSITIVE`, etc.
- Sign: `IS POSITIVE`, `IS NEGATIVE`, `IS ZERO`
- Combined: `AND`, `OR`, `NOT`
- Parenthesised
- 88-level condition names (`IF status-flag-set` where `88
  status-flag-set VALUE "Y"` is declared)

### Arithmetic expressions
- Full operator set with COBOL precedence
- Numeric and identifier operands
- Parentheses

## What is NOT covered

The following live-fire COBOL features are intentionally out of scope
for this example:

- **FILE SECTION** with `FD` entries for indexed/relative files
  (we keep `INPUT-OUTPUT SECTION` schematic only -- `SELECT...ASSIGN`
  is parsed but `READ`/`WRITE`/`OPEN`/`CLOSE` verbs that operate on
  files are not part of the verb set)
- **REPORT SECTION** (RW-style report writer)
- **SCREEN SECTION** (screen I/O)
- **EXEC SQL** / **EXEC CICS** embedded blocks (these are
  preprocessor-injected in real systems)
- **COPY** and **REPLACE** preprocessor directives (a full
  implementation would resolve copybooks before the parse)
- **OO COBOL** (`CLASS`, `METHOD`, `OBJECT`) -- 2002 standard
- **Intrinsic-function set** beyond the most common ones
  (`FUNCTION CURRENT-DATE`, etc., are not parsed; user-written
  functions are.)
- Vendor-specific extensions (Micro Focus dialect, IBM-specific
  date intrinsics, etc.)

A future revision could add any of these incrementally; the grammar
is structured so each verb is its own production and adding one
does not perturb the others.

## File layout

```
cobol/
├── cobol_grammar.lime  -- Lime grammar (the entire core)
├── cobol_tokens.h      -- Token-code declarations + helpers
├── tokenize.c          -- Hand-written COBOL lexer
├── tokenize.h          -- Lexer interface
├── main.c              -- CLI driver (stdin or argv path)
├── Makefile            -- Build entry
├── README.md           -- This file
└── tests/
    ├── hello.cob       -- Smallest possible valid program
    ├── payroll.cob     -- DATA DIVISION with PIC + USAGE + REDEFINES
    ├── controlflow.cob -- IF / EVALUATE / PERFORM exercise
    └── arithmetic.cob  -- COMPUTE / ADD / DIVIDE exercise
```

## Why "substantial" and not "complete"

A truly complete COBOL grammar is multiple weeks of focused work and
several thousand grammar rules.  This example is sized to:

- Compile real COBOL programs that you might write today for a batch
  job (the [`tests/`](tests/) directory has examples).
- Demonstrate Lime's ability to handle a non-trivial real-world
  language (~200 grammar rules across the four divisions, several
  hundred reserved words in the lexer).
- Stay maintainable: each division is its own grammar section,
  cleanly separated, so adding new verbs or clauses is local.
- Show off `%symbol_prefix` so the COBOL parser's internal
  namespace doesn't leak.

If you need full COBOL coverage in production, consider GnuCOBOL or
IBM Enterprise COBOL.  This example is designed to be a teaching
artifact and a credible demonstration of Lime's capacity to host a
complex grammar -- not a drop-in replacement for those tools.
