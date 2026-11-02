// Count files under a root. Read only: lstat and directory listings.
// Symlinks are not followed. Nothing is opened for write, and nothing is created.
//   walk [root] [seconds]     defaults: / 30
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <vector>
#include <sys/stat.h>

namespace fs = std::filesystem;

struct Tot {
    std::atomic<long long> files{0}, bytes{0}, dirs{0};
    std::atomic<int> stop{0};
    std::chrono::steady_clock::time_point deadline;
};

static void walk(const fs::path &root, Tot &t) {
    std::vector<fs::path> st;
    st.push_back(root);
    while (!st.empty()) {
        if (t.stop.load(std::memory_order_relaxed)) return;
        if (std::chrono::steady_clock::now() >= t.deadline) {
            t.stop.store(1, std::memory_order_relaxed);
            return;
        }
        fs::path cur = st.back();
        st.pop_back();
        std::error_code ec;
        fs::directory_iterator it(cur, fs::directory_options::skip_permission_denied, ec);
        if (ec) continue;
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) break;
            fs::path p = it->path();
            struct stat sb;
            if (lstat(p.c_str(), &sb) != 0) continue;
            if (S_ISDIR(sb.st_mode)) {
                t.dirs.fetch_add(1, std::memory_order_relaxed);
                st.push_back(p);
            } else {
                t.files.fetch_add(1, std::memory_order_relaxed);
                t.bytes.fetch_add((long long)sb.st_size, std::memory_order_relaxed);
            }
        }
    }
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "/";
    int seconds = argc > 2 ? std::atoi(argv[2]) : 30;
    if (seconds < 0) seconds = 0;
    Tot t;
    t.dirs.store(1);
    t.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    std::vector<fs::path> kids;
    std::error_code ec;
    fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        std::fprintf(stderr, "cannot read %s\n", root);
        return 1;
    }
    for (; it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        struct stat sb;
        if (lstat(it->path().c_str(), &sb) != 0) continue;
        if (S_ISDIR(sb.st_mode)) {
            t.dirs.fetch_add(1, std::memory_order_relaxed);
            kids.push_back(it->path());
        } else {
            t.files.fetch_add(1, std::memory_order_relaxed);
            t.bytes.fetch_add((long long)sb.st_size, std::memory_order_relaxed);
        }
    }
    std::vector<std::thread> th;
    for (int lane = 0; lane < 4; lane++) {
        th.emplace_back([&, lane]() {
            for (size_t i = lane; i < kids.size(); i += 4) walk(kids[i], t);
        });
    }
    for (auto &h : th) h.join();
    std::printf("%lld\n%lld\n", t.files.load(), t.bytes.load());
    return 0;
}
