use rust_compare::{bench, read_fixture};
use pest::Parser;
use pest_derive::Parser;

#[derive(Parser)]
#[grammar_inline = r#"
WHITESPACE = _{ " " | "\t" | "\n" | "\r" }
value = { object | array | string | number | "true" | "false" | "null" }
object = { "{" ~ "}" | "{" ~ pair ~ ("," ~ pair)* ~ "}" }
pair = { string ~ ":" ~ value }
array = { "[" ~ "]" | "[" ~ value ~ ("," ~ value)* ~ "]" }
string = @{ "\"" ~ (!"\"" ~ ANY)* ~ "\"" }
number = @{ "-"? ~ ("0" | ASCII_NONZERO_DIGIT ~ ASCII_DIGIT*) ~ ("." ~ ASCII_DIGIT+)? ~ (("e" | "E") ~ ("+" | "-")? ~ ASCII_DIGIT+)? }
"#]
struct JsonGrammar;

fn main() {
    let input = read_fixture();
    println!("// fixture: {} bytes", input.len());
    bench("pest parse", 50, || {
        let _ = JsonGrammar::parse(Rule::value, &input);
    });
}
