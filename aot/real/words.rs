// Words in lipsum-cicero.txt. Same open-address count as words.tpth_s.
fn mix(h: i32, c: i32) -> i32 {
    ((h as u32) ^ (c as u32)).wrapping_mul(16777619) as i32
}

fn main() {
    let raw = std::fs::read("lipsum-cicero.txt").expect("lipsum");
    let n = raw.len() as i32;
    let t = 32768usize;
    let mut off = vec![0i32; t];
    let mut span = vec![0i32; t];
    let mut cnt = vec![0i32; t];
    let mut hsh = vec![0i32; t];
    for _rep in 0..50 {
        off.fill(0);
        span.fill(0);
        cnt.fill(0);
        hsh.fill(0);
        let mut i = 0i32;
        while i < n {
        let mut c = raw[i as usize] as i32;
        if c >= 65 && c <= 90 {
            c += 32;
        }
        let mut letter = c >= 97 && c <= 122;
        if !letter {
            i += 1;
            continue;
        }
        let beg = i;
        let mut h = 0i32;
        while i < n {
            c = raw[i as usize] as i32;
            if c >= 65 && c <= 90 {
                c += 32;
            }
            letter = c >= 97 && c <= 122;
            if !letter {
                break;
            }
            h = mix(h, c);
            i += 1;
        }
        let wlen = i - beg;
        let mut slot = (h & 32767) as usize;
        loop {
            if span[slot] == 0 {
                off[slot] = beg;
                span[slot] = wlen;
                cnt[slot] = 1;
                hsh[slot] = h;
                break;
            }
            if hsh[slot] == h && span[slot] == wlen {
                let mut same = 1;
                let mut k = 0i32;
                while k < wlen {
                    let mut a = raw[(off[slot] + k) as usize] as i32;
                    let mut b = raw[(beg + k) as usize] as i32;
                    if a >= 65 && a <= 90 {
                        a += 32;
                    }
                    if b >= 65 && b <= 90 {
                        b += 32;
                    }
                    if a != b {
                        same = 0;
                        break;
                    }
                    k += 1;
                }
                if same == 1 {
                    cnt[slot] += 1;
                    break;
                }
            }
            slot += 1;
            if slot == t {
                slot = 0;
            }
        }
    }
    }
    let mut sum = 0i64;
    for s in 0..t {
        if span[s] != 0 {
            sum += hsh[s] as i64 * cnt[s] as i64 + span[s] as i64;
        }
    }
    println!("{sum}");
}
