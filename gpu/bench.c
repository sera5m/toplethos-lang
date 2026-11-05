/* Five-second sustained GPU runs. Not a Toplethos host.
   The device stays open. The timed part is only the resubmits.

   gcc -O2 -o bench bench.c -ldl && ./bench 1
   1  sequential array work
   2  mixed math, divergent, some gathers
   3  long integer chains, one load and one store
   4  a random mix of 1, 2, and 3

   Warmup is not counted. A sample of the last result is checked.
*/

#define VK_NO_PROTOTYPES
#include "vulkan_core.h"
#include "bench_spv.inc"

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

enum { k_n = 32 * 1024 * 1024, k_local = 256, k_mask = k_n - 1 };

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

static int mem_type(const VkPhysicalDeviceMemoryProperties *mp, uint32_t bits, VkMemoryPropertyFlags want) {
    uint32_t i;
    for (i = 0; i < mp->memoryTypeCount; i++) {
        if ((bits & (1u << i)) == 0) continue;
        if ((mp->memoryTypes[i].propertyFlags & want) != want) continue;
        return (int)i;
    }
    return -1;
}

static uint32_t cpu_stream(uint32_t v) {
    uint32_t a = v, b = v ^ 0x9e3779b9u, c = v + 1u, d = v * 3u, k;
    for (k = 0; k < 32u; k++) {
        a = a * 0x9E3779B1u + b;
        b = b * 0x85EBCA77u + c;
        c = c * 0xC2B2AE3Du + d;
        d = d * 0x27BB2EE7u + a;
    }
    return a ^ b ^ c ^ d;
}

static uint32_t cpu_isqrt(uint32_t v) {
    uint32_t g = v | 1u;
    g = (g + v / g) >> 1;
    g = (g + v / g) >> 1;
    g = (g + v / g) >> 1;
    g = (g + v / g) >> 1;
    return g;
}

static uint32_t cpu_disorder(const uint32_t *x, uint32_t i) {
    uint32_t sel = i & 3u;
    uint32_t v = x[i];
    if (sel == 0u) {
        uint32_t r = 11u;
        r = r * v + 7u;
        r = r * v + 5u;
        r = r * v + 3u;
        r = r * v + 1u;
        return r;
    }
    if (sel == 1u) return cpu_isqrt(v | 1u);
    if (sel == 2u) {
        uint32_t a = x[i];
        uint32_t b = x[(i + 64u) & k_mask];
        uint32_t c = x[(i + 4096u) & k_mask];
        uint32_t d = x[(i * 3u + 17u) & k_mask];
        return (a + (b << 1) + (c << 2)) ^ d;
    }
    {
        uint32_t a = x[(i - 1u) & k_mask];
        uint32_t c = x[(i + 1u) & k_mask];
        return (a + v + v + c) >> 2;
    }
}

static uint32_t cpu_arith(uint32_t v) {
    uint32_t a, b, c, d, k;
    v |= 1u;
    a = v;
    b = v + 1u;
    c = v ^ 0x11111111u;
    d = v * 7u + 3u;
    for (k = 0; k < 64u; k++) {
        a = a * 0x9E3779B1u + 1u;
        b = b * 0x85EBCA77u + 1u;
        c = c * 0xC2B2AE3Du + 1u;
        d = d * 0x27BB2EE7u + 1u;
    }
    return a ^ b ^ c ^ d;
}

int main(int argc, char **argv) {
    int mode, repeats, family = -1;
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
    PFN_vkResetFences ResetFences;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkWaitForFences WaitForFences;
    VkApplicationInfo app;
    VkInstanceCreateInfo ici;
    VkInstance inst = VK_NULL_HANDLE;
    uint32_t ndev = 0, i, fam_n = 0, groups;
    VkPhysicalDevice *devs = NULL;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props;
    VkQueueFamilyProperties *fams = NULL;
    float prio = 1.f;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memp;
    VkBufferCreateInfo bci;
    VkBuffer xin = VK_NULL_HANDLE, yout = VK_NULL_HANDLE, stage = VK_NULL_HANDLE;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo mai;
    VkDeviceMemory mx = VK_NULL_HANDLE, my = VK_NULL_HANDLE, ms = VK_NULL_HANDLE;
    VkShaderModule mods[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkPipeline pipes[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorSetLayoutBinding binds[2];
    VkDescriptorSetLayoutCreateInfo lci;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkPipelineLayoutCreateInfo plci;
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkDescriptorPoolSize psz;
    VkDescriptorPoolCreateInfo pooli;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo dsai;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorBufferInfo bi[2];
    VkWriteDescriptorSet wr[2];
    VkCommandPoolCreateInfo cpci;
    VkCommandPool cpool = VK_NULL_HANDLE;
    VkCommandBuffer cmds[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkCommandBuffer cmd_up = VK_NULL_HANDLE, cmd_down = VK_NULL_HANDLE;
    VkCommandBufferBeginInfo begin;
    VkBufferCopy copy;
    VkBufferMemoryBarrier bar;
    VkFenceCreateInfo fci;
    VkFence fence = VK_NULL_HANDLE;
    VkSubmitInfo sub;
    const uint32_t *spv[3];
    size_t spv_bytes[3];
    uint32_t *xs = NULL, *got = NULL;
    size_t bytes = (size_t)k_n * sizeof(uint32_t);
    uint64_t reps[3] = {0, 0, 0};
    int last = 0;
    uint64_t rng = 0x123456789abcdefu;
    double t0, upload_ms, elapsed = 0, deadline;
    int have = 0, vram, hostmem, m;
    const char *title[] = {"", "stream", "disorder", "arith", "mixed"};

    if (argc != 2 || argv[1][0] < '1' || argv[1][0] > '4' || argv[1][1]) {
        fprintf(stderr, "usage: ./bench 1|2|3|4\n");
        return 1;
    }
    mode = argv[1][0] - '0';
    repeats = mode == 3 ? 4 : mode == 2 ? 1 : 2;

    lib = open_vulkan();
    if (!lib) fail("libvulkan did not load");
    get = (PFN_vkGetInstanceProcAddr)vk_sym(lib, "vkGetInstanceProcAddr");
    if (!get) fail("no vkGetInstanceProcAddr");
    CreateInstance = (PFN_vkCreateInstance)must(get, NULL, "vkCreateInstance");
    memset(&app, 0, sizeof app);
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "bench";
    app.apiVersion = VK_API_VERSION_1_2;
    memset(&ici, 0, sizeof ici);
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    check(CreateInstance(&ici, NULL, &inst), "instance");
    EnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)must(get, inst, "vkEnumeratePhysicalDevices");
    GetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)must(get, inst, "vkGetPhysicalDeviceProperties");
    GetQueueFamilies = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)must(get, inst, "vkGetPhysicalDeviceQueueFamilyProperties");
    GetMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)must(get, inst, "vkGetPhysicalDeviceMemoryProperties");
    CreateDevice = (PFN_vkCreateDevice)must(get, inst, "vkCreateDevice");
    DestroyInstance = (PFN_vkDestroyInstance)must(get, inst, "vkDestroyInstance");
    check(EnumeratePhysicalDevices(inst, &ndev, NULL), "count devices");
    if (!ndev) fail("no Vulkan device");
    devs = (VkPhysicalDevice *)calloc(ndev, sizeof *devs);
    check(EnumeratePhysicalDevices(inst, &ndev, devs), "list devices");
    memset(&props, 0, sizeof props);
    for (i = 0; i < ndev; i++) {
        VkPhysicalDeviceProperties p;
        GetPhysicalDeviceProperties(devs[i], &p);
        if (p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
            p.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) continue;
        if (strstr(p.deviceName, "llvmpipe") || strstr(p.deviceName, "SwiftShader")) continue;
        if (!have || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            gpu = devs[i];
            props = p;
            have = 1;
            if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) break;
        }
    }
    if (!have) fail("no GPU");
    GetQueueFamilies(gpu, &fam_n, NULL);
    fams = (VkQueueFamilyProperties *)calloc(fam_n, sizeof *fams);
    GetQueueFamilies(gpu, &fam_n, fams);
    for (i = 0; i < fam_n; i++) {
        if (fams[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { family = (int)i; break; }
    }
    if (family < 0) fail("no compute queue");
    memset(&qci, 0, sizeof qci);
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = (uint32_t)family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    memset(&dci, 0, sizeof dci);
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    check(CreateDevice(gpu, &dci, NULL, &dev), "device");

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
    ResetFences = (PFN_vkResetFences)must(get, inst, "vkResetFences");
    QueueSubmit = (PFN_vkQueueSubmit)must(get, inst, "vkQueueSubmit");
    WaitForFences = (PFN_vkWaitForFences)must(get, inst, "vkWaitForFences");
    GetDeviceQueue(dev, (uint32_t)family, 0, &queue);
    GetMemoryProperties(gpu, &memp);

    memset(&bci, 0, sizeof bci);
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(CreateBuffer(dev, &bci, NULL, &xin), "xin");
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    check(CreateBuffer(dev, &bci, NULL, &yout), "yout");
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(CreateBuffer(dev, &bci, NULL, &stage), "stage");
    GetBufferMemoryRequirements(dev, xin, &req);
    vram = mem_type(&memp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vram < 0) fail("no device memory");
    memset(&mai, 0, sizeof mai);
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.memoryTypeIndex = (uint32_t)vram;
    mai.allocationSize = req.size;
    check(AllocateMemory(dev, &mai, NULL, &mx), "mem x");
    check(BindBufferMemory(dev, xin, mx, 0), "bind x");
    GetBufferMemoryRequirements(dev, yout, &req);
    mai.allocationSize = req.size;
    check(AllocateMemory(dev, &mai, NULL, &my), "mem y");
    check(BindBufferMemory(dev, yout, my, 0), "bind y");
    GetBufferMemoryRequirements(dev, stage, &req);
    hostmem = mem_type(&memp, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (hostmem < 0) fail("no host memory");
    mai.memoryTypeIndex = (uint32_t)hostmem;
    mai.allocationSize = req.size;
    check(AllocateMemory(dev, &mai, NULL, &ms), "mem stage");
    check(BindBufferMemory(dev, stage, ms, 0), "bind stage");

    xs = (uint32_t *)malloc(bytes);
    if (!xs) fail("out of memory");
    for (i = 0; i < k_n; i++) xs[i] = i * 0x9E3779B1u + 1u;
    {
        void *mapped = NULL;
        check(MapMemory(dev, ms, 0, bytes, 0, &mapped), "map");
        memcpy(mapped, xs, bytes);
        UnmapMemory(dev, ms);
    }

    spv[0] = k_stream;
    spv[1] = k_disorder;
    spv[2] = k_arith;
    spv_bytes[0] = sizeof k_stream;
    spv_bytes[1] = sizeof k_disorder;
    spv_bytes[2] = sizeof k_arith;
    for (m = 0; m < 3; m++) {
        VkShaderModuleCreateInfo smi;
        VkPipelineShaderStageCreateInfo stage_info;
        VkComputePipelineCreateInfo pci;
        memset(&smi, 0, sizeof smi);
        smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smi.codeSize = spv_bytes[m];
        smi.pCode = spv[m];
        check(CreateShaderModule(dev, &smi, NULL, &mods[m]), "shader");
        memset(&stage_info, 0, sizeof stage_info);
        stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage_info.module = mods[m];
        stage_info.pName = "main";
        memset(&pci, 0, sizeof pci);
        pci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pci.stage = stage_info;
        if (m == 0) {
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
            check(CreateDescriptorSetLayout(dev, &lci, NULL, &layout), "set layout");
            memset(&plci, 0, sizeof plci);
            plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            plci.setLayoutCount = 1;
            plci.pSetLayouts = &layout;
            check(CreatePipelineLayout(dev, &plci, NULL, &pl), "pipeline layout");
        }
        pci.layout = pl;
        check(CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, NULL, &pipes[m]), "pipeline");
    }

    memset(&psz, 0, sizeof psz);
    psz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    psz.descriptorCount = 2;
    memset(&pooli, 0, sizeof pooli);
    pooli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pooli.maxSets = 1;
    pooli.poolSizeCount = 1;
    pooli.pPoolSizes = &psz;
    check(CreateDescriptorPool(dev, &pooli, NULL, &pool), "pool");
    memset(&dsai, 0, sizeof dsai);
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &layout;
    check(AllocateDescriptorSets(dev, &dsai, &set), "set");
    memset(bi, 0, sizeof bi);
    bi[0].buffer = xin;
    bi[0].range = VK_WHOLE_SIZE;
    bi[1].buffer = yout;
    bi[1].range = VK_WHOLE_SIZE;
    memset(wr, 0, sizeof wr);
    wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr[0].dstSet = set;
    wr[0].dstBinding = 0;
    wr[0].descriptorCount = 1;
    wr[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wr[0].pBufferInfo = &bi[0];
    wr[1] = wr[0];
    wr[1].dstBinding = 1;
    wr[1].pBufferInfo = &bi[1];
    UpdateDescriptorSets(dev, 2, wr, 0, NULL);

    memset(&cpci, 0, sizeof cpci);
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = (uint32_t)family;
    check(CreateCommandPool(dev, &cpci, NULL, &cpool), "cmd pool");
    memset(&begin, 0, sizeof begin);
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    {
        VkCommandBufferAllocateInfo cbai;
        memset(&cbai, 0, sizeof cbai);
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = cpool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        check(AllocateCommandBuffers(dev, &cbai, &cmd_up), "cmd up");
        check(AllocateCommandBuffers(dev, &cbai, &cmd_down), "cmd down");
        for (m = 0; m < 3; m++) check(AllocateCommandBuffers(dev, &cbai, &cmds[m]), "cmd");
    }
    memset(&begin, 0, sizeof begin);
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    memset(&copy, 0, sizeof copy);
    copy.size = bytes;
    check(BeginCommandBuffer(cmd_up, &begin), "begin up");
    CmdCopyBuffer(cmd_up, stage, xin, 1, &copy);
    check(EndCommandBuffer(cmd_up), "end up");

    groups = (uint32_t)(k_n / k_local);
    memset(&bar, 0, sizeof bar);
    bar.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.size = VK_WHOLE_SIZE;
    bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bar.buffer = xin;
    for (m = 0; m < 3; m++) {
        int r, times = (mode == 4) ? 1 : (m + 1 == mode ? repeats : 1);
        check(BeginCommandBuffer(cmds[m], &begin), "begin");
        CmdPipelineBarrier(cmds[m], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 1, &bar, 0, NULL);
        CmdBindPipeline(cmds[m], VK_PIPELINE_BIND_POINT_COMPUTE, pipes[m]);
        CmdBindDescriptorSets(cmds[m], VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
        for (r = 0; r < times; r++) CmdDispatch(cmds[m], groups, 1, 1);
        check(EndCommandBuffer(cmds[m]), "end");
    }
    check(BeginCommandBuffer(cmd_down, &begin), "begin down");
    bar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bar.buffer = yout;
    CmdPipelineBarrier(cmd_down, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &bar, 0, NULL);
    CmdCopyBuffer(cmd_down, yout, stage, 1, &copy);
    check(EndCommandBuffer(cmd_down), "end down");

    memset(&fci, 0, sizeof fci);
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    check(CreateFence(dev, &fci, NULL, &fence), "fence");
    memset(&sub, 0, sizeof sub);
    sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd_up;
    t0 = now_ms();
    check(QueueSubmit(queue, 1, &sub, fence), "upload");
    check(WaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX), "upload wait");
    upload_ms = now_ms() - t0;

    if (mode != 4) {
        sub.pCommandBuffers = &cmds[mode - 1];
        check(ResetFences(dev, 1, &fence), "reset warmup");
        check(QueueSubmit(queue, 1, &sub, fence), "warmup");
        check(WaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX), "warmup wait");
        t0 = now_ms();
        deadline = t0 + 5000.0;
        while (now_ms() < deadline) {
            check(ResetFences(dev, 1, &fence), "reset");
            check(QueueSubmit(queue, 1, &sub, fence), "submit");
            check(WaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX), "wait");
            reps[mode - 1]++;
        }
        elapsed = now_ms() - t0;
    } else {
        int pick;
        for (m = 0; m < 3; m++) {
            sub.pCommandBuffers = &cmds[m];
            check(ResetFences(dev, 1, &fence), "reset warmup");
            check(QueueSubmit(queue, 1, &sub, fence), "warmup");
            check(WaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX), "warmup wait");
        }
        t0 = now_ms();
        deadline = t0 + 5000.0;
        while (now_ms() < deadline) {
            rng = rng * 0x9E3779B97F4A7C15ull + 1u;
            pick = (int)((rng >> 33) % 3u);
            last = pick;
            sub.pCommandBuffers = &cmds[pick];
            check(ResetFences(dev, 1, &fence), "reset");
            check(QueueSubmit(queue, 1, &sub, fence), "submit");
            check(WaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX), "wait");
            reps[pick]++;
        }
        elapsed = now_ms() - t0;
    }

    sub.pCommandBuffers = &cmd_down;
    check(ResetFences(dev, 1, &fence), "reset down");
    check(QueueSubmit(queue, 1, &sub, fence), "download");
    check(WaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX), "download wait");
    {
        void *mapped = NULL;
        uint32_t sample[8] = {0, 1, 2, 3, 64, 4096, k_n / 2, k_n - 1};
        int s, which = mode == 4 ? last : mode - 1;
        check(MapMemory(dev, ms, 0, bytes, 0, &mapped), "map result");
        got = (uint32_t *)mapped;
        for (s = 0; s < 8; s++) {
            uint32_t idx = sample[s];
            uint32_t want = which == 0 ? cpu_stream(xs[idx]) : which == 1 ? cpu_disorder(xs, idx) : cpu_arith(xs[idx]);
            if (got[idx] != want) {
                fprintf(stderr, "sample %u gpu %u cpu %u (kernel %d)\n", idx, got[idx], want, which + 1);
                fail("sample does not match");
            }
        }
        UnmapMemory(dev, ms);
    }

    printf("bench: %d %s\n", mode, title[mode]);
    printf("device: %s\n", props.deviceName);
    printf("elements: %d   buffer: %.1f MiB in, %.1f MiB out\n", k_n, (double)bytes / (1024.0 * 1024.0), (double)bytes / (1024.0 * 1024.0));
    printf("upload: %.3f ms   (not in the 5 seconds)\n", upload_ms);
    printf("seconds: %.3f\n", elapsed / 1000.0);
    if (mode != 4) {
        uint64_t dispatches = reps[mode - 1] * (uint64_t)repeats;
        double elems = (double)dispatches * (double)k_n;
        double moved = elems * 8.0;
        printf("submits: %llu   dispatches: %llu\n", (unsigned long long)reps[mode - 1], (unsigned long long)dispatches);
        printf("elements/s: %.3f billion\n", elems / (elapsed / 1000.0) / 1e9);
        printf("moved: %.1f GiB/s   (one read and one write per element)\n", moved / (elapsed / 1000.0) / 1e9);
        if (mode == 1) printf("counted ops/s: %.3f billion   (256 integer ops per element)\n", elems * 256.0 / (elapsed / 1000.0) / 1e9);
        if (mode == 3) printf("counted ops/s: %.3f billion   (512 integer ops per element)\n", elems * 512.0 / (elapsed / 1000.0) / 1e9);
    } else {
        for (m = 0; m < 3; m++) printf("%s submits: %llu\n", title[m + 1], (unsigned long long)reps[m]);
    }
    printf("sample: match\n");

    DestroyFence(dev, fence, NULL);
    DestroyCommandPool(dev, cpool, NULL);
    DestroyDescriptorPool(dev, pool, NULL);
    for (m = 0; m < 3; m++) {
        DestroyPipeline(dev, pipes[m], NULL);
        DestroyShaderModule(dev, mods[m], NULL);
    }
    DestroyPipelineLayout(dev, pl, NULL);
    DestroyDescriptorSetLayout(dev, layout, NULL);
    DestroyBuffer(dev, stage, NULL);
    DestroyBuffer(dev, yout, NULL);
    DestroyBuffer(dev, xin, NULL);
    FreeMemory(dev, ms, NULL);
    FreeMemory(dev, my, NULL);
    FreeMemory(dev, mx, NULL);
    DestroyDevice(dev, NULL);
    DestroyInstance(inst, NULL);
    free(xs);
    free(devs);
    free(fams);
    return 0;
}
