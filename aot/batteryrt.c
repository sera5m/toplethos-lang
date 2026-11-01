/* Timing harness for the shared rows of the battery. The work stays in the
   program. This only reads a clock and prints one row. */
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif
