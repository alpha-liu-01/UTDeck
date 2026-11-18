/*
 * UTDeck stage 1.2: check signed integer division, modulo and multiply on a Vulkan driver.
 *
 *   intops [intops.spv]
 *
 * Runs intops.comp at the local sizes Geekbench's tuner tries (32 to 1024) over many push-constant
 * combinations, and compares every result with a CPU reference that follows SPIR-V's rules.
 * Prints one line per (local size, case) with mismatches, and a summary. Exit 0 = all correct.
 */
#include "vkc.h"

#define N 65536
#define K 12

static const char *names[K] = {
    "x/y", "x%y", "x/b", "x%b", "a/b", "a%b", "i%c", "i/c", "x*y", "a*c", "chain+1", "i%chain*c",
};

/* OpSDiv: truncate toward zero. OpSMod: the result takes the sign of the divisor. */
static int32_t sdiv(int32_t a, int32_t b) { return a / b; }
static int32_t smod(int32_t a, int32_t b)
{
    int32_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0)))
        r += b;
    return r;
}
static int32_t imul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng; }

int main(int argc, char **argv)
{
    const char *spv = argc > 1 ? argv[1] : "intops.spv";
    vkc_ctx c;
    vkc_init(&c);
    printf("device: %s, driver: %s %s\n", c.props.deviceName, c.driver.driverName, c.driver.driverInfo);

    vkc_buffer bx = vkc_buffer_new(&c, N * 4), by = vkc_buffer_new(&c, N * 4), br = vkc_buffer_new(&c, N * K * 4);
    int32_t *x = bx.map, *y = by.map, *r = br.map;
    /* Inputs: small and large magnitudes, both signs; y never 0 and never -1 with INT_MIN. */
    for (int i = 0; i < N; i++) {
        uint32_t m = rnd() % 4;
        int32_t v = m == 0 ? (int32_t)(rnd() % 200) - 100
                  : m == 1 ? (int32_t)(rnd() % 20000) - 10000
                  : m == 2 ? (int32_t)(rnd() >> 1) - (int32_t)(rnd() >> 1)
                           : (int32_t)rnd();
        if (v == INT32_MIN) v++;
        x[i] = v;
        int32_t d = (int32_t)(rnd() % 2 ? rnd() % 64 : rnd() % 100000) * (rnd() % 2 ? 1 : -1);
        y[i] = d == 0 ? 7 : d;
    }

    /* Push-constant cases (a, b, c): the kinds of values index arithmetic sees, and negatives. */
    static const int32_t cases[][3] = {
        { 1920, 1, 3 }, { 1080, 2, 5 }, { 639, 8, 2 }, { 4096, 16, 64 }, { 33, 4, 7 },
        { -1920, 3, 5 }, { 1920, -3, 5 }, { -100, -7, 3 }, { 100, 7, -3 }, { 65537, 65536, 255 },
        { 70000, 3, 70001 }, { 123456789, 1000, 77 }, { -123456789, 999, 13 }, { 2147483000, 65535, 3 },
    };
    const int ncases = sizeof(cases) / sizeof(cases[0]);
    static const uint32_t locals[] = { 32, 64, 128, 256, 512, 1024 };

    long total_bad = 0, total = 0;
    for (unsigned li = 0; li < sizeof(locals) / sizeof(locals[0]); li++) {
        if (locals[li] > c.props.limits.maxComputeWorkGroupSize[0]) continue;
        vkc_buffer bufs[3] = { bx, by, br };
        vkc_pipeline p = vkc_pipeline_new(&c, spv, bufs, 3, 16, locals[li]);
        for (int ci = 0; ci < ncases; ci++) {
            int32_t a = cases[ci][0], b = cases[ci][1], cc = cases[ci][2];
            int32_t chain = sdiv(sdiv(a, b) - sdiv(cc, b), cc) + 1;
            if (chain == 0) { fprintf(stderr, "case %d: chain is 0, skipped\n", ci); continue; }
            int32_t push[4] = { a, b, cc, N };
            memset(r, 0x5a, N * K * 4);
            if (vkc_dispatch(&c, &p, push, sizeof(push), (N + locals[li] - 1) / locals[li], 1, 1) < 0)
                return 3;
            long bad[K] = { 0 };
            int shown = 0;
            for (int i = 0; i < N; i++) {
                int32_t e[K] = {
                    sdiv(x[i], y[i]), smod(x[i], y[i]), sdiv(x[i], b), smod(x[i], b), sdiv(a, b), smod(a, b),
                    smod(i, cc), sdiv(i, cc), imul(x[i], y[i]), imul(a, cc), chain, imul(smod(i, chain), cc),
                };
                for (int k = 0; k < K; k++) {
                    total++;
                    if (r[i * K + k] != e[k]) {
                        bad[k]++;
                        total_bad++;
                        if (shown++ < 4)
                            printf("  local %u case %d i=%d %s: x=%d y=%d a=%d b=%d c=%d got %d want %d\n", locals[li],
                                   ci, i, names[k], x[i], y[i], a, b, cc, r[i * K + k], e[k]);
                    }
                }
            }
            int any = 0;
            for (int k = 0; k < K; k++) any |= bad[k] != 0;
            if (any) {
                printf("local %u case %d (a=%d b=%d c=%d):", locals[li], ci, a, b, cc);
                for (int k = 0; k < K; k++) if (bad[k]) printf(" %s=%ld", names[k], bad[k]);
                printf("\n");
            }
        }
        vkc_pipeline_free(&c, &p);
    }
    printf("checked %ld results, %ld wrong: %s\n", total, total_bad, total_bad ? "FAIL" : "PASS");
    return total_bad ? 1 : 0;
}
