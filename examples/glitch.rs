// Same rings as examples/glitch.cpp. The photo is examples/street.bmp.
//   glitch <file.bmp> [out.ppm]
use std::env;
use std::fs::File;
use std::io::Write;
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

fn load_bmp(path: &str) -> Option<(Vec<i32>, i32, i32)> {
    let data = std::fs::read(path).ok()?;
    if data.len() < 54 || data[0] != b'B' || data[1] != b'M' {
        return None;
    }
    let u32_at = |o: usize| u32::from_le_bytes([data[o], data[o + 1], data[o + 2], data[o + 3]]);
    let u16_at = |o: usize| u16::from_le_bytes([data[o], data[o + 1]]);
    let off = u32_at(10) as usize;
    let w = u32_at(18) as i32;
    let mut h = u32_at(22) as i32;
    let top = h < 0;
    if top {
        h = -h;
    }
    if u16_at(28) != 24 || u32_at(30) != 0 || w < 1 || h < 1 {
        return None;
    }
    let stride = ((w * 3 + 3) & !3) as usize;
    if off + stride * (h as usize) > data.len() {
        return None;
    }
    let mut pix = vec![0i32; (w * h) as usize];
    for y in 0..h {
        let desty = if top { y } else { h - 1 - y };
        let row = off + (y as usize) * stride;
        for x in 0..w {
            let i = row + (x as usize) * 3;
            let b = data[i] as i32;
            let g = data[i + 1] as i32;
            let r = data[i + 2] as i32;
            pix[(desty * w + x) as usize] = (r << 16) | (g << 8) | b;
        }
    }
    Some((pix, w, h))
}

fn main() -> ExitCode {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("glitch <file.bmp> [out.ppm]");
        return ExitCode::from(1);
    }
    let (mut pix, w, h) = match load_bmp(&args[1]) {
        Some(v) => v,
        None => {
            eprintln!("bitmap");
            return ExitCode::from(1);
        }
    };
    let n = pix.len();
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
    if args.len() > 2 {
        let mut rgb = Vec::with_capacity(n * 3);
        for p in &out {
            rgb.push((*p / 65536) as u8);
            rgb.push((*p / 256 - (*p / 65536) * 256) as u8);
            rgb.push((*p - (*p / 256) * 256) as u8);
        }
        let mut o = match File::create(&args[2]) {
            Ok(o) => o,
            Err(_) => {
                eprintln!("cannot write {}", args[2]);
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
