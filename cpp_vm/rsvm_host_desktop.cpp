#include "rs_vm.hpp"
#include "rs_vm_nseq.h"
#include "rsvm_parallel_batch.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static rsvm_pbatch_queue_t g_pbatch;

static void p_i32(int32_t v, void*) { printf("%d\n", (int)v); }
static void p_str(const char* s, uint8_t len, void*) {
    if (s && len) fwrite(s, 1, len, stdout);
    fputc('\n', stdout);
}
static void p_char(char c, void*) { fputc(c, stdout); }
static void p_delay(uint32_t ms, void*) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000u);
#endif
}

static int pbatch_host(const rsvm_pbatch_item_t* item, void*) {
    // Placeholder: real implementation dispatches to polymorphic kernel
    // or GPU grid using item->fn_id + item->attr_word + slices.
    fprintf(stderr, "[pbatch fn=%d attr=0x%x out=%d]\n",
            (int)item->fn_id, (unsigned)item->attr_word, (int)item->out_slot);
    return 0;
}

static int p_native(const char* name, const int32_t* args, int nargs,
                    int32_t* out, void*) {
    if (out) *out = 0;
    if (!name) return 0;

    // @load_into_parallel_exec_queue  (enqueue one item)
    // args: fn_id, attr_word, n_in, in0..inN, out_slot
    if (strcmp(name, "pbatch_enqueue") == 0 && nargs >= 4) {
        rsvm_pbatch_item_t it;
        memset(&it, 0, sizeof it);
        it.fn_id     = args[0];
        it.attr_word = args[1];
        it.n_in      = args[2];
        int ncopy = it.n_in > 8 ? 8 : it.n_in;
        for (int i = 0; i < ncopy && (3 + i) < nargs - 1; ++i)
            it.in_slots[i] = args[3 + i];
        it.out_slot = args[nargs - 1];
        int rc = rsvm_pbatch_enqueue(&g_pbatch, &it);
        rsvm_pbatch_maybe_flush(&g_pbatch, pbatch_host, NULL);
        if (out) *out = rc;
        return 0;
    }

    // @exec_parallel  (manual flush)
    if (strcmp(name, "pbatch_flush") == 0) {
        int n = rsvm_pbatch_flush(&g_pbatch, pbatch_host, NULL);
        if (out) *out = n;
        return 0;
    }

    fprintf(stderr, "[native %s]\n", name);
    return 0;
}

static int p_nid(int nid, const int32_t* args, int nargs, int32_t* out, void*) {
    (void)args; (void)nargs;
    if (out) *out = 0;
    fprintf(stderr, "[nid %s]\n", rsvm_nid_name(nid));
    return 0;
}

void rsvm_install_desktop_host(rsvm_t* vm) {
    rsvm_pbatch_init(&g_pbatch, 64, 4);   // flush at 64 items, hint 4 threads
    rsvm_host_t h; memset(&h, 0, sizeof h);
    h.print_i32 = p_i32; h.print_str = p_str; h.print_char = p_char;
    h.delay_ms = p_delay;
    h.native_call = p_native;
    h.native_id = p_nid;
    rsvm_set_host(vm, &h);
}
