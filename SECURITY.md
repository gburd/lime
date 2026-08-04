# Security Policy

Lime is a parser and lexer generator whose output, and whose runtime
extension/composition machinery, can be embedded inside long-running,
privileged hosts, most notably PostgreSQL backends. That makes both the
generated parsers and the runtime loader a real attack surface: a
maliciously crafted grammar, snapshot, or input stream could in principle
trigger memory-safety faults, resource exhaustion, or unexpected code
execution in the host. We take reports in this area seriously.

## Reporting a vulnerability

Please report suspected vulnerabilities privately, not as a public issue or
pull request.

- Email: security@lime-parser.invalid  *(placeholder — replace with the
  project's real security contact before publishing)*
- Or open a private/security advisory through the repository host's
  advisory mechanism if one is available.

Include enough detail to reproduce: the affected version (see
`lime -x`), the grammar / input / snapshot that triggers the issue, the
build configuration (compiler, `-Dllvm`, sanitizers), and the observed
behavior (crash, hang, memory error, etc.). A minimal reproducer and, if
you have one, an ASan/UBSan/Valgrind trace speeds triage considerably.

We aim to acknowledge a report within a few business days and to agree on
a coordinated disclosure timeline with the reporter.

## Scope

In scope:

- Memory-safety defects (out-of-bounds read/write, use-after-free, double
  free) in the runtime library (`src/`) reachable from generated parsers,
  the composition/extension machinery, or the snapshot loader.
- Crashes, hangs, or unbounded resource consumption triggered by attacker-
  controlled *parser input* to a generated parser built from a trusted
  grammar.
- Defects in the dynamic extension loader (`dlopen`-based plugin loading)
  that let an attacker escalate beyond loading a plugin they were already
  authorized to load.

Out of scope (by design, not a vulnerability):

- Feeding a fully attacker-controlled *grammar* or *snapshot* to the
  generator or loader. The grammar/snapshot is trusted input, equivalent
  to source code; loading an untrusted grammar is equivalent to running
  untrusted code and is not a security boundary Lime defends.
- Resource use proportional to a legitimately large but well-formed
  grammar.
- Issues in third-party dependencies (LLVM, libc) that are not amplified
  by Lime.

## Supported versions

Security fixes target the latest released version. Older versions may
receive fixes at the maintainers' discretion; see `docs/SUPPORT.md`.
