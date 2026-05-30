use rust_compare::{bench, read_fixture};
use rust_compare::json_tok::Tok;
use logos::Logos;

fn main() {
    let input = read_fixture();
    println!("// fixture: {} bytes", input.len());
    bench("logos tokenize", 200, || {
        let mut lex = Tok::lexer(&input);
        while let Some(_) = lex.next() {}
    });
}
