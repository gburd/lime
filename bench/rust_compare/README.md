# rust_compare bench

JSON-parsing benchmark comparing Lime's `--rust` + `--rustlex`
outputs against logos / lalrpop / nom / pest / serde_json on a
common 226 KB JSON fixture.

See `docs/RUST_BENCHMARK.md` (in the repo root) for the headline
numbers and methodology.

## Running

```bash
cargo build --release
for b in serde lime logos lalrpop nom pest; do
    echo "=== bench_$b ==="
    ./target/release/bench_$b
done
```

## Regenerating the lime outputs

`src/json_parser.rs` and `src/json_lexer.rs` are tracked in git
so the bench builds out-of-the-box without requiring lime on
PATH.  After modifying `json.lime` or `json.lex`, regenerate:

```bash
# from the repo root, with build/lime built:
./build/lime "-T$(pwd)/limpar.c" --rust bench/rust_compare/json.lime
./build/lime -X --rustlex bench/rust_compare/json.lex
mv bench/rust_compare/json.rs     bench/rust_compare/src/json_parser.rs
mv bench/rust_compare/json_lex.rs bench/rust_compare/src/json_lexer.rs
```

The intermediate `json.c`, `json.h`, `json.rs`, `json_lex.c`,
`json_lex.h`, `json_lex.rs`, and `json.out` files are gitignored
because they're trivially regenerated from `json.lime` /
`json.lex` and would otherwise churn on every `lime` invocation.
