/* GPU SIMD sort. Not a Toplethos host.
   One workgroup sorts one 32*32 tile in device memory. The CPU checks with qsort.
   A CPU Vulkan device is a failure. This does not pass unless a GPU ran the shader.

   Linux:  gcc -O2 -o gpu_simd_test gpu_simd_test.c -ldl && ./gpu_simd_test
   Windows, from a developer prompt that has the Vulkan loader:
           gcc -O2 -o gpu_simd_test.exe gpu_simd_test.c
*/

#define VK_NO_PROTOTYPES
#include "vulkan_core.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static void *open_vulkan(void) { return (void *)LoadLibraryA("vulkan-1.dll"); }
static void *vk_sym(void *lib, const char *name) { return (void *)GetProcAddress((HMODULE)lib, name); }
#else
#include <dlfcn.h>
static void *open_vulkan(void) { return dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL); }
static void *vk_sym(void *lib, const char *name) { return dlsym(lib, name); }
#endif

enum { k_tile = 1024, k_tiles = 4096 };

#include "sort_spv.inc"

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

static PFN_vkVoidFunction must(PFN_vkGetInstanceProcAddr get, VkInstance inst, const char *name) {
    PFN_vkVoidFunction p = get(inst, name);
    if (!p) {
        fprintf(stderr, "vulkan is missing %s\n", name);
        exit(1);
    }
    return p;
}

static uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static int cmp_u32(const void *pa, const void *pb) {
    uint32_t a = *(const uint32_t *)pa;
    uint32_t b = *(const uint32_t *)pb;
    return (a > b) - (a < b);
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* Top byte is brightness. The low 24 bits are the color the terminal draws. */
static uint32_t pack(uint32_t n) {
    uint32_t r = mix(n * 3u + 1u) & 255u;
    uint32_t g = mix(n * 3u + 2u) & 255u;
    uint32_t b = mix(n * 3u + 3u) & 255u;
    uint32_t y = (r * 54u + g * 183u + b * 19u) >> 8;
    return (y << 24) | (r << 16) | (g << 8) | b;
}

static void draw_tile(const char *label, const uint32_t *tile) {
    static const char ramp[] = " .:-=+*#%@";
    uint32_t y, x;
    printf("%s\n", label);
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x++) {
            uint32_t p = tile[y * 32u + x];
            unsigned lum = (p >> 24) & 255u;
            char ch = ramp[lum * 10u / 256u];
            printf("\033[38;2;%u;%u;%um%c", (p >> 16) & 255u, (p >> 8) & 255u, p & 255u, ch);
        }
        printf("\033[0m\n");
    }
}

static int mem_type(const VkPhysicalDeviceMemoryProperties *mp, uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags reject) {
    uint32_t i;
    for (i = 0; i < mp->memoryTypeCount; i++) {
        if ((bits & (1u << i)) == 0) continue;
        if ((mp->memoryTypes[i].propertyFlags & want) != want) continue;
        if (reject && (mp->memoryTypes[i].propertyFlags & reject)) continue;
        return (int)i;
    }
    return -1;
}

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
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkWaitForFences WaitForFences;
    PFN_vkCreateQueryPool CreateQueryPool;
    PFN_vkDestroyQueryPool DestroyQueryPool;
    PFN_vkResetQueryPool ResetQueryPool;
    PFN_vkCmdWriteTimestamp CmdWriteTimestamp;
    PFN_vkGetQueryPoolResults GetQueryPoolResults;
    VkQueryPool qpool = VK_NULL_HANDLE;
    VkApplicationInfo app;
    VkInstanceCreateInfo ici;
    VkInstance inst = VK_NULL_HANDLE;
    uint32_t ndev = 0, i, fam_n = 0, family = 0;
    VkPhysicalDevice *devs = NULL;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props, best;
    int have = 0, vram = -1, staging = -1;
    VkQueueFamilyProperties *fams = NULL;
    float prio = 1.f;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memp;
    VkBufferCreateInfo bci;
    VkBuffer gpu_buf = VK_NULL_HANDLE, stage = VK_NULL_HANDLE;
    VkMemoryRequirements greq, sreq;
    VkMemoryAllocateInfo mai;
    VkDeviceMemory gpu_mem = VK_NULL_HANDLE, stage_mem = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smi;
    VkShaderModule mod = VK_NULL_HANDLE;
    VkDescriptorSetLayoutBinding bind;
    VkDescriptorSetLayoutCreateInfo lci;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkPipelineLayoutCreateInfo plci;
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo stage_info;
    VkComputePipelineCreateInfo pci;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkDescriptorPoolSize psz;
    VkDescriptorPoolCreateInfo pooli;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo dsai;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorBufferInfo bi;
    VkWriteDescriptorSet wr;
    VkCommandPoolCreateInfo cpci;
    VkCommandPool cpool = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cbai;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferBeginInfo begin;
    VkBufferCopy copy;
    VkBufferMemoryBarrier bar;
    VkFenceCreateInfo fci;
    VkFence fence = VK_NULL_HANDLE;
    VkSubmitInfo sub;
    uint32_t *src, *ref, *got;
    size_t bytes = (size_t)k_tiles * k_tile * sizeof(uint32_t);
    uint32_t t;
    const char *kind;
    double t0, upload_ms = 0, queue_ms = 0, check_ms = 0, gpu_ms = -1;

    lib = open_vulkan();
    if (!lib) fail("libvulkan did not load. The GPU driver is not installed, or this machine has no Vulkan loader.");
    get = (PFN_vkGetInstanceProcAddr)vk_sym(lib, "vkGetInstanceProcAddr");
    if (!get) fail("vulkan loader has no vkGetInstanceProcAddr");
    CreateInstance = (PFN_vkCreateInstance)must(get, NULL, "vkCreateInstance");

    memset(&app, 0, sizeof app);
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "gpu_simd_test";
    app.apiVersion = VK_API_VERSION_1_0;
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

    check(EnumeratePhysicalDevices(inst, &ndev, NULL), "vkEnumeratePhysicalDevices");
    if (!ndev) fail("no Vulkan device");
    devs = (VkPhysicalDevice *)calloc(ndev, sizeof *devs);
    if (!devs) fail("out of memory");
    check(EnumeratePhysicalDevices(inst, &ndev, devs), "vkEnumeratePhysicalDevices");
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
    family = fam_n;
    for (i = 0; i < fam_n; i++) {
        if (fams[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { family = i; break; }
    }
    if (family == fam_n) fail("GPU has no compute queue");

    memset(&qci, 0, sizeof qci);
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    memset(&dci, 0, sizeof dci);
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
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
    QueueSubmit = (PFN_vkQueueSubmit)must(get, inst, "vkQueueSubmit");
    WaitForFences = (PFN_vkWaitForFences)must(get, inst, "vkWaitForFences");
    CreateQueryPool = (PFN_vkCreateQueryPool)get(inst, "vkCreateQueryPool");
    DestroyQueryPool = (PFN_vkDestroyQueryPool)get(inst, "vkDestroyQueryPool");
    ResetQueryPool = (PFN_vkResetQueryPool)get(inst, "vkResetQueryPool");
    CmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)get(inst, "vkCmdWriteTimestamp");
    GetQueryPoolResults = (PFN_vkGetQueryPoolResults)get(inst, "vkGetQueryPoolResults");
    if (CreateQueryPool && ResetQueryPool && CmdWriteTimestamp && GetQueryPoolResults && fams[family].timestampValidBits) {
        VkQueryPoolCreateInfo qci_pool;
        memset(&qci_pool, 0, sizeof qci_pool);
        qci_pool.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qci_pool.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qci_pool.queryCount = 2;
        if (CreateQueryPool(dev, &qci_pool, NULL, &qpool) != VK_SUCCESS) qpool = VK_NULL_HANDLE;
        else ResetQueryPool(dev, qpool, 0, 2);
    }
    GetDeviceQueue(dev, family, 0, &queue);

    GetMemoryProperties(gpu, &memp);
    memset(&bci, 0, sizeof bci);
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(CreateBuffer(dev, &bci, NULL, &gpu_buf), "device buffer");
    GetBufferMemoryRequirements(dev, gpu_buf, &greq);
    vram = mem_type(&memp, greq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (vram < 0) vram = mem_type(&memp, greq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (vram < 0) fail("GPU has no device-local memory");
    memset(&mai, 0, sizeof mai);
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = greq.size;
    mai.memoryTypeIndex = (uint32_t)vram;
    check(AllocateMemory(dev, &mai, NULL, &gpu_mem), "device memory");
    check(BindBufferMemory(dev, gpu_buf, gpu_mem, 0), "bind device memory");

    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(CreateBuffer(dev, &bci, NULL, &stage), "staging buffer");
    GetBufferMemoryRequirements(dev, stage, &sreq);
    staging = mem_type(&memp, sreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
    if (staging < 0) fail("no host-visible memory for the upload");
    mai.allocationSize = sreq.size;
    mai.memoryTypeIndex = (uint32_t)staging;
    check(AllocateMemory(dev, &mai, NULL, &stage_mem), "staging memory");
    check(BindBufferMemory(dev, stage, stage_mem, 0), "bind staging memory");

    src = (uint32_t *)malloc(bytes);
    ref = (uint32_t *)malloc(bytes);
    if (!src || !ref) fail("out of memory");
    for (t = 0; t < (uint32_t)k_tiles * k_tile; t++) src[t] = pack(t + 1u);
    memcpy(ref, src, bytes);
    t0 = now_ms();
    {
        void *mapped = NULL;
        check(MapMemory(dev, stage_mem, 0, bytes, 0, &mapped), "map");
        memcpy(mapped, src, bytes);
        UnmapMemory(dev, stage_mem);
    }
    upload_ms = now_ms() - t0;

    memset(&smi, 0, sizeof smi);
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = sizeof k_spv;
    smi.pCode = k_spv;
    check(CreateShaderModule(dev, &smi, NULL, &mod), "shader");

    memset(&bind, 0, sizeof bind);
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    memset(&lci, 0, sizeof lci);
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = 1;
    lci.pBindings = &bind;
    check(CreateDescriptorSetLayout(dev, &lci, NULL, &layout), "descriptor layout");
    memset(&plci, 0, sizeof plci);
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &layout;
    check(CreatePipelineLayout(dev, &plci, NULL, &pl), "pipeline layout");
    memset(&stage_info, 0, sizeof stage_info);
    stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_info.module = mod;
    stage_info.pName = "main";
    memset(&pci, 0, sizeof pci);
    pci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pci.stage = stage_info;
    pci.layout = pl;
    check(CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, NULL, &pipe), "compute pipeline");

    memset(&psz, 0, sizeof psz);
    psz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    psz.descriptorCount = 1;
    memset(&pooli, 0, sizeof pooli);
    pooli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pooli.maxSets = 1;
    pooli.poolSizeCount = 1;
    pooli.pPoolSizes = &psz;
    check(CreateDescriptorPool(dev, &pooli, NULL, &pool), "descriptor pool");
    memset(&dsai, 0, sizeof dsai);
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &layout;
    check(AllocateDescriptorSets(dev, &dsai, &set), "descriptor set");
    memset(&bi, 0, sizeof bi);
    bi.buffer = gpu_buf;
    bi.offset = 0;
    bi.range = VK_WHOLE_SIZE;
    memset(&wr, 0, sizeof wr);
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = set;
    wr.dstBinding = 0;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wr.pBufferInfo = &bi;
    UpdateDescriptorSets(dev, 1, &wr, 0, NULL);

    memset(&cpci, 0, sizeof cpci);
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = family;
    check(CreateCommandPool(dev, &cpci, NULL, &cpool), "command pool");
    memset(&cbai, 0, sizeof cbai);
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cpool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    check(AllocateCommandBuffers(dev, &cbai, &cmd), "command buffer");
    memset(&begin, 0, sizeof begin);
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(BeginCommandBuffer(cmd, &begin), "begin");
    memset(&copy, 0, sizeof copy);
    copy.size = bytes;
    CmdCopyBuffer(cmd, stage, gpu_buf, 1, &copy);
    memset(&bar, 0, sizeof bar);
    bar.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.buffer = gpu_buf;
    bar.size = VK_WHOLE_SIZE;
    CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 1, &bar, 0, NULL);
    CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
    if (qpool) CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, qpool, 0);
    CmdDispatch(cmd, k_tiles, 1, 1);
    if (qpool) CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, qpool, 1);
    bar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &bar, 0, NULL);
    CmdCopyBuffer(cmd, gpu_buf, stage, 1, &copy);
    check(EndCommandBuffer(cmd), "end");

    memset(&fci, 0, sizeof fci);
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    check(CreateFence(dev, &fci, NULL, &fence), "fence");
    memset(&sub, 0, sizeof sub);
    sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    t0 = now_ms();
    check(QueueSubmit(queue, 1, &sub, fence), "submit");
    check(WaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX), "wait");
    queue_ms = now_ms() - t0;
    if (qpool) {
        uint64_t ticks[2];
        if (GetQueryPoolResults(dev, qpool, 0, 2, sizeof ticks, ticks, sizeof ticks[0], VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS && ticks[1] > ticks[0])
            gpu_ms = (double)(ticks[1] - ticks[0]) * (double)props.limits.timestampPeriod / 1e6;
    }

    {
        void *mapped = NULL;
        check(MapMemory(dev, stage_mem, 0, bytes, 0, &mapped), "map result");
        got = (uint32_t *)mapped;
        t0 = now_ms();
        for (t = 0; t < k_tiles; t++) {
            uint32_t *e = ref + (size_t)t * k_tile;
            uint32_t *g = got + (size_t)t * k_tile;
            uint32_t j;
            qsort(e, k_tile, sizeof(uint32_t), cmp_u32);
            if (memcmp(e, g, k_tile * sizeof(uint32_t)) != 0) {
                for (j = 0; j < k_tile; j++) {
                    if (e[j] != g[j]) {
                        fprintf(stderr, "tile %u index %u gpu %u cpu %u\n", t, j, g[j], e[j]);
                        break;
                    }
                }
                fail("GPU result does not match the CPU sort");
            }
        }
        check_ms = now_ms() - t0;
        draw_tile("tile 0 before, noise", src);
        draw_tile("tile 0 after, dark to bright", got);
        draw_tile("tile 1 after", got + k_tile);
        UnmapMemory(dev, stage_mem);
    }

    kind = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "discrete" : "integrated";
    printf("device: %s\n", props.deviceName);
    printf("type: %s\n", kind);
    printf("memory: %s\n", (memp.memoryTypes[vram].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? "device-local+host-visible" : "device-local");
    printf("tiles: %d\n", k_tiles);
    printf("tile: 32x32\n");
    printf("pixels: %zu\n", (size_t)k_tiles * k_tile);
    printf("buffer: %zu bytes, copied up and back\n", bytes);
    printf("upload: %.3f ms\n", upload_ms);
    printf("queue: %.3f ms\n", queue_ms);
    if (gpu_ms >= 0) {
        double elems = (double)k_tiles * k_tile;
        printf("gpu: %.3f ms\n", gpu_ms);
        printf("gpu rate: %.1f million pixels/s\n", elems / gpu_ms / 1000.0);
    } else {
        printf("gpu: no timestamp\n");
    }
    printf("check: %.3f ms\n", check_ms);
    printf("match: yes\n");

    DestroyFence(dev, fence, NULL);
    if (qpool) DestroyQueryPool(dev, qpool, NULL);
    DestroyCommandPool(dev, cpool, NULL);
    DestroyDescriptorPool(dev, pool, NULL);
    DestroyPipeline(dev, pipe, NULL);
    DestroyPipelineLayout(dev, pl, NULL);
    DestroyDescriptorSetLayout(dev, layout, NULL);
    DestroyShaderModule(dev, mod, NULL);
    DestroyBuffer(dev, stage, NULL);
    DestroyBuffer(dev, gpu_buf, NULL);
    FreeMemory(dev, stage_mem, NULL);
    FreeMemory(dev, gpu_mem, NULL);
    DestroyDevice(dev, NULL);
    DestroyInstance(inst, NULL);
    free(src);
    free(ref);
    free(devs);
    free(fams);
    return 0;
}
