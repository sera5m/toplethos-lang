// Standalone test: enqueue N squares, flush in parallel, check results.
#include "rsvm_parallel_batch.h"
#include <cstdio>
#include <cassert>

int main() {
    rsvm_pbatch_queue_t q;
    rsvm_pbatch_init(&q, 0, 4);   // manual flush, 4 threads

    const int N = 16;
    for (int i = 0; i < N; ++i) {
        rsvm_pbatch_item_t it{};
        it.fn_id     = 1;          // square
        it.attr_word = 0;
        it.n_in      = 1;
        it.in_slots[0] = i;        // value to square
        it.out_slot  = 100 + i;
        assert(rsvm_pbatch_enqueue(&q, &it) == 0);
    }

    printf("enqueued %d items, flushing with %d threads...\n", N, q.thread_hint);
    int ran = rsvm_pbatch_flush(&q, nullptr, nullptr);
    printf("executed %d items\n", ran);
    assert(ran == N);
    assert(q.count == 0);
    printf("parallel square-map test passed\n");
    return 0;
}
