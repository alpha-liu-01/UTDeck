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
} vkc_ctx;

typedef struct {
    VkBuffer buf;
    VkDeviceMemory mem;
    void *map;
    VkDeviceSize size;
} vkc_buffer;

static inline void vkc_init(vkc_ctx *c)
{
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
    for (uint32_t i = 0; i < nbuf; i++)
        bind[i] = (VkDescriptorSetLayoutBinding){ .binding = i, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                  .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
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

    VkDescriptorPoolSize ps = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = nbuf };
    VkDescriptorPoolCreateInfo dpci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
    VKC_CHECK(vkCreateDescriptorPool(c->dev, &dpci, NULL, &p.dpool));
    VkDescriptorSetAllocateInfo dsai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                         .descriptorPool = p.dpool, .descriptorSetCount = 1, .pSetLayouts = &p.dsl };
    VKC_CHECK(vkAllocateDescriptorSets(c->dev, &dsai, &p.set));
    VkDescriptorBufferInfo dbi[8];
    VkWriteDescriptorSet w[8];
    for (uint32_t i = 0; i < nbuf; i++) {
        dbi[i] = (VkDescriptorBufferInfo){ .buffer = bufs[i].buf, .offset = 0, .range = VK_WHOLE_SIZE };
        w[i] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = p.set,
                                       .dstBinding = i, .descriptorCount = 1,
                                       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi[i] };
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
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->layout, 0, 1, &p->set, 0, NULL);
    vkCmdPushConstants(cb, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size, push);
    vkCmdDispatch(cb, gx, gy, gz);
    VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                           .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    VKC_CHECK(vkEndCommandBuffer(cb));
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    VKC_CHECK(vkCreateFence(c->dev, &fci, NULL, &fence));
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
    double t0 = vkc_now();
    VkResult r = vkQueueSubmit(c->queue, 1, &si, fence);
    if (r == VK_SUCCESS)
        r = vkWaitForFences(c->dev, 1, &fence, VK_TRUE, 10ull * 1000 * 1000 * 1000);
    double t = vkc_now() - t0;
    vkDestroyFence(c->dev, fence, NULL);
    vkFreeCommandBuffers(c->dev, c->pool, 1, &cb);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "dispatch %ux%ux%u failed: VkResult %d after %.3f s\n", gx, gy, gz, r, t);
        return -1.0;
    }
    return t;
}
