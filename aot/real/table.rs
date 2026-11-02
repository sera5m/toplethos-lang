// Same four lanes and 64 ticks as table.tpth_s. Lane 0 is added first.
fn lane_sum(lane: i32) -> f64 {
    let base = lane * 256;
    let mut w = [1.0f64; 256];
    let mut a = [0.0f64; 256];
    for _t in 0..64 {
        for i in 0..256 {
            let id = base + i as i32;
            a[i] = a[i] * 0.97 + (id as f64) * 0.001;
            w[i] = w[i] + a[i] * 0.0001;
        }
    }
    let mut s = 0.0;
    for i in 0..256 {
        s += w[i];
    }
    s
}

fn main() {
    let mut s = 0.0;
    for lane in 0..4 {
        s += lane_sum(lane);
    }
    println!("{s:.9}");
}
