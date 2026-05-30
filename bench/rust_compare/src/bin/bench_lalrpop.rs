use rust_compare::{bench, read_fixture};
use rust_compare::json_tok::Tok;
use logos::Logos;
use lalrpop_util::lalrpop_mod;

lalrpop_mod!(json_lalrpop);

fn main() {
    let input = read_fixture();
    println!("// fixture: {} bytes", input.len());
    let parser = json_lalrpop::ValueParser::new();
    bench("lalrpop tokenize", 200, || {
        let mut lex = Tok::lexer(&input);
        while let Some(_) = lex.next() {}
    });
    let tokens: Vec<_> = Tok::lexer(&input).spanned()
        .map(|(t, span)| (span.start, t.unwrap(), span.end))
        .collect();
    bench("lalrpop parse", 100, || {
        let _ = parser.parse(tokens.iter().cloned().map(Ok::<_, ()>));
    });
    bench("lalrpop lex+parse", 100, || {
        let lex = Tok::lexer(&input).spanned()
            .map(|(t, span)| Ok::<_, ()>((span.start, t.unwrap(), span.end)));
        let _ = parser.parse(lex);
    });
}
