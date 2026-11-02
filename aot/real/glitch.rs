// Same rings as glitch.cpp. Integer math, so the checksum can match.
//   glitch <file.rgb> <w> <h> [out.ppm]
use std::env;
use std::fs::File;
use std::io::{Read, Write};
use std::process::ExitCode;

fn lum(p: i32) -> i32 {
    let r = p / 65536;
    let g = p / 256 - r * 256;
    let b = p - (p / 256) * 256;
    r + r + g * 5 + b
}

fn iatan2(y: i32, x: i32) -> i32 {
    let ax = if x < 0 { -x } else { x };
    let ay = if y < 0 { -y } else { y };
    if ax == 0 && ay == 0 {
        return 0;
    }
    let a = if ax > ay { ay * 32 / ax } else { 64 - ax * 32 / ay };
    if x >= 0 && y >= 0 {
        return a;
    }
    if x < 0 && y >= 0 {
        return 128 - a;
    }
    if x < 0 && y < 0 {
        return 128 + a;
    }
    if a == 0 { 0 } else { 256 - a }
}

fn less_pix(a: i32, b: i32) -> bool {
    let la = lum(a);
    let lb = lum(b);
    if la != lb { la < lb } else { a < b }
}

fn sift(a: &mut [i32], mut root: i32, end: i32) {
    loop {
        let mut child = root * 2 + 1;
        if child > end {
            return;
        }
        if child + 1 <= end && less_pix(a[child as usize], a[(child + 1) as usize]) {
            child += 1;
        }
        if less_pix(a[root as usize], a[child as usize]) {
            a.swap(root as usize, child as usize);
            root = child;
        } else {
            return;
        }
    }
}

fn heapsort(a: &mut [i32]) {
    let n = a.len() as i32;
    let mut start = (n - 2) / 2;
    while start >= 0 {
        sift(a, start, n - 1);
        start -= 1;
    }
    let mut end = n - 1;
    while end > 0 {
        a.swap(0, end as usize);
        end -= 1;
        sift(a, 0, end);
    }
}

fn main() -> ExitCode {
    let args: Vec<String> = env::args().collect();
    if args.len() < 4 {
        eprintln!("glitch <file.rgb> <w> <h> [out.ppm]");
        return ExitCode::from(1);
    }
    let w: i32 = args[2].parse().unwrap_or(0);
    let h: i32 = args[3].parse().unwrap_or(0);
    let mut f = match File::open(&args[1]) {
        Ok(f) => f,
        Err(_) => {
            eprintln!("cannot read {}", args[1]);
            return ExitCode::from(1);
        }
    };
    let mut raw = vec![0u8; (w * h * 3) as usize];
    if f.read_exact(&mut raw).is_err() {
        eprintln!("short read");
        return ExitCode::from(1);
    }
    let n = (w * h) as usize;
    let mut pix = vec![0i32; n];
    for i in 0..n {
        pix[i] = ((raw[i * 3] as i32) << 16) | ((raw[i * 3 + 1] as i32) << 8) | (raw[i * 3 + 2] as i32);
    }
    heapsort(&mut pix);
    let mut out = vec![0i32; n];
    let cx = w / 2;
    let cy = h / 2;
    let mut maxr = 1i32;
    for y in 0..h {
        for x in 0..w {
            let dx = x - cx;
            let dy = y - cy;
            let ax = if dx < 0 { -dx } else { dx };
            let ay = if dy < 0 { -dy } else { dy };
            let rad = if ax > ay { ax + ay / 2 } else { ay + ax / 2 };
            if rad > maxr {
                maxr = rad;
            }
        }
    }
    let lo = lum(pix[0]);
    let mut hi = lum(pix[n - 1]);
    if hi <= lo {
        hi = lo + 1;
    }
    let span = hi - lo;
    for y in 0..h {
        for x in 0..w {
            let dx = x - cx;
            let dy = y - cy;
            let ax = if dx < 0 { -dx } else { dx };
            let ay = if dy < 0 { -dy } else { dy };
            let rad = if ax > ay { ax + ay / 2 } else { ay + ax / 2 };
            let ang = iatan2(dy, dx);
            let mut target = hi - rad * span / maxr;
            target -= ang * span / 2048;
            if target < lo {
                target = lo;
            }
            if target > hi {
                target = hi;
            }
            let mut a = 0i32;
            let mut b = (n as i32) - 1;
            while a < b {
                let mid = (a + b + 1) / 2;
                if lum(pix[mid as usize]) <= target {
                    a = mid;
                } else {
                    b = mid - 1;
                }
            }
            let p = pix[a as usize];
            let mut r = p / 65536;
            let mut g = p / 256 - r * 256;
            let mut b = p - (p / 256) * 256;
            let m = rad - (rad / 3) * 3;
            if m == 0 {
                let t = r;
                r = b;
                b = t;
            } else if m == 1 {
                let t = r;
                r = g;
                g = b;
                b = t;
            }
            out[(y * w + x) as usize] = (r << 16) | (g << 8) | b;
        }
    }
    let mut row = vec![0i32; w as usize];
    for y in 0..h {
        if y - (y / 17) * 17 != 0 {
            continue;
        }
        let shift = y - (y / 5) * 5 - 2;
        for x in 0..w {
            let mut srcx = x - shift;
            while srcx < 0 {
                srcx += w;
            }
            while srcx >= w {
                srcx -= w;
            }
            row[x as usize] = out[(y * w + srcx) as usize];
        }
        for x in 0..w {
            out[(y * w + x) as usize] = row[x as usize];
        }
    }
    let mut sum: i64 = 0;
    for p in &out {
        sum += *p as i64;
    }
    if args.len() > 4 {
        let mut rgb = Vec::with_capacity(n * 3);
        for p in &out {
            rgb.push((*p / 65536) as u8);
            rgb.push((*p / 256 - (*p / 65536) * 256) as u8);
            rgb.push((*p - (*p / 256) * 256) as u8);
        }
        let mut o = match File::create(&args[4]) {
            Ok(o) => o,
            Err(_) => {
                eprintln!("cannot write {}", args[4]);
                return ExitCode::from(1);
            }
        };
        let _ = write!(o, "P6\n{w} {h}\n255\n");
        if o.write_all(&rgb).is_err() {
            return ExitCode::from(1);
        }
    }
    println!("{sum}");
    ExitCode::from(0)
}
