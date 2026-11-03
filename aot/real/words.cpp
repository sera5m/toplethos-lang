// Words in lipsum-cicero.txt. Same open-address count as words.tpth_s.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

static int32_t mix(int32_t h, int32_t c) {
    uint32_t x = ((uint32_t)h ^ (uint32_t)c) * 16777619u;
    return (int32_t)x;
}

int main() {
    const int T = 32768;
    std::vector<unsigned char> raw(140000);
    FILE *f = std::fopen("lipsum-cicero.txt", "rb");
    if (!f) return 1;
    int n = (int)std::fread(raw.data(), 1, raw.size(), f);
    int extra = std::fgetc(f);
    std::fclose(f);
    if (extra != EOF) return 1;
    std::vector<int32_t> off(T), span(T), cnt(T), hsh(T);
    for (int rep = 0; rep < 50; rep++) {
        std::fill(off.begin(), off.end(), 0);
        std::fill(span.begin(), span.end(), 0);
        std::fill(cnt.begin(), cnt.end(), 0);
        std::fill(hsh.begin(), hsh.end(), 0);
        int i = 0;
        while (i < n) {
        int c = raw[i];
        if (c >= 65 && c <= 90) c += 32;
        int letter = (c >= 97 && c <= 122);
        if (!letter) {
            i += 1;
            continue;
        }
        int beg = i;
        int32_t h = 0;
        while (i < n) {
            c = raw[i];
            if (c >= 65 && c <= 90) c += 32;
            letter = (c >= 97 && c <= 122);
            if (!letter) break;
            h = mix(h, c);
            i += 1;
        }
        int wlen = i - beg;
        int slot = (int)(h & 32767);
        while (true) {
            if (span[slot] == 0) {
                off[slot] = beg;
                span[slot] = wlen;
                cnt[slot] = 1;
                hsh[slot] = h;
                break;
            }
            if (hsh[slot] == h && span[slot] == wlen) {
                int same = 1;
                for (int k = 0; k < wlen; k++) {
                    int a = raw[off[slot] + k];
                    int b = raw[beg + k];
                    if (a >= 65 && a <= 90) a += 32;
                    if (b >= 65 && b <= 90) b += 32;
                    if (a != b) { same = 0; break; }
                }
                if (same) {
                    cnt[slot] += 1;
                    break;
                }
            }
            slot += 1;
            if (slot == T) slot = 0;
        }
    }
    }
    long long sum = 0;
    for (int s = 0; s < T; s++) {
        if (span[s] != 0)
            sum += (long long)hsh[s] * cnt[s] + span[s];
    }
    std::printf("%lld\n", sum);
    return 0;
}
