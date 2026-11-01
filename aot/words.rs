// Same rule as words.tpth_s: ASCII letters, A-Z folded to a-z, first-seen order.
use std::collections::HashMap;
use std::fs;
use std::process::ExitCode;

fn letter(c: u8) -> bool {
    (c >= b'A' && c <= b'Z') || (c >= b'a' && c <= b'z')
}

fn main() -> ExitCode {
    let path = "/home/ash/Downloads/lipsum-cicero.txt";
    let text = match fs::read(path) {
        Ok(b) => b,
        Err(_) => {
            eprintln!("cannot read {path}");
            return ExitCode::from(1);
        }
    };
    let mut counts: HashMap<Vec<u8>, i32> = HashMap::new();
    let mut order: Vec<Vec<u8>> = Vec::new();
    let mut i = 0;
    while i < text.len() {
        if letter(text[i]) {
            let start = i;
            i += 1;
            while i < text.len() && letter(text[i]) {
                i += 1;
            }
            let mut w = text[start..i].to_vec();
            for b in &mut w {
                if *b >= b'A' && *b <= b'Z' {
                    *b += 32;
                }
            }
            if let Some(n) = counts.get_mut(&w) {
                *n += 1;
            } else {
                counts.insert(w.clone(), 1);
                order.push(w);
            }
        } else {
            i += 1;
        }
    }
    println!("words, instances");
    for w in &order {
        let s = std::str::from_utf8(w).unwrap();
        println!("{s}: {}", counts[w]);
    }
    ExitCode::from(0)
}
