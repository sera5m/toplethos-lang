// Same battery as battery.cpp. No crates: rustc can build this one file.
// rustc -C opt-level=3 -C llvm-args=-ffp-contract=off -o /tmp/battery_rs aot/battery.rs
use std::sync::{Mutex, Arc};
use std::thread;
use std::time::Instant;

const N: i32 = 2_000_000;
const T: i32 = 4;

#[inline(never)]
fn horner(x: f64) -> f64 {
    let mut y = 8.0;
    y = 7.0 + x * y;
    y = 6.0 + x * y;
    y = 5.0 + x * y;
    y = 4.0 + x * y;
    y = 3.0 + x * y;
    y = 2.0 + x * y;
    y = 1.0 + x * y;
    y
}

struct Poly {
    c0: f64,
    c1: f64,
    c2: f64,
    c3: f64,
    c4: f64,
    c5: f64,
    c6: f64,
    c7: f64,
}

impl Poly {
    #[inline(never)]
    fn eval(&self, x: f64) -> f64 {
        let mut y = self.c7;
        y = self.c6 + x * y;
        y = self.c5 + x * y;
        y = self.c4 + x * y;
        y = self.c3 + x * y;
        y = self.c2 + x * y;
        y = self.c1 + x * y;
        y = self.c0 + x * y;
        y
    }
}

#[inline(never)]
fn checked(c: &[f64], x: f64) -> f64 {
    let mut y = c[7];
    let mut k = 6;
    loop {
        if k >= c.len() {
            return 0.0;
        }
        y = c[k] + x * y;
        if k == 0 {
            break;
        }
        k -= 1;
    }
    y
}

#[inline(never)]
fn raw(c: *const f64, x: f64) -> f64 {
    unsafe {
        let mut y = *c.add(7);
        y = *c.add(6) + x * y;
        y = *c.add(5) + x * y;
        y = *c.add(4) + x * y;
        y = *c.add(3) + x * y;
        y = *c.add(2) + x * y;
        y = *c.add(1) + x * y;
        y = *c.add(0) + x * y;
        y
    }
}

fn sum_range(lo: i32, hi: i32) -> f64 {
    let mut s = 0.0;
    let mut i = lo;
    while i < hi {
        s += horner(i as f64 * 0.000001);
        i += 1;
    }
    s
}

fn row(name: &str, t: Instant, sum: f64) {
    println!("{name} {:.6} {:.9}", t.elapsed().as_secs_f64(), sum);
}

fn main() {
    let p = Poly { c0: 1.0, c1: 2.0, c2: 3.0, c3: 4.0, c4: 5.0, c5: 6.0, c6: 7.0, c7: 8.0 };
    let coeff = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0];

    let t = Instant::now();
    let mut s = 0.0;
    let mut i = 0;
    while i < N {
        let x = i as f64 * 0.000001;
        let mut y = 8.0;
        y = 7.0 + x * y;
        y = 6.0 + x * y;
        y = 5.0 + x * y;
        y = 4.0 + x * y;
        y = 3.0 + x * y;
        y = 2.0 + x * y;
        y = 1.0 + x * y;
        s += y;
        i += 1;
    }
    row("direct", t, s);

    let t = Instant::now();
    s = 0.0;
    i = 0;
    while i < N {
        s += horner(i as f64 * 0.000001);
        i += 1;
    }
    row("call", t, s);

    let t = Instant::now();
    s = 0.0;
    i = 0;
    while i < N {
        s += p.eval(i as f64 * 0.000001);
        i += 1;
    }
    row("object", t, s);

    let t = Instant::now();
    s = 0.0;
    i = 0;
    while i < N {
        s += checked(&coeff, i as f64 * 0.000001);
        i += 1;
    }
    row("checked", t, s);

    let t = Instant::now();
    s = 0.0;
    i = 0;
    let ptr = coeff.as_ptr();
    while i < N {
        s += raw(ptr, i as f64 * 0.000001);
        i += 1;
    }
    row("raw", t, s);

    let t = Instant::now();
    let chunk = N / T;
    let mut handles = Vec::new();
    for k in 0..T {
        let lo = k * chunk;
        let hi = if k == T - 1 { N } else { lo + chunk };
        handles.push(thread::spawn(move || sum_range(lo, hi)));
    }
    s = 0.0;
    for h in handles {
        s += h.join().unwrap();
    }
    row("threads", t, s);

    // Rust std has no async executor. These are the same four tasks as `threads`,
    // started the same way. C++ `async` is std::async. Do not read this row as tokio.
    let t = Instant::now();
    let mut tasks = Vec::new();
    for k in 0..T {
        let lo = k * chunk;
        let hi = if k == T - 1 { N } else { lo + chunk };
        tasks.push(thread::spawn(move || sum_range(lo, hi)));
    }
    s = 0.0;
    for h in tasks {
        s += h.join().unwrap();
    }
    row("async", t, s);

    let t = Instant::now();
    let shared = Arc::new(Mutex::new(0.0));
    let mut locked = Vec::new();
    for _ in 0..T {
        let shared = Arc::clone(&shared);
        locked.push(thread::spawn(move || {
            for _ in 0..50_000 {
                *shared.lock().unwrap() += 1.0;
            }
        }));
    }
    for h in locked {
        h.join().unwrap();
    }
    row("mutex", t, *shared.lock().unwrap());
}
