use logos::Logos;

#[derive(Logos, Debug, Clone, PartialEq)]
#[logos(skip r"[ \t\n\r]+")]
pub enum Tok {
    #[token("{")] LBrace,
    #[token("}")] RBrace,
    #[token("[")] LBracket,
    #[token("]")] RBracket,
    #[token(":")] Colon,
    #[token(",")] Comma,
    #[token("true")]  True,
    #[token("false")] False,
    #[token("null")]  Null,
    #[regex(r#"-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?"#)] Number,
    #[regex(r#""([^"\\]|\\.)*""#)] String,
}
