// Ramer-Douglas-Peucker on the butterfly
// x = sin(t) * (exp(cos(t)) - 2*cos(4t) - sin(t/12)^5)
// y = cos(t) * the same core
// t = 0, span/360, ... 12*pi. 361 points. Epsilon 0.5.
// sin, cos, and exp are the same short series in every language, not libm.
#include <cstdio>
#include <vector>

static void plot(const char *path, const double *xs, const double *ys, const int *keep, int n) {
    const int W = 640, H = 480;
    double minx = xs[0], maxx = xs[0], miny = ys[0], maxy = ys[0];
    for (int i = 1; i < n; i++) {
        if (xs[i] < minx) minx = xs[i];
        if (xs[i] > maxx) maxx = xs[i];
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }
    double dx = maxx - minx, dy = maxy - miny;
    if (dx < 1e-9) dx = 1;
    if (dy < 1e-9) dy = 1;
    std::vector<unsigned char> img(W * H * 3, 8);
    auto px = [&](double x) { return 24 + (int)((x - minx) / dx * (W - 48)); };
    auto py = [&](double y) { return H - 24 - (int)((y - miny) / dy * (H - 48)); };
    auto put = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        int i = (y * W + x) * 3;
        img[i] = r; img[i + 1] = g; img[i + 2] = b;
    };
    auto line = [&](int x0, int y0, int x1, int y1, unsigned char r, unsigned char g, unsigned char b) {
        int dx = x1 - x0, dy = y1 - y0;
        int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        int err = dx - dy, x = x0, y = y0;
        while (true) {
            put(x, y, r, g, b);
            if (x == x1 && y == y1) break;
            int e2 = err * 2;
            if (e2 > -dy) { err -= dy; x += sx; }
            if (e2 < dx) { err += dx; y += sy; }
        }
    };
    for (int i = 1; i < n; i++)
        line(px(xs[i - 1]), py(ys[i - 1]), px(xs[i]), py(ys[i]), 70, 55, 30);
    int prev = -1;
    for (int i = 0; i < n; i++) {
        if (keep[i] != 1) continue;
        int x = px(xs[i]), y = py(ys[i]);
        if (prev >= 0) line(prev & 0xffff, prev >> 16, x, y, 235, 190, 70);
        prev = x | (y << 16);
        for (int oy = -1; oy <= 1; oy++)
            for (int ox = -1; ox <= 1; ox++) put(x + ox, y + oy, 255, 230, 160);
    }
    FILE *o = std::fopen(path, "wb");
    if (!o) {
        std::fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    std::fprintf(o, "P6\n%d %d\n255\n", W, H);
    std::fwrite(img.data(), 1, img.size(), o);
    std::fclose(o);
}

static const double PI = 3.141592653589793;
static const double TW = 6.283185307179586;
static const double HALF = 1.5707963267948966;

static void sc(double x, double *s, double *c) {
    double y = x;
    while (y >= TW) y -= TW;
    while (y < 0.0) y += TW;
    int q = 0;
    while (y >= HALF) {
        y -= HALF;
        q += 1;
    }
    double x2 = y * y;
    double sp = y * (1.0 - x2 / 6.0 * (1.0 - x2 / 20.0 * (1.0 - x2 / 42.0 * (1.0 - x2 / 72.0))));
    double cp = 1.0 - x2 / 2.0 * (1.0 - x2 / 12.0 * (1.0 - x2 / 30.0 * (1.0 - x2 / 56.0)));
    if (q == 0) { *s = sp; *c = cp; return; }
    if (q == 1) { *s = cp; *c = -sp; return; }
    if (q == 2) { *s = -sp; *c = -cp; return; }
    *s = -cp;
    *c = sp;
}

static double ex(double z) {
    double term = 1.0, s = 1.0, k = 1.0;
    for (int i = 0; i < 16; i++) {
        term = term * z / k;
        s = s + term;
        k = k + 1.0;
    }
    return s;
}

int main(int argc, char **argv) {
    const int N = 361;
    double xs[361], ys[361];
    int keep[361];
    double span = 12.0 * PI;
    for (int i = 0; i < N; i++) {
        double t = span * (double)i / 360.0;
        double s, c, s4, c4, s12, c12;
        sc(t, &s, &c);
        sc(4.0 * t, &s4, &c4);
        sc(t / 12.0, &s12, &c12);
        double s5 = s12 * s12 * s12 * s12 * s12;
        double core = ex(c) - 2.0 * c4 - s5;
        xs[i] = s * core;
        ys[i] = c * core;
        keep[i] = 1;
    }
    int sa[800], sb[800], sp = 1;
    sa[0] = 0;
    sb[0] = N - 1;
    double eps2 = 0.25;
    while (sp > 0) {
        sp -= 1;
        int a = sa[sp], b = sb[sp];
        if (b < a + 2) continue;
        double best = -1.0;
        int besti = -1;
        double dx = xs[b] - xs[a], dy = ys[b] - ys[a];
        double den2 = dx * dx + dy * dy;
        for (int i = a + 1; i < b; i++) {
            double num = (ys[a] - ys[i]) * dx - (xs[a] - xs[i]) * dy;
            if (num < 0.0) num = -num;
            double cross2 = num * num;
            if (cross2 > best || (cross2 == best && i > besti)) {
                best = cross2;
                besti = i;
            }
        }
        if (best > eps2 * den2) {
            sa[sp] = besti; sb[sp] = b; sp += 1;
            sa[sp] = a; sb[sp] = besti; sp += 1;
        } else {
            for (int j = a + 1; j < b; j++) keep[j] = 0;
        }
    }
    int kept = 0;
    double sm = 0.0;
    for (int i = 0; i < N; i++) {
        if (keep[i] == 1) {
            kept += 1;
            sm += xs[i] + ys[i];
        }
    }
    std::printf("%d\n%.9f\n", kept, sm);
    if (argc > 1) plot(argv[1], xs, ys, keep, N);
    return 0;
}
