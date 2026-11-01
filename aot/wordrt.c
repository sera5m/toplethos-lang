/* Runtime for the word-count slice. The scan loop is compiled from Toplethos.
   This file is the map and the file read, the same role as the C++ and Rust
   standard libraries. A word key is the slice, with A-Z folded to a-z. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *p;
    long n;
} TpStr;

typedef struct {
    char *p;
    uint32_t n;
    uint32_t hash;
    int32_t count;
} Ent;

typedef struct {
    Ent *e;
    uint32_t cap;
    uint32_t live;
    uint32_t *order;
    uint32_t order_n;
    uint32_t order_cap;
} Map;

TpStr tp_readfile(const char *path) {
    FILE *f = fopen(path, "rb");
    TpStr s;
    long n;
    char *p;
    size_t got;
    if (!f) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(1);
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(1);
    }
    n = ftell(f);
    if (n < 0) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(1);
    }
    rewind(f);
    p = (char *)malloc((size_t)n + 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    got = fread(p, 1, (size_t)n, f);
    fclose(f);
    p[got] = 0;
    s.p = p;
    s.n = (long)got;
    return s;
}

static uint32_t fnv(const char *s, uint32_t n) {
    uint32_t h = 2166136261u;
    uint32_t i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

static Map *grow(Map *m) {
    uint32_t ncap = m->cap << 1;
    Ent *n = (Ent *)calloc(ncap, sizeof(Ent));
    char **keys = (char **)malloc(m->order_n * sizeof(char *));
    uint32_t mask = ncap - 1;
    uint32_t i;
    if (!n || (m->order_n && !keys)) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    for (i = 0; i < m->order_n; i++) keys[i] = m->e[m->order[i]].p;
    for (i = 0; i < m->cap; i++) {
        uint32_t j;
        if (!m->e[i].p) continue;
        j = m->e[i].hash & mask;
        while (n[j].p) j = (j + 1) & mask;
        n[j] = m->e[i];
    }
    free(m->e);
    m->e = n;
    m->cap = ncap;
    for (i = 0; i < m->order_n; i++) {
        uint32_t j;
        int found = 0;
        for (j = 0; j < m->cap; j++) {
            if (m->e[j].p == keys[i]) {
                m->order[i] = j;
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "map grow lost a key\n");
            exit(1);
        }
    }
    free(keys);
    return m;
}

void *tp_map_new(void) {
    Map *m = (Map *)calloc(1, sizeof(Map));
    if (!m) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    m->cap = 1024;
    m->e = (Ent *)calloc(m->cap, sizeof(Ent));
    m->order_cap = 256;
    m->order = (uint32_t *)calloc(m->order_cap, sizeof(uint32_t));
    if (!m->e || !m->order) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return m;
}

void tp_map_add(void *vp, const char *base, long start, long end) {
    Map *m = (Map *)vp;
    long n;
    char *copy;
    long i;
    uint32_t h, mask, slot;
    int found;
    if (end <= start) return;
    n = end - start;
    if (n > 0x7fffffffL) {
        fprintf(stderr, "word too long\n");
        exit(1);
    }
    copy = (char *)malloc((size_t)n + 1);
    if (!copy) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)base[start + i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        copy[i] = (char)c;
    }
    copy[n] = 0;
    if ((m->live + 1) * 10 >= m->cap * 7) m = grow(m);
    h = fnv(copy, (uint32_t)n);
    mask = m->cap - 1;
    slot = h & mask;
    found = 0;
    for (;;) {
        Ent *e = &m->e[slot];
        if (!e->p) break;
        if (e->hash == h && e->n == (uint32_t)n && memcmp(e->p, copy, (size_t)n) == 0) {
            e->count += 1;
            free(copy);
            found = 1;
            break;
        }
        slot = (slot + 1) & mask;
    }
    if (found) return;
    m->e[slot].p = copy;
    m->e[slot].n = (uint32_t)n;
    m->e[slot].hash = h;
    m->e[slot].count = 1;
    m->live += 1;
    if (m->order_n == m->order_cap) {
        uint32_t nc = m->order_cap << 1;
        uint32_t *no = (uint32_t *)realloc(m->order, nc * sizeof(uint32_t));
        if (!no) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        m->order = no;
        m->order_cap = nc;
    }
    m->order[m->order_n++] = slot;
}

void tp_map_print(void *vp) {
    Map *m = (Map *)vp;
    uint32_t i;
    for (i = 0; i < m->order_n; i++) {
        Ent *e = &m->e[m->order[i]];
        printf("%.*s: %d\n", (int)e->n, e->p, e->count);
    }
}

#ifdef __cplusplus
}
#endif

