//! Library wrapper around the lime-generated parser.
//!
//! The generated parser lives in `parser.rs` produced by:
//!   lime --rust calc.lime
//!
//! `eval(input)` tokenises an expression and feeds it to the parser.

#[allow(clippy::all)]
mod parser;

use parser::{CalcParser, ParseError, INTEGER, MINUS, PLUS, TIMES};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CalcError {
    Parse(ParseError),
    BadChar(char),
}

impl From<ParseError> for CalcError {
    fn from(e: ParseError) -> Self { CalcError::Parse(e) }
}

/// Tokenise + parse + evaluate.  Returns the integer result.
pub fn eval(input: &str) -> Result<i64, CalcError> {
    let mut p = CalcParser::new();
    let mut chars = input.chars().peekable();
    while let Some(&c) = chars.peek() {
        if c.is_whitespace() {
            chars.next();
        } else if c.is_ascii_digit() {
            let mut n: i64 = 0;
            while let Some(&d) = chars.peek() {
                if d.is_ascii_digit() {
                    n = n * 10 + (d as i64 - '0' as i64);
                    chars.next();
                } else {
                    break;
                }
            }
            p.push(INTEGER, n)?;
        } else {
            chars.next();
            let tok = match c {
                '+' => PLUS,
                '-' => MINUS,
                '*' => TIMES,
                _ => return Err(CalcError::BadChar(c)),
            };
            p.push(tok, 0)?;
        }
    }
    p.finalize()?;
    Ok(p.last_result())
}

/// Test helper: a small extension on the generated parser that
/// peeks at the most-recent reduce's LHS value.  Generated parsers
/// don't expose this by default; we add it here for the example
/// using a trait extension.
trait ParserPeek {
    fn last_result(&self) -> i64;
}

impl ParserPeek for CalcParser {
    fn last_result(&self) -> i64 {
        // The accept reduce stores the program rule's LHS; that
        // value lives at the top of the stack right before accept
        // pops it.  After accept, we've cleared the working stack
        // but kept a `final_value` field via `take_final_value()`
        // -- subsequent commits on this branch wire that field
        // explicitly.  For now, peek via a public field added
        // post-hoc by the generator; if absent, return 0 and let
        // tests skip.
        self.final_value
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn simple_addition() {
        assert_eq!(eval("1 + 2").unwrap(), 3);
    }
    #[test]
    fn precedence() {
        assert_eq!(eval("1 + 2 * 3").unwrap(), 7);
    }
    #[test]
    fn left_assoc() {
        assert_eq!(eval("10 - 3 - 2").unwrap(), 5);
    }
    #[test]
    fn syntax_error() {
        assert!(eval("1 +").is_err());
    }
    #[test]
    fn bad_char() {
        assert!(matches!(eval("1 / 2"), Err(CalcError::BadChar('/'))));
    }
}
