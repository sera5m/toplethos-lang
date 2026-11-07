// Same mixed-ops kernel as mix.tpth_s. N = 1000000.
// rustc -C opt-level=3 -C llvm-args=--fp-contract=off -o /tmp/mix_rs aot/mix.rs
use std::time::Instant;

const N: i32 = 1_000_000;

struct Mix {
    bias: i32,
    scale: f64,
}

#[inline(never)]
fn stew(a: i32, b: i32) -> i32 {
    let mut n = a;
    let mut i = 0;
    while i < 4 {
        n = (n + b + i) & 255;
        let sh = i & 3;
        n = n ^ (b >> sh);
        i += 1;
    }
    n
}

#[inline(never)]
fn spice(x: f64, k: i32) -> f64 {
    let mut y = x;
    let mut i = 0;
    while i < 4 {
        let t = (i + k) as f64;
        y = (y * 1.25 + t) / (2.0 + (i as f64));
        y = y - t * 0.125;
        i += 1;
    }
    y
}

fn main() {
    let tap = [3, 5, 7, 11, 13, 17, 19, 23];
    let m = Mix { bias: 9, scale: 0.015625 };
    let mut a: i32 = 1;
    let mut b: i32 = 7;
    let t0 = Instant::now();
    let mut s = 0.0;
    let mut i = 0;
    while i < N {
        let k = i & 7;
        let t = tap[k as usize];
        a = a + t + m.bias;
        a = a - (a / 251) * 251;
        b = ((b * 3) + t) & 255;
        let sh = b & 3;
        let mut c = (a << sh) & 511;
        c = c ^ (b | t);
        c = c % 97;
        c = !c & 127;
        a = stew(c, b);
        let x = (a as f64) * m.scale;
        let mut y = spice(x, t);
        let odd = c & 1;
        if odd == 0 {
            y = y + x;
        } else {
            y = y - x * 0.5;
        }
        s += y;
        i += 1;
    }
    let sec = t0.elapsed().as_secs_f64();
    println!("mix {sec:.6} {s:.9}");
}
