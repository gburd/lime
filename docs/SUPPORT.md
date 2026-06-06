# Lime Support Policy

This document describes which Lime releases receive bug-fix and
security backports, for how long, and what kinds of changes are
allowed in those backports.

## Release Tracks

Lime ships three kinds of releases:

| Track          | Support window           | What gets backported            |
|----------------|--------------------------|---------------------------------|
| **LTS**        | 24 months from cut date  | Bugs (incl. security), CI fixes |
| Latest stable  | Until next LTS or +12 mo | All bugs                        |
| Older stable   | None                     | Nothing; users upgrade          |

The current LTS is **v1.3.0** (cut June 2026).  It will receive
backports through **June 2028**.  The previous (non-LTS) stables
v1.0.0–v1.2.0 are no longer maintained; users should upgrade to
v1.3.x.

## Backport Criteria

A change qualifies for backport to LTS only if all of:

1. It fixes a **demonstrated bug** (not a refactor, not a feature).
2. It is **safe**: no public-API changes, no behaviour changes
   in correct programs, no ABI breakage, no performance
   regressions outside the noise floor.
3. It is **small**: ideally ≤200 lines of diff against the LTS
   tree, including any new test.  Larger fixes are evaluated case-
   by-case; the bar is "could a maintainer audit this in one
   sitting?".
4. It has **a regression test**.  No exceptions for "obvious"
   fixes.

Security fixes (memory safety, denial-of-service from malformed
grammar input, sandbox escape from generated code) skip the size
limit and ship under embargo if needed.

## Versioning Within LTS

LTS patch releases are cut as `v1.3.<patch>`.  A patch release
contains only backports; new features go to the next minor on the
**latest stable** track (v1.4.0, v1.5.0, ...).

Each backport bumps the patch component:

```
v1.3.0  -- initial LTS cut
v1.3.1  -- first backport batch
v1.3.2  -- second backport batch
...
```

Patch releases do **NOT** change the ABI, do **NOT** change the
emitted C/Rust output beyond what's necessary to fix the
specific bug, and do **NOT** introduce new public API surface.

## Reporting Issues

To request a backport, file an issue at
<https://codeberg.org/gregburd/lime/issues> or
<https://github.com/gburd/lime/issues> with:

- The Lime version where the bug exists
- A minimal reproducer (grammar + invocation)
- The expected vs actual behaviour
- Whether you've already verified the bug is fixed in latest
  stable (if applicable)

Tag the issue `lts-backport-candidate` if you believe it
qualifies.  A maintainer will triage within a week.

## End-of-Life

When v1.3.x reaches EOL (June 2028), the next LTS will be
announced at least 6 months in advance.  A migration guide
(public API changes, deprecated directives, etc.) ships with
the announcement.  EOL'd releases do NOT get further backports
even for security; affected users must upgrade.

## Build Reproducibility

LTS releases are built reproducibly: the same source + same
toolchain produces byte-identical artefacts.  This is enforced by
CI on every tag push.  See `docs/RELEASING.md` for the full
reproducibility checklist.

The 13-artefact distro-package set (rpm/deb/apk/arch/brew + source
tarball + sha256) is rebuilt from the source tarball on every
patch release, so consumers of distro packages benefit from
the same backports as source-build consumers.
