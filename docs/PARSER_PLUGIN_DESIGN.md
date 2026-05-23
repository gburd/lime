# Parser Plugin Design

This document describes the architecture of the runtime parser management
system, including the plugin interface, dynamic loading, registry, and
hot-swap mechanisms.

## Goals

1. Allow multiple parser implementations to coexist in a single process.
2. Enable loading parser plugins from shared libraries at runtime.
3. Support statically linked plugins for embedded and single-binary deployments.
4. Provide atomic hot-swap so that parser upgrades do not disrupt in-flight
   parse sessions.
5. Maintain thread safety throughout: concurrent reads, serialized mutations.

## Non-Goals

- The plugin system does not manage grammar extensions (that is handled by the
  existing extension system in `src/extension.{h,c}`).
- Plugins are not sandboxed; they run in the same address space as the host
  application.
- The system does not provide a network protocol for remote parser services.

## Architecture Overview

```
  Application
      |
      v
  +-------------------+
  | ParserManager     |  singleton or per-application
  |  - registry[]     |  thread-safe plugin registry
  |  - active_handle  |  which plugin is "current"
  |  - active_snap    |  atomic pointer to active ParserSnapshot
  +--------+----------+
           |
   +-------+--------+--------+---- ...
   |                 |        |
   v                 v        v
  +-----------+  +-----------+  +-----------+
  | Plugin A  |  | Plugin B  |  | Plugin C  |
  | (static)  |  | (.so)     |  | (.so)     |
  +-----------+  +-----------+  +-----------+
       |              |              |
       v              v              v
  +----------+   +----------+   +----------+
  | Snapshot  |   | Snapshot  |   | Snapshot  |
  +----------+   +----------+   +----------+
```

Multiple plugins can be loaded simultaneously. Exactly one is designated
as the "active" plugin. The active plugin's snapshot is what
`parser_manager_get_snapshot()` returns to callers.

## Plugin Interface (`LimeParserPlugin`)

Every plugin provides a `LimeParserPlugin` struct with callback function
pointers. The struct is defined in `include/parser_manager.h`.

### Required Callbacks

| Callback | Signature | Purpose |
|----------|-----------|---------|
| `get_name` | `const char *(void)` | Human-readable plugin name |
| `get_version` | `LimePluginVersion (void)` | Semantic version |
| `get_abi_major` | `uint16_t (void)` | ABI major version (must match manager) |
| `get_abi_minor` | `uint16_t (void)` | ABI minor version (informational) |
| `get_capabilities` | `uint32_t (void)` | Bitmask of `LimePluginCaps` |
| `create_snapshot` | `ParserSnapshot *(const char *, char **)` | Build snapshot from grammar |
| `destroy` | `void (void)` | Release all plugin resources |

### Optional Callbacks

| Callback | Purpose |
|----------|---------|
| `init` | One-time setup after loading |
| `validate_snapshot` | Integrity check on produced snapshots |
| `serialize_snapshot` | Persist snapshot to bytes |
| `deserialize_snapshot` | Restore snapshot from bytes |

### ABI Versioning

The plugin ABI uses a two-component version:

- **Major**: Incremented when breaking changes are made to the
  `LimeParserPlugin` struct layout or callback semantics. Plugins with
  a different major version are rejected at load time.
- **Minor**: Incremented when new optional callbacks are added to the end
  of the struct. A plugin with a lower minor version is accepted; the
  manager treats any callbacks beyond what the plugin knows about as NULL.

The current ABI version is defined by `LIME_PLUGIN_ABI_VERSION_MAJOR`
and `LIME_PLUGIN_ABI_VERSION_MINOR` in `parser_manager.h`.

The `_reserved[8]` array at the end of the struct provides space for
future expansion without immediately bumping the ABI major version.

## Dynamic Loading

### Shared Library Plugins

Dynamic plugins are standard shared libraries (`.so` on Linux, `.dylib` on
macOS, `.dll` on Windows) that export a single entry point:

```c
LIME_PLUGIN_EXPORT const LimeParserPlugin *lime_plugin_entry(void);
```

The manager's `parser_manager_load()` function performs:

1. **Path resolution**: If the path is relative, search each directory in
   `config.plugin_search_paths` for a matching file.
2. **dlopen()**: Open the shared library with `RTLD_NOW | RTLD_LOCAL` to
   resolve all symbols immediately and keep the plugin's symbols isolated.
3. **Symbol lookup**: `dlsym()` for `"lime_plugin_entry"`.
4. **Entry call**: Invoke the entry function to get the `LimeParserPlugin*`.
5. **ABI check**: Verify `get_abi_major()` matches `LIME_PLUGIN_ABI_VERSION_MAJOR`.
6. **Name uniqueness**: Verify no other loaded plugin has the same name.
7. **init()**: Call `init(user_data)` if non-NULL. Roll back on failure.
8. **Registration**: Assign a `LimePluginHandle` and add to the registry.

### Static Plugins

Statically linked plugins skip the dlopen path. The application calls
`parser_manager_register()` directly with a pointer to a `LimeParserPlugin`
struct. The same ABI check, name uniqueness check, and init sequence apply.

### Unloading

`parser_manager_unload()`:

1. If the plugin is the active plugin, clear the active designation.
2. Call `destroy()`.
3. Remove from the registry.
4. If dynamically loaded, `dlclose()` the library handle.

The active snapshot (if produced by this plugin) is not freed immediately.
It is reference-counted; in-flight parse sessions keep their references
and the snapshot is freed when the last reference is released.

## Plugin Registry

### Data Structure

```c
typedef struct PluginEntry {
    LimePluginHandle        handle;
    const LimeParserPlugin *plugin;
    void                   *dl_handle;    // NULL for static plugins
    char                   *library_path; // NULL for static plugins
    bool                    is_dynamic;
} PluginEntry;

struct ParserManager {
    PluginEntry    *entries;          // dynamic array
    uint32_t        count;
    uint32_t        capacity;
    LimePluginHandle next_handle;     // monotonically increasing

    LimePluginHandle active_handle;   // currently active plugin
    _Atomic(ParserSnapshot *) active_snap; // atomic for lock-free reads

    ParserManagerConfig config;       // copy of creation config

    pthread_rwlock_t lock;            // protects entries, count, active_handle
};
```

### Concurrency Model

The registry uses a `pthread_rwlock_t`:

- **Readers** (`parser_manager_get_snapshot`, `parser_manager_list_plugins`,
  `parser_manager_find_by_name`, etc.) acquire a shared read lock. Multiple
  readers proceed concurrently.
- **Writers** (`parser_manager_load`, `parser_manager_unload`,
  `parser_manager_set_active`, `parser_manager_hot_swap`) acquire an
  exclusive write lock.

The active snapshot pointer is stored as an `_Atomic(ParserSnapshot *)`.
This allows `parser_manager_get_snapshot()` to be lock-free in the common
case: it atomically loads the pointer, acquires a reference, and returns.
The read lock is only needed if the caller also wants plugin metadata.

### Handle Assignment

Handles are assigned from a monotonically increasing counter starting at 1.
Handle 0 (`LIME_PLUGIN_HANDLE_INVALID`) is reserved. Handles are never reused
within a manager's lifetime, even after unloading. This prevents ABA problems
where a stale handle accidentally refers to a different plugin.

## Active Parser and Hot-Swap

### Setting the Active Plugin

`parser_manager_set_active(mgr, handle, grammar_file)`:

1. Acquire write lock.
2. Look up the plugin by handle.
3. If `grammar_file` is non-NULL, call `plugin->create_snapshot(grammar_file, &error)`.
4. If `config.validate_on_load` and `plugin->validate_snapshot` is non-NULL,
   validate the snapshot.
5. If `config.auto_jit` and the plugin advertises `LIME_CAP_JIT`, call
   `lime_jit_compile(snap)`.
6. Atomically store the new snapshot pointer.
7. Release the old snapshot reference (if any).
8. Update `active_handle`.
9. Release write lock.

### Hot-Swap Guarantee

`parser_manager_hot_swap()` provides the same semantics as `set_active()`
but is explicitly documented to be atomic from the reader's perspective.
Implementation-wise they are equivalent because the snapshot pointer update
is already a single atomic store.

The key guarantee: there is no window where `parser_manager_get_snapshot()`
returns NULL during a swap. The old snapshot remains visible to new readers
until the atomic store of the new snapshot completes.

### Session Isolation

Parse sessions call `parse_begin(snap)` which acquires a reference to the
snapshot. From that point on, the session is pinned to that snapshot version.
Even if the active plugin is changed or the original plugin is unloaded, the
session continues with stable table pointers until `parse_end()` is called.

```
Thread A (parser):          Thread B (admin):
  snap = get_snapshot()
  ctx = parse_begin(snap)
  release(snap)              hot_swap(new_plugin, grammar)
  parse_token(ctx, ...)      // old snap still alive via ctx
  parse_token(ctx, ...)
  parse_end(ctx)             // old snap refcount -> 0, freed
```

## Version Compatibility

### Plugin Version Checks

The manager provides utility functions for comparing semantic versions:

- `lime_plugin_version_compare(a, b)` -- three-way comparison
- `lime_plugin_version_satisfies(actual, required)` -- true if actual >= required

Applications can use these to implement upgrade policies. For example,
reject loading a plugin whose version is lower than the currently active one.

### Upgrade / Downgrade Flow

```
1. Load new plugin version (parser_manager_load)
2. Optionally test it (create_snapshot + validate_snapshot)
3. Hot-swap to the new plugin (parser_manager_hot_swap)
4. Unload old plugin version (parser_manager_unload)
```

If step 3 fails, the old plugin remains active. The new plugin can be
unloaded without affecting any parse sessions.

## Error Handling

All manager functions return a `ParserManagerStatus` enum. The
`parser_manager_status_string()` function converts codes to human-readable
messages suitable for logging.

Errors are non-fatal to the manager itself. A failed load, unload, or
swap leaves the manager in a consistent state. The previous active plugin
and snapshot remain undisturbed.

Plugin callbacks that fail (init returns false, create_snapshot returns NULL)
cause the manager operation to fail, but the manager does not crash or enter
an inconsistent state.

## Memory Management

### Ownership Rules

| Resource | Owner | Freed by |
|----------|-------|----------|
| `ParserManager` struct | Caller of `parser_manager_create` | `parser_manager_destroy` |
| `PluginEntry` array | ParserManager | `parser_manager_destroy` |
| `LimeParserPlugin` struct (static) | Application | Application |
| `LimeParserPlugin` struct (dynamic) | Shared library | `dlclose` via `parser_manager_unload` |
| `ParserSnapshot` | Reference-counted | Last `snapshot_release` call |
| Plugin name/version strings | Plugin (returned by callbacks) | Plugin's `destroy` |
| Error message strings | Caller | `free()` |
| `library_path` in PluginEntry | ParserManager (strdup'd) | `parser_manager_unload` / `destroy` |

### Snapshot Lifecycle Under the Manager

```
parser_manager_set_active(mgr, h, "grammar.y")
   |
   plugin->create_snapshot("grammar.y")
   |  returns snap (refcount = 1)
   |
   atomic_store(&mgr->active_snap, snap)    // manager holds ref
   |
   parser_manager_get_snapshot(mgr)
   |  snap = atomic_load(&mgr->active_snap)
   |  snapshot_acquire(snap)                 // refcount = 2
   |  return snap
   |
   parse_begin(snap)                         // refcount = 3
   |  snapshot_acquire(snap)
   |
   lime_snapshot_release(snap)              // caller drops ref, refcount = 2
   |
   parser_manager_hot_swap(mgr, h2, "new.y")
   |  new_snap = plugin2->create_snapshot()
   |  old = atomic_exchange(&mgr->active_snap, new_snap)
   |  snapshot_release(old)                  // refcount = 1 (parse ctx holds it)
   |
   parse_end(ctx)
   |  snapshot_release(snap)                 // refcount = 0, freed
```

## Integration with Existing Systems

### Extension System

The parser manager operates above the extension system. A plugin that
advertises `LIME_CAP_EXTENSIBLE` can have its snapshot modified via the
existing `create_modified_snapshot()` pipeline:

```c
ParserSnapshot *base = parser_manager_get_snapshot(mgr);
ParserSnapshot *modified;
create_modified_snapshot(base, mods, nmods, registry, &modified, NULL, NULL);
parser_manager_set_snapshot(mgr, modified);
lime_snapshot_release(modified);
lime_snapshot_release(base);
```

### JIT Compilation

Plugins that advertise `LIME_CAP_JIT` produce snapshots whose action
tables are suitable for JIT compilation via `jit_attach_to_snapshot()`.
If `config.auto_jit` is true, the manager automatically triggers JIT
compilation after snapshot creation.

### Tokenizer

The tokenizer (`include/tokenize.h`) is independent of the plugin system.
A parse session creates a `Tokenizer` from a `TokenTable` (which may be
part of the snapshot or separately managed) and feeds tokens to
`parse_token()`.

## Example: Plugin Implementation

See `examples/plugin_template/` for a complete example. The minimal
implementation is:

```c
#include "parser_manager.h"
#include "parser.h"

static const char *my_get_name(void) { return "my_parser"; }

static LimePluginVersion my_get_version(void) {
    return (LimePluginVersion){1, 0, 0};
}

static uint16_t my_get_abi_major(void) { return LIME_PLUGIN_ABI_VERSION_MAJOR; }
static uint16_t my_get_abi_minor(void) { return LIME_PLUGIN_ABI_VERSION_MINOR; }

static uint32_t my_get_capabilities(void) { return LIME_CAP_SNAPSHOT; }

static ParserSnapshot *my_create_snapshot(const char *grammar_file,
                                          char **error) {
    return lime_snapshot_create(grammar_file, error);
}

static void my_destroy(void) { /* cleanup */ }

static const LimeParserPlugin my_plugin = {
    .get_name         = my_get_name,
    .get_version      = my_get_version,
    .get_abi_major    = my_get_abi_major,
    .get_abi_minor    = my_get_abi_minor,
    .get_capabilities = my_get_capabilities,
    .init             = NULL,
    .destroy          = my_destroy,
    .create_snapshot  = my_create_snapshot,
    .validate_snapshot      = NULL,
    .serialize_snapshot     = NULL,
    .deserialize_snapshot   = NULL,
};

LIME_PLUGIN_EXPORT const LimeParserPlugin *lime_plugin_entry(void) {
    return &my_plugin;
}
```

## Future Considerations

- **Plugin dependencies**: A plugin could declare dependencies on other
  plugins, enabling layered grammar compositions.
- **Plugin namespaces**: Avoid symbol collisions between plugins loaded
  into the same process (`RTLD_LOCAL` handles this for most cases).
- **Remote plugins**: A wrapper plugin that proxies snapshot operations
  over a network socket or shared memory channel.
- **Plugin sandboxing**: On platforms that support it (seccomp, Capsicum),
  restrict plugin code to snapshot-producing operations only.
- **Telemetry hooks**: Allow the manager to emit events (plugin loaded,
  swap occurred, etc.) for monitoring integration.
