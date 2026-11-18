/*
 * UTDeck stage 1.2: a Vulkan layer that logs every compute dispatch a program records, with what
 * is needed to replay it in spvrun: the shader (SPIR-V saved once, named by a hash), the
 * pipeline's specialization constants, the work-group counts, the push-constant bytes, and the
 * size, offset and range of every buffer bound to the descriptor sets in use.
 *
 * Enable (Khronos loader):
 *   VK_LAYER_PATH=<dir with utdeck_dispatchlog.json> VK_INSTANCE_LAYERS=VK_LAYER_UTDECK_dispatchlog
 *   UTDECK_DISPATCHLOG_DIR=<output dir>     (log.txt and shader-<hash>.spv go there)
 *
 * Log lines (one per vkCmdDispatch, at record time):
 *   D <seq> cb=<cmdbuf> pipe=<id> shader=<hash> spec=<id=val,...> groups=<x>,<y>,<z> push=<hex>
 *     sets=<set>:<binding>:<type>:<buffersize>:<offset>:<range>;...
 * and P lines when a compute pipeline is created, S lines when a command buffer is submitted.
 * Timing lines (stage 1.4; t = milliseconds since the log opened, dur = time spent in the call):
 *   M t=<ms> shader=<hash>                      a shader module is created (workload boundaries)
 *   K cb=<cmdbuf> draws=<n> idisp=<n> ts=<n> barriers=<n> copies=<n>   what else a submitted
 *                                               command buffer holds (only when not all zero)
 *   Q t=<ms> dur=<ms> cbs=<n> fence=<0|1> polls=<n> r=<VkResult>   vkQueueSubmit; polls = calls to
 *                                               vkGetFenceStatus/vkGetEventStatus since the last Q
 *   W t=<ms> dur=<ms> what=<fences|queueidle|deviceidle|semaphores> n=<count> r=<VkResult>
 *   G t=<ms> dur=<ms> n=<queries> flags=<hex> r=<VkResult>       vkGetQueryPoolResults
 * Only binding array element 0 is recorded. Records, it does not change anything.
 */
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#define EXPORT __attribute__((visibility("default")))

/* ---------- a small handle -> value table ---------- */
typedef struct { uint64_t key; void *val; } entry;
typedef struct { entry *e; size_t cap, n; } table;

static size_t hslot(uint64_t k, size_t cap) { k ^= k >> 33; k *= 0xff51afd7ed558ccdULL; k ^= k >> 33; return k & (cap - 1); }
static void *tget(table *t, uint64_t k)
{
    if (!t->cap) return NULL;
    for (size_t i = hslot(k, t->cap);; i = (i + 1) & (t->cap - 1)) {
        if (t->e[i].key == k) return t->e[i].val;
        if (!t->e[i].key) return NULL;
    }
}
static void tput(table *t, uint64_t k, void *v)
{
    if (t->n * 2 + 2 > t->cap) {
        table nt = { calloc(t->cap ? t->cap * 2 : 1024, sizeof(entry)), t->cap ? t->cap * 2 : 1024, 0 };
        for (size_t i = 0; i < t->cap; i++) if (t->e[i].key) tput(&nt, t->e[i].key, t->e[i].val);
        free(t->e); *t = nt;
    }
    for (size_t i = hslot(k, t->cap);; i = (i + 1) & (t->cap - 1)) {
        if (t->e[i].key == k) { t->e[i].val = v; return; }
        if (!t->e[i].key) { t->e[i].key = k; t->e[i].val = v; t->n++; return; }
    }
}

/* ---------- state ---------- */
#define MAXB 16
#define MAXSETS 8
typedef struct { VkDescriptorType type; uint64_t buffer; VkDeviceSize offset, range; int used; } binding;
typedef struct { binding b[MAXB]; } dset;
typedef struct { uint64_t hash; char spec[256]; uint32_t id; } pipeinfo;
typedef struct { uint32_t n; VkDescriptorUpdateTemplateEntry e[32]; } templ;
typedef struct {
    uint64_t pipe;
    uint64_t sets[MAXSETS];
    uint32_t dynoff[MAXSETS][MAXB];
    unsigned char push[256];
    uint32_t push_size;
    dset pushdesc[MAXSETS];
    int has_pushdesc[MAXSETS];
    uint32_t draws, idisp, ts, barriers, copies; /* reset at vkBeginCommandBuffer */
} cbstate;

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static table t_module, t_pipe, t_buffer, t_set, t_templ, t_cb, t_dev;
static FILE *logf;
static char outdir[512];
static uint64_t seq;
static uint32_t pipe_ids;

static PFN_vkGetInstanceProcAddr next_gipa;
typedef struct {
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkCreateShaderModule CreateShaderModule;
    PFN_vkCreateComputePipelines CreateComputePipelines;
    PFN_vkCreateBuffer CreateBuffer;
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets;
    PFN_vkCreateDescriptorUpdateTemplate CreateDescriptorUpdateTemplate;
    PFN_vkUpdateDescriptorSetWithTemplate UpdateDescriptorSetWithTemplate;
    PFN_vkCmdBindPipeline CmdBindPipeline;
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets;
    PFN_vkCmdPushConstants CmdPushConstants;
    PFN_vkCmdPushDescriptorSetKHR CmdPushDescriptorSetKHR;
    PFN_vkCmdDispatch CmdDispatch;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkBeginCommandBuffer BeginCommandBuffer;
    PFN_vkCmdDraw CmdDraw;
    PFN_vkCmdDrawIndexed CmdDrawIndexed;
    PFN_vkCmdDispatchIndirect CmdDispatchIndirect;
    PFN_vkCmdWriteTimestamp CmdWriteTimestamp;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkCmdCopyBuffer CmdCopyBuffer;
    PFN_vkWaitForFences WaitForFences;
    PFN_vkGetFenceStatus GetFenceStatus;
    PFN_vkGetEventStatus GetEventStatus;
    PFN_vkQueueWaitIdle QueueWaitIdle;
    PFN_vkDeviceWaitIdle DeviceWaitIdle;
    PFN_vkWaitSemaphores WaitSemaphores;
    PFN_vkGetQueryPoolResults GetQueryPoolResults;
} devfns;
static devfns *g_dev; /* one device is enough for our use */

static double t0;
static uint64_t polls;
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static uint64_t fnv(const void *p, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= ((const unsigned char *)p)[i]; h *= 1099511628211ULL; }
    return h ? h : 1;
}

static void open_log(void)
{
    if (logf) return;
    const char *d = getenv("UTDECK_DISPATCHLOG_DIR");
    snprintf(outdir, sizeof(outdir), "%s", d && *d ? d : "/tmp/utdeck-dispatchlog");
    mkdir(outdir, 0755);
    char path[600];
    snprintf(path, sizeof(path), "%s/log.txt", outdir);
    logf = fopen(path, "w");
    if (logf) setvbuf(logf, NULL, _IOLBF, 0);
    t0 = now_ms();
}

static int is_buffer_type(VkDescriptorType t)
{
    return t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
           t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC || t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
}

/* ---------- hooks ---------- */
static VKAPI_ATTR VkResult VKAPI_CALL l_CreateShaderModule(VkDevice dev, const VkShaderModuleCreateInfo *ci,
                                                           const VkAllocationCallbacks *a, VkShaderModule *m)
{
    VkResult r = g_dev->CreateShaderModule(dev, ci, a, m);
    if (r == VK_SUCCESS) {
        uint64_t h = fnv(ci->pCode, ci->codeSize);
        pthread_mutex_lock(&mu);
        open_log();
        tput(&t_module, (uint64_t)*m, (void *)(uintptr_t)h);
        if (logf) fprintf(logf, "M t=%.3f shader=%016" PRIx64 "\n", now_ms() - t0, h);
        char path[700];
        snprintf(path, sizeof(path), "%s/shader-%016" PRIx64 ".spv", outdir, h);
        struct stat st;
        if (stat(path, &st)) { FILE *f = fopen(path, "wb"); if (f) { fwrite(ci->pCode, 1, ci->codeSize, f); fclose(f); } }
        pthread_mutex_unlock(&mu);
    }
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL l_CreateComputePipelines(VkDevice dev, VkPipelineCache c, uint32_t n,
                                                               const VkComputePipelineCreateInfo *ci,
                                                               const VkAllocationCallbacks *a, VkPipeline *p)
{
    VkResult r = g_dev->CreateComputePipelines(dev, c, n, ci, a, p);
    if (r != VK_SUCCESS) return r;
    pthread_mutex_lock(&mu);
    open_log();
    for (uint32_t i = 0; i < n; i++) {
        pipeinfo *pi = calloc(1, sizeof(*pi));
        pi->hash = (uint64_t)(uintptr_t)tget(&t_module, (uint64_t)ci[i].stage.module);
        pi->id = ++pipe_ids;
        const VkSpecializationInfo *si = ci[i].stage.pSpecializationInfo;
        size_t o = 0;
        for (uint32_t k = 0; si && k < si->mapEntryCount && o < sizeof(pi->spec) - 32; k++) {
            const VkSpecializationMapEntry *me = &si->pMapEntries[k];
            uint32_t v = 0;
            memcpy(&v, (const char *)si->pData + me->offset, me->size < 4 ? me->size : 4);
            o += snprintf(pi->spec + o, sizeof(pi->spec) - o, "%s%u=%u", k ? "," : "", me->constantID, v);
        }
        tput(&t_pipe, (uint64_t)p[i], pi);
        if (logf) fprintf(logf, "P pipe=%u shader=%016" PRIx64 " spec=%s\n", pi->id, pi->hash, pi->spec);
    }
    pthread_mutex_unlock(&mu);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL l_CreateBuffer(VkDevice dev, const VkBufferCreateInfo *ci,
                                                     const VkAllocationCallbacks *a, VkBuffer *b)
{
    VkResult r = g_dev->CreateBuffer(dev, ci, a, b);
    if (r == VK_SUCCESS) {
        pthread_mutex_lock(&mu);
        tput(&t_buffer, (uint64_t)*b, (void *)(uintptr_t)(ci->size + 1));
        pthread_mutex_unlock(&mu);
    }
    return r;
}

static dset *get_set(uint64_t s)
{
    dset *d = tget(&t_set, s);
    if (!d) { d = calloc(1, sizeof(*d)); tput(&t_set, s, d); }
    return d;
}

static void record_write(dset *d, uint32_t binding_, VkDescriptorType type, const VkDescriptorBufferInfo *bi)
{
    if (binding_ >= MAXB || !is_buffer_type(type) || !bi) return;
    d->b[binding_] = (binding){ type, (uint64_t)bi->buffer, bi->offset, bi->range, 1 };
}

static VKAPI_ATTR void VKAPI_CALL l_UpdateDescriptorSets(VkDevice dev, uint32_t nw, const VkWriteDescriptorSet *w,
                                                         uint32_t nc, const VkCopyDescriptorSet *c)
{
    pthread_mutex_lock(&mu);
    for (uint32_t i = 0; i < nw; i++)
        if (w[i].dstArrayElement == 0 && is_buffer_type(w[i].descriptorType))
            record_write(get_set((uint64_t)w[i].dstSet), w[i].dstBinding, w[i].descriptorType, w[i].pBufferInfo);
    pthread_mutex_unlock(&mu);
    g_dev->UpdateDescriptorSets(dev, nw, w, nc, c);
}

static VKAPI_ATTR VkResult VKAPI_CALL l_CreateDescriptorUpdateTemplate(VkDevice dev,
                                                                       const VkDescriptorUpdateTemplateCreateInfo *ci,
                                                                       const VkAllocationCallbacks *a,
                                                                       VkDescriptorUpdateTemplate *t)
{
    VkResult r = g_dev->CreateDescriptorUpdateTemplate(dev, ci, a, t);
    if (r == VK_SUCCESS) {
        templ *tp = calloc(1, sizeof(*tp));
        tp->n = ci->descriptorUpdateEntryCount < 32 ? ci->descriptorUpdateEntryCount : 32;
        memcpy(tp->e, ci->pDescriptorUpdateEntries, tp->n * sizeof(tp->e[0]));
        pthread_mutex_lock(&mu);
        tput(&t_templ, (uint64_t)*t, tp);
        pthread_mutex_unlock(&mu);
    }
    return r;
}

static VKAPI_ATTR void VKAPI_CALL l_UpdateDescriptorSetWithTemplate(VkDevice dev, VkDescriptorSet s,
                                                                    VkDescriptorUpdateTemplate t, const void *data)
{
    pthread_mutex_lock(&mu);
    templ *tp = tget(&t_templ, (uint64_t)t);
    if (tp) {
        dset *d = get_set((uint64_t)s);
        for (uint32_t i = 0; i < tp->n; i++)
            if (tp->e[i].dstArrayElement == 0 && is_buffer_type(tp->e[i].descriptorType))
                record_write(d, tp->e[i].dstBinding, tp->e[i].descriptorType,
                             (const VkDescriptorBufferInfo *)((const char *)data + tp->e[i].offset));
    }
    pthread_mutex_unlock(&mu);
    g_dev->UpdateDescriptorSetWithTemplate(dev, s, t, data);
}

static cbstate *get_cb(VkCommandBuffer cb)
{
    cbstate *s = tget(&t_cb, (uint64_t)(uintptr_t)cb);
    if (!s) { s = calloc(1, sizeof(*s)); tput(&t_cb, (uint64_t)(uintptr_t)cb, s); }
    return s;
}

static VKAPI_ATTR void VKAPI_CALL l_CmdBindPipeline(VkCommandBuffer cb, VkPipelineBindPoint bp, VkPipeline p)
{
    if (bp == VK_PIPELINE_BIND_POINT_COMPUTE) { pthread_mutex_lock(&mu); get_cb(cb)->pipe = (uint64_t)p; pthread_mutex_unlock(&mu); }
    g_dev->CmdBindPipeline(cb, bp, p);
}

static VKAPI_ATTR void VKAPI_CALL l_CmdBindDescriptorSets(VkCommandBuffer cb, VkPipelineBindPoint bp, VkPipelineLayout l,
                                                          uint32_t first, uint32_t n, const VkDescriptorSet *sets,
                                                          uint32_t ndyn, const uint32_t *dyn)
{
    if (bp == VK_PIPELINE_BIND_POINT_COMPUTE) {
        pthread_mutex_lock(&mu);
        cbstate *s = get_cb(cb);
        uint32_t di = 0;
        for (uint32_t i = 0; i < n && first + i < MAXSETS; i++) {
            s->sets[first + i] = (uint64_t)sets[i];
            s->has_pushdesc[first + i] = 0;
            dset *d = tget(&t_set, (uint64_t)sets[i]);
            for (uint32_t b = 0; d && b < MAXB; b++)
                if (d->b[b].used && (d->b[b].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC ||
                                     d->b[b].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC))
                    s->dynoff[first + i][b] = di < ndyn ? dyn[di++] : 0;
        }
        pthread_mutex_unlock(&mu);
    }
    g_dev->CmdBindDescriptorSets(cb, bp, l, first, n, sets, ndyn, dyn);
}

static VKAPI_ATTR void VKAPI_CALL l_CmdPushDescriptorSetKHR(VkCommandBuffer cb, VkPipelineBindPoint bp, VkPipelineLayout l,
                                                            uint32_t set, uint32_t nw, const VkWriteDescriptorSet *w)
{
    if (bp == VK_PIPELINE_BIND_POINT_COMPUTE && set < MAXSETS) {
        pthread_mutex_lock(&mu);
        cbstate *s = get_cb(cb);
        s->has_pushdesc[set] = 1;
        for (uint32_t i = 0; i < nw; i++)
            if (w[i].dstArrayElement == 0 && is_buffer_type(w[i].descriptorType))
                record_write(&s->pushdesc[set], w[i].dstBinding, w[i].descriptorType, w[i].pBufferInfo);
        pthread_mutex_unlock(&mu);
    }
    g_dev->CmdPushDescriptorSetKHR(cb, bp, l, set, nw, w);
}

static VKAPI_ATTR void VKAPI_CALL l_CmdPushConstants(VkCommandBuffer cb, VkPipelineLayout l, VkShaderStageFlags st,
                                                     uint32_t off, uint32_t size, const void *v)
{
    if (off + size <= 256) {
        pthread_mutex_lock(&mu);
        cbstate *s = get_cb(cb);
        memcpy(s->push + off, v, size);
        if (off + size > s->push_size) s->push_size = off + size;
        pthread_mutex_unlock(&mu);
    }
    g_dev->CmdPushConstants(cb, l, st, off, size, v);
}

static VKAPI_ATTR void VKAPI_CALL l_CmdDispatch(VkCommandBuffer cb, uint32_t x, uint32_t y, uint32_t z)
{
    pthread_mutex_lock(&mu);
    cbstate *s = get_cb(cb);
    pipeinfo *pi = tget(&t_pipe, s->pipe);
    if (logf) {
        char push[600] = "", sets[1600] = "";
        size_t o = 0;
        for (uint32_t i = 0; i < s->push_size && o < sizeof(push) - 3; i++) o += snprintf(push + o, sizeof(push) - o, "%02x", s->push[i]);
        o = 0;
        for (uint32_t si = 0; si < MAXSETS; si++) {
            dset *d = s->has_pushdesc[si] ? &s->pushdesc[si] : (s->sets[si] ? tget(&t_set, s->sets[si]) : NULL);
            for (uint32_t b = 0; d && b < MAXB && o < sizeof(sets) - 80; b++) {
                if (!d->b[b].used) continue;
                uint64_t bsz = (uint64_t)(uintptr_t)tget(&t_buffer, d->b[b].buffer);
                o += snprintf(sets + o, sizeof(sets) - o, "%u:%u:%d:%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%u;", si, b,
                              (int)d->b[b].type, bsz ? bsz - 1 : 0, (uint64_t)d->b[b].offset,
                              (uint64_t)d->b[b].range, s->dynoff[si][b]);
            }
        }
        fprintf(logf, "D %" PRIu64 " cb=%" PRIxPTR " pipe=%u shader=%016" PRIx64 " spec=%s groups=%u,%u,%u push=%s sets=%s\n",
                ++seq, (uintptr_t)cb, pi ? pi->id : 0, pi ? pi->hash : 0, pi ? pi->spec : "", x, y, z, push, sets);
    }
    pthread_mutex_unlock(&mu);
    g_dev->CmdDispatch(cb, x, y, z);
}

static VKAPI_ATTR VkResult VKAPI_CALL l_QueueSubmit(VkQueue q, uint32_t n, const VkSubmitInfo *si, VkFence f)
{
    uint32_t cbs = 0;
    pthread_mutex_lock(&mu);
    if (logf)
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t c = 0; c < si[i].commandBufferCount; c++) {
                VkCommandBuffer cb = si[i].pCommandBuffers[c];
                cbstate *s = get_cb(cb);
                fprintf(logf, "S batch=%u cb=%" PRIxPTR "\n", i, (uintptr_t)cb);
                if (s->draws | s->idisp | s->ts | s->barriers | s->copies)
                    fprintf(logf, "K cb=%" PRIxPTR " draws=%u idisp=%u ts=%u barriers=%u copies=%u\n", (uintptr_t)cb,
                            s->draws, s->idisp, s->ts, s->barriers, s->copies);
                cbs++;
            }
    pthread_mutex_unlock(&mu);
    double a = now_ms();
    VkResult r = g_dev->QueueSubmit(q, n, si, f);
    double b = now_ms();
    pthread_mutex_lock(&mu);
    if (logf) fprintf(logf, "Q t=%.3f dur=%.3f cbs=%u fence=%d polls=%" PRIu64 " r=%d\n", a - t0, b - a, cbs, f != VK_NULL_HANDLE, polls, (int)r);
    polls = 0;
    pthread_mutex_unlock(&mu);
    return r;
}

/* Count-only command hooks (reset at vkBeginCommandBuffer). */
static VKAPI_ATTR VkResult VKAPI_CALL l_BeginCommandBuffer(VkCommandBuffer cb, const VkCommandBufferBeginInfo *bi)
{
    pthread_mutex_lock(&mu);
    cbstate *s = get_cb(cb);
    s->draws = s->idisp = s->ts = s->barriers = s->copies = 0;
    pthread_mutex_unlock(&mu);
    return g_dev->BeginCommandBuffer(cb, bi);
}
#define COUNT(field) do { pthread_mutex_lock(&mu); get_cb(cb)->field++; pthread_mutex_unlock(&mu); } while (0)
static VKAPI_ATTR void VKAPI_CALL l_CmdDraw(VkCommandBuffer cb, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{ COUNT(draws); g_dev->CmdDraw(cb, a, b, c, d); }
static VKAPI_ATTR void VKAPI_CALL l_CmdDrawIndexed(VkCommandBuffer cb, uint32_t a, uint32_t b, uint32_t c, int32_t d, uint32_t e)
{ COUNT(draws); g_dev->CmdDrawIndexed(cb, a, b, c, d, e); }
static VKAPI_ATTR void VKAPI_CALL l_CmdDispatchIndirect(VkCommandBuffer cb, VkBuffer buf, VkDeviceSize off)
{ COUNT(idisp); g_dev->CmdDispatchIndirect(cb, buf, off); }
static VKAPI_ATTR void VKAPI_CALL l_CmdWriteTimestamp(VkCommandBuffer cb, VkPipelineStageFlagBits st, VkQueryPool qp, uint32_t q)
{ COUNT(ts); g_dev->CmdWriteTimestamp(cb, st, qp, q); }
static VKAPI_ATTR void VKAPI_CALL l_CmdPipelineBarrier(VkCommandBuffer cb, VkPipelineStageFlags s, VkPipelineStageFlags d,
                                                       VkDependencyFlags f, uint32_t nm, const VkMemoryBarrier *m,
                                                       uint32_t nb, const VkBufferMemoryBarrier *b,
                                                       uint32_t ni, const VkImageMemoryBarrier *i)
{ COUNT(barriers); g_dev->CmdPipelineBarrier(cb, s, d, f, nm, m, nb, b, ni, i); }
static VKAPI_ATTR void VKAPI_CALL l_CmdCopyBuffer(VkCommandBuffer cb, VkBuffer s, VkBuffer d, uint32_t n, const VkBufferCopy *r)
{ COUNT(copies); g_dev->CmdCopyBuffer(cb, s, d, n, r); }
#undef COUNT

/* Timed waits. */
static void log_wait(const char *what, uint32_t n, double a, double b, VkResult r)
{
    pthread_mutex_lock(&mu);
    if (logf) fprintf(logf, "W t=%.3f dur=%.3f what=%s n=%u r=%d\n", a - t0, b - a, what, n, (int)r);
    pthread_mutex_unlock(&mu);
}
static VKAPI_ATTR VkResult VKAPI_CALL l_WaitForFences(VkDevice dev, uint32_t n, const VkFence *f, VkBool32 all, uint64_t tmo)
{ double a = now_ms(); VkResult r = g_dev->WaitForFences(dev, n, f, all, tmo); log_wait("fences", n, a, now_ms(), r); return r; }
static VKAPI_ATTR VkResult VKAPI_CALL l_QueueWaitIdle(VkQueue q)
{ double a = now_ms(); VkResult r = g_dev->QueueWaitIdle(q); log_wait("queueidle", 1, a, now_ms(), r); return r; }
static VKAPI_ATTR VkResult VKAPI_CALL l_DeviceWaitIdle(VkDevice dev)
{ double a = now_ms(); VkResult r = g_dev->DeviceWaitIdle(dev); log_wait("deviceidle", 1, a, now_ms(), r); return r; }
static VKAPI_ATTR VkResult VKAPI_CALL l_WaitSemaphores(VkDevice dev, const VkSemaphoreWaitInfo *wi, uint64_t tmo)
{ double a = now_ms(); VkResult r = g_dev->WaitSemaphores(dev, wi, tmo); log_wait("semaphores", wi->semaphoreCount, a, now_ms(), r); return r; }
static VKAPI_ATTR VkResult VKAPI_CALL l_GetFenceStatus(VkDevice dev, VkFence f)
{ pthread_mutex_lock(&mu); polls++; pthread_mutex_unlock(&mu); return g_dev->GetFenceStatus(dev, f); }
static VKAPI_ATTR VkResult VKAPI_CALL l_GetEventStatus(VkDevice dev, VkEvent e)
{ pthread_mutex_lock(&mu); polls++; pthread_mutex_unlock(&mu); return g_dev->GetEventStatus(dev, e); }
static VKAPI_ATTR VkResult VKAPI_CALL l_GetQueryPoolResults(VkDevice dev, VkQueryPool qp, uint32_t first, uint32_t n, size_t sz,
                                                            void *data, VkDeviceSize stride, VkQueryResultFlags fl)
{
    double a = now_ms();
    VkResult r = g_dev->GetQueryPoolResults(dev, qp, first, n, sz, data, stride, fl);
    double b = now_ms();
    pthread_mutex_lock(&mu);
    if (logf) fprintf(logf, "G t=%.3f dur=%.3f n=%u flags=%x r=%d\n", a - t0, b - a, n, (unsigned)fl, (int)r);
    pthread_mutex_unlock(&mu);
    return r;
}

/* ---------- layer plumbing ---------- */
static VKAPI_ATTR VkResult VKAPI_CALL l_CreateInstance(const VkInstanceCreateInfo *ci, const VkAllocationCallbacks *a,
                                                       VkInstance *inst)
{
    VkLayerInstanceCreateInfo *lci = (VkLayerInstanceCreateInfo *)ci->pNext;
    while (lci && !(lci->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && lci->function == VK_LAYER_LINK_INFO))
        lci = (VkLayerInstanceCreateInfo *)lci->pNext;
    if (!lci) return VK_ERROR_INITIALIZATION_FAILED;
    next_gipa = lci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    lci->u.pLayerInfo = lci->u.pLayerInfo->pNext;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)next_gipa(NULL, "vkCreateInstance");
    VkResult r = create(ci, a, inst);
    pthread_mutex_lock(&mu); open_log(); if (logf) fprintf(logf, "# utdeck dispatchlog: instance created\n"); pthread_mutex_unlock(&mu);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL l_CreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci,
                                                     const VkAllocationCallbacks *a, VkDevice *dev)
{
    VkLayerDeviceCreateInfo *lci = (VkLayerDeviceCreateInfo *)ci->pNext;
    while (lci && !(lci->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && lci->function == VK_LAYER_LINK_INFO))
        lci = (VkLayerDeviceCreateInfo *)lci->pNext;
    if (!lci) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = lci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = lci->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    lci->u.pLayerInfo = lci->u.pLayerInfo->pNext;
    PFN_vkCreateDevice create = (PFN_vkCreateDevice)gipa(NULL, "vkCreateDevice");
    VkResult r = create(pd, ci, a, dev);
    if (r != VK_SUCCESS) return r;
    devfns *d = calloc(1, sizeof(*d));
    d->gdpa = gdpa;
#define L(n) d->n = (PFN_vk##n)gdpa(*dev, "vk" #n)
    L(CreateShaderModule); L(CreateComputePipelines); L(CreateBuffer); L(UpdateDescriptorSets);
    L(CreateDescriptorUpdateTemplate); L(UpdateDescriptorSetWithTemplate); L(CmdBindPipeline);
    L(CmdBindDescriptorSets); L(CmdPushConstants); L(CmdDispatch); L(QueueSubmit);
    L(BeginCommandBuffer); L(CmdDraw); L(CmdDrawIndexed); L(CmdDispatchIndirect); L(CmdWriteTimestamp);
    L(CmdPipelineBarrier); L(CmdCopyBuffer); L(WaitForFences); L(GetFenceStatus); L(GetEventStatus);
    L(QueueWaitIdle); L(DeviceWaitIdle); L(WaitSemaphores); L(GetQueryPoolResults);
    if (!d->WaitSemaphores) d->WaitSemaphores = (PFN_vkWaitSemaphores)gdpa(*dev, "vkWaitSemaphoresKHR");
    d->CmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)gdpa(*dev, "vkCmdPushDescriptorSetKHR");
    if (!d->CreateDescriptorUpdateTemplate)
        d->CreateDescriptorUpdateTemplate = (PFN_vkCreateDescriptorUpdateTemplate)gdpa(*dev, "vkCreateDescriptorUpdateTemplateKHR");
    if (!d->UpdateDescriptorSetWithTemplate)
        d->UpdateDescriptorSetWithTemplate = (PFN_vkUpdateDescriptorSetWithTemplate)gdpa(*dev, "vkUpdateDescriptorSetWithTemplateKHR");
#undef L
    pthread_mutex_lock(&mu);
    g_dev = d;
    tput(&t_dev, (uint64_t)(uintptr_t)*dev, d);
    open_log();
    if (logf) fprintf(logf, "# device created\n");
    pthread_mutex_unlock(&mu);
    return r;
}

static PFN_vkVoidFunction hook(const char *n)
{
#define H(x) if (!strcmp(n, "vk" #x)) return (PFN_vkVoidFunction)l_##x
    H(CreateShaderModule); H(CreateComputePipelines); H(CreateBuffer); H(UpdateDescriptorSets);
    H(CreateDescriptorUpdateTemplate); H(UpdateDescriptorSetWithTemplate); H(CmdBindPipeline);
    H(CmdBindDescriptorSets); H(CmdPushConstants); H(CmdPushDescriptorSetKHR); H(CmdDispatch); H(QueueSubmit);
    H(BeginCommandBuffer); H(CmdDraw); H(CmdDrawIndexed); H(CmdDispatchIndirect); H(CmdWriteTimestamp);
    H(CmdPipelineBarrier); H(CmdCopyBuffer); H(WaitForFences); H(GetFenceStatus); H(GetEventStatus);
    H(QueueWaitIdle); H(DeviceWaitIdle); H(WaitSemaphores); H(GetQueryPoolResults);
    if (!strcmp(n, "vkWaitSemaphoresKHR")) return (PFN_vkVoidFunction)l_WaitSemaphores;
#undef H
    if (!strcmp(n, "vkCreateDescriptorUpdateTemplateKHR")) return (PFN_vkVoidFunction)l_CreateDescriptorUpdateTemplate;
    if (!strcmp(n, "vkUpdateDescriptorSetWithTemplateKHR")) return (PFN_vkVoidFunction)l_UpdateDescriptorSetWithTemplate;
    return NULL;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL utdeck_GetDeviceProcAddr(VkDevice dev, const char *n)
{
    devfns *d = tget(&t_dev, (uint64_t)(uintptr_t)dev);
    PFN_vkVoidFunction next = d ? d->gdpa(dev, n) : NULL;
    if (!next) return NULL; /* not offered below us: do not pretend */
    PFN_vkVoidFunction h = hook(n);
    return h ? h : next;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL utdeck_GetInstanceProcAddr(VkInstance inst, const char *n)
{
    if (!strcmp(n, "vkCreateInstance")) return (PFN_vkVoidFunction)l_CreateInstance;
    if (!strcmp(n, "vkCreateDevice")) return (PFN_vkVoidFunction)l_CreateDevice;
    if (!strcmp(n, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)utdeck_GetInstanceProcAddr;
    if (!strcmp(n, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)utdeck_GetDeviceProcAddr;
    PFN_vkVoidFunction next = next_gipa ? next_gipa(inst, n) : NULL;
    if (!next) return NULL;
    PFN_vkVoidFunction h = hook(n);
    return h ? h : next;
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *v)
{
    if (v->loaderLayerInterfaceVersion > 2) v->loaderLayerInterfaceVersion = 2;
    v->pfnGetInstanceProcAddr = utdeck_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr = utdeck_GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
