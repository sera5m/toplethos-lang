// One file. Same Horner polynomial as battery.rs and battery.tpth_s.
// y = 1 + x*(2 + x*(3 + x*(4 + x*(5 + x*(6 + x*(7 + x*8))))))
// x = i * 0.000001, i = 0 .. N-1, N = 2000000.
// direct, call, and object do that sum in the same order.
// checked and raw do it through an array, with and without a bounds test.
// threads and async split the same N across 4 tasks and add the partials in order.
// mutex is a different, smaller job: 4 threads adding under one lock.
//
// g++ -O2 -ffp-contract=off -pthread -o /tmp/battery_cpp aot/battery.cpp
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

constexpr int N = 2000000;
constexpr int T = 4;

__attribute__((noinline)) double horner(double x) {
    double y = 8.0;
    y = 7.0 + x * y;
    y = 6.0 + x * y;
    y = 5.0 + x * y;
    y = 4.0 + x * y;
    y = 3.0 + x * y;
    y = 2.0 + x * y;
    y = 1.0 + x * y;
    return y;
}

struct Poly {
    double c0, c1, c2, c3, c4, c5, c6, c7;
    __attribute__((noinline)) double eval(double x) const {
        double y = c7;
        y = c6 + x * y;
        y = c5 + x * y;
        y = c4 + x * y;
        y = c3 + x * y;
        y = c2 + x * y;
        y = c1 + x * y;
        y = c0 + x * y;
        return y;
    }
};

__attribute__((noinline)) double checked(const double *c, int n, double x) {
    double y = c[7];
    for (int k = 6; k >= 0; --k) {
        if ((unsigned)k >= (unsigned)n) return 0.0;
        y = c[k] + x * y;
    }
    return y;
}

__attribute__((noinline)) double raw(const double *c, double x) {
    double y = c[7];
    y = c[6] + x * y;
    y = c[5] + x * y;
    y = c[4] + x * y;
    y = c[3] + x * y;
    y = c[2] + x * y;
    y = c[1] + x * y;
    y = c[0] + x * y;
    return y;
}

static double sum_range(int lo, int hi) {
    double s = 0.0;
    for (int i = lo; i < hi; ++i) {
        double x = (double)i * 0.000001;
        s += horner(x);
    }
    return s;
}

static void row(const char *name, double sec, double sum) {
    std::printf("%s %.6f %.9f\n", name, sec, sum);
}

int main() {
    using clock_ = std::chrono::steady_clock;
    Poly p{1, 2, 3, 4, 5, 6, 7, 8};
    double coeff[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    auto t = clock_::now();
    double s = 0.0;
    for (int i = 0; i < N; ++i) {
        double x = (double)i * 0.000001;
        double y = 8.0;
        y = 7.0 + x * y;
        y = 6.0 + x * y;
        y = 5.0 + x * y;
        y = 4.0 + x * y;
        y = 3.0 + x * y;
        y = 2.0 + x * y;
        y = 1.0 + x * y;
        s += y;
    }
    row("direct", std::chrono::duration<double>(clock_::now() - t).count(), s);

    t = clock_::now();
    s = 0.0;
    for (int i = 0; i < N; ++i) s += horner((double)i * 0.000001);
    row("call", std::chrono::duration<double>(clock_::now() - t).count(), s);

    t = clock_::now();
    s = 0.0;
    for (int i = 0; i < N; ++i) s += p.eval((double)i * 0.000001);
    row("object", std::chrono::duration<double>(clock_::now() - t).count(), s);

    t = clock_::now();
    s = 0.0;
    for (int i = 0; i < N; ++i) s += checked(coeff, 8, (double)i * 0.000001);
    row("checked", std::chrono::duration<double>(clock_::now() - t).count(), s);

    t = clock_::now();
    s = 0.0;
    for (int i = 0; i < N; ++i) s += raw(coeff, (double)i * 0.000001);
    row("raw", std::chrono::duration<double>(clock_::now() - t).count(), s);

    t = clock_::now();
    std::vector<double> part(T, 0.0);
    std::vector<std::thread> th;
    std::mutex start_gate;
    std::condition_variable wake;
    int ready = 0;
    int finished = 0;
    bool go = false;
    int chunk = N / T;
    for (int k = 0; k < T; ++k) {
        int lo = k * chunk;
        int hi = (k == T - 1) ? N : lo + chunk;
        th.emplace_back([&, k, lo, hi]() {
            {
                std::unique_lock<std::mutex> hold(start_gate);
                ready += 1;
                wake.notify_all();
                wake.wait(hold, [&]() { return go; });
            }
            part[k] = sum_range(lo, hi);
            std::lock_guard<std::mutex> hold(start_gate);
            finished += 1;
            wake.notify_all();
        });
    }
    {
        std::unique_lock<std::mutex> hold(start_gate);
        wake.wait(hold, [&]() { return ready == T; });
    }
    row("threads_init", std::chrono::duration<double>(clock_::now() - t).count(), 0.0);
    t = clock_::now();
    {
        std::lock_guard<std::mutex> hold(start_gate);
        go = true;
    }
    wake.notify_all();
    {
        std::unique_lock<std::mutex> hold(start_gate);
        wake.wait(hold, [&]() { return finished == T; });
    }
    s = 0.0;
    for (double v : part) s += v;
    row("threads_work", std::chrono::duration<double>(clock_::now() - t).count(), s);
    t = clock_::now();
    for (auto &h : th) h.join();
    row("threads_join", std::chrono::duration<double>(clock_::now() - t).count(), s);

    t = clock_::now();
    std::vector<std::future<double>> fut;
    for (int k = 0; k < T; ++k) {
        int lo = k * chunk;
        int hi = (k == T - 1) ? N : lo + chunk;
        fut.push_back(std::async(std::launch::async, sum_range, lo, hi));
    }
    s = 0.0;
    for (auto &f : fut) s += f.get();
    row("async", std::chrono::duration<double>(clock_::now() - t).count(), s);

    t = clock_::now();
    double shared = 0.0;
    std::mutex gate;
    std::vector<std::thread> locked;
    constexpr int M = 50000;
    for (int k = 0; k < T; ++k) {
        locked.emplace_back([&]() {
            for (int i = 0; i < M; ++i) {
                std::lock_guard<std::mutex> hold(gate);
                shared += 1.0;
            }
        });
    }
    for (auto &h : locked) h.join();
    row("mutex", std::chrono::duration<double>(clock_::now() - t).count(), shared);
    return 0;
}
