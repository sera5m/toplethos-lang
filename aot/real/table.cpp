// Same four lanes and 64 ticks as table.tpth_s. Lane 0 is added first.
#include <cstdio>

static double lane_sum(int lane) {
    int base = lane * 256;
    double w[256], a[256];
    for (int i = 0; i < 256; i++) {
        w[i] = 1.0;
        a[i] = 0.0;
    }
    for (int t = 0; t < 64; t++) {
        for (int i = 0; i < 256; i++) {
            int id = base + i;
            a[i] = a[i] * 0.97 + (double)id * 0.001;
            w[i] = w[i] + a[i] * 0.0001;
        }
    }
    double s = 0.0;
    for (int i = 0; i < 256; i++) s += w[i];
    return s;
}

int main() {
    double s = 0.0;
    for (int lane = 0; lane < 4; lane++) s += lane_sum(lane);
    std::printf("%.9f\n", s);
    return 0;
}
