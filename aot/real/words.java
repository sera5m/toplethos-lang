// Words in lipsum-cicero.txt. Same open-address count as words.cpp.
// Default run is one process of 50 passes, then the checksum.
// --kernel warms up, then prints the best of seven 50-pass kernels.
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;

public final class words {
    static int mix(int h, int c) {
        return (h ^ c) * 16777619;
    }

    static void pass(byte[] raw, int n, int[] off, int[] span, int[] cnt, int[] hsh) {
        final int T = off.length;
        Arrays.fill(off, 0);
        Arrays.fill(span, 0);
        Arrays.fill(cnt, 0);
        Arrays.fill(hsh, 0);
        int i = 0;
        while (i < n) {
            int c = raw[i] & 255;
            if (c >= 65 && c <= 90) c += 32;
            int letter = (c >= 97 && c <= 122) ? 1 : 0;
            if (letter == 0) {
                i += 1;
                continue;
            }
            int beg = i;
            int h = 0;
            while (i < n) {
                c = raw[i] & 255;
                if (c >= 65 && c <= 90) c += 32;
                letter = (c >= 97 && c <= 122) ? 1 : 0;
                if (letter == 0) break;
                h = mix(h, c);
                i += 1;
            }
            int wlen = i - beg;
            int slot = h & 32767;
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
                    int base = off[slot];
                    for (int k = 0; k < wlen; k++) {
                        int a = raw[base + k] & 255;
                        int b = raw[beg + k] & 255;
                        if (a >= 65 && a <= 90) a += 32;
                        if (b >= 65 && b <= 90) b += 32;
                        if (a != b) { same = 0; break; }
                    }
                    if (same == 1) {
                        cnt[slot] += 1;
                        break;
                    }
                }
                slot += 1;
                if (slot == T) slot = 0;
            }
        }
    }

    static long checksum(int[] span, int[] cnt, int[] hsh) {
        long sum = 0;
        for (int s = 0; s < span.length; s++) {
            if (span[s] != 0)
                sum += (long) hsh[s] * cnt[s] + span[s];
        }
        return sum;
    }

    static void fifty(byte[] raw, int n, int[] off, int[] span, int[] cnt, int[] hsh) {
        for (int rep = 0; rep < 50; rep++)
            pass(raw, n, off, span, cnt, hsh);
    }

    public static void main(String[] args) throws IOException {
        byte[] raw = Files.readAllBytes(Path.of("lipsum-cicero.txt"));
        if (raw.length > 140000) {
            System.err.println("file longer than the array");
            System.exit(1);
        }
        int n = raw.length;
        int[] off = new int[32768];
        int[] span = new int[32768];
        int[] cnt = new int[32768];
        int[] hsh = new int[32768];
        if (args.length > 0 && args[0].equals("--kernel")) {
            long sum = 0;
            for (int w = 0; w < 10; w++) {
                fifty(raw, n, off, span, cnt, hsh);
                sum = checksum(span, cnt, hsh);
            }
            double best = 1e9;
            for (int t = 0; t < 7; t++) {
                long t0 = System.nanoTime();
                fifty(raw, n, off, span, cnt, hsh);
                sum = checksum(span, cnt, hsh);
                double dt = (System.nanoTime() - t0) / 1e9;
                if (dt < best) best = dt;
            }
            System.out.printf("%d%n%.4f%n", sum, best);
            return;
        }
        fifty(raw, n, off, span, cnt, hsh);
        System.out.println(checksum(span, cnt, hsh));
    }
}
