# LLM Oracle Disambiguation Example

Demonstrates using an LLM (Large Language Model) as a custom disambiguation
strategy for the Lime parser generator's extensible grammar system.

## Overview

When multiple grammar extensions are loaded and a parse conflict arises
(e.g., a token could be interpreted by two different grammars), this example
queries an LLM API to decide which interpretation is correct.

The example includes:

- **llm_client.c/h** -- HTTP client for OpenAI and Anthropic APIs using libcurl
- **nlsql_extension.c** -- Grammar extension for natural-language SQL constructs,
  plus a custom `DisambiguationStrategyVTable` that queries the LLM

## Building

### With libcurl (full LLM support)

```
make
```

Requires libcurl development headers (`libcurl-dev` or `curl-devel`).

### Without libcurl (stub mode)

```
make no-curl
```

The LLM client will compile but always return "unavailable". Useful for
testing the extension registration and conflict simulation flow without
network dependencies.

## Running

### Environment variables

| Variable | Description | Default |
|---|---|---|
| `LIME_LLM_API_KEY` | API key for the LLM provider | (none -- LLM disabled) |
| `LIME_LLM_PROVIDER` | `openai` or `anthropic` | `openai` |
| `LIME_LLM_MODEL` | Model name to use | `gpt-4o-mini` (OpenAI) or `claude-sonnet-4-20250514` (Anthropic) |
| `LIME_LLM_BASE_URL` | Override API endpoint URL | Provider default |

### Example

```bash
# Without LLM (demonstrates fallback behavior)
./nlsql_demo

# With OpenAI
export LIME_LLM_API_KEY="sk-..."
./nlsql_demo

# With Anthropic
export LIME_LLM_API_KEY="sk-ant-..."
export LIME_LLM_PROVIDER=anthropic
./nlsql_demo
```

### Sample output (no LLM)

```
=== LLM Oracle Disambiguation Demo ===

Registered extension: nlsql (id=1)
Loaded extension: nlsql

Created LLM disambiguation context
  LLM available: no (set LIME_LLM_API_KEY)

--- Simulating conflict resolution ---

Conflict: Token 'show': standard SQL SELECT vs NL-SQL show query
  Interpretation 1: standard_sql (priority 10)
  Interpretation 2: nlsql (priority 5)

Resolution: unresolved (LLM unavailable or failed)

  Tip: Set LIME_LLM_API_KEY environment variable to enable LLM.
  Supported providers:
    LIME_LLM_PROVIDER=openai (default)
    LIME_LLM_PROVIDER=anthropic

Done.
```

### Sample output (with LLM)

```
=== LLM Oracle Disambiguation Demo ===

Registered extension: nlsql (id=1)
Loaded extension: nlsql

Created LLM disambiguation context
  LLM available: yes (key set)

--- Simulating conflict resolution ---

Conflict: Token 'show': standard SQL SELECT vs NL-SQL show query
  Interpretation 1: standard_sql (priority 10)
  Interpretation 2: nlsql (priority 5)

Resolution: extension 0 wins
  Confidence: 0.80
  Explanation: LLM chose interpretation 1 (ext 0, 'standard_sql')

Done.
```

## How it works

### Custom disambiguation strategy

The example implements the `DisambiguationStrategyVTable` interface:

```c
static const DisambiguationStrategyVTable llm_strategy_vtable = {
    .init    = llm_strategy_init,     // Create LLM client from env vars
    .resolve = llm_strategy_resolve,  // Query LLM for conflict resolution
    .update  = llm_strategy_update,   // (no-op in this example)
    .destroy = llm_strategy_destroy,  // Clean up LLM client
};
```

The strategy is registered using `disambiguation_create_custom()`:

```c
DisambiguationContext *dis = disambiguation_create_custom(
    &llm_strategy_vtable, registry);
```

### LLM prompt format

The strategy sends a structured prompt to the LLM:

```
Grammar conflict at token 42, state 100.
Description: Token 'show': standard SQL SELECT vs NL-SQL show query

Available interpretations:
1. Extension 0 (standard_sql), token 42, priority 10
2. Extension 1 (nlsql), token 42, priority 5

Which interpretation is correct?
```

The LLM is instructed to respond with `CHOICE: N` where N is the 1-based
index of the winning interpretation.

### Fallback behavior

When the LLM is unavailable (no API key, network error, timeout), the
`resolve` callback returns `false`, indicating the conflict is unresolved.
The calling code can then fall back to a deterministic strategy like
priority-based disambiguation.

## Files

| File | Description |
|---|---|
| `llm_client.h` | LLM client public API |
| `llm_client.c` | LLM client implementation (libcurl or stub) |
| `nlsql_extension.c` | NL-SQL extension + LLM strategy + demo main |
| `Makefile` | Build instructions |
| `README.md` | This file |
