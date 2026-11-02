// Open-address counts. Keys are (i * 17 + 3) mod 500, twenty thousand of them.
// This is the dictionary row that does not need a text file.
#include <cstdio>

int main() {
    int key[1024];
    int cnt[1024];
    for (int i = 0; i < 1024; i++) {
        key[i] = -1;
        cnt[i] = 0;
    }
    for (int i = 0; i < 20000; i++) {
        int k = i * 17 + 3;
        k = k - (k / 500) * 500;
        int slot = k;
        while (true) {
            if (key[slot] == -1) {
                key[slot] = k;
                cnt[slot] = 1;
                break;
            }
            if (key[slot] == k) {
                cnt[slot] = cnt[slot] + 1;
                break;
            }
            slot = slot + 1;
            if (slot == 1024) slot = 0;
        }
    }
    long long sum = 0;
    for (int i = 0; i < 1024; i++) {
        if (key[i] != -1) sum += (long long)key[i] * cnt[i];
    }
    std::printf("%lld\n", sum);
    return 0;
}
