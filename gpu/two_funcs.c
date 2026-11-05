/* Two unrelated GPU functions, submitted from two host threads.
   Not a Toplethos host. A GPU does not pin these to two cores.
   If the device offers two compute queues, they may run at once.
   If it offers one, they run one after the other, and the program says so.

   Linux: gcc -O2 -o two_funcs two_funcs.c -ldl -pthread && ./two_funcs
*/

#define VK_NO_PROTOTYPES
#include "vulkan_core.h"
#include "two_spv.inc"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { k_n = 1 << 20, k_local = 64, k_rounds = 128 };

#ifdef _WIN32
#include <windows.h>
static void *open_vulkan(void) { return (void *)LoadLibraryA("vulkan-1.dll"); }
static void *vk_sym(void *lib, const char *name) { return (void *)GetProcAddress((HMODULE)lib, name); }
#else
#include <dlfcn.h>
static void *open_vulkan(void) { return dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL); }
static void *vk_sym(void *lib, const char *name) { return dlsym(lib, name); }
#endif

static void fail(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static void check(VkResult r, const char *what) {
    if (r != VK_SUCCESS) {
        fprintf(stderr, "%s failed (%d)\n", what, (int)r);
        exit(1);
    }
}

static void *must(PFN_vkGetInstanceProcAddr get, VkInstance inst, const char *name) {
    void *p = (void *)get(inst, name);
    if (!p) {
        fprintf(stderr, "missing %s\n", name);
        exit(1);
    }
    return p;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static double span_ms(PFN_vkGetQueryPoolResults getres, VkDevice dev, VkQueryPool qp, float period) {
    uint64_t ticks[2];
    if (!qp) return -1;
    if (getres(dev, qp, 0, 2, sizeof ticks, ticks, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS) return -1;
    if (ticks[1] < ticks[0]) return -1;
    return (double)(ticks[1] - ticks[0]) * (double)period / 1e6;
}

static int mem_type(const VkPhysicalDeviceMemoryProperties *mp, uint32_t bits, VkMemoryPropertyFlags want) {
    uint32_t i;
    for (i = 0; i < mp->memoryTypeCount; i++) {
        if ((bits & (1u << i)) == 0) continue;
        if ((mp->memoryTypes[i].propertyFlags & want) != want) continue;
        return (int)i;
    }
    return -1;
}

static uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static uint32_t fn_f(uint32_t v) {
    uint32_t i;
    for (i = 0; i < k_rounds; i++) {
        v ^= v << 13;
        v ^= v >> 17;
        v ^= v << 5;
        v *= 0x9E3779B1u;
    }
    return v;
}

static uint32_t fn_g(uint32_t v) {
    uint32_t i, s = 0;
    for (i = 0; i < k_rounds; i++) {
        s += v * (i + 3u);
        v = (v << 7) | (v >> 25);
        v ^= 0xA5A5A5A5u;
    }
    return s ^ v;
}

typedef struct Job {
    VkQueue queue;
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore wait_sem;
    VkDevice dev;
    PFN_vkQueueSubmit submit;
    PFN_vkWaitForFences wait;
    double ms;
    VkResult err;
} Job;

static void *job_main(void *arg) {
    Job *j = arg;
    VkSubmitInfo sub;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    memset(&sub, 0, sizeof sub);
    sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &j->cmd;
    if (j->wait_sem) {
        sub.waitSemaphoreCount = 1;
        sub.pWaitSemaphores = &j->wait_sem;
        sub.pWaitDstStageMask = &wait_stage;
    }
    double t0 = now_ms();
    j->err = j->submit(j->queue, 1, &sub, j->fence);
    if (j->err == VK_SUCCESS) j->err = j->wait(j->dev, 1, &j->fence, VK_TRUE, UINT64_MAX);
    j->ms = now_ms() - t0;
    return NULL;
}

static void run_job(Job *j) { job_main(j); }

int main(void) {
    void *lib;
    PFN_vkGetInstanceProcAddr get;
    PFN_vkCreateInstance CreateInstance;
    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetQueueFamilies;
    PFN_vkGetPhysicalDeviceMemoryProperties GetMemoryProperties;
    PFN_vkCreateDevice CreateDevice;
    PFN_vkDestroyDevice DestroyDevice;
    PFN_vkGetDeviceQueue GetDeviceQueue;
    PFN_vkCreateBuffer CreateBuffer;
    PFN_vkDestroyBuffer DestroyBuffer;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
    PFN_vkAllocateMemory AllocateMemory;
    PFN_vkFreeMemory FreeMemory;
    PFN_vkBindBufferMemory BindBufferMemory;
    PFN_vkMapMemory MapMemory;
    PFN_vkUnmapMemory UnmapMemory;
    PFN_vkCreateShaderModule CreateShaderModule;
    PFN_vkDestroyShaderModule DestroyShaderModule;
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout;
    PFN_vkCreatePipelineLayout CreatePipelineLayout;
    PFN_vkDestroyPipelineLayout DestroyPipelineLayout;
    PFN_vkCreateComputePipelines CreateComputePipelines;
    PFN_vkDestroyPipeline DestroyPipeline;
    PFN_vkCreateDescriptorPool CreateDescriptorPool;
    PFN_vkDestroyDescriptorPool DestroyDescriptorPool;
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets;
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets;
    PFN_vkCreateCommandPool CreateCommandPool;
    PFN_vkDestroyCommandPool DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkBeginCommandBuffer BeginCommandBuffer;
    PFN_vkEndCommandBuffer EndCommandBuffer;
    PFN_vkCmdCopyBuffer CmdCopyBuffer;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkCmdBindPipeline CmdBindPipeline;
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets;
    PFN_vkCmdDispatch CmdDispatch;
    PFN_vkCreateFence CreateFence;
    PFN_vkDestroyFence DestroyFence;
    PFN_vkCreateSemaphore CreateSemaphore;
    PFN_vkDestroySemaphore DestroySemaphore;
    PFN_vkResetFences ResetFences;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkWaitForFences WaitForFences;
    PFN_vkCreateQueryPool CreateQueryPool;
    PFN_vkDestroyQueryPool DestroyQueryPool;
    PFN_vkCmdResetQueryPool CmdResetQueryPool;
    PFN_vkCmdWriteTimestamp CmdWriteTimestamp;
    PFN_vkGetQueryPoolResults GetQueryPoolResults;
    VkQueryPool qp_up = VK_NULL_HANDLE, qp_f = VK_NULL_HANDLE, qp_g = VK_NULL_HANDLE;
    VkQueryPool qp_df = VK_NULL_HANDLE, qp_dg = VK_NULL_HANDLE;
    VkApplicationInfo app;
    VkInstanceCreateInfo ici;
    VkInstance inst = VK_NULL_HANDLE;
    uint32_t ndev = 0, i, fam_n = 0;
    VkPhysicalDevice *devs = NULL;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props, best;
    int have = 0;
    VkQueueFamilyProperties *fams = NULL;
    int fam_a = -1, fam_b = -1, nq = 1;
    float prios[2] = {1.f, 1.f};
    VkDeviceQueueCreateInfo qcis[2];
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue_f = VK_NULL_HANDLE, queue_g = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memp;
    VkBufferCreateInfo bci;
    VkBuffer xin = VK_NULL_HANDLE, yf = VK_NULL_HANDLE, yg = VK_NULL_HANDLE, stage = VK_NULL_HANDLE;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo mai;
    VkDeviceMemory mx = VK_NULL_HANDLE, mf = VK_NULL_HANDLE, mg = VK_NULL_HANDLE, ms = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smi;
    VkShaderModule mod = VK_NULL_HANDLE;
    VkDescriptorSetLayoutBinding binds[2];
    VkDescriptorSetLayoutCreateInfo lci;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkPipelineLayoutCreateInfo plci;
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkSpecializationMapEntry ent;
    VkSpecializationInfo spec;
    VkPipelineShaderStageCreateInfo stage_info;
    VkComputePipelineCreateInfo pci;
    VkPipeline pipe_f = VK_NULL_HANDLE, pipe_g = VK_NULL_HANDLE;
    VkDescriptorPoolSize psz;
    VkDescriptorPoolCreateInfo pooli;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo dsai;
    VkDescriptorSetLayout layouts[2];
    VkDescriptorSet sets[2];
    VkDescriptorBufferInfo bi[2];
    VkWriteDescriptorSet wr[2];
    VkCommandPoolCreateInfo cpci;
    VkCommandPool pool_a = VK_NULL_HANDLE, pool_b = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cbai;
    VkCommandBuffer cmd_up = VK_NULL_HANDLE, cmd_f = VK_NULL_HANDLE, cmd_g = VK_NULL_HANDLE;
    VkCommandBuffer cmd_down_f = VK_NULL_HANDLE, cmd_down_g = VK_NULL_HANDLE;
    VkCommandBufferBeginInfo begin;
    VkBufferCopy copy;
    VkBufferMemoryBarrier bar;
    VkFenceCreateInfo fci;
    VkFence fence_up = VK_NULL_HANDLE, fence_f = VK_NULL_HANDLE, fence_g = VK_NULL_HANDLE;
    VkSemaphore sem_f = VK_NULL_HANDLE, sem_g = VK_NULL_HANDLE;
    VkSemaphoreCreateInfo sci;
    VkSubmitInfo sub;
    uint32_t *xs = NULL, *scratch = NULL;
    size_t bytes = (size_t)k_n * sizeof(uint32_t);
    uint32_t which;
    Job job_f, job_g;
    double alone_f = 0, alone_g = 0, both = 0, t0;
    double fill_ms = 0, host_in_ms = 0, host_out_f = 0, host_out_g = 0;
    double gpu_in_ms = -1, gpu_f_ms = -1, gpu_g_ms = -1, gpu_out_f = -1, gpu_out_g = -1;
    double insert_wait = 0;
    int vram = -1, hostmem = -1;
    const char *kind;

    lib = open_vulkan();
    if (!lib) fail("libvulkan did not load");
    get = (PFN_vkGetInstanceProcAddr)vk_sym(lib, "vkGetInstanceProcAddr");
    if (!get) fail("vulkan loader has no vkGetInstanceProcAddr");
    CreateInstance = (PFN_vkCreateInstance)must(get, NULL, "vkCreateInstance");
    memset(&app, 0, sizeof app);
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "two_funcs";
    app.apiVersion = VK_API_VERSION_1_2;
    memset(&ici, 0, sizeof ici);
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    check(CreateInstance(&ici, NULL, &inst), "vkCreateInstance");

    EnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)must(get, inst, "vkEnumeratePhysicalDevices");
    GetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)must(get, inst, "vkGetPhysicalDeviceProperties");
    GetQueueFamilies = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)must(get, inst, "vkGetPhysicalDeviceQueueFamilyProperties");
    GetMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)must(get, inst, "vkGetPhysicalDeviceMemoryProperties");
    CreateDevice = (PFN_vkCreateDevice)must(get, inst, "vkCreateDevice");
    DestroyInstance = (PFN_vkDestroyInstance)must(get, inst, "vkDestroyInstance");
    check(EnumeratePhysicalDevices(inst, &ndev, NULL), "count devices");
    if (!ndev) fail("no Vulkan device");
    devs = (VkPhysicalDevice *)calloc(ndev, sizeof *devs);
    if (!devs) fail("out of memory");
    check(EnumeratePhysicalDevices(inst, &ndev, devs), "list devices");
    memset(&best, 0, sizeof best);
    for (i = 0; i < ndev; i++) {
        VkPhysicalDeviceProperties p;
        GetPhysicalDeviceProperties(devs[i], &p);
        if (p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
            p.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) continue;
        if (strstr(p.deviceName, "llvmpipe") || strstr(p.deviceName, "SwiftShader")) continue;
        if (!have || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            gpu = devs[i];
            best = p;
            have = 1;
            if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) break;
        }
    }
    if (!have) fail("no GPU. A CPU Vulkan device does not count.");
    props = best;
    GetQueueFamilies(gpu, &fam_n, NULL);
    fams = (VkQueueFamilyProperties *)calloc(fam_n, sizeof *fams);
    if (!fams) fail("out of memory");
    GetQueueFamilies(gpu, &fam_n, fams);
    printf("device: %s\n", props.deviceName);
    for (i = 0; i < fam_n; i++) {
        if ((fams[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0) continue;
        printf("compute family %u: %u queue%s, flags 0x%x\n", i, fams[i].queueCount, fams[i].queueCount == 1 ? "" : "s", fams[i].queueFlags);
        if (fam_a < 0) fam_a = (int)i;
        else if (fam_b < 0) fam_b = (int)i;
    }
    if (fam_a < 0) fail("GPU has no compute queue");
    memset(qcis, 0, sizeof qcis);
    if (fam_b >= 0) {
        nq = 2;
        qcis[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qcis[0].queueFamilyIndex = (uint32_t)fam_a;
        qcis[0].queueCount = 1;
        qcis[0].pQueuePriorities = &prios[0];
        qcis[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qcis[1].queueFamilyIndex = (uint32_t)fam_b;
        qcis[1].queueCount = 1;
        qcis[1].pQueuePriorities = &prios[1];
    } else if (fams[fam_a].queueCount >= 2) {
        nq = 2;
        qcis[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qcis[0].queueFamilyIndex = (uint32_t)fam_a;
        qcis[0].queueCount = 2;
        qcis[0].pQueuePriorities = prios;
    } else {
        nq = 1;
        qcis[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qcis[0].queueFamilyIndex = (uint32_t)fam_a;
        qcis[0].queueCount = 1;
        qcis[0].pQueuePriorities = &prios[0];
    }
    memset(&dci, 0, sizeof dci);
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = fam_b >= 0 ? 2u : 1u;
    dci.pQueueCreateInfos = qcis;
    check(CreateDevice(gpu, &dci, NULL, &dev), "vkCreateDevice");

    DestroyDevice = (PFN_vkDestroyDevice)must(get, inst, "vkDestroyDevice");
    GetDeviceQueue = (PFN_vkGetDeviceQueue)must(get, inst, "vkGetDeviceQueue");
    CreateBuffer = (PFN_vkCreateBuffer)must(get, inst, "vkCreateBuffer");
    DestroyBuffer = (PFN_vkDestroyBuffer)must(get, inst, "vkDestroyBuffer");
    GetBufferMemoryRequirements = (PFN_vkGetBufferMemoryRequirements)must(get, inst, "vkGetBufferMemoryRequirements");
    AllocateMemory = (PFN_vkAllocateMemory)must(get, inst, "vkAllocateMemory");
    FreeMemory = (PFN_vkFreeMemory)must(get, inst, "vkFreeMemory");
    BindBufferMemory = (PFN_vkBindBufferMemory)must(get, inst, "vkBindBufferMemory");
    MapMemory = (PFN_vkMapMemory)must(get, inst, "vkMapMemory");
    UnmapMemory = (PFN_vkUnmapMemory)must(get, inst, "vkUnmapMemory");
    CreateShaderModule = (PFN_vkCreateShaderModule)must(get, inst, "vkCreateShaderModule");
    DestroyShaderModule = (PFN_vkDestroyShaderModule)must(get, inst, "vkDestroyShaderModule");
    CreateDescriptorSetLayout = (PFN_vkCreateDescriptorSetLayout)must(get, inst, "vkCreateDescriptorSetLayout");
    DestroyDescriptorSetLayout = (PFN_vkDestroyDescriptorSetLayout)must(get, inst, "vkDestroyDescriptorSetLayout");
    CreatePipelineLayout = (PFN_vkCreatePipelineLayout)must(get, inst, "vkCreatePipelineLayout");
    DestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)must(get, inst, "vkDestroyPipelineLayout");
    CreateComputePipelines = (PFN_vkCreateComputePipelines)must(get, inst, "vkCreateComputePipelines");
    DestroyPipeline = (PFN_vkDestroyPipeline)must(get, inst, "vkDestroyPipeline");
    CreateDescriptorPool = (PFN_vkCreateDescriptorPool)must(get, inst, "vkCreateDescriptorPool");
    DestroyDescriptorPool = (PFN_vkDestroyDescriptorPool)must(get, inst, "vkDestroyDescriptorPool");
    AllocateDescriptorSets = (PFN_vkAllocateDescriptorSets)must(get, inst, "vkAllocateDescriptorSets");
    UpdateDescriptorSets = (PFN_vkUpdateDescriptorSets)must(get, inst, "vkUpdateDescriptorSets");
    CreateCommandPool = (PFN_vkCreateCommandPool)must(get, inst, "vkCreateCommandPool");
    DestroyCommandPool = (PFN_vkDestroyCommandPool)must(get, inst, "vkDestroyCommandPool");
    AllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)must(get, inst, "vkAllocateCommandBuffers");
    BeginCommandBuffer = (PFN_vkBeginCommandBuffer)must(get, inst, "vkBeginCommandBuffer");
    EndCommandBuffer = (PFN_vkEndCommandBuffer)must(get, inst, "vkEndCommandBuffer");
    CmdCopyBuffer = (PFN_vkCmdCopyBuffer)must(get, inst, "vkCmdCopyBuffer");
    CmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)must(get, inst, "vkCmdPipelineBarrier");
    CmdBindPipeline = (PFN_vkCmdBindPipeline)must(get, inst, "vkCmdBindPipeline");
    CmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)must(get, inst, "vkCmdBindDescriptorSets");
    CmdDispatch = (PFN_vkCmdDispatch)must(get, inst, "vkCmdDispatch");
    CreateFence = (PFN_vkCreateFence)must(get, inst, "vkCreateFence");
    DestroyFence = (PFN_vkDestroyFence)must(get, inst, "vkDestroyFence");
    CreateSemaphore = (PFN_vkCreateSemaphore)must(get, inst, "vkCreateSemaphore");
    DestroySemaphore = (PFN_vkDestroySemaphore)must(get, inst, "vkDestroySemaphore");
    ResetFences = (PFN_vkResetFences)must(get, inst, "vkResetFences");
    QueueSubmit = (PFN_vkQueueSubmit)must(get, inst, "vkQueueSubmit");
    WaitForFences = (PFN_vkWaitForFences)must(get, inst, "vkWaitForFences");
    CreateQueryPool = (PFN_vkCreateQueryPool)get(inst, "vkCreateQueryPool");
    DestroyQueryPool = (PFN_vkDestroyQueryPool)get(inst, "vkDestroyQueryPool");
    CmdResetQueryPool = (PFN_vkCmdResetQueryPool)get(inst, "vkCmdResetQueryPool");
    CmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)get(inst, "vkCmdWriteTimestamp");
    GetQueryPoolResults = (PFN_vkGetQueryPoolResults)get(inst, "vkGetQueryPoolResults");
    GetDeviceQueue(dev, (uint32_t)fam_a, 0, &queue_f);
    if (nq == 2 && fam_b >= 0) GetDeviceQueue(dev, (uint32_t)fam_b, 0, &queue_g);
    else if (nq == 2) GetDeviceQueue(dev, (uint32_t)fam_a, 1, &queue_g);
    else queue_g = queue_f;

    GetMemoryProperties(gpu, &memp);
    memset(&bci, 0, sizeof bci);
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(CreateBuffer(dev, &bci, NULL, &xin), "input buffer");
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    check(CreateBuffer(dev, &bci, NULL, &yf), "f buffer");
    check(CreateBuffer(dev, &bci, NULL, &yg), "g buffer");
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(CreateBuffer(dev, &bci, NULL, &stage), "stage");
    GetBufferMemoryRequirements(dev, xin, &req);
    vram = mem_type(&memp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vram < 0) fail("no device memory");
    memset(&mai, 0, sizeof mai);
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = (uint32_t)vram;
    check(AllocateMemory(dev, &mai, NULL, &mx), "input memory");
    check(BindBufferMemory(dev, xin, mx, 0), "bind input");
    GetBufferMemoryRequirements(dev, yf, &req);
    mai.allocationSize = req.size;
    check(AllocateMemory(dev, &mai, NULL, &mf), "f memory");
    check(BindBufferMemory(dev, yf, mf, 0), "bind f");
    GetBufferMemoryRequirements(dev, yg, &req);
    mai.allocationSize = req.size;
    check(AllocateMemory(dev, &mai, NULL, &mg), "g memory");
    check(BindBufferMemory(dev, yg, mg, 0), "bind g");
    GetBufferMemoryRequirements(dev, stage, &req);
    hostmem = mem_type(&memp, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (hostmem < 0) fail("no host memory");
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = (uint32_t)hostmem;
    check(AllocateMemory(dev, &mai, NULL, &ms), "stage memory");
    check(BindBufferMemory(dev, stage, ms, 0), "bind stage");

    xs = (uint32_t *)malloc(bytes);
    scratch = (uint32_t *)malloc(bytes);
    if (!xs || !scratch) fail("out of memory");
    t0 = now_ms();
    for (i = 0; i < k_n; i++) xs[i] = mix(i + 1u);
    fill_ms = now_ms() - t0;
    t0 = now_ms();
    {
        void *mapped = NULL;
        check(MapMemory(dev, ms, 0, bytes, 0, &mapped), "map");
        memcpy(mapped, xs, bytes);
        UnmapMemory(dev, ms);
    }
    host_in_ms = now_ms() - t0;

    memset(&smi, 0, sizeof smi);
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = sizeof k_spv;
    smi.pCode = k_spv;
    check(CreateShaderModule(dev, &smi, NULL, &mod), "shader");
    memset(binds, 0, sizeof binds);
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1] = binds[0];
    binds[1].binding = 1;
    memset(&lci, 0, sizeof lci);
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = 2;
    lci.pBindings = binds;
    check(CreateDescriptorSetLayout(dev, &lci, NULL, &layout), "layout");
    memset(&plci, 0, sizeof plci);
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &layout;
    check(CreatePipelineLayout(dev, &plci, NULL, &pl), "pipeline layout");
    memset(&ent, 0, sizeof ent);
    ent.constantID = 0;
    ent.offset = 0;
    ent.size = sizeof which;
    memset(&spec, 0, sizeof spec);
    spec.mapEntryCount = 1;
    spec.pMapEntries = &ent;
    spec.dataSize = sizeof which;
    spec.pData = &which;
    memset(&stage_info, 0, sizeof stage_info);
    stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_info.module = mod;
    stage_info.pName = "main";
    stage_info.pSpecializationInfo = &spec;
    memset(&pci, 0, sizeof pci);
    pci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pci.stage = stage_info;
    pci.layout = pl;
    which = 0;
    check(CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, NULL, &pipe_f), "pipeline f");
    which = 1;
    check(CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, NULL, &pipe_g), "pipeline g");

    memset(&psz, 0, sizeof psz);
    psz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    psz.descriptorCount = 4;
    memset(&pooli, 0, sizeof pooli);
    pooli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pooli.maxSets = 2;
    pooli.poolSizeCount = 1;
    pooli.pPoolSizes = &psz;
    check(CreateDescriptorPool(dev, &pooli, NULL, &pool), "descriptor pool");
    layouts[0] = layout;
    layouts[1] = layout;
    memset(&dsai, 0, sizeof dsai);
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = pool;
    dsai.descriptorSetCount = 2;
    dsai.pSetLayouts = layouts;
    check(AllocateDescriptorSets(dev, &dsai, sets), "descriptor sets");
    memset(wr, 0, sizeof wr);
    bi[0].buffer = xin;
    bi[0].offset = 0;
    bi[0].range = VK_WHOLE_SIZE;
    bi[1].buffer = yf;
    bi[1].offset = 0;
    bi[1].range = VK_WHOLE_SIZE;
    wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr[0].dstSet = sets[0];
    wr[0].dstBinding = 0;
    wr[0].descriptorCount = 1;
    wr[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wr[0].pBufferInfo = &bi[0];
    wr[1] = wr[0];
    wr[1].dstBinding = 1;
    wr[1].pBufferInfo = &bi[1];
    UpdateDescriptorSets(dev, 2, wr, 0, NULL);
    bi[1].buffer = yg;
    wr[0].dstSet = sets[1];
    wr[1].dstSet = sets[1];
    wr[1].pBufferInfo = &bi[1];
    UpdateDescriptorSets(dev, 2, wr, 0, NULL);

    memset(&cpci, 0, sizeof cpci);
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = (uint32_t)fam_a;
    check(CreateCommandPool(dev, &cpci, NULL, &pool_a), "pool a");
    if (fam_b >= 0) {
        cpci.queueFamilyIndex = (uint32_t)fam_b;
        check(CreateCommandPool(dev, &cpci, NULL, &pool_b), "pool b");
    } else {
        pool_b = pool_a;
    }
    memset(&cbai, 0, sizeof cbai);
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandPool = pool_a;
    cbai.commandBufferCount = 1;
    check(AllocateCommandBuffers(dev, &cbai, &cmd_up), "cmd up");
    check(AllocateCommandBuffers(dev, &cbai, &cmd_f), "cmd f");
    check(AllocateCommandBuffers(dev, &cbai, &cmd_down_f), "cmd down f");
    cbai.commandPool = pool_b;
    check(AllocateCommandBuffers(dev, &cbai, &cmd_g), "cmd g");
    check(AllocateCommandBuffers(dev, &cbai, &cmd_down_g), "cmd down g");

    if (CreateQueryPool && DestroyQueryPool && CmdResetQueryPool && CmdWriteTimestamp && GetQueryPoolResults &&
        props.apiVersion >= VK_API_VERSION_1_2 && fams[fam_a].timestampValidBits &&
        (fam_b < 0 || fams[fam_b].timestampValidBits)) {
        VkQueryPoolCreateInfo qpi;
        VkQueryPool *slots[5];
        int s;
        memset(&qpi, 0, sizeof qpi);
        qpi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qpi.queryCount = 2;
        slots[0] = &qp_up;
        slots[1] = &qp_f;
        slots[2] = &qp_g;
        slots[3] = &qp_df;
        slots[4] = &qp_dg;
        for (s = 0; s < 5; s++) {
            if (CreateQueryPool(dev, &qpi, NULL, slots[s]) != VK_SUCCESS) *slots[s] = VK_NULL_HANDLE;
        }
    }

    memset(&begin, 0, sizeof begin);
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    memset(&copy, 0, sizeof copy);
    copy.size = bytes;
    check(BeginCommandBuffer(cmd_up, &begin), "begin up");
    if (qp_up) {
        CmdResetQueryPool(cmd_up, qp_up, 0, 2);
        CmdWriteTimestamp(cmd_up, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_up, 0);
    }
    CmdCopyBuffer(cmd_up, stage, xin, 1, &copy);
    if (qp_up) CmdWriteTimestamp(cmd_up, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_up, 1);
    check(EndCommandBuffer(cmd_up), "end up");

    memset(&bar, 0, sizeof bar);
    bar.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.size = VK_WHOLE_SIZE;
    check(BeginCommandBuffer(cmd_f, &begin), "begin f");
    bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bar.buffer = xin;
    CmdPipelineBarrier(cmd_f, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 1, &bar, 0, NULL);
    CmdBindPipeline(cmd_f, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_f);
    CmdBindDescriptorSets(cmd_f, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &sets[0], 0, NULL);
    if (qp_f) {
        CmdResetQueryPool(cmd_f, qp_f, 0, 2);
        CmdWriteTimestamp(cmd_f, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_f, 0);
    }
    CmdDispatch(cmd_f, k_n / k_local, 1, 1);
    if (qp_f) CmdWriteTimestamp(cmd_f, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_f, 1);
    check(EndCommandBuffer(cmd_f), "end f");
    check(BeginCommandBuffer(cmd_g, &begin), "begin g");
    CmdPipelineBarrier(cmd_g, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 1, &bar, 0, NULL);
    CmdBindPipeline(cmd_g, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_g);
    CmdBindDescriptorSets(cmd_g, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &sets[1], 0, NULL);
    if (qp_g) {
        CmdResetQueryPool(cmd_g, qp_g, 0, 2);
        CmdWriteTimestamp(cmd_g, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_g, 0);
    }
    CmdDispatch(cmd_g, k_n / k_local, 1, 1);
    if (qp_g) CmdWriteTimestamp(cmd_g, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_g, 1);
    check(EndCommandBuffer(cmd_g), "end g");

    check(BeginCommandBuffer(cmd_down_f, &begin), "begin down f");
    bar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bar.buffer = yf;
    CmdPipelineBarrier(cmd_down_f, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &bar, 0, NULL);
    if (qp_df) {
        CmdResetQueryPool(cmd_down_f, qp_df, 0, 2);
        CmdWriteTimestamp(cmd_down_f, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_df, 0);
    }
    CmdCopyBuffer(cmd_down_f, yf, stage, 1, &copy);
    if (qp_df) CmdWriteTimestamp(cmd_down_f, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_df, 1);
    check(EndCommandBuffer(cmd_down_f), "end down f");
    check(BeginCommandBuffer(cmd_down_g, &begin), "begin down g");
    bar.buffer = yg;
    CmdPipelineBarrier(cmd_down_g, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &bar, 0, NULL);
    if (qp_dg) {
        CmdResetQueryPool(cmd_down_g, qp_dg, 0, 2);
        CmdWriteTimestamp(cmd_down_g, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_dg, 0);
    }
    CmdCopyBuffer(cmd_down_g, yg, stage, 1, &copy);
    if (qp_dg) CmdWriteTimestamp(cmd_down_g, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_dg, 1);
    check(EndCommandBuffer(cmd_down_g), "end down g");

    memset(&fci, 0, sizeof fci);
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    check(CreateFence(dev, &fci, NULL, &fence_up), "fence up");
    check(CreateFence(dev, &fci, NULL, &fence_f), "fence f");
    check(CreateFence(dev, &fci, NULL, &fence_g), "fence g");
    memset(&sci, 0, sizeof sci);
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    check(CreateSemaphore(dev, &sci, NULL, &sem_f), "sem f");
    check(CreateSemaphore(dev, &sci, NULL, &sem_g), "sem g");
    memset(&sub, 0, sizeof sub);
    sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd_up;
    sub.signalSemaphoreCount = 2;
    {
        VkSemaphore sigs[2];
        sigs[0] = sem_f;
        sigs[1] = sem_g;
        sub.pSignalSemaphores = sigs;
        t0 = now_ms();
        check(QueueSubmit(queue_f, 1, &sub, fence_up), "submit up");
    }
    check(WaitForFences(dev, 1, &fence_up, VK_TRUE, UINT64_MAX), "wait up");
    insert_wait = now_ms() - t0;
    if (qp_up) gpu_in_ms = span_ms(GetQueryPoolResults, dev, qp_up, props.limits.timestampPeriod);

    memset(&job_f, 0, sizeof job_f);
    memset(&job_g, 0, sizeof job_g);
    job_f.queue = queue_f;
    job_f.cmd = cmd_f;
    job_f.fence = fence_f;
    job_f.dev = dev;
    job_f.submit = QueueSubmit;
    job_f.wait = WaitForFences;
    job_f.wait_sem = sem_f;
    job_g.queue = queue_g;
    job_g.cmd = cmd_g;
    job_g.fence = fence_g;
    job_g.dev = dev;
    job_g.submit = QueueSubmit;
    job_g.wait = WaitForFences;
    job_g.wait_sem = sem_g;

    run_job(&job_f);
    check(job_f.err, "f alone");
    alone_f = job_f.ms;
    if (qp_f) gpu_f_ms = span_ms(GetQueryPoolResults, dev, qp_f, props.limits.timestampPeriod);
    run_job(&job_g);
    check(job_g.err, "g alone");
    alone_g = job_g.ms;
    if (qp_g) gpu_g_ms = span_ms(GetQueryPoolResults, dev, qp_g, props.limits.timestampPeriod);

    if (nq == 2) {
        pthread_t th_f, th_g;
        check(ResetFences(dev, 1, &fence_f), "reset f");
        check(ResetFences(dev, 1, &fence_g), "reset g");
        job_f.wait_sem = VK_NULL_HANDLE;
        job_g.wait_sem = VK_NULL_HANDLE;
        t0 = now_ms();
        if (pthread_create(&th_f, NULL, job_main, &job_f) != 0) fail("thread f");
        if (pthread_create(&th_g, NULL, job_main, &job_g) != 0) fail("thread g");
        pthread_join(th_f, NULL);
        pthread_join(th_g, NULL);
        both = now_ms() - t0;
        check(job_f.err, "f beside g");
        check(job_g.err, "g beside f");
    }

    sub.signalSemaphoreCount = 0;
    sub.pSignalSemaphores = NULL;
    sub.pCommandBuffers = &cmd_down_f;
    check(ResetFences(dev, 1, &fence_f), "reset f for copy");
    check(QueueSubmit(queue_f, 1, &sub, fence_f), "submit down f");
    check(WaitForFences(dev, 1, &fence_f, VK_TRUE, UINT64_MAX), "wait down f");
    if (qp_df) gpu_out_f = span_ms(GetQueryPoolResults, dev, qp_df, props.limits.timestampPeriod);
    {
        void *mapped = NULL;
        uint32_t *got;
        check(MapMemory(dev, ms, 0, bytes, 0, &mapped), "map f");
        got = (uint32_t *)mapped;
        t0 = now_ms();
        memcpy(scratch, got, bytes);
        host_out_f = now_ms() - t0;
        for (i = 0; i < k_n; i++) {
            uint32_t want = fn_f(xs[i]);
            if (got[i] != want) {
                fprintf(stderr, "f mismatch at %u gpu %u cpu %u\n", i, got[i], want);
                fail("f does not match");
            }
        }
        printf("f(x0) = %u\n", got[0]);
        UnmapMemory(dev, ms);
    }
    sub.pCommandBuffers = &cmd_down_g;
    check(ResetFences(dev, 1, &fence_g), "reset g for copy");
    check(QueueSubmit(queue_g, 1, &sub, fence_g), "submit down g");
    check(WaitForFences(dev, 1, &fence_g, VK_TRUE, UINT64_MAX), "wait down g");
    if (qp_dg) gpu_out_g = span_ms(GetQueryPoolResults, dev, qp_dg, props.limits.timestampPeriod);
    {
        void *mapped = NULL;
        uint32_t *got;
        check(MapMemory(dev, ms, 0, bytes, 0, &mapped), "map g");
        got = (uint32_t *)mapped;
        t0 = now_ms();
        memcpy(scratch, got, bytes);
        host_out_g = now_ms() - t0;
        for (i = 0; i < k_n; i++) {
            uint32_t want = fn_g(xs[i]);
            if (got[i] != want) {
                fprintf(stderr, "g mismatch at %u gpu %u cpu %u\n", i, got[i], want);
                fail("g does not match");
            }
        }
        printf("g(x0) = %u\n", got[0]);
        UnmapMemory(dev, ms);
    }

    kind = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "discrete" : "integrated";
    printf("type: %s\n", kind);
    printf("lanes: %d   buffer: %.2f MiB each way\n", k_n, (double)bytes / (1024.0 * 1024.0));
    printf("f: 128 rounds of a hash\n");
    printf("g: 128 rounds of a rotate and a sum, not the same math\n");
    printf("host fill:      %8.3f ms\n", fill_ms);
    printf("host insert:    %8.3f ms   (memcpy into staging)\n", host_in_ms);
    if (gpu_in_ms >= 0) {
        double mib = (double)bytes / (1024.0 * 1024.0);
        printf("gpu insert:     %8.3f ms   %7.1f MiB/s\n", gpu_in_ms, gpu_in_ms > 0 ? mib / (gpu_in_ms / 1000.0) : 0);
        printf("insert wait:    %8.3f ms   (submit through fence)\n", insert_wait);
        printf("f calculate:    %8.3f ms\n", gpu_f_ms);
        printf("g calculate:    %8.3f ms\n", gpu_g_ms);
        printf("gpu extract f:  %8.3f ms   %7.1f MiB/s\n", gpu_out_f, gpu_out_f > 0 ? mib / (gpu_out_f / 1000.0) : 0);
        printf("gpu extract g:  %8.3f ms   %7.1f MiB/s\n", gpu_out_g, gpu_out_g > 0 ? mib / (gpu_out_g / 1000.0) : 0);
    } else {
        printf("gpu timestamps unavailable\n");
    }
    printf("host extract f: %8.3f ms\n", host_out_f);
    printf("host extract g: %8.3f ms\n", host_out_g);
    printf("f queue wait:   %8.3f ms   (driver plus calculate)\n", alone_f);
    printf("g queue wait:   %8.3f ms\n", alone_g);
    if (nq == 2) {
        printf("both:    %8.3f ms   (two host threads, two queues)\n", both);
        printf("sum:     %8.3f ms\n", alone_f + alone_g);
        if (both + 0.3 < alone_f + alone_g && both < (alone_f + alone_g) * 0.85)
            printf("overlapped: yes\n");
        else
            printf("overlapped: no  (two queues were offered, the card still ran them apart)\n");
    } else {
        printf("queues: 1\n");
        printf("overlapped: no  (this GPU did not offer a second compute queue)\n");
    }
    printf("match: yes\n");

    DestroyFence(dev, fence_up, NULL);
    DestroyFence(dev, fence_f, NULL);
    DestroyFence(dev, fence_g, NULL);
    DestroySemaphore(dev, sem_f, NULL);
    DestroySemaphore(dev, sem_g, NULL);
    if (qp_up) DestroyQueryPool(dev, qp_up, NULL);
    if (qp_f) DestroyQueryPool(dev, qp_f, NULL);
    if (qp_g) DestroyQueryPool(dev, qp_g, NULL);
    if (qp_df) DestroyQueryPool(dev, qp_df, NULL);
    if (qp_dg) DestroyQueryPool(dev, qp_dg, NULL);
    if (pool_b != pool_a) DestroyCommandPool(dev, pool_b, NULL);
    DestroyCommandPool(dev, pool_a, NULL);
    DestroyDescriptorPool(dev, pool, NULL);
    DestroyPipeline(dev, pipe_f, NULL);
    DestroyPipeline(dev, pipe_g, NULL);
    DestroyPipelineLayout(dev, pl, NULL);
    DestroyDescriptorSetLayout(dev, layout, NULL);
    DestroyShaderModule(dev, mod, NULL);
    DestroyBuffer(dev, stage, NULL);
    DestroyBuffer(dev, yg, NULL);
    DestroyBuffer(dev, yf, NULL);
    DestroyBuffer(dev, xin, NULL);
    FreeMemory(dev, ms, NULL);
    FreeMemory(dev, mg, NULL);
    FreeMemory(dev, mf, NULL);
    FreeMemory(dev, mx, NULL);
    DestroyDevice(dev, NULL);
    DestroyInstance(inst, NULL);
    free(xs);
    free(scratch);
    free(devs);
    free(fams);
    return 0;
}
