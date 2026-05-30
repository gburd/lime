use rust_compare::{bench, read_fixture};
fn main() {
    let input = read_fixture();
    println!("// fixture: {} bytes", input.len());
    bench("serde_json", 200, || {
        let _: serde_json::Value = serde_json::from_str(&input).unwrap();
    });
}
