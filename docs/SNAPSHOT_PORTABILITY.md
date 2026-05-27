# Snapshot Cross-Architecture Portability

This document audits the cross-architecture portability of every
data path that persists or transports a `ParserSnapshot` and records
the conclusions.  Commissioned for the v0.6.x stabilization round.

## TL;DR

**There is no binary snapshot wire format.**  Snapshots are either
(a) live in-memory C structs in the producer process or (b) statically
linked into a generated `.so` by `lime -n` + `cc`.  In both cases
they are produced and consumed in the same address space (or in a
process that linked the same target-arch `.so`), so endianness,
word size, struct padding, and pointer width never cross an
architecture boundary at runtime.

A grammar produced on one architecture is consumed on the same
architecture.  Cross-arch shipping happens at the source level:
either the `.lime` grammar text or the lime-generated `.c` parser
file is checked in / shipped, and the consumer compiles it for
their target.

The `LimeSnapshotDiff` API added in v0.6.x is also in-process only:
the diff struct has C pointer fields and is not designed for
serialization to disk.

## Audit by data path

### 1. `ParserSnapshot` in-process

`struct ParserSnapshot` lives entirely on the producer's heap.
Field layout follows the producing compiler's ABI; no in-process
consumer parses it differently.

**Conclusion:** native-only.  Cross-arch nonsensical.

### 2. `lime_snapshot_create` subprocess pipeline

`src/snapshot_create.c::compile_grammar_file_to_snapshot` runs:
1. `lime` (parser generator) on the grammar.
2. `cc` (system compiler) on the generated `.c`.
3. `dlopen` on the resulting `.so`.
4. `dlsym` to find `lime_snapshot_entry`.
5. Call the entry function; it returns a `ParserSnapshot *` whose
   tables were `memcpy`'d out of static arrays inside the `.so`.

The `cc` invocation produces a native-arch `.so`.  The static
arrays embedded in that `.so` are also native-arch.  The `.so` is
loaded into the same process that called `lime_snapshot_create`,
so the consuming address space and the producing address space
are identical.

**Conclusion:** native-only by construction.  A snapshot produced
on x86_64 cannot be `dlopen`'d on aarch64 (the `.so` itself is
arch-specific, would fail at load time before any field access).

### 3. `lime_compile_grammar_in_process` (v0.5.4+)

In-process LALR rebuild.  Produces a `ParserSnapshot *` directly
in the calling process's heap; no intermediate `.so`.  Fields are
populated via `build_snapshot_from_lime` with native-width integer
types and direct `memcpy` for table contents.

**Conclusion:** native-only by construction.  No cross-arch
exposure.

### 4. `mod_serialize.c` — extension grammar modifications

`mod_serialize_to_text` writes a textual `.lime` grammar fragment
representing a `GrammarModification`.  Output is plain UTF-8 text
that round-trips through `lime_compile_grammar_text` on any
architecture.

**Conclusion:** portable.  Text format, no binary representation.

### 5. `LimeSnapshotDiff` (v0.6.x)

In-process diff/patch API.  The `LimeSnapshotDiff` struct contains
heap-allocated change-list arrays of (offset, value) records with
host-native integer widths.  Designed for the same-process
producer-consumer case (incremental composition); not designed for
disk persistence or network transport.

**Conclusion:** native-only by design.  If a future consumer wants
cross-arch diff transport, they must write a separate
serialize/deserialize layer that encodes integers explicitly
little-endian and length-prefixes string fields.

### 6. `lime -n <grammar>` — generated `.c` file (`*_snapshot.c`)

Lime can emit a `<Prefix>BuildSnapshot()` C function that
populates a `ParserSnapshot` from compile-time-constant tables.
The `.c` file is portable C source: it compiles with any
compatible `cc` on any architecture and produces a native-arch
parser.  This is how the PG team ships pre-generated parsers on
multiple architectures: the `.c` file is checked in once and
each architecture's build pipeline compiles it.

**Conclusion:** portable at the source level; native at the
binary level.

## Hazards looked for and not found

The audit looked for the following hazard patterns and confirmed
they don't exist in the snapshot data paths:

  * `fwrite(&u32, sizeof u32, 1, fp)` writing native-endian
    integers to disk.  None.  (`snapshot_create.c::fwrite` writes
    grammar TEXT, not binary integers.)
  * `memcpy` of an entire struct between processes / arches via
    shared memory or a pipe.  None.
  * `htonl`/`ntohl`-equivalent code that's only sometimes called.
    None -- no byte-order conversion needed because no wire format
    exists.
  * `size_t` fields persisted as `sizeof(size_t)` bytes.  None.
  * Pointer fields written verbatim (would require offset
    translation).  None.
  * Compiler-injected struct padding leaking via direct struct
    `fwrite`.  None.

## What this means for the PG team

PG ships lime-generated parsers checked into the source tree as
`.c` files.  The build system on each target arch (x86_64, aarch64,
ppc64le, s390x, ...) compiles the `.c` against its native compiler
and produces an arch-native parser binary.  No cross-arch transport
of snapshot binaries is required or used.

If a future workflow wants to **distribute a precompiled parser
binary** across architectures, the lime-generated `.c` is the
intended distribution unit -- ship the `.c`, not a `.so`.

## What this means for incremental composition (`LimeSnapshotDiff`)

The diff API is in-process only.  When daemon-startup composition
warms a parser cache by replaying the prior session's diff against
a fresh base, the diff must have been produced in the same process
or by a process linking the same target-arch lime build.  Cross-
arch diff transport is out of scope for v0.6.x and would require
a new serialization layer (see "future work" in `snapshot_diff.h`).

## Future work (when a real consumer asks)

If a consumer needs persistent / cross-arch snapshot transport:

1. Add a `snapshot_serialize_to_buffer(snap, &buf, &len)` helper
   that writes a length-prefixed, little-endian, version-tagged
   binary form of every yy_* table.
2. Add `snapshot_deserialize_from_buffer(buf, len, &snap)` that
   validates the version + endianness header, then decodes.
3. Bump a new `LIME_SNAPSHOT_WIRE_VERSION` constant.  Old buffers
   produce an explicit "rebuild required" error rather than a
   silent crash.
4. Add cross-arch tests by mocking endian-swap in a unit test.

None of this is in scope for v0.6.x because no consumer requests
it: the PG team consumes lime-generated `.c` (portable at source
level) and the in-process composition path runs in a single
address space.
