// 3x3 box of the frame. Not an FFT. One input buffer and one output buffer.
// The 160x120 frame is 57 KiB. It does not need 30 GiB, and it is not tiled.
//   blur <file.rgb> <w> <h> [out.ppm]
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "blur <file.rgb> <w> <h> [out.ppm]\n");
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
    std::vector<int> pix(w * h), out(w * h);
    for (int i = 0; i < w * h; i++)
        pix[i] = (raw[i * 3] << 16) | (raw[i * 3 + 1] << 8) | raw[i * 3 + 2];
    long long sum = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int rs = 0, gs = 0, bs = 0, c = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                    int p = pix[yy * w + xx];
                    int r = p / 65536;
                    int g = p / 256 - r * 256;
                    int b = p - (p / 256) * 256;
                    rs += r; gs += g; bs += b; c += 1;
                }
            }
            int r = rs / c, g = gs / c, b = bs / c;
            out[y * w + x] = (r << 16) | (g << 8) | b;
            sum += out[y * w + x];
        }
    }
    if (argc > 4) {
        FILE *o = std::fopen(argv[4], "wb");
        if (!o) {
            std::fprintf(stderr, "cannot write %s\n", argv[4]);
            return 1;
        }
        std::fprintf(o, "P6\n%d %d\n255\n", w, h);
        std::vector<unsigned char> rgb(out.size() * 3);
        for (size_t i = 0; i < out.size(); i++) {
            rgb[i * 3] = (unsigned char)(out[i] / 65536);
            rgb[i * 3 + 1] = (unsigned char)(out[i] / 256 - (out[i] / 65536) * 256);
            rgb[i * 3 + 2] = (unsigned char)(out[i] - (out[i] / 256) * 256);
        }
        std::fwrite(rgb.data(), 1, rgb.size(), o);
        std::fclose(o);
    }
    std::printf("%lld\n", sum);
    return 0;
}
