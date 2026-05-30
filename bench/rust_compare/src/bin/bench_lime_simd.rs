use rust_compare::{bench, read_fixture};
use rust_compare::json_parser as parser;
use rust_compare::json_lexer_simd as lexer;

fn run_parse(_input: &str, tokens: &[lexer::Token]) -> bool {
    let mut p = parser::JsonParseParser::new();
    for tok in tokens {
        if tok.rule_id == lexer::JSON_RULE_WS { continue; }
        let code: u16 = match tok.rule_id {
            id if id == lexer::JSON_RULE_LBRACE   => parser::LBRACE,
            id if id == lexer::JSON_RULE_RBRACE   => parser::RBRACE,
            id if id == lexer::JSON_RULE_LBRACKET => parser::LBRACKET,
            id if id == lexer::JSON_RULE_RBRACKET => parser::RBRACKET,
            id if id == lexer::JSON_RULE_COLON    => parser::COLON,
            id if id == lexer::JSON_RULE_COMMA    => parser::COMMA,
            id if id == lexer::JSON_RULE_STR      => parser::STRING,
            id if id == lexer::JSON_RULE_NUM      => parser::NUMBER,
            id if id == lexer::JSON_RULE_TRUE     => parser::TRUE,
            id if id == lexer::JSON_RULE_FALSE    => parser::FALSE,
            id if id == lexer::JSON_RULE_NULL     => parser::NULL,
            _ => return false,
        };
        if p.push(code, 0).is_err() { return false; }
    }
    p.finalize().unwrap_or(false)
}

fn main() {
    let input = read_fixture();
    let mut lex = lexer::Lexer::new();
    let tokens = lex.tokenize(&input).expect("tokenise");
    println!("// fixture: {} bytes, {} tokens", input.len(), tokens.len());

    bench("lime+simd: tokenize", 200, || {
        let mut l = lexer::Lexer::new();
        let _ = l.tokenize(&input).unwrap();
    });
    bench("lime+simd: parse",    200, || {
        let _ = run_parse(&input, &tokens);
    });
    bench("lime+simd: lex+parse",100, || {
        let mut l = lexer::Lexer::new();
        let toks = l.tokenize(&input).unwrap();
        let _ = run_parse(&input, &toks);
    });
}
