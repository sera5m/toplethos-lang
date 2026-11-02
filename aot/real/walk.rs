// Count files under a root. Read only: symlink_metadata and read_dir.
// Symlinks are not followed. Nothing is created or written.
//   walk [root] [seconds]     defaults: / 30
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicI64, Ordering};
use std::thread;
use std::time::{Duration, Instant};

fn walk(root: &Path, deadline: Instant, stop: &AtomicBool, files: &AtomicI64, bytes: &AtomicI64, dirs: &AtomicI64) {
    let mut st = vec![root.to_path_buf()];
    while let Some(cur) = st.pop() {
        if stop.load(Ordering::Relaxed) || Instant::now() >= deadline {
            stop.store(true, Ordering::Relaxed);
            return;
        }
        let rd = match fs::read_dir(&cur) {
            Ok(rd) => rd,
            Err(_) => continue,
        };
        for ent in rd.flatten() {
            let path = ent.path();
            let meta = match fs::symlink_metadata(&path) {
                Ok(m) => m,
                Err(_) => continue,
            };
            if meta.file_type().is_dir() {
                dirs.fetch_add(1, Ordering::Relaxed);
                st.push(path);
            } else {
                files.fetch_add(1, Ordering::Relaxed);
                bytes.fetch_add(meta.len() as i64, Ordering::Relaxed);
            }
        }
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let root = if args.len() > 1 { args[1].clone() } else { "/".to_string() };
    let seconds: u64 = if args.len() > 2 { args[2].parse().unwrap_or(30) } else { 30 };
    let files = AtomicI64::new(0);
    let bytes = AtomicI64::new(0);
    let dirs = AtomicI64::new(1);
    let stop = AtomicBool::new(false);
    let deadline = Instant::now() + Duration::from_secs(seconds);
    let rd = match fs::read_dir(&root) {
        Ok(rd) => rd,
        Err(_) => {
            eprintln!("cannot read {root}");
            std::process::exit(1);
        }
    };
    let mut kids: Vec<PathBuf> = Vec::new();
    for ent in rd.flatten() {
        let path = ent.path();
        let meta = match fs::symlink_metadata(&path) {
            Ok(m) => m,
            Err(_) => continue,
        };
        if meta.file_type().is_dir() {
            dirs.fetch_add(1, Ordering::Relaxed);
            kids.push(path);
        } else {
            files.fetch_add(1, Ordering::Relaxed);
            bytes.fetch_add(meta.len() as i64, Ordering::Relaxed);
        }
    }
    let files_r = &files;
    let bytes_r = &bytes;
    let dirs_r = &dirs;
    let stop_r = &stop;
    let kids_r = &kids;
    thread::scope(|s| {
        for lane in 0..4 {
            let lane = lane;
            s.spawn(move || {
                let mut i = lane;
                while i < kids_r.len() {
                    walk(&kids_r[i], deadline, stop_r, files_r, bytes_r, dirs_r);
                    i += 4;
                }
            });
        }
    });
    println!("{}", files.load(Ordering::Relaxed));
    println!("{}", bytes.load(Ordering::Relaxed));
}
