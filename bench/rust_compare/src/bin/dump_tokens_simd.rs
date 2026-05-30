//! Dump the lime tokenizer's token stream on the fixture to a hex
//! file.  Used as a correctness baseline before the v0.8.3 emit
//! rewrite.
use rust_compare::json_lexer_simd::{Lexer, Token};

fn main() {
    let bytes = std::fs::read("fixture.json").unwrap();
    let s = std::str::from_utf8(&bytes).unwrap();
    let mut lex = Lexer::new();
    let tokens = lex.tokenize(s).unwrap();
    let out_path = std::env::args().nth(1).expect("usage: dump_tokens <out>");
    let mut buf = String::new();
    for t in &tokens {
        buf += &format!("{} {} {} {} {}\n", t.rule_id, t.start, t.len, t.line, t.column);
    }
    std::fs::write(&out_path, &buf).unwrap();
    eprintln!("wrote {} tokens to {}", tokens.len(), out_path);
}
