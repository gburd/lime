# `.agent/skills/`

Vendored Agent Skills available in this repository.

## hegel — property-based testing

Source: https://github.com/hegeldev/hegel-skill (MIT)

Vendored locally so AI coding assistants working in this repo
can reach the property-based testing methodology without an
external skill installation.  Includes a C-language reference
covering `gburd/hegel-c` (C99 binding for Hegel), which the
upstream skill repo doesn't ship.

To use the skill in an agent that supports the Agent Skills
standard:

  - **Claude Code**: see
    https://code.claude.com/docs/en/skills for local-skill
    installation, or the upstream marketplace path
    (`/plugin marketplace add hegeldev/hegel-skill`).
  - **Codex**: `skill-installer install
    https://github.com/hegeldev/hegel-skill/tree/main/skills/hegel`
    (then this local copy serves as the C-language reference).
  - **Other agents**: load `.agent/skills/hegel/SKILL.md`
    directly.

Layout:

```
.agent/skills/hegel/
├── SKILL.md                        Methodology / property catalogue
└── references/
    └── c/
        └── reference.md            hegel-c (C99) API + hand-rolled
                                    stateful-test pattern + Lime-
                                    specific suggested property targets
```

The upstream `references/{rust,go,cpp,typescript}/` references
are not vendored here -- this is a C project, and an agent
working on Rust/Go/C++/TypeScript code should fetch those from
the upstream skill repo directly.
