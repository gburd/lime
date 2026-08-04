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
