#include "rsvm_parallel_batch.h"
#include <string.h>
#include <stdio.h>

void rsvm_pbatch_init(rsvm_pbatch_queue_t* q, int32_t fill_trigger, int32_t thread_hint) {
    if (!q) return;
    memset(q, 0, sizeof(*q));
    q->capacity     = RSVM_PBATCH_MAX_ITEMS;
    q->fill_trigger = fill_trigger > 0 ? fill_trigger : 0;
    q->thread_hint  = thread_hint > 0 ? thread_hint : 1;
}

void rsvm_pbatch_reset(rsvm_pbatch_queue_t* q) {
    if (!q) return;
    q->count = 0;
}

int rsvm_pbatch_enqueue(rsvm_pbatch_queue_t* q, const rsvm_pbatch_item_t* item) {
    if (!q || !item || q->count >= q->capacity) return -1;
    q->items[q->count++] = *item;
    return 0;
}

int rsvm_pbatch_flush(rsvm_pbatch_queue_t* q, rsvm_pbatch_host_fn host, void* user) {
    if (!q || !host || q->count == 0) return 0;
    int executed = 0;
    // Simple sequential dispatch for now. Replace body with thread-pool
    // or GPU grid launch that consumes q->items[0..count).
    // thread_hint is available for the real scheduler.
    for (int32_t i = 0; i < q->count; ++i) {
        if (host(&q->items[i], user) == 0)
            ++executed;
    }
    int32_t n = q->count;
    q->count = 0;
    return executed;
}

int rsvm_pbatch_maybe_flush(rsvm_pbatch_queue_t* q, rsvm_pbatch_host_fn host, void* user) {
    if (!q) return 0;
    if (q->fill_trigger > 0 && q->count >= q->fill_trigger)
        return rsvm_pbatch_flush(q, host, user);
    return 0;
}
