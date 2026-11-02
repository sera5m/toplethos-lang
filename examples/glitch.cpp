// The photo is examples/street.bmp. 24-bit, uncompressed.
// Sort by luminance, paint rings around the lamp, tear channels, shift every 17th row.
//   glitch <file.bmp> [out.ppm]
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

static int load_bmp(const char *path, std::vector<int> &pix, int &w, int &h) {
    FILE *f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    unsigned char hdr[54];
    if (std::fread(hdr, 1, 54, f) != 54 || hdr[0] != 'B' || hdr[1] != 'M') {
        std::fprintf(stderr, "bitmap\n");
        std::fclose(f);
        return 1;
    }
    auto u32 = [](unsigned char *p) {
        return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
    };
    auto u16 = [](unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); };
    unsigned off = u32(hdr + 10);
    int ww = (int)u32(hdr + 18);
    int hh = (int)u32(hdr + 22);
    int top = hh < 0;
    if (top) hh = -hh;
    if (u16(hdr + 28) != 24 || u32(hdr + 30) != 0 || ww < 1 || hh < 1) {
        std::fprintf(stderr, "bitmap\n");
        std::fclose(f);
        return 1;
    }
    if (std::fseek(f, (long)off, SEEK_SET) != 0) {
        std::fprintf(stderr, "bitmap\n");
        std::fclose(f);
        return 1;
    }
    int stride = (ww * 3 + 3) & ~3;
    std::vector<unsigned char> row(stride);
    pix.assign(ww * hh, 0);
    for (int y = 0; y < hh; y++) {
        if (std::fread(row.data(), 1, stride, f) != (size_t)stride) {
            std::fprintf(stderr, "bitmap\n");
            std::fclose(f);
            return 1;
        }
        int desty = top ? y : hh - 1 - y;
        for (int x = 0; x < ww; x++) {
            int b = row[x * 3], g = row[x * 3 + 1], r = row[x * 3 + 2];
            pix[desty * ww + x] = (r << 16) | (g << 8) | b;
        }
    }
    std::fclose(f);
    w = ww;
    h = hh;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "glitch <file.bmp> [out.ppm]\n");
        return 1;
    }
    int w = 0, h = 0;
    std::vector<int> pix;
    if (load_bmp(argv[1], pix, w, h)) return 1;
    std::vector<unsigned char> ppm;
    long long sum = run(pix, w, h, argc > 2 ? &ppm : nullptr);
    if (argc > 2) {
        FILE *o = std::fopen(argv[2], "wb");
        if (!o) {
            std::fprintf(stderr, "cannot write %s\n", argv[2]);
            return 1;
        }
        std::fprintf(o, "P6\n%d %d\n255\n", w, h);
        std::fwrite(ppm.data(), 1, ppm.size(), o);
        std::fclose(o);
    }
    std::printf("%lld\n", sum);
    return 0;
}
