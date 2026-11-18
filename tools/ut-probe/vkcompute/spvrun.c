/*
 * UTDeck stage 1.2: run any compute shader (SPIR-V) with given inputs, time it, dump its outputs.
 * Used for differential testing (the same shader and inputs on two drivers, outputs compared on
 * the PC with spvcmp.py) and for timing dispatches.
 *
 *   spvrun SHADER.spv [options]
 *     --spec ID=VALUE        32-bit specialization constant (repeatable)
 *     --push V,V,...         push constants, 32-bit each: integers, or floats with an "f" suffix
 *     --push-hex HEX         push constants as raw bytes (hex, as utdeck-dispatchlog logs them)
 *     --buf KIND:COUNT       next storage buffer (binding 0, 1, ...), COUNT 32-bit elements. KIND:
 *                              zero, randf (floats in [-1, 1]), randu (any 32-bit word),
 *                              randpos (floats in [0, 1]), gauss (a normalized Gaussian of COUNT taps),
 *                              iota (0, 1, 2, ... as integers), rand@LO,HI (floats in [LO, HI])
 *     --img KIND:WxH         next binding is a W x H storage image, format R8_UNORM, filled like a
 *                            buffer (KIND as above, applied to its bytes as 32-bit words)
 *     --out B                buffer at binding B is an output: written to DIR/b<B>.bin (repeatable)
 *     --groups X,Y,Z         workgroup counts
 *     --repeat N             dispatch N times, print each time (default 1)
 *     --batch N              each repeat records N command buffers (one dispatch and a barrier
 *                            each) and hands them to one vkQueueSubmit, then vkQueueWaitIdle
 *     --interval MS          pause MS milliseconds between repeated dispatches (default 0)
 *     --dump DIR             where outputs go (default: no dump)
 *     --seed S               input seed (default 1): the same seed gives the same inputs everywhere
 *     --hash                 after the last dispatch, print a 64-bit FNV-1a hash of every buffer
 *     --profile              per dispatch: time in vkQueueSubmit, time waiting, and GPU time
 *                            between two timestamps around the dispatch
 *
 * Exit 0 if every dispatch completed, 3 if one failed (for example VK_ERROR_DEVICE_LOST).
 */
#include "vkc.h"
#include <errno.h>
#include <math.h>
#include <sys/stat.h>

#define MAXB 8

static uint32_t rng;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void fill(uint32_t *p, uint32_t n, const char *kind)
{
    if (!strcmp(kind, "zero")) memset(p, 0, n * 4);
    else if (!strcmp(kind, "randu")) for (uint32_t i = 0; i < n; i++) p[i] = rnd();
    else if (!strcmp(kind, "iota")) for (uint32_t i = 0; i < n; i++) p[i] = i;
    else if (!strcmp(kind, "randf") || !strcmp(kind, "randpos")) {
        float *f = (float *)p;
        int pos = !strcmp(kind, "randpos");
        for (uint32_t i = 0; i < n; i++) {
            float u = (rnd() >> 8) / 16777216.0f;
            f[i] = pos ? u : 2.0f * u - 1.0f;
        }
    } else if (!strncmp(kind, "rand@", 5)) {
        float lo = 0, hi = 1, *f = (float *)p;
        if (sscanf(kind + 5, "%f,%f", &lo, &hi) != 2) { fprintf(stderr, "rand@LO,HI expected: %s\n", kind); exit(2); }
        for (uint32_t i = 0; i < n; i++) f[i] = lo + (hi - lo) * ((rnd() >> 8) / 16777216.0f);
    } else if (!strcmp(kind, "gauss")) {
        float *f = (float *)p, sum = 0, sigma = n / 6.0f + 1e-6f;
        for (uint32_t i = 0; i < n; i++) {
            float d = (float)i - (n - 1) / 2.0f;
            f[i] = expf(-d * d / (2 * sigma * sigma));
            sum += f[i];
        }
        for (uint32_t i = 0; i < n; i++) f[i] /= sum;
    } else {
        fprintf(stderr, "unknown buffer kind %s\n", kind);
        exit(2);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: spvrun SHADER.spv [options]\n"); return 2; }
    const char *spv = argv[1];
    uint32_t spec_ids[16], spec_vals[16], nspec = 0;
    uint32_t push[32], npush = 0;
    char kinds[MAXB][16];
    uint32_t counts[MAXB], imgw[MAXB] = { 0 }, imgh[MAXB] = { 0 }, nbuf = 0;
    int is_out[MAXB] = { 0 };
    uint32_t gx = 1, gy = 1, gz = 1, repeat = 1, batch = 0, seed = 1, interval_ms = 0;
    const char *dump = NULL;
    int do_hash = 0, profile = 0;

    for (int i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
        if (!strcmp(a, "--spec")) {
            if (nspec >= 16 || sscanf(v, "%u=%u", &spec_ids[nspec], &spec_vals[nspec]) != 2) { fprintf(stderr, "bad --spec\n"); return 2; }
            nspec++, i++;
        } else if (!strcmp(a, "--push")) {
            char tmp[512];
            snprintf(tmp, sizeof(tmp), "%s", v);
            for (char *t = strtok(tmp, ","); t && npush < 32; t = strtok(NULL, ",")) {
                size_t l = strlen(t);
                if (l && t[l - 1] == 'f') { float f = strtof(t, NULL); memcpy(&push[npush++], &f, 4); }
                else push[npush++] = (uint32_t)strtol(t, NULL, 0);
            }
            i++;
        } else if (!strcmp(a, "--push-hex")) {
            size_t l = strlen(v);
            unsigned char *pb = (unsigned char *)push;
            memset(push, 0, sizeof(push));
            for (size_t k = 0; k + 1 < l && k / 2 < sizeof(push); k += 2) {
                unsigned int byte;
                sscanf(v + k, "%2x", &byte);
                pb[k / 2] = (unsigned char)byte;
            }
            npush = (uint32_t)((l / 2 + 3) / 4);
            i++;
        } else if (!strcmp(a, "--buf")) {
            if (nbuf >= MAXB || sscanf(v, "%15[^:]:%u", kinds[nbuf], &counts[nbuf]) != 2) { fprintf(stderr, "bad --buf\n"); return 2; }
            nbuf++, i++;
        } else if (!strcmp(a, "--img")) {
            if (nbuf >= MAXB || sscanf(v, "%15[^:]:%ux%u", kinds[nbuf], &imgw[nbuf], &imgh[nbuf]) != 3) { fprintf(stderr, "bad --img\n"); return 2; }
            counts[nbuf] = (imgw[nbuf] * imgh[nbuf] + 3) / 4;
            nbuf++, i++;
        } else if (!strcmp(a, "--out")) { uint32_t b = atoi(v); if (b < MAXB) is_out[b] = 1; i++; }
        else if (!strcmp(a, "--groups")) { if (sscanf(v, "%u,%u,%u", &gx, &gy, &gz) < 1) return 2; i++; }
        else if (!strcmp(a, "--repeat")) { repeat = atoi(v); i++; }
        else if (!strcmp(a, "--batch")) { batch = atoi(v); i++; }
        else if (!strcmp(a, "--interval")) { interval_ms = atoi(v); i++; }
        else if (!strcmp(a, "--dump")) { dump = v; i++; }
        else if (!strcmp(a, "--seed")) { seed = atoi(v); i++; }
        else if (!strcmp(a, "--hash")) { do_hash = 1; }
        else if (!strcmp(a, "--profile")) { profile = 1; }
        else { fprintf(stderr, "unknown option %s\n", a); return 2; }
    }

    vkc_ctx c;
    vkc_init(&c);
    c.profile = profile;
    printf("device: %s, driver: %s %s\n", c.props.deviceName, c.driver.driverName, c.driver.driverInfo);
    vkc_buffer bufs[MAXB];
    rng = seed ? seed : 1;
    for (uint32_t b = 0; b < nbuf; b++) {
        bufs[b] = imgw[b] ? vkc_image_new(&c, imgw[b], imgh[b], VK_FORMAT_R8_UNORM, 1)
                          : vkc_buffer_new(&c, (VkDeviceSize)counts[b] * 4);
        fill(bufs[b].map, counts[b], kinds[b]);
        if (imgw[b]) vkc_image_upload(&c, &bufs[b]);
    }
    vkc_pipeline p = vkc_pipeline_new_spec(&c, spv, bufs, nbuf, npush ? npush * 4 : 4, spec_ids, spec_vals, nspec);

    int failed = 0;
    double best = 1e9, worst = 0, start = vkc_now();
    for (uint32_t r = 0; r < repeat; r++) {
        if (r && interval_ms) {
            struct timespec ts = { interval_ms / 1000, (interval_ms % 1000) * 1000000L };
            nanosleep(&ts, NULL);
        }
        double at = vkc_now() - start;
        double t = batch ? vkc_dispatch_batch(&c, &p, push, npush ? npush * 4 : 4, gx, gy, gz, batch)
                         : vkc_dispatch(&c, &p, push, npush ? npush * 4 : 4, gx, gy, gz);
        if (t < 0) { failed = 1; break; }
        if (t < best) best = t;
        if (t > worst) worst = t;
        if (batch)
            printf("batch %u at %.3f s: %.3f ms for %u dispatches, %.3f ms each (submit %.3f, wait %.3f, gpu span %.3f)\n",
                   r, at, t * 1000, batch, t * 1000 / batch, c.last_submit_ms, c.last_wait_ms, c.last_gpu_ms);
        else if (profile)
            printf("dispatch %u at %.3f s: %.3f ms (submit %.3f, wait %.3f, gpu %.3f, not gpu %.3f)\n", r, at, t * 1000,
                   c.last_submit_ms, c.last_wait_ms, c.last_gpu_ms, t * 1000 - c.last_gpu_ms);
        else
            printf("dispatch %u at %.3f s: %.3f ms\n", r, at, t * 1000);
    }
    if (!failed) printf("best: %.3f ms, worst: %.3f ms\n", best * 1000, worst * 1000);
    if (!failed)
        for (uint32_t b = 0; b < nbuf; b++)
            if (imgw[b]) vkc_image_download(&c, &bufs[b]);

    if (do_hash && !failed) {
        for (uint32_t b = 0; b < nbuf; b++) {
            uint64_t h = 1469598103934665603ULL;
            const unsigned char *d = bufs[b].map;
            for (uint64_t k = 0; k < (uint64_t)counts[b] * 4; k++) { h ^= d[k]; h *= 1099511628211ULL; }
            printf("hash b%u %016llx\n", b, (unsigned long long)h);
        }
    }

    if (dump && !failed) {
        if (mkdir(dump, 0755) && errno != EEXIST) { perror(dump); return 2; }
        for (uint32_t b = 0; b < nbuf; b++) {
            if (!is_out[b]) continue;
            char path[512];
            snprintf(path, sizeof(path), "%s/b%u.bin", dump, b);
            FILE *f = fopen(path, "wb");
            if (!f || fwrite(bufs[b].map, 4, counts[b], f) != counts[b]) { perror(path); return 2; }
            fclose(f);
        }
    }
    vkc_pipeline_free(&c, &p);
    return failed ? 3 : 0;
}
