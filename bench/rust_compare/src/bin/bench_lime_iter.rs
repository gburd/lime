// Streaming-iterator benchmark: comparable to bench_logos.
// Uses Lexer::iter() which doesn't allocate Vec<Token>.
use rust_compare::{bench, read_fixture};
use rust_compare::json_lexer as lexer;

fn main() {
    let input = read_fixture();
    println!("// fixture: {} bytes", input.len());
    bench("lime: tokenize (iter)", 200, || {
        let mut lex = lexer::Lexer::new();
        for tok in lex.iter(&input) {
            // touch the token to prevent dead-code-elim
            let _ = tok.unwrap();
        }
    });
}
