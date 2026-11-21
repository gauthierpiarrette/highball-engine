/* Legacy scalar SSE min/max write only the low destination lane. */
#include "fid.h"

#define SCALAR_OP(op) \
static void run_##op(const void *left, const void *right, void *out) \
{ \
    __asm__ volatile("movups (%1), %%xmm0\n\t" \
                     "movups (%2), %%xmm1\n\t" \
                     #op " %%xmm1, %%xmm0\n\t" \
                     "movups %%xmm0, (%0)" \
                     : : "r"(out), "r"(left), "r"(right) : "xmm0", "xmm1", "memory"); \
}

SCALAR_OP(minss)
SCALAR_OP(maxss)
SCALAR_OP(minsd)
SCALAR_OP(maxsd)

typedef void (*scalar_op)(const void *, const void *, void *);

static void check_op(const char *name, scalar_op run, const void *left, const void *right, size_t lane_size)
{
    unsigned char out[16], expected[16];
    char got_hex[33], expected_hex[33];

    memcpy(expected, left, sizeof(expected));
    memcpy(expected, right, lane_size);
    run(left, right, out);
    fid_hex(got_hex, out, sizeof(out));
    fid_hex(expected_hex, expected, sizeof(expected));
    fid_check(!memcmp(out, expected, sizeof(out)), name, "got %s expected %s", got_hex, expected_hex);
}

int main(void)
{
    const uint32_t small_f[] = { 0x3f800000, 0xdeadbeef, 0x01234567, 0x89abcdef };
    const uint32_t large_f[] = { 0x40400000, 0x76543210, 0x55aa55aa, 0xff00ff00 };
    const uint64_t small_d[] = { UINT64_C(0x3ff0000000000000), UINT64_C(0x0123456789abcdef) };
    const uint64_t large_d[] = { UINT64_C(0x4008000000000000), UINT64_C(0x76543210deadbeef) };

    fid_name = "scalarminmax";
    check_op("minss", run_minss, large_f, small_f, sizeof(small_f[0]));
    check_op("maxss", run_maxss, small_f, large_f, sizeof(small_f[0]));
    check_op("minsd", run_minsd, large_d, small_d, sizeof(small_d[0]));
    check_op("maxsd", run_maxsd, small_d, large_d, sizeof(small_d[0]));
    return fid_summary();
}
