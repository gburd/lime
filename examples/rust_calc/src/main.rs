//! rust_calc binary: read an arithmetic expression from argv,
//! parse it via the lime-generated parser, print accept/reject.

use rust_calc::eval;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let input = args.join(" ");
    if input.is_empty() {
        eprintln!("usage: rust_calc <expression>");
        std::process::exit(2);
    }
    match eval(&input) {
        Ok(v) => println!("accept: {} = {}", input, v),
        Err(e) => {
            eprintln!("reject: {} -- {:?}", input, e);
            std::process::exit(1);
        }
    }
}
