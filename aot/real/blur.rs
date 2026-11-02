// Same 3x3 box as blur.cpp. One input frame, one output frame.
//   blur <file.rgb> <w> <h> [out.ppm]
use std::env;
use std::fs::File;
use std::io::{Read, Write};
use std::process::ExitCode;

fn main() -> ExitCode {
    let args: Vec<String> = env::args().collect();
    if args.len() < 4 {
        eprintln!("blur <file.rgb> <w> <h> [out.ppm]");
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
    let n = (w * h) as usize;
    let mut raw = vec![0u8; n * 3];
    if f.read_exact(&mut raw).is_err() {
        eprintln!("short read");
        return ExitCode::from(1);
    }
    let mut pix = vec![0i32; n];
    for i in 0..n {
        pix[i] = ((raw[i * 3] as i32) << 16) | ((raw[i * 3 + 1] as i32) << 8) | (raw[i * 3 + 2] as i32);
    }
    let mut out = vec![0i32; n];
    let mut sum: i64 = 0;
    for y in 0..h {
        for x in 0..w {
            let mut rs = 0;
            let mut gs = 0;
            let mut bs = 0;
            let mut c = 0;
            for dy in -1..=1 {
                for dx in -1..=1 {
                    let xx = x + dx;
                    let yy = y + dy;
                    if xx < 0 || yy < 0 || xx >= w || yy >= h {
                        continue;
                    }
                    let p = pix[(yy * w + xx) as usize];
                    let r = p / 65536;
                    let g = p / 256 - r * 256;
                    let b = p - (p / 256) * 256;
                    rs += r;
                    gs += g;
                    bs += b;
                    c += 1;
                }
            }
            let r = rs / c;
            let g = gs / c;
            let b = bs / c;
            let p = (r << 16) | (g << 8) | b;
            out[(y * w + x) as usize] = p;
            sum += p as i64;
        }
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
