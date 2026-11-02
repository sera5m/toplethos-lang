// Same butterfly and the same series as rdp.cpp. Epsilon 0.5.
fn sc(x: f64) -> (f64, f64) {
    let tw = 6.283185307179586;
    let half = 1.5707963267948966;
    let mut y = x;
    while y >= tw {
        y -= tw;
    }
    while y < 0.0 {
        y += tw;
    }
    let mut q = 0;
    while y >= half {
        y -= half;
        q += 1;
    }
    let x2 = y * y;
    let sp = y * (1.0 - x2 / 6.0 * (1.0 - x2 / 20.0 * (1.0 - x2 / 42.0 * (1.0 - x2 / 72.0))));
    let cp = 1.0 - x2 / 2.0 * (1.0 - x2 / 12.0 * (1.0 - x2 / 30.0 * (1.0 - x2 / 56.0)));
    if q == 0 { return (sp, cp); }
    if q == 1 { return (cp, -sp); }
    if q == 2 { return (-sp, -cp); }
    (-cp, sp)
}

fn ex(z: f64) -> f64 {
    let mut term = 1.0;
    let mut s = 1.0;
    let mut k = 1.0;
    for _ in 0..16 {
        term = term * z / k;
        s += term;
        k += 1.0;
    }
    s
}

fn main() {
    let n = 361;
    let mut xs = [0.0f64; 361];
    let mut ys = [0.0f64; 361];
    let mut keep = [1i32; 361];
    let span = 12.0 * 3.141592653589793;
    for i in 0..n {
        let t = span * (i as f64) / 360.0;
        let (s, c) = sc(t);
        let (_s4, c4) = sc(4.0 * t);
        let (s12, _c12) = sc(t / 12.0);
        let s5 = s12 * s12 * s12 * s12 * s12;
        let core = ex(c) - 2.0 * c4 - s5;
        xs[i] = s * core;
        ys[i] = c * core;
    }
    let mut sa = [0i32; 800];
    let mut sb = [0i32; 800];
    sa[0] = 0;
    sb[0] = (n as i32) - 1;
    let mut sp = 1i32;
    let eps2 = 0.25;
    while sp > 0 {
        sp -= 1;
        let a = sa[sp as usize];
        let b = sb[sp as usize];
        if b < a + 2 {
            continue;
        }
        let mut best = -1.0;
        let mut besti = -1i32;
        let dx = xs[b as usize] - xs[a as usize];
        let dy = ys[b as usize] - ys[a as usize];
        let den2 = dx * dx + dy * dy;
        let mut i = a + 1;
        while i < b {
            let mut num = (ys[a as usize] - ys[i as usize]) * dx - (xs[a as usize] - xs[i as usize]) * dy;
            if num < 0.0 {
                num = -num;
            }
            let cross2 = num * num;
            if cross2 > best || (cross2 == best && i > besti) {
                best = cross2;
                besti = i;
            }
            i += 1;
        }
        if best > eps2 * den2 {
            sa[sp as usize] = besti;
            sb[sp as usize] = b;
            sp += 1;
            sa[sp as usize] = a;
            sb[sp as usize] = besti;
            sp += 1;
        } else {
            let mut j = a + 1;
            while j < b {
                keep[j as usize] = 0;
                j += 1;
            }
        }
    }
    let mut kept = 0i32;
    let mut sm = 0.0;
    for i in 0..n {
        if keep[i] == 1 {
            kept += 1;
            sm += xs[i] + ys[i];
        }
    }
    println!("{kept}");
    println!("{sm:.9}");
}
