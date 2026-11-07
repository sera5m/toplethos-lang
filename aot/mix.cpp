// Same mixed-ops kernel as mix.tpth_s. N = 1000000.
// g++ -O2 -ffp-contract=off -o /tmp/mix_cpp aot/mix.cpp
#include <cstdint>
#include <cstdio>
#include <chrono>

static constexpr int N = 1000000;

struct Mix {
    int bias;
    double scale;
};

__attribute__((noinline))
static int stew(int a, int b) {
    int n = a;
    for (int i = 0; i < 4; ++i) {
        n = (n + b + i) & 255;
        int sh = i & 3;
        n = n ^ (b >> sh);
    }
    return n;
}

__attribute__((noinline))
static double spice(double x, int k) {
    double y = x;
    for (int i = 0; i < 4; ++i) {
        double t = static_cast<double>(i + k);
        y = (y * 1.25 + t) / (2.0 + static_cast<double>(i));
        y = y - t * 0.125;
    }
    return y;
}

int main() {
    int tap[8] = {3, 5, 7, 11, 13, 17, 19, 23};
    Mix m;
    m.bias = 9;
    m.scale = 0.015625;
    double s = 0.0;
    int a = 1;
    int b = 7;
    auto t0 = std::chrono::steady_clock::now();
    s = 0.0;
    for (int i = 0; i < N; ++i) {
        int k = i & 7;
        int t = tap[k];
        a = a + t + m.bias;
        a = a - (a / 251) * 251;
        b = ((b * 3) + t) & 255;
        int sh = b & 3;
        int c = (a << sh) & 511;
        c = c ^ (b | t);
        c = c % 97;
        c = ~c & 127;
        a = stew(c, b);
        double x = static_cast<double>(a) * m.scale;
        double y = spice(x, t);
        int odd = c & 1;
        if (odd == 0) y = y + x;
        else y = y - x * 0.5;
        s = s + y;
    }
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("mix %.6f %.9f\n", sec, s);
    return 0;
}
