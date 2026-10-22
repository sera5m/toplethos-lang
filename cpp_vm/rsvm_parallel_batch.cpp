#include "rsvm_parallel_batch.h"
#include <string.h>
#include <stdio.h>
#include <thread>
#include <vector>
#include <atomic>

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

// Demo kernel: square the first input slot value, write to out_slot.
// In a real system these would be buffer indices; here we treat in_slots[0]
// as the raw value for the test.
static int demo_square(const rsvm_pbatch_item_t* item, void*) {
    int32_t v = item->in_slots[0];
    int32_t r = v * v;
    // In real use we would write r into the VM slot item->out_slot.
    // For the standalone test we just print.
    printf("square(%d) = %d\n", (int)v, (int)r);
    return 0;
}

int rsvm_pbatch_flush(rsvm_pbatch_queue_t* q, rsvm_pbatch_host_fn host, void* user) {
    if (!q || q->count == 0) return 0;
    rsvm_pbatch_host_fn fn = host ? host : demo_square;
    int nthreads = q->thread_hint > 0 ? q->thread_hint : 1;
    if (nthreads > q->count) nthreads = q->count;

    std::atomic<int> executed{0};
    std::vector<std::thread> threads;
    threads.reserve(nthreads);

    int32_t total = q->count;
    for (int t = 0; t < nthreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int32_t i = t; i < total; i += nthreads) {
                if (fn(&q->items[i], user) == 0)
                    executed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& th : threads) th.join();

    q->count = 0;
    return executed.load();
}

int rsvm_pbatch_maybe_flush(rsvm_pbatch_queue_t* q, rsvm_pbatch_host_fn host, void* user) {
    if (!q) return 0;
    if (q->fill_trigger > 0 && q->count >= q->fill_trigger)
        return rsvm_pbatch_flush(q, host, user);
    return 0;
}
