#pragma once
// Parallel function batch queue for desktop Toplethos backend.
// Neurons/sectors enqueue calls with per-item parameters; flush runs them
// as one data-parallel batch (CPU thread pool now, GPU grid later).

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RSVM_PBATCH_MAX_ITEMS  4096
#define RSVM_PBATCH_MAX_QUEUES 8

typedef struct {
    int32_t  fn_id;          // interned function / nid
    int32_t  attr_word;      // dtype, mode, flags packed
    int32_t  n_in;
    int32_t  in_slots[8];    // slot indices or buffer ids (caller-defined)
    int32_t  out_slot;
} rsvm_pbatch_item_t;

typedef struct {
    rsvm_pbatch_item_t items[RSVM_PBATCH_MAX_ITEMS];
    int32_t            count;
    int32_t            capacity;
    int32_t            fill_trigger;   // flush when count >= this (0 = manual only)
    int32_t            thread_hint;    // desired parallelism
} rsvm_pbatch_queue_t;

// Lifecycle
void rsvm_pbatch_init(rsvm_pbatch_queue_t* q, int32_t fill_trigger, int32_t thread_hint);
void rsvm_pbatch_reset(rsvm_pbatch_queue_t* q);

// Enqueue one call with its own parameters. Returns 0 on success, -1 if full.
int  rsvm_pbatch_enqueue(rsvm_pbatch_queue_t* q, const rsvm_pbatch_item_t* item);

// Flush: run all queued items as a batch. Returns number executed.
// host_fn is called once per item (or the implementation may batch them).
typedef int (*rsvm_pbatch_host_fn)(const rsvm_pbatch_item_t* item, void* user);
int  rsvm_pbatch_flush(rsvm_pbatch_queue_t* q, rsvm_pbatch_host_fn host, void* user);

// Auto-flush check (call after enqueue if you want fill-level triggering).
int  rsvm_pbatch_maybe_flush(rsvm_pbatch_queue_t* q, rsvm_pbatch_host_fn host, void* user);

#ifdef __cplusplus
}
#endif
