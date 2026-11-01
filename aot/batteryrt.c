/* Clock and the thread row for the compiled battery.
   Init is thread creation until every worker is waiting.
   Work starts only after that. Join is measured on its own. */
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

extern double horner(double);

static struct timespec tp_t0;
static const char *tp_name;

void tp_begin(const char *name) {
    tp_name = name;
    clock_gettime(CLOCK_MONOTONIC, &tp_t0);
}

void tp_end(double sum) {
    struct timespec t1;
    double sec;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    sec = (double)(t1.tv_sec - tp_t0.tv_sec) + (double)(t1.tv_nsec - tp_t0.tv_nsec) * 1e-9;
    printf("%s %.6f %.9f\n", tp_name, sec, sum);
}

enum { TP_WORKERS = 4 };

struct TpSlot {
    int lo, hi;
    double sum;
    pthread_t th;
};

static pthread_mutex_t tp_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tp_cv = PTHREAD_COND_INITIALIZER;
static int tp_ready, tp_go, tp_done;
static struct TpSlot tp_slots[TP_WORKERS];

static double tp_since(struct timespec t0) {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
}

static void *tp_worker(void *arg) {
    struct TpSlot *s = (struct TpSlot *)arg;
    double acc = 0.0;
    int i;
    pthread_mutex_lock(&tp_mu);
    tp_ready += 1;
    pthread_cond_broadcast(&tp_cv);
    while (!tp_go) pthread_cond_wait(&tp_cv, &tp_mu);
    pthread_mutex_unlock(&tp_mu);
    for (i = s->lo; i < s->hi; i++) acc += horner((double)i * 0.000001);
    s->sum = acc;
    pthread_mutex_lock(&tp_mu);
    tp_done += 1;
    pthread_cond_broadcast(&tp_cv);
    pthread_mutex_unlock(&tp_mu);
    return NULL;
}

void tp_threads(int n) {
    struct timespec t0;
    int chunk = n / TP_WORKERS;
    int k;
    double sum = 0.0;
    tp_ready = 0;
    tp_go = 0;
    tp_done = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (k = 0; k < TP_WORKERS; k++) {
        tp_slots[k].lo = k * chunk;
        tp_slots[k].hi = (k == TP_WORKERS - 1) ? n : (k + 1) * chunk;
        tp_slots[k].sum = 0.0;
        pthread_create(&tp_slots[k].th, NULL, tp_worker, &tp_slots[k]);
    }
    pthread_mutex_lock(&tp_mu);
    while (tp_ready < TP_WORKERS) pthread_cond_wait(&tp_cv, &tp_mu);
    pthread_mutex_unlock(&tp_mu);
    printf("threads_init %.6f %.9f\n", tp_since(t0), 0.0);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_mutex_lock(&tp_mu);
    tp_go = 1;
    pthread_cond_broadcast(&tp_cv);
    while (tp_done < TP_WORKERS) pthread_cond_wait(&tp_cv, &tp_mu);
    pthread_mutex_unlock(&tp_mu);
    for (k = 0; k < TP_WORKERS; k++) sum += tp_slots[k].sum;
    printf("threads_work %.6f %.9f\n", tp_since(t0), sum);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (k = 0; k < TP_WORKERS; k++) pthread_join(tp_slots[k].th, NULL);
    printf("threads_join %.6f %.9f\n", tp_since(t0), sum);
}

#ifdef __cplusplus
}
#endif
