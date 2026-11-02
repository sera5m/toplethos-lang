// Same open-address counts as dict.cpp.
fn main() {
    let mut key = [-1i32; 1024];
    let mut cnt = [0i32; 1024];
    for i in 0..20000 {
        let mut k = i * 17 + 3;
        k = k - (k / 500) * 500;
        let mut slot = k;
        loop {
            if key[slot as usize] == -1 {
                key[slot as usize] = k;
                cnt[slot as usize] = 1;
                break;
            }
            if key[slot as usize] == k {
                cnt[slot as usize] = cnt[slot as usize] + 1;
                break;
            }
            slot = slot + 1;
            if slot == 1024 {
                slot = 0;
            }
        }
    }
    let mut sum: i64 = 0;
    for i in 0..1024 {
        if key[i] != -1 {
            sum += key[i] as i64 * cnt[i] as i64;
        }
    }
    println!("{sum}");
}
