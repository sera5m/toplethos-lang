/* Desktop clock and CPU threads for the USE.md host. */
#ifdef __cplusplus
extern "C" {
#endif
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double mark_time;

double tp_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void tp_mark(void) { mark_time = tp_now(); }

double tp_lap(void) { return tp_now() - mark_time; }

typedef double (*tp_fn)(int lane, void *extra);

typedef struct {
    tp_fn fn;
    void *extra;
    int lane;
    double out;
} tp_job;

#define TP_MAX 64
static pthread_t tp_th[TP_MAX];
static tp_job tp_jobs[TP_MAX];
static pthread_mutex_t tp_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tp_cv = PTHREAD_COND_INITIALIZER;
static int tp_n, tp_ready, tp_done, tp_go, tp_live;

static void tp_fail(const char *msg) {
    fprintf(stderr, "failure: %s\n", msg);
    exit(1);
}

static void *tp_entry(void *arg) {
    tp_job *j = (tp_job *)arg;
    pthread_mutex_lock(&tp_mu);
    tp_ready += 1;
    pthread_cond_broadcast(&tp_cv);
    while (!tp_go)
        pthread_cond_wait(&tp_cv, &tp_mu);
    pthread_mutex_unlock(&tp_mu);
    j->out = j->fn(j->lane, j->extra);
    pthread_mutex_lock(&tp_mu);
    tp_done += 1;
    pthread_cond_broadcast(&tp_cv);
    pthread_mutex_unlock(&tp_mu);
    return 0;
}

void tp_fork(int n, tp_fn fn, void *extra) {
    int i;
    if (n < 1 || n > TP_MAX)
        tp_fail("fork takes a constant thread count");
    if (tp_live)
        tp_fail("fork before the previous join");
    tp_n = n;
    tp_ready = 0;
    tp_done = 0;
    tp_go = 0;
    tp_live = 1;
    for (i = 0; i < n; i++) {
        tp_jobs[i].fn = fn;
        tp_jobs[i].extra = extra;
        tp_jobs[i].lane = i;
        tp_jobs[i].out = 0.0;
        if (pthread_create(&tp_th[i], 0, tp_entry, &tp_jobs[i]) != 0)
            tp_fail("fork");
    }
    pthread_mutex_lock(&tp_mu);
    while (tp_ready < n)
        pthread_cond_wait(&tp_cv, &tp_mu);
    pthread_mutex_unlock(&tp_mu);
}

double tp_start(void) {
    int i;
    double s = 0.0;
    if (!tp_live || tp_go)
        tp_fail("start before fork");
    pthread_mutex_lock(&tp_mu);
    tp_go = 1;
    pthread_cond_broadcast(&tp_cv);
    while (tp_done < tp_n)
        pthread_cond_wait(&tp_cv, &tp_mu);
    pthread_mutex_unlock(&tp_mu);
    for (i = 0; i < tp_n; i++)
        s += tp_jobs[i].out;
    return s;
}

void tp_finish(void) {
    int i;
    if (!tp_live || !tp_go || tp_done < tp_n)
        tp_fail("finish before start");
    for (i = 0; i < tp_n; i++)
        pthread_join(tp_th[i], 0);
    tp_live = 0;
}

double tp_run(int n, tp_fn fn, void *extra) {
    double s;
    tp_fork(n, fn, extra);
    s = tp_start();
    tp_finish();
    return s;
}

static uint32_t tp_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t tp_u16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/* 24-bit BI_RGB bitmap into i32 cells. A cell is 16 bytes: the value, then a present flag. */
void tp_loadbmp(const char *path, void *cells, int n) {
    FILE *f;
    unsigned char hdr[54];
    unsigned char *base, *row;
    uint32_t off, comp;
    int32_t w, h;
    int top, stride, y, x;
    uint16_t bpp;
    f = fopen(path, "rb");
    if (!f)
        tp_fail(path);
    if (fread(hdr, 1, 54, f) != 54 || hdr[0] != 'B' || hdr[1] != 'M')
        tp_fail("bitmap");
    off = tp_u32(hdr + 10);
    w = (int32_t)tp_u32(hdr + 18);
    h = (int32_t)tp_u32(hdr + 22);
    bpp = tp_u16(hdr + 28);
    comp = tp_u32(hdr + 30);
    top = h < 0;
    if (top)
        h = -h;
    if (bpp != 24 || comp != 0 || w < 1 || h < 1 || (long long)w * h != n)
        tp_fail("bitmap");
    if (fseek(f, (long)off, SEEK_SET) != 0)
        tp_fail("bitmap");
    stride = (w * 3 + 3) & ~3;
    row = (unsigned char *)malloc((size_t)stride);
    if (!row)
        tp_fail("bitmap");
    base = (unsigned char *)cells;
    for (y = 0; y < h; y++) {
        int desty = top ? y : h - 1 - y;
        if (fread(row, 1, (size_t)stride, f) != (size_t)stride)
            tp_fail("bitmap");
        for (x = 0; x < w; x++) {
            unsigned char *cell = base + ((size_t)(desty * w + x) << 4);
            int b = row[x * 3], g = row[x * 3 + 1], r = row[x * 3 + 2];
            int32_t p = (r << 16) | (g << 8) | b;
            *(int64_t *)cell = p;
            cell[8] = 1;
        }
    }
    free(row);
    fclose(f);
}
#ifdef __cplusplus
}
#endif

