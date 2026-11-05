/* VPULL_MEMO check: every method that changes what rsx_fetch_plan_init reads
 * (vertex_attribs, vertex_data4f, base offset/index, frequency divider op) must
 * renew rsx_state.vattr_gen, so a memo keyed on it can never reuse a stale plan.
 * Random method stream through the real rsx_process_method; after each method,
 * if that state's bytes changed, vattr_gen must have changed too. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "rsx_commands.h"

static uint32_t rng = 4321u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 4; }
static uint64_t hash_vstate(const rsx_state* s)
{
    uint64_t h = 1469598103934665603ull;
    const uint8_t* p = (const uint8_t*)s->vertex_attribs;
    for (size_t i = 0; i < sizeof s->vertex_attribs; i++) h = (h ^ p[i]) * 1099511628211ull;
    p = (const uint8_t*)s->vertex_data4f;
    for (size_t i = 0; i < sizeof s->vertex_data4f; i++) h = (h ^ p[i]) * 1099511628211ull;
    h = (h ^ s->vertex_data_base_offset) * 1099511628211ull;
    h = (h ^ s->vertex_data_base_index) * 1099511628211ull;
    h = (h ^ s->frequency_divider_op) * 1099511628211ull;
    return h;
}

int main(void)
{
    static rsx_state st; rsx_state_init(&st);
    long changed = 0, missed = 0;
    for (int i = 0; i < 2000000; i++) {
        uint32_t r = rnd() % 100, method, data = rnd() ^ (rnd() << 12);
        if (r < 20)      method = 0x1680 + (rnd() % 16) * 4;          /* array offset */
        else if (r < 40) method = 0x1740 + (rnd() % 16) * 4;          /* array format */
        else if (r < 55) method = 0x1C00 + (rnd() % 64) * 4;          /* DATA4F */
        else if (r < 60) method = 0x1738;                             /* base offset */
        else if (r < 65) method = 0x173C;                             /* base index */
        else if (r < 68) method = 0x1FC0;                             /* divider op */
        else if (r < 80) method = 0x1F00 + (rnd() % 32) * 4;          /* constants */
        else if (r < 90) method = 0x1A00 + (rnd() % 128) * 4;         /* textures */
        else             method = 0x0A00 + (rnd() % 64) * 4;          /* viewport/clip state */
        if (method == 0x1808 || method == 0x1814 || method == 0x1824) continue;   /* no draws: no backend here */
        const uint64_t h0 = hash_vstate(&st); const uint32_t g0 = st.vattr_gen;
        rsx_process_method(&st, method, data);
        if (hash_vstate(&st) != h0) { changed++; if (st.vattr_gen == g0) { if (missed < 5) printf("MISSED method 0x%04X\n", method); missed++; } }
    }
    printf("%ld state changes, %ld without a new vattr_gen\n", changed, missed);
    return missed ? 1 : 0;
}
