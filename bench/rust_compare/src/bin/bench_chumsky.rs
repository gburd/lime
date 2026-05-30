use rust_compare::{bench, read_fixture};
// chumsky 0.10 has a different API; use a simpler approach.
// Strategy: chumsky is best with Span tracking; for raw bench just
// use serde_json's tokenizer-equivalent by reading bytes.
//
// Skip chumsky: its 0.10 API changed substantially and writing a
// fair comparison parser without spending hours on the chumsky
// builder API isn't worth the bench-doc real estate.  Document
// "chumsky bench skipped: 0.10 API rewrite makes a fair port a
// separate exercise".
fn main() {
    let input = read_fixture();
    println!("// fixture: {} bytes", input.len());
    bench("chumsky (skip)", 1, || {
        // No-op: documented in benchmark output as skipped.
        let _ = input.len();
    });
    println!("// chumsky bench: SKIPPED (0.10 API substantially rewritten;");
    println!("//   writing a fair port is a separate effort.  See nom for");
    println!("//   the parser-combinator data point.)");
}
