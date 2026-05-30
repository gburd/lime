pub mod json_tok;
pub mod json_parser;
pub mod json_lexer;
pub mod json_lexer_memchr;

use std::time::Instant;

pub fn bench<F: FnMut()>(label: &str, n: u32, mut f: F) {
    for _ in 0..10 { f(); }
    let t0 = Instant::now();
    for _ in 0..n { f(); }
    let elapsed = t0.elapsed();
    let per = elapsed / n;
    let bytes = std::env::var("FIXTURE_BYTES").ok()
        .and_then(|s| s.parse::<u64>().ok()).unwrap_or(226620);
    let mb_per_sec = (bytes as f64 * n as f64) / (elapsed.as_secs_f64() * 1024.0 * 1024.0);
    println!("{:18} {:>9.3} us/parse {:>9.1} MB/s  ({} iter, {:.2?})",
             label, per.as_secs_f64() * 1e6, mb_per_sec, n, elapsed);
}

pub fn read_fixture() -> String {
    std::fs::read_to_string(
        std::env::var("FIXTURE").unwrap_or_else(|_| "fixture.json".to_string())
    ).expect("fixture.json")
}
