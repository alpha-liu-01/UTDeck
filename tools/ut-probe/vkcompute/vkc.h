/*
 * UTDeck: a minimal Vulkan compute helper for small driver tests (stage 1.2).
 * One device, one compute queue, host-visible coherent storage buffers, one pipeline with a
 * specialization constant 0 (the local size X) and a push-constant block.
 */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#define VKC_CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { \
    fprintf(stderr, "%s:%d: %s = %d\n", __FILE__, __LINE__, #x, r_); exit(2); } } while (0)

typedef struct {
    VkInstance instance;
    VkPhysicalDevice pdev;
    VkDevice dev;
    VkQueue queue;
    uint32_t qfam;
    VkCommandPool pool;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceDriverProperties driver;
    /* --profile: timestamps around each dispatch, and the split of the last dispatch's time */
    int profile;
    VkQueryPool qpool;
    double last_submit_ms, last_wait_ms, last_gpu_ms;
} vkc_ctx;

typedef struct {
    VkBuffer buf;
    VkDeviceMemory mem;
    void *map;
    VkDeviceSize size;
    /* A storage image (stage 1.4) when img != 0: buf/map is then its host-visible copy, moved in
     * with vkc_image_upload and out with vkc_image_download. */
    VkImage img;
    VkDeviceMemory img_mem;
    VkImageView view;
    uint32_t w, h;
} vkc_buffer;

static inline void vkc_init(vkc_ctx *c)
{
    memset(c, 0, sizeof(*c));
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                              .pApplicationName = "utdeck-vkcompute", .apiVersion = VK_API_VERSION_1_2 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VKC_CHECK(vkCreateInstance(&ici, NULL, &c->instance));
    uint32_t n = 1;
    VkResult r = vkEnumeratePhysicalDevices(c->instance, &n, &c->pdev);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || n == 0) { fprintf(stderr, "no Vulkan device\n"); exit(2); }
    c->driver = (VkPhysicalDeviceDriverProperties){ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
    VkPhysicalDeviceProperties2 p2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &c->driver };
    vkGetPhysicalDeviceProperties2(c->pdev, &p2);
    c->props = p2.properties;

    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(c->pdev, &nq, NULL);
    VkQueueFamilyProperties qp[16];
    if (nq > 16) nq = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(c->pdev, &nq, qp);
    c->qfam = UINT32_MAX;
    for (uint32_t i = 0; i < nq; i++)
        if (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { c->qfam = i; break; }
    if (c->qfam == UINT32_MAX) { fprintf(stderr, "no compute queue\n"); exit(2); }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                    .queueFamilyIndex = c->qfam, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
                               .pQueueCreateInfos = &qci };
    /* VKC_GLOBAL_PRIORITY=low|medium|high|realtime: ask for a queue global priority
     * (VK_KHR_global_priority), as a game or a translation layer would. */
    VkDeviceQueueGlobalPriorityCreateInfoKHR gp = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR };
    const char *gp_ext = "VK_KHR_global_priority";
    const char *gps = getenv("VKC_GLOBAL_PRIORITY");
    if (gps && *gps) {
        gp.globalPriority = !strcmp(gps, "low") ? VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR
                          : !strcmp(gps, "high") ? VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR
                          : !strcmp(gps, "realtime") ? VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR
                                                     : VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;
        qci.pNext = &gp;
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = &gp_ext;
        fprintf(stderr, "vkc: queue global priority %s\n", gps);
    }
    VKC_CHECK(vkCreateDevice(c->pdev, &dci, NULL, &c->dev));
    vkGetDeviceQueue(c->dev, c->qfam, 0, &c->queue);
    VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                    .queueFamilyIndex = c->qfam };
    VKC_CHECK(vkCreateCommandPool(c->dev, &pci, NULL, &c->pool));
}

/* Host-visible and coherent, so no cache maintenance is involved in what this test checks. */
static inline vkc_buffer vkc_buffer_new(vkc_ctx *c, VkDeviceSize size)
{
    vkc_buffer b = { .size = size };
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
                               .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    VKC_CHECK(vkCreateBuffer(c->dev, &bci, NULL, &b.buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c->dev, b.buf, &req);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(c->pdev, &mp);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) { type = i; break; }
    if (type == UINT32_MAX) { fprintf(stderr, "no host-visible coherent memory\n"); exit(2); }
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                 .allocationSize = req.size, .memoryTypeIndex = type };
    VKC_CHECK(vkAllocateMemory(c->dev, &mai, NULL, &b.mem));
    VKC_CHECK(vkBindBufferMemory(c->dev, b.buf, b.mem, 0));
    VKC_CHECK(vkMapMemory(c->dev, b.mem, 0, size, 0, &b.map));
    return b;
}

static inline VkCommandBuffer vkc_once_begin(vkc_ctx *c)
{
    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                         .commandPool = c->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                         .commandBufferCount = 1 };
    VkCommandBuffer cb;
    VKC_CHECK(vkAllocateCommandBuffers(c->dev, &cbai, &cb));
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    VKC_CHECK(vkBeginCommandBuffer(cb, &bi));
    return cb;
}

static inline void vkc_once_end(vkc_ctx *c, VkCommandBuffer cb)
{
    VKC_CHECK(vkEndCommandBuffer(cb));
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
    VKC_CHECK(vkQueueSubmit(c->queue, 1, &si, VK_NULL_HANDLE));
    VKC_CHECK(vkQueueWaitIdle(c->queue));
    vkFreeCommandBuffers(c->dev, c->pool, 1, &cb);
}

static inline void vkc_image_barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to)
{
    VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                               .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT, .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                               .oldLayout = from, .newLayout = to,
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .image = img, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}

/* A w x h storage image of the given format (optimal tiling, device memory, layout GENERAL after
 * vkc_image_upload), with a host-visible copy of texel_size * w * h bytes. */
static inline vkc_buffer vkc_image_new(vkc_ctx *c, uint32_t w, uint32_t h, VkFormat fmt, uint32_t texel_size)
{
    vkc_buffer b = vkc_buffer_new(c, ((VkDeviceSize)w * h * texel_size + 3) & ~(VkDeviceSize)3);
    b.w = w; b.h = h;
    VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                              .format = fmt, .extent = { w, h, 1 }, .mipLevels = 1, .arrayLayers = 1,
                              .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    VKC_CHECK(vkCreateImage(c->dev, &ici, NULL, &b.img));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(c->dev, b.img, &req);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(c->pdev, &mp);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { type = i; break; }
    if (type == UINT32_MAX) for (uint32_t i = 0; i < mp.memoryTypeCount; i++) if (req.memoryTypeBits & (1u << i)) { type = i; break; }
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = type };
    VKC_CHECK(vkAllocateMemory(c->dev, &mai, NULL, &b.img_mem));
    VKC_CHECK(vkBindImageMemory(c->dev, b.img, b.img_mem, 0));
    VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = b.img,
                                  .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt,
                                  .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VKC_CHECK(vkCreateImageView(c->dev, &vci, NULL, &b.view));
    return b;
}

static inline void vkc_image_upload(vkc_ctx *c, vkc_buffer *b)
{
    VkCommandBuffer cb = vkc_once_begin(c);
    vkc_image_barrier(cb, b->img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy r = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { b->w, b->h, 1 } };
    vkCmdCopyBufferToImage(cb, b->buf, b->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    vkc_image_barrier(cb, b->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    vkc_once_end(c, cb);
}

static inline void vkc_image_download(vkc_ctx *c, vkc_buffer *b)
{
    VkCommandBuffer cb = vkc_once_begin(c);
    vkc_image_barrier(cb, b->img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    VkBufferImageCopy r = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { b->w, b->h, 1 } };
    vkCmdCopyImageToBuffer(cb, b->img, VK_IMAGE_LAYOUT_GENERAL, b->buf, 1, &r);
    vkc_once_end(c, cb);
}

static inline void *vkc_read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    void *p = malloc(*size);
    if (fread(p, 1, *size, f) != *size) { perror(path); exit(2); }
    fclose(f);
    return p;
}

typedef struct {
    VkDescriptorSetLayout dsl;
    VkPipelineLayout layout;
    VkPipeline pipe;
    VkDescriptorPool dpool;
    VkDescriptorSet set;
} vkc_pipeline;

/* nbuf storage buffers at bindings 0..nbuf-1 (bufs[i] at binding i), push constants of push_size
 * bytes, nspec 32-bit specialization constants (ids spec_ids[i], values spec_vals[i]). */
static inline vkc_pipeline vkc_pipeline_new_spec(vkc_ctx *c, const char *spv_path, vkc_buffer *bufs, uint32_t nbuf,
                                          uint32_t push_size, const uint32_t *spec_ids,
                                          const uint32_t *spec_vals, uint32_t nspec)
{
    vkc_pipeline p;
    VkDescriptorSetLayoutBinding bind[8];
    uint32_t nimg = 0;
    for (uint32_t i = 0; i < nbuf; i++) {
        nimg += bufs[i].img != VK_NULL_HANDLE;
        bind[i] = (VkDescriptorSetLayoutBinding){ .binding = i,
                                                  .descriptorType = bufs[i].img ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                  .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
    }
    VkDescriptorSetLayoutCreateInfo dlci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                             .bindingCount = nbuf, .pBindings = bind };
    VKC_CHECK(vkCreateDescriptorSetLayout(c->dev, &dlci, NULL, &p.dsl));
    VkPushConstantRange pcr = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = push_size };
    VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                        .setLayoutCount = 1, .pSetLayouts = &p.dsl,
                                        .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr };
    VKC_CHECK(vkCreatePipelineLayout(c->dev, &plci, NULL, &p.layout));

    size_t spv_size;
    uint32_t *spv = vkc_read_file(spv_path, &spv_size);
    VkShaderModuleCreateInfo smci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                      .codeSize = spv_size, .pCode = spv };
    VkShaderModule mod;
    VKC_CHECK(vkCreateShaderModule(c->dev, &smci, NULL, &mod));
    free(spv);
    VkSpecializationMapEntry me[16];
    if (nspec > 16) nspec = 16;
    for (uint32_t i = 0; i < nspec; i++)
        me[i] = (VkSpecializationMapEntry){ .constantID = spec_ids[i], .offset = 4 * i, .size = 4 };
    VkSpecializationInfo si = { .mapEntryCount = nspec, .pMapEntries = me, .dataSize = 4 * nspec, .pData = spec_vals };
    VkComputePipelineCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                   .module = mod, .pName = "main", .pSpecializationInfo = &si },
        .layout = p.layout };
    VKC_CHECK(vkCreateComputePipelines(c->dev, VK_NULL_HANDLE, 1, &cpci, NULL, &p.pipe));
    vkDestroyShaderModule(c->dev, mod, NULL);

    VkDescriptorPoolSize ps[2] = { { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = nbuf - nimg ? nbuf - nimg : 1 },
                                   { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = nimg ? nimg : 1 } };
    VkDescriptorPoolCreateInfo dpci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                        .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = ps };
    VKC_CHECK(vkCreateDescriptorPool(c->dev, &dpci, NULL, &p.dpool));
    VkDescriptorSetAllocateInfo dsai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                         .descriptorPool = p.dpool, .descriptorSetCount = 1, .pSetLayouts = &p.dsl };
    VKC_CHECK(vkAllocateDescriptorSets(c->dev, &dsai, &p.set));
    VkDescriptorBufferInfo dbi[8];
    VkDescriptorImageInfo dii[8];
    VkWriteDescriptorSet w[8];
    for (uint32_t i = 0; i < nbuf; i++) {
        dbi[i] = (VkDescriptorBufferInfo){ .buffer = bufs[i].buf, .offset = 0, .range = VK_WHOLE_SIZE };
        dii[i] = (VkDescriptorImageInfo){ .imageView = bufs[i].view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
        w[i] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = p.set,
                                       .dstBinding = i, .descriptorCount = 1,
                                       .descriptorType = bufs[i].img ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                       .pBufferInfo = bufs[i].img ? NULL : &dbi[i], .pImageInfo = bufs[i].img ? &dii[i] : NULL };
    }
    vkUpdateDescriptorSets(c->dev, nbuf, w, 0, NULL);
    return p;
}

/* The common case: specialization constant 0 = local size X. */
static inline vkc_pipeline vkc_pipeline_new(vkc_ctx *c, const char *spv_path, vkc_buffer *bufs, uint32_t nbuf,
                                     uint32_t push_size, uint32_t local_x)
{
    const uint32_t id = 0;
    return vkc_pipeline_new_spec(c, spv_path, bufs, nbuf, push_size, &id, &local_x, 1);
}

static inline void vkc_pipeline_free(vkc_ctx *c, vkc_pipeline *p)
{
    vkDestroyPipeline(c->dev, p->pipe, NULL);
    vkDestroyDescriptorPool(c->dev, p->dpool, NULL);
    vkDestroyPipelineLayout(c->dev, p->layout, NULL);
    vkDestroyDescriptorSetLayout(c->dev, p->dsl, NULL);
}

static inline double vkc_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

/* N command buffers, one dispatch and one barrier each, handed to ONE vkQueueSubmit and waited for
 * with vkQueueWaitIdle: the pattern Geekbench's Particle Physics uses (stage 1.4). Returns the wall
 * time in seconds, or -1 on failure; last_submit_ms / last_wait_ms / last_gpu_ms as for one
 * dispatch (the GPU time spans from the start of the first command buffer to the end of the last). */
static inline double vkc_dispatch_batch(vkc_ctx *c, vkc_pipeline *p, const void *push, uint32_t push_size,
                                 uint32_t gx, uint32_t gy, uint32_t gz, uint32_t n)
{
    VkCommandBuffer *cbs = calloc(n, sizeof(*cbs));
    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                         .commandPool = c->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                         .commandBufferCount = n };
    VKC_CHECK(vkAllocateCommandBuffers(c->dev, &cbai, cbs));
    if (c->profile && !c->qpool) {
        VkQueryPoolCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                      .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 };
        VKC_CHECK(vkCreateQueryPool(c->dev, &qci, NULL, &c->qpool));
    }
    for (uint32_t i = 0; i < n; i++) {
        VkCommandBuffer cb = cbs[i];
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        VKC_CHECK(vkBeginCommandBuffer(cb, &bi));
        if (c->profile && i == 0) {
            vkCmdResetQueryPool(cb, c->qpool, 0, 2);
            vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, c->qpool, 0);
        }
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipe);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->layout, 0, 1, &p->set, 0, NULL);
        vkCmdPushConstants(cb, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size, push);
        vkCmdDispatch(cb, gx, gy, gz);
        VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                               .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
        if (c->profile && i == n - 1)
            vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, c->qpool, 1);
        VKC_CHECK(vkEndCommandBuffer(cb));
    }
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = n, .pCommandBuffers = cbs };
    double t0 = vkc_now();
    VkResult r = vkQueueSubmit(c->queue, 1, &si, VK_NULL_HANDLE);
    double t1 = vkc_now();
    if (r == VK_SUCCESS) r = vkQueueWaitIdle(c->queue);
    double t = vkc_now() - t0;
    c->last_submit_ms = (t1 - t0) * 1000;
    c->last_wait_ms = (t0 + t - t1) * 1000;
    c->last_gpu_ms = -1;
    if (c->profile && r == VK_SUCCESS) {
        uint64_t ts[2];
        if (vkGetQueryPoolResults(c->dev, c->qpool, 0, 2, sizeof(ts), ts, 8, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            c->last_gpu_ms = (ts[1] - ts[0]) * (double)c->props.limits.timestampPeriod / 1e6;
    }
    vkFreeCommandBuffers(c->dev, c->pool, n, cbs);
    free(cbs);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "batch of %u dispatches %ux%ux%u failed: VkResult %d after %.3f s\n", n, gx, gy, gz, r, t);
        return -1.0;
    }
    return t;
}

/* One dispatch, submitted alone and waited for. Returns the wall time in seconds, or -1 if the
 * submission or the wait failed (for example VK_ERROR_DEVICE_LOST after a GPU timeout). */
static inline double vkc_dispatch(vkc_ctx *c, vkc_pipeline *p, const void *push, uint32_t push_size,
                           uint32_t gx, uint32_t gy, uint32_t gz)
{
    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                         .commandPool = c->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                         .commandBufferCount = 1 };
    VkCommandBuffer cb;
    VKC_CHECK(vkAllocateCommandBuffers(c->dev, &cbai, &cb));
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    VKC_CHECK(vkBeginCommandBuffer(cb, &bi));
    if (c->profile && !c->qpool) {
        VkQueryPoolCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                      .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 };
        VKC_CHECK(vkCreateQueryPool(c->dev, &qci, NULL, &c->qpool));
    }
    if (c->profile) {
        vkCmdResetQueryPool(cb, c->qpool, 0, 2);
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, c->qpool, 0);
    }
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->layout, 0, 1, &p->set, 0, NULL);
    vkCmdPushConstants(cb, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size, push);
    vkCmdDispatch(cb, gx, gy, gz);
    VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                           .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    if (c->profile)
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, c->qpool, 1);
    VKC_CHECK(vkEndCommandBuffer(cb));
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    VKC_CHECK(vkCreateFence(c->dev, &fci, NULL, &fence));
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
    double t0 = vkc_now();
    VkResult r = vkQueueSubmit(c->queue, 1, &si, fence);
    double t1 = vkc_now();
    if (r == VK_SUCCESS)
        r = vkWaitForFences(c->dev, 1, &fence, VK_TRUE, 10ull * 1000 * 1000 * 1000);
    double t = vkc_now() - t0;
    c->last_submit_ms = (t1 - t0) * 1000;
    c->last_wait_ms = (t0 + t - t1) * 1000;
    c->last_gpu_ms = -1;
    if (c->profile && r == VK_SUCCESS) {
        uint64_t ts[2];
        if (vkGetQueryPoolResults(c->dev, c->qpool, 0, 2, sizeof(ts), ts, 8, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            c->last_gpu_ms = (ts[1] - ts[0]) * (double)c->props.limits.timestampPeriod / 1e6;
    }
    vkDestroyFence(c->dev, fence, NULL);
    vkFreeCommandBuffers(c->dev, c->pool, 1, &cb);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "dispatch %ux%ux%u failed: VkResult %d after %.3f s\n", gx, gy, gz, r, t);
        return -1.0;
    }
    return t;
}
