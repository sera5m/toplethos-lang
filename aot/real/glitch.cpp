// Sort the frame by luminance, then paint concentric rings around the lamp.
// A ring also tears the channels, and every 17th row shifts. Integer math only.
// The blur is a separate program: this host has no FFT, and one frame fits.
//   glitch <file.rgb> <w> <h> [out.ppm]
#include <cstdio>
#include <cstdlib>
#include <vector>

static int lum(int p) {
    int r = p / 65536;
    int g = p / 256 - r * 256;
    int b = p - (p / 256) * 256;
    return r + r + g * 5 + b;
}

static int iatan2(int y, int x) {
    int ax = x < 0 ? -x : x;
    int ay = y < 0 ? -y : y;
    int a;
    if (ax == 0 && ay == 0) return 0;
    if (ax > ay) a = ay * 32 / ax;
    else a = 64 - ax * 32 / ay;
    if (x >= 0 && y >= 0) return a;
    if (x < 0 && y >= 0) return 128 - a;
    if (x < 0 && y < 0) return 128 + a;
    if (a == 0) return 0;
    return 256 - a;
}

static bool less_pix(int a, int b) {
    int la = lum(a), lb = lum(b);
    if (la != lb) return la < lb;
    return a < b;
}

static void sift(std::vector<int> &a, int root, int end) {
    while (true) {
        int child = root * 2 + 1;
        if (child > end) return;
        if (child + 1 <= end && less_pix(a[child], a[child + 1])) child = child + 1;
        if (less_pix(a[root], a[child])) {
            int t = a[root];
            a[root] = a[child];
            a[child] = t;
            root = child;
        } else return;
    }
}

static void heapsort(std::vector<int> &a) {
    int n = (int)a.size();
    for (int start = (n - 2) / 2; start >= 0; start--) sift(a, start, n - 1);
    for (int end = n - 1; end > 0; end--) {
        int t = a[0];
        a[0] = a[end];
        a[end] = t;
        sift(a, 0, end - 1);
    }
}

static long long run(std::vector<int> &pix, int w, int h, std::vector<unsigned char> *ppm) {
    heapsort(pix);
    int n = w * h;
    std::vector<int> out(n);
    int cx = w / 2, cy = h / 2;
    int maxr = 1;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int dx = x - cx, dy = y - cy;
            int ax = dx < 0 ? -dx : dx;
            int ay = dy < 0 ? -dy : dy;
            int rad = ax > ay ? ax + ay / 2 : ay + ax / 2;
            if (rad > maxr) maxr = rad;
        }
    }
    int lo = lum(pix[0]);
    int hi = lum(pix[n - 1]);
    if (hi <= lo) hi = lo + 1;
    int span = hi - lo;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int dx = x - cx, dy = y - cy;
            int ax = dx < 0 ? -dx : dx;
            int ay = dy < 0 ? -dy : dy;
            int rad = ax > ay ? ax + ay / 2 : ay + ax / 2;
            int ang = iatan2(dy, dx);
            int target = hi - rad * span / maxr;
            target = target - ang * span / 2048;
            if (target < lo) target = lo;
            if (target > hi) target = hi;
            int a = 0, right = n - 1;
            while (a < right) {
                int mid = (a + right + 1) / 2;
                if (lum(pix[mid]) <= target) a = mid;
                else right = mid - 1;
            }
            int p = pix[a];
            int r = p / 65536;
            int g = p / 256 - r * 256;
            int b = p - (p / 256) * 256;
            int m = rad - (rad / 3) * 3;
            if (m == 0) {
                int t = r; r = b; b = t;
            } else if (m == 1) {
                int t = r; r = g; g = b; b = t;
            }
            out[y * w + x] = (r << 16) | (g << 8) | b;
        }
    }
    std::vector<int> row(w);
    for (int y = 0; y < h; y++) {
        if (y - (y / 17) * 17 != 0) continue;
        int shift = y - (y / 5) * 5 - 2;
        for (int x = 0; x < w; x++) {
            int srcx = x - shift;
            while (srcx < 0) srcx += w;
            while (srcx >= w) srcx -= w;
            row[x] = out[y * w + srcx];
        }
        for (int x = 0; x < w; x++) out[y * w + x] = row[x];
    }
    long long sum = 0;
    for (int p : out) sum += p;
    if (ppm) {
        ppm->resize(n * 3);
        for (int i = 0; i < n; i++) {
            (*ppm)[i * 3] = (unsigned char)(out[i] / 65536);
            (*ppm)[i * 3 + 1] = (unsigned char)(out[i] / 256 - (out[i] / 65536) * 256);
            (*ppm)[i * 3 + 2] = (unsigned char)(out[i] - (out[i] / 256) * 256);
        }
    }
    return sum;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "glitch <file.rgb> <w> <h> [out.ppm]\n");
        return 1;
    }
    int w = std::atoi(argv[2]), h = std::atoi(argv[3]);
    FILE *f = std::fopen(argv[1], "rb");
    if (!f) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    std::vector<unsigned char> raw(w * h * 3);
    if (std::fread(raw.data(), 1, raw.size(), f) != raw.size()) {
        std::fprintf(stderr, "short read\n");
        return 1;
    }
    std::fclose(f);
    std::vector<int> pix(w * h);
    for (int i = 0; i < w * h; i++)
        pix[i] = (raw[i * 3] << 16) | (raw[i * 3 + 1] << 8) | raw[i * 3 + 2];
    std::vector<unsigned char> ppm;
    long long sum = run(pix, w, h, argc > 4 ? &ppm : nullptr);
    if (argc > 4) {
        FILE *o = std::fopen(argv[4], "wb");
        if (!o) {
            std::fprintf(stderr, "cannot write %s\n", argv[4]);
            return 1;
        }
        std::fprintf(o, "P6\n%d %d\n255\n", w, h);
        std::fwrite(ppm.data(), 1, ppm.size(), o);
        std::fclose(o);
    }
    std::printf("%lld\n", sum);
    return 0;
}
