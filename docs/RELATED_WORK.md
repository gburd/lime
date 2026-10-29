# Related work: runtime and modular grammar composition

Runtime and modular grammar modification is not new. Several systems,
spanning decades, support constructing, extending, or composing grammars
at runtime or through a modular build step. Lime's contribution is a
narrower, differentiated point in this space, not a first.

## Prior art

| System | What it does | Relative to Lime |
|---|---|---|
| **GNU Bison (GLR mode, `%glr-parser`)** | Generalized-LR parsing handles grammars that are not LALR(1); `%merge` resolves ambiguity at the forest level. | A more permissive *algorithm* for ambiguity, but not runtime load/unload of extensions. |
| **Marpa** (Kegler) | Earley-based; grammars are constructible and modifiable at runtime. | Genuine runtime grammar modification, on a more permissive (Earley) engine. |
| **Silver / ableC** (Van Wyk et al., U. Minnesota) | Modular language extension with a *modular determinism analysis*: each extension is analysed once in isolation, and any set that passes composes without conflict. | A **stronger** guarantee than Lime offers, via an offline analysis rather than runtime load. |
| **SDF2 / SGLR / Spoofax** (Visser et al.) | Modular syntax composition with scannerless GLR and declarative disambiguation filters. | Modular composition with decades of production experience; offline, GLR-based. |
| **Copper** (Van Wyk & Schwerdfeger) | Context-aware scanning: the parser state tells the scanner which terminals are viable. | The correct answer to the keyword-collision problem Lime hits in multi-dialect composition. |
| **DuckDB runtime-extensible parser** (Mühleisen & Raasveldt, CIDR 2025) | Replaces DuckDB's YACC-derived parser with a PEG grammar loaded at runtime; extensions add or replace rules by editing the grammar data structure.  Argues parser speed is irrelevant next to query execution and that YACC's monolithic tables and error messages are the real costs. | **The closest competitor to Lime's stated goal** -- runtime SQL-dialect extension inside a database -- reached by the opposite design choice.  See the section below. |
| **Rats! / Rascal** | PEG / modular grammar toolkits with grammar composition and modular syntax. | Modular composition on PEG/GLL substrates. |
| **Katahdin** | Grammar modifiable *during* execution of the program being parsed. | More dynamic than Lime; research language. |
| **Raku (Perl 6) grammars** | First-class, runtime-mutable grammars in the language itself. | Runtime mutation as a language feature. |
| **Racket `#lang` / reader macros** | Reader extension at read time; language-level syntax extension. | Read-time extension, Lisp lineage. |
| **Common Lisp reader macros** | Reader-level syntax extension. | The 1960s answer to the question. |
| **tree-sitter** | Incremental parsing with dynamic external scanners. | Runtime external scanners, incremental; different goal. |
| **ANTLR** | Semantic / dynamic predicates alter parse decisions at runtime. | Runtime parse-decision control, not grammar load/unload. |

## Lime's actual position

Lime's defensible, differentiated contribution is narrower than "no one
else does runtime grammar modification" (which is false):

> **Runtime load and unload of grammar extensions against a deterministic
> LALR(1) table, in-process, in C, with copy-on-write snapshots so live
> parsers are never disturbed.**

That combination -- deterministic LALR(1) + hot load/unload + COW
snapshots + a C-embeddable, zero-dependency generator, targeting in-process
hosts with zero-downtime requirements -- is the useful, real position. The
systems above generally require either a more permissive parsing algorithm
(Earley, GLR, PEG) or an offline composition/analysis step.

## LALR rebuild vs. runtime PEG: the DuckDB comparison

DuckDB and Lime target the same problem: let an extension change the SQL
grammar of a running database.  They choose opposite substrates, and the
trade-off is now measurable rather than rhetorical.

**DuckDB's position** (as we read the CIDR 2025 paper): parsing is a
negligible share of query time; a PEG grammar can be modified at runtime
as a data structure, with no table construction; PEG's ordered choice
means there are no conflicts to resolve; and generated YACC tables give
poor error messages.

**What Lime pays for LALR.**  Every change to the extension set rebuilds
the parser tables.  Measured with `bench/bench_pg_rebuild` on the full
PostgreSQL grammar (`examples/pg/pg_grammar.lime`: 3,841 states, 561
terminals, 3,600 rules, 147k actions, 0 conflicts), in-process, Debian 13
/ Xeon 8488C, one core, CPU time, median of 9:

| | CPU time | peak RSS |
|---|---:|---:|
| before the v1.11.3 review fixes (measured on `gram.lime`, the no-precedence copy; within a few % of the same build on `pg_grammar.lime`) | 5.0 s | ~854 MB |
| after (`acttab_insert` offset index, word-wide `SetUnion`) | 1.77 s | 855 MB |
| + sparse config-table clear, same-symbol chain in `buildshifts`, per-lookahead slot index in `acttab_insert`, bitset follow sets | **0.80 s** | **325 MB** |
| the same, PG + a 30-rule dialect extension (`--compose bench/pg_dialect_ext.lime`) | 0.82 s | 332 MB |

Every row builds byte-identical tables (`tests/test_lalr_identity.c`).
Of the 0.80 s, packing the action table is 0.22 s, follow sets 0.17 s,
LR(0) states 0.10 s and freeing the generator's memory 0.12 s.

So a PostgreSQL-scale recompose costs under a second of CPU and ~330 MB
of transient memory.  That is acceptable once at backend start or on
`CREATE EXTENSION`; it is not acceptable per session, per statement, or
per backend (500 backends each rebuilding would be the dominant startup
cost -- a shared, prebuilt snapshot is the obvious mitigation and is not
implemented).  A PEG edit has no equivalent cost.  On this axis DuckDB's
design is simply better, and Lime should not pretend otherwise.

**What Lime gets for it.**  PEG's ordered choice does not resolve
ambiguity, it hides it: in `A <- 'a' / 'a' 'b'` the second alternative
can never match, silently.  Two dialect extensions whose rules overlap
compose without complaint and one of them quietly stops working.  An
LALR rebuild instead *finds* every shift/reduce and reduce/reduce
conflict the composition introduces, before any query runs, and Lime can
report and diff them (`--diff-conflicts`).  In a database, where a query
silently changing meaning after `CREATE EXTENSION` is the worst failure,
detecting overlap up front is a real advantage.  It is only an advantage
if conflicts are surfaced loudly, not auto-resolved by load order -- see
`docs/COMPOSITION_STRATEGY.md`.

**Parse speed** is roughly a wash at this level and argues for neither
design: Lime's runtime engine is within ~15% of bison on the benchmarks
in `docs/BENCHMARKS_VS_BISON.md` (slower against `flex -CF`, faster
against default flex).  Lime has no JIT; the LLVM JIT it once had was
slower than the table interpreter (`docs/JIT_ANALYSIS.md`).

**Bottom line.**  Choose runtime PEG when grammar edits are frequent and
silent overlap is tolerable; choose Lime's LALR when edits are rare (load
time) and you want conflicts reported before they bite.  The open
question for Lime is not speed but the composition policy: the conflict
report only helps if overlap fails loudly by default.

## What each system does better than Lime

- **Silver / ableC**: modular determinism analysis gives a compositional
  guarantee Lime does not (yet) have -- the single most valuable thing
  Lime could adopt for "load MySQL, Oracle, QUEL at once."
- **SDF2 / Spoofax**: a mature declarative disambiguation-filter
  vocabulary, a ready-made design for Lime's conflict-policy work.
- **Copper**: context-aware scanning solves the reserved-vs-unreserved
  keyword collision problem that Lime's shared-token composition hits.
- **Marpa / Earley**: strictly more grammars parse without conflict, at a
  higher runtime cost.
- **GLR (Bison, Elkhound)**: hybrid LR/GLR runs deterministic LR where it
  can and forks only where it must -- directly applicable to Lime's
  multi-dialect tier selection.

## See also

- `docs/COMPARISON.md` -- feature comparison with Yacc, Bison, ANTLR,
  Menhir.
- `docs/MULTI_GRAMMAR.md` -- Lime's runtime disambiguation tiers.
- `docs/GLR.md` -- Lime's generalized-LR path.
