#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <math.h>

#include "util.c"

#include "GLFW/glfw3.h"

#include "vulkan.c"

typedef struct {
    v3 Position;
    v3 Color;
    v2 TexCoord;
} vertex;

static vertex Vertices[] = {
    { .Position = { -8.0f, 0.0f, -8.0f }, .Color = { 1.0f, 1.0f, 1.0f }, .TexCoord = { 0.0f, 8.0f } },
    { .Position = { 8.0f, 0.0f, -8.0f }, .Color = { 1.0f, 1.0f, 1.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { 8.0f, 0.0f, 8.0f }, .Color = { 1.0f, 1.0f, 1.0f }, .TexCoord = { 8.0f, 0.0f } },
    { .Position = { -8.0f, 0.0f, 8.0f }, .Color = { 1.0f, 1.0f, 1.0f }, .TexCoord = { 8.0f, 8.0f } },

#define CUBE(X, Y, Z, R, G, B, H) \
    { .Position = { (X) + 0.0f, (Y) + 0.0f*(H), (Z) + 0.0f }, .Color = { R, G, B }, .TexCoord = { 0.0f, 0.0f } },\
    { .Position = { (X) + 1.0f, (Y) + 0.0f*(H), (Z) + 0.0f }, .Color = { R, G, B }, .TexCoord = { 0.0f, 0.0f } },\
    { .Position = { (X) + 1.0f, (Y) + 0.0f*(H), (Z) + 1.0f }, .Color = { R, G, B }, .TexCoord = { 0.0f, 0.0f } },\
    { .Position = { (X) + 0.0f, (Y) + 0.0f*(H), (Z) + 1.0f }, .Color = { R, G, B }, .TexCoord = { 0.0f, 0.0f } },\
    { .Position = { (X) + 0.0f, (Y) + 1.0f*(H), (Z) + 0.0f }, .Color = { R, G, B }, .TexCoord = { 0.0f, 0.0f } },\
    { .Position = { (X) + 1.0f, (Y) + 1.0f*(H), (Z) + 0.0f }, .Color = { R, G, B }, .TexCoord = { 0.0f, 0.0f } },\
    { .Position = { (X) + 1.0f, (Y) + 1.0f*(H), (Z) + 1.0f }, .Color = { R, G, B }, .TexCoord = { 0.0f, 0.0f } },\
    { .Position = { (X) + 0.0f, (Y) + 1.0f*(H), (Z) + 1.0f }, .Color = { R, G, B }, .TexCoord = { 0.0f, 0.0f } }

    CUBE(-3.0f, 0.0f, -3.0f, 1.0f, 0.1f, 0.1f, 1.0f),
    CUBE(-1.0f, 0.0f, -1.0f, 0.1f, 1.0f, 0.1f, 2.0f),
    CUBE(2.0f, 0.0f, 2.0f, 0.1f, 0.1f, 1.0f, 3.0f)

#undef CUBE
};
static uint32_t Indices[] = {
    0, 1, 2, 2, 3, 0,
     
    // NOTE(blackedout): Cube vertex indices
    //   7--------6
    //  /|       /|
    // 4--------5 |
    // | 3------|-2
    // |/       |/
    // 0--------1
#define CUBE(B)\
    /* (B) + 0, (B) + 3, (B) + 2, (B) + 2, (B) + 1, (B) + 0,*/ \
    (B) + 4, (B) + 5, (B) + 6, (B) + 6, (B) + 7, (B) + 4,\
    (B) + 0, (B) + 1, (B) + 5, (B) + 5, (B) + 4, (B) + 0,\
    (B) + 3, (B) + 7, (B) + 6, (B) + 6, (B) + 2, (B) + 3,\
    (B) + 3, (B) + 0, (B) + 4, (B) + 4, (B) + 7, (B) + 3,\
    (B) + 1, (B) + 2, (B) + 6, (B) + 6, (B) + 5, (B) + 1

    CUBE(4),
    CUBE(12),
    CUBE(20)

#undef CUBE
};

typedef struct {
    m4 M, V, P;
} ubo_mats;

typedef struct {
    int FramebufferWidth;
    int FramebufferHeight;

    int IsSuperDown;

    int IsDragging;
    double LastCursorX, LastCursorY;
    v2 DragDelta;

    float CamAzi, CamPol;
    float CamZoom;
} context;

static void ErrorCallbackGLFW(int Code, const char *Description) {
    printfc(CODE_RED, "GLFW error %d: %s\n", Code, Description);
}

static void CursorPositionCallbackGLFW(GLFWwindow *Window, double PosX, double PosY) {
    context *Context_ = (context *)glfwGetWindowUserPointer(Window);
    context Context = *Context_;

    double CursorDeltaX = Context.LastCursorX - PosX;
    double CursorDeltaY = Context.LastCursorY - PosY;

    if(Context.IsDragging) {
        Context.DragDelta.E[0] += CursorDeltaX;
        Context.DragDelta.E[1] += CursorDeltaY;
    }

    Context.LastCursorX = PosX;
    Context.LastCursorY = PosY;
    *Context_ = Context;
}

static void MouseButtonCallbackGLFW(GLFWwindow *Window, int Button, int Action, int Mods) {
    context *Context_ = (context *)glfwGetWindowUserPointer(Window);
    context Context = *Context_;

    if(Button == GLFW_MOUSE_BUTTON_LEFT) {
        Context.IsDragging = Action != GLFW_RELEASE;
    }

    *Context_ = Context;
}

static void KeyCallbackGLFW(GLFWwindow *Window, int Key, int Scancode, int Action, int Mods) {
    context *Context_ = (context *)glfwGetWindowUserPointer(Window);
    context Context = *Context_;

    if(Key == GLFW_KEY_LEFT_SUPER) {
        Context.IsSuperDown = Action != GLFW_RELEASE;
    }

    *Context_ = Context;
}

static void ScrollCallbackGLFW(GLFWwindow *Window, double OffsetX, double OffsetY) {
    context *Context_ = (context *)glfwGetWindowUserPointer(Window);
    context Context = *Context_;

    float NewZoom = Max(0.01f, Min(100.0f, Context.CamZoom*(float)pow(1.1, OffsetY)));
#ifdef __APPLE__
    if(Context.IsSuperDown) {
        Context.CamZoom = NewZoom;
    } else {
        Context.CamAzi -= 0.1f*OffsetX;
        Context.CamPol -= 0.1f*OffsetY;
    }
#else
    Context.CamZoom = NewZoom;
#endif

    *Context_ = Context;
}

static void FramebufferSizeCallbackGLFW(GLFWwindow *Window, int Width, int Height) {
    context *Context = (context *)glfwGetWindowUserPointer(Window);
    Context->FramebufferWidth = Width;
    Context->FramebufferHeight = Height;
    //printf("Framebuffer size (%d, %d)\n", Width, Height);
}

enum {
    DESCRIPTOR_SET_LAYOUT_DEFAULT_UNIFORM,
    DESCRIPTOR_SET_LAYOUT_DEFAULT_SAMPLER_IMAGE,

    DESCRIPTOR_SET_LAYOUT_COUNT
};

typedef struct {
    vulkan_shader Default;
    VkDescriptorSetLayout DescriptorSetLayouts[DESCRIPTOR_SET_LAYOUT_COUNT];

    vulkan_shader_uniform_buffers_description UniformBufferDescriptions[1]; // TODO(blackedout): This is ugly
    VkDeviceMemory UniformBufferMemory;
    
    VkBuffer UniformMatsBuffers[MAX_ACQUIRED_IMAGE_COUNT];
    ubo_mats *UniformMats[MAX_ACQUIRED_IMAGE_COUNT];
    VkDescriptorSet UniformMatsSets[MAX_ACQUIRED_IMAGE_COUNT];

    VkSampler DefaultSampler;

    VkDescriptorPool DefaultDescriptorPool;
    VkDescriptorPool UniformDescriptorPool;

    VkDescriptorSet DefaultSamplerImageSet;
} shaders;

static void DestroyShaders(vulkan_surface_device Device, shaders Shaders) {
    vkDestroyDescriptorPool(Device.Handle, Shaders.DefaultDescriptorPool, 0);
    vkDestroySampler(Device.Handle, Shaders.DefaultSampler, 0);

    vkDestroyDescriptorPool(Device.Handle, Shaders.UniformDescriptorPool, 0);
    vkFreeMemory(Device.Handle, Shaders.UniformBufferMemory, 0);
    for(uint32_t I = 0; I < ArrayCount(Shaders.UniformMatsBuffers); ++I) {
        vkDestroyBuffer(Device.Handle, Shaders.UniformMatsBuffers[I], 0);
    }

    VulkanDestroyDescriptorSetLayouts(Device, Shaders.DescriptorSetLayouts, ArrayCount(Shaders.DescriptorSetLayouts));
    vkDestroyShaderModule(Device.Handle, Shaders.Default.Frag, 0);
    vkDestroyShaderModule(Device.Handle, Shaders.Default.Vert, 0);
}

static int LoadShaders(vulkan_surface_device Device, shaders *Shaders, vulkan_image *Images) {
    int Result = 1;
    uint8_t *BytesVS = 0, *BytesFS = 0;
    uint64_t ByteCountVS, ByteCountFS;
    CheckGoto(LoadFileContentsCStd("bin/shaders/default.vert.spv", &BytesVS, &ByteCountVS), label_Exit);
    CheckGoto(LoadFileContentsCStd("bin/shaders/default.frag.spv", &BytesFS, &ByteCountFS), label_Exit);

    shaders LocalShaders;
    CheckGoto(VulkanCreateShaderModule(Device, BytesVS, ByteCountVS, &LocalShaders.Default.Vert), label_Exit);
    CheckGoto(VulkanCreateShaderModule(Device, BytesFS, ByteCountFS, &LocalShaders.Default.Frag), label_VS);

    // NOTE(blackedout): Create all descriptor set layouts
    VkDescriptorSetLayoutBinding DefaultUniformDescriptorSetLayoutBinding[] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .pImmutableSamplers = 0 }
    };
    VkDescriptorSetLayoutBinding DefaultDescriptorSetLayoutBindings[] = {
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = 0 },
        { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = 0 }
    };
    vulkan_descriptor_set_layout_description DescriptorSetDescriptions[] = {
        [DESCRIPTOR_SET_LAYOUT_DEFAULT_UNIFORM] =
        { .Flags = 0, .Bindings = DefaultUniformDescriptorSetLayoutBinding, .BindingsCount = ArrayCount(DefaultUniformDescriptorSetLayoutBinding) },
        [DESCRIPTOR_SET_LAYOUT_DEFAULT_SAMPLER_IMAGE] =
        { .Flags = 0, .Bindings = DefaultDescriptorSetLayoutBindings, .BindingsCount = ArrayCount(DefaultDescriptorSetLayoutBindings) },
    };
    CheckGoto(VulkanCreateDescriptorSetLayouts(Device, DescriptorSetDescriptions, ArrayCount(DescriptorSetDescriptions), LocalShaders.DescriptorSetLayouts), label_FS);
    
    // NOTE(blackedout): Create all uniform buffers mapped with unique descriptor set pool and correctly initialized sets
    vulkan_shader_uniform_buffers_description UniformBufferDescriptions[] = {
        { LocalShaders.UniformMatsBuffers, (void **)LocalShaders.UniformMats, LocalShaders.UniformMatsSets, 0, sizeof(ubo_mats) }
    };
    CheckGoto(VulkanCreateShaderUniformBuffers(Device, LocalShaders.DescriptorSetLayouts[DESCRIPTOR_SET_LAYOUT_DEFAULT_UNIFORM], UniformBufferDescriptions,
                                                ArrayCount(UniformBufferDescriptions), &LocalShaders.UniformBufferMemory, &LocalShaders.UniformDescriptorPool), label_DescriptorSetLayouts);
    StaticAssert(ArrayCount(LocalShaders.UniformBufferDescriptions) == ArrayCount(UniformBufferDescriptions));
    memcpy(LocalShaders.UniformBufferDescriptions, UniformBufferDescriptions, sizeof(UniformBufferDescriptions));

    VkSamplerCreateInfo DefaultSamplerCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0f,
        .anisotropyEnable = Device.Features.samplerAnisotropy,
        .maxAnisotropy = Device.Properties.limits.maxSamplerAnisotropy, // NOTE(blackedout): Spec says ignored if not enabled, so this is fine
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };
    VulkanCheckGoto(vkCreateSampler(Device.Handle, &DefaultSamplerCreateInfo, 0, &LocalShaders.DefaultSampler), label_UniformBuffers);

    VkDescriptorPoolSize DescriptorPoolSizes[] = {
        { .type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1 },
        { .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 1 }
    };
    VkDescriptorPoolCreateInfo DescriptorPoolCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .maxSets = 1,
        .poolSizeCount = ArrayCount(DescriptorPoolSizes),
        .pPoolSizes = DescriptorPoolSizes
    };
    VulkanCheckGoto(vkCreateDescriptorPool(Device.Handle, &DescriptorPoolCreateInfo, 0, &LocalShaders.DefaultDescriptorPool), label_Sampler);
    
    VkDescriptorSetLayout DescriptorSetLayouts[] = {
        LocalShaders.DescriptorSetLayouts[DESCRIPTOR_SET_LAYOUT_DEFAULT_SAMPLER_IMAGE]
    };
    VkDescriptorSetAllocateInfo DescriptorSetAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = 0,
        .descriptorPool = LocalShaders.DefaultDescriptorPool,
        .descriptorSetCount = ArrayCount(DescriptorSetLayouts),
        .pSetLayouts = DescriptorSetLayouts
    };
    VulkanCheckGoto(vkAllocateDescriptorSets(Device.Handle, &DescriptorSetAllocateInfo, &LocalShaders.DefaultSamplerImageSet), label_DefaultDescriptorPool);

    VkDescriptorImageInfo ImageInfo = {
        .sampler = VULKAN_NULL_HANDLE,
        .imageView = Images[0].ViewHandle,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo SamplerInfo = {
        .sampler = LocalShaders.DefaultSampler,
    };
    VkWriteDescriptorSet WriteDescriptorSets[] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = 0,
            .dstSet = LocalShaders.DefaultSamplerImageSet,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .pImageInfo = &SamplerInfo,
            .pBufferInfo = 0,
            .pTexelBufferView = 0
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = 0,
            .dstSet = LocalShaders.DefaultSamplerImageSet,
            .dstBinding = 2,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &ImageInfo,
            .pBufferInfo = 0,
            .pTexelBufferView = 0
        }
    };
    vkUpdateDescriptorSets(Device.Handle, ArrayCount(WriteDescriptorSets), WriteDescriptorSets, 0, 0);

    *Shaders = LocalShaders;

    Result = 0;
    goto label_Exit;

label_DefaultDescriptorPool:
    vkDestroyDescriptorPool(Device.Handle, LocalShaders.DefaultDescriptorPool, 0);
label_Sampler:
    vkDestroySampler(Device.Handle, LocalShaders.DefaultSampler, 0);
label_UniformBuffers:
    vkDestroyDescriptorPool(Device.Handle, LocalShaders.UniformDescriptorPool, 0);
    vkFreeMemory(Device.Handle, LocalShaders.UniformBufferMemory, 0);
    for(uint32_t I = 0; I < ArrayCount(LocalShaders.UniformMatsBuffers); ++I) {
        vkDestroyBuffer(Device.Handle, LocalShaders.UniformMatsBuffers[I], 0);
    }
label_DescriptorSetLayouts:
    VulkanDestroyDescriptorSetLayouts(Device, LocalShaders.DescriptorSetLayouts, ArrayCount(LocalShaders.DescriptorSetLayouts));
label_FS:
    vkDestroyShaderModule(Device.Handle, LocalShaders.Default.Frag, 0);
label_VS:
    vkDestroyShaderModule(Device.Handle, LocalShaders.Default.Vert, 0);
label_Exit:
    free(BytesVS);
    free(BytesFS);
    return Result;
}

int main() {
    int Result = 1;

    // NOTE(blackedout): "Setting up Vulkan on MacOS without Xcode"
    // From: https://gist.github.com/Resparing/d30634fcd533ec5b3235791b21265850 (2024-07-03)
    // Removing the environment variables is not necessary, since they seem to have the same lifetime as the process.
#ifdef __APPLE__
    if(setenv("VK_ICD_FILENAMES", "vulkan/icd.d/MoltenVK_icd.json", 1) ||
       setenv("VK_LAYER_PATH", "vulkan/explicit_layer.d", 1)) {
        printf("Failed to set MoltenVK environment variables.\n");
        goto label_Exit;
    }
#endif

    glfwSetErrorCallback(ErrorCallbackGLFW);
    if(glfwInit() == 0) {
        goto label_Exit;
    }

    AssertMessageGoto(glfwVulkanSupported() != 0, label_TerminateGLFW, "GLFW says Vulkan is not supported on this platform.\n");

    uint32_t RequiredInstanceExtensionCount;
    const char **RequiredInstanceExtensionsGLFW = glfwGetRequiredInstanceExtensions(&RequiredInstanceExtensionCount);
    AssertMessageGoto(RequiredInstanceExtensionsGLFW > 0, label_TerminateGLFW, "GLFW didn't return any Vulkan extensions. On macOS this might be because MoltenVK is not linked correctly.\n");

    VkInstance VulkanInstance;
    CheckGoto(VulkanCreateInstance(RequiredInstanceExtensionsGLFW, RequiredInstanceExtensionCount, &VulkanInstance), label_TerminateGLFW);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *Window = glfwCreateWindow(1280, 720, "glfw-vulkan-template", 0, 0);
    CheckGoto(Window == 0, label_DestroyVulkanInstance);
    context Context = {0};
    Context.CamPol = -0.01f;
    Context.CamZoom = 1.0f;
    glfwSetWindowUserPointer(Window, &Context);
    glfwSetCursorPosCallback(Window, CursorPositionCallbackGLFW);
    glfwSetMouseButtonCallback(Window, MouseButtonCallbackGLFW);
    glfwSetKeyCallback(Window, KeyCallbackGLFW);
    glfwSetScrollCallback(Window, ScrollCallbackGLFW);
    glfwSetFramebufferSizeCallback(Window, FramebufferSizeCallbackGLFW);

    VkSurfaceKHR VulkanSurface;
    VulkanCheckGoto(glfwCreateWindowSurface(VulkanInstance, Window, 0, &VulkanSurface), label_DestroyVulkanInstance);

    vulkan_surface_device VulkanSurfaceDevice;
    // NOTE(blackedout): The surface is freed inside of this function on failure.
    CheckGoto(VulkanCreateSurfaceDevice(VulkanInstance, VulkanSurface, &VulkanSurfaceDevice), label_DestroyVulkanInstance);

    int Width, Height;
    glfwGetFramebufferSize(Window, &Width, &Height);
    Context.FramebufferWidth = Width;
    Context.FramebufferHeight = Height;

    // NOTE(blackedout): Create command pools, buffer, get queues and load static assets
    VkCommandPoolCreateInfo GraphicsCommandPoolCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = 0,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = VulkanSurfaceDevice.GraphicsQueueFamilyIndex
    };
    VkCommandPool VulkanGraphicsCommandPool;
    VulkanCheckGoto(vkCreateCommandPool(VulkanSurfaceDevice.Handle, &GraphicsCommandPoolCreateInfo, 0, &VulkanGraphicsCommandPool), label_DestroySurfaceDevice);

    VkCommandBufferAllocateInfo GraphicsCommandBufferAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = 0,
        .commandPool = VulkanGraphicsCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer GraphicsCommandBuffer;
    VulkanCheckGoto(vkAllocateCommandBuffers(VulkanSurfaceDevice.Handle, &GraphicsCommandBufferAllocateInfo, &GraphicsCommandBuffer), label_DestroyCommandPool);

    VkQueue VulkanGraphicsQueue;
    vkGetDeviceQueue(VulkanSurfaceDevice.Handle, VulkanSurfaceDevice.GraphicsQueueFamilyIndex, 0, &VulkanGraphicsQueue);

    vulkan_static_buffers VulkanStaticBuffers;
    uint64_t VerticesByteOffset, IndicesByteOffset;
    vulkan_mesh_subbuf MeshSubbufs[] = {
        {
            .Vertices = { .Source = Vertices, .ByteCount = sizeof(Vertices), .OffsetPointer = &VerticesByteOffset },
            .Indices = { .Source = Indices, .ByteCount = sizeof(Indices), .OffsetPointer = &IndicesByteOffset }
        }
    };

    uint8_t TileImageBytes[] = {
        0xff, 0xff, 0xff, 0xff,
        0xf9, 0xf9, 0xfd, 0xff,
        0xf9, 0xf9, 0xfd, 0xff,
        0xff, 0xff, 0xff, 0xff,
    };
    vulkan_image Images[] = {
        { .Handle = VULKAN_NULL_HANDLE, .ViewHandle = VULKAN_NULL_HANDLE, .Type = VK_IMAGE_TYPE_2D, .ViewType = VK_IMAGE_VIEW_TYPE_2D, .Format = VK_FORMAT_R8G8B8A8_SRGB, .Width = 2, .Height = 2, .Depth = 1, .Source = TileImageBytes, .ByteCount = sizeof(TileImageBytes), .Offset = 0 },
    };
    CheckGoto(VulkanCreateStaticImagesAndBuffers(VulkanSurfaceDevice, MeshSubbufs, ArrayCount(MeshSubbufs), Images, ArrayCount(Images), VulkanGraphicsCommandPool, VulkanGraphicsQueue, &VulkanStaticBuffers), label_DestroyCommandPool);

    shaders VulkanShaders;
    CheckGoto(LoadShaders(VulkanSurfaceDevice, &VulkanShaders, Images), label_DestroyStaticImagesAndBuffers);

    VkVertexInputBindingDescription VertexInputBindingDescription = {
        .binding = 0,
        .stride = sizeof(vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription VertexAttributeDescriptions[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vertex, Position) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vertex, Color) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vertex, TexCoord) },
    };

    VkPipelineVertexInputStateCreateInfo PipelineVertexInputStateCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &VertexInputBindingDescription,
        .vertexAttributeDescriptionCount = ArrayCount(VertexAttributeDescriptions),
        .pVertexAttributeDescriptions = VertexAttributeDescriptions,
    };

    VkSampleCountFlagBits SampleCount = Min(VulkanSurfaceDevice.MaxSampleCount, VK_SAMPLE_COUNT_4_BIT);
    vulkan_graphics_pipeline_info VulkanGraphicsPipelineInfo;
    CheckGoto(VulkanCreateDefaultGraphicsPipeline(VulkanSurfaceDevice, VulkanShaders.Default.Vert, VulkanShaders.Default.Frag, VulkanSurfaceDevice.InitialExtent, VulkanSurfaceDevice.InitialSurfaceFormat.format, SampleCount, PipelineVertexInputStateCreateInfo, VulkanShaders.DescriptorSetLayouts, ArrayCount(VulkanShaders.DescriptorSetLayouts), &VulkanGraphicsPipelineInfo), label_DestroyShaders);

    vulkan_swapchain_handler VulkanSwapchainHandler;
    VkExtent2D InitialExtent = { .width = (uint32_t)Width, .height = (uint32_t)Height };
    CheckGoto(VulkanCreateSwapchainAndHandler(VulkanSurfaceDevice, InitialExtent, SampleCount, VulkanGraphicsPipelineInfo.RenderPass, &VulkanSwapchainHandler), label_DestroyGraphicsPipeline);

    VkExtent2D FramebufferExtent;
    int A = 0;
    while(glfwWindowShouldClose(Window) == 0) {
        memset(Context.DragDelta.E, 0, sizeof(Context.DragDelta.E));
        glfwPollEvents();

        Context.CamAzi += 0.01f*Context.DragDelta.E[0];
        Context.CamPol += 0.01f*Context.DragDelta.E[1];

        v3 AxisX = {1.0f, 0.0f, 0.0f};
        v3 AxisY = {0.0f, 1.0f, 0.0f};
        m4 ViewRotation = MultiplyM4M4(TranslationM4(0.0f, 0.0f, -8.0f/Context.CamZoom), MultiplyM4M4(RotationM4(AxisX, -Context.CamPol), RotationM4(AxisY, -Context.CamAzi)));

        VkExtent2D FramebufferExtent = {
            .width = Context.FramebufferWidth,
            .height = Context.FramebufferHeight
        };

        VkExtent2D AcquiredImageExtent;
        VkFramebuffer AcquiredFramebuffer;
        uint32_t AcquiredImageBufIndex;
        CheckGoto(VulkanAcquireNextImage(VulkanSurfaceDevice, &VulkanSwapchainHandler, FramebufferExtent, &AcquiredImageExtent, &AcquiredFramebuffer, &AcquiredImageBufIndex), label_IdleDestroyAndExit);
        //printf("current (%d, %d), acquired (%d, %d)\n", Context.FramebufferWidth, Context.FramebufferHeight, AcquiredImageExtent.width, AcquiredImageExtent.height);

        VulkanCheckGoto(vkResetCommandBuffer(GraphicsCommandBuffer, 0), label_IdleDestroyAndExit);

        VkRect2D RenderArea = {
            .offset = { 0, 0 },
            .extent = AcquiredImageExtent
        };
        VkClearValue RenderClearValues[] = {
            {
                //.color = { .float32 = { (A & 1), 0.5f*(A & 2), 0.0f, 1.0f } }
                .color = { .float32 = { 0.6f, 0.8f, 0.99f, 1.0f } }
                //.color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } }
            },
            { .depthStencil = { .depth = 1.0f, .stencil = 0 } }
        };

        VkCommandBufferBeginInfo GraphicsCommandBufferBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = 0,
            .flags = 0,
            .pInheritanceInfo = 0
        };

        VulkanCheckGoto(vkBeginCommandBuffer(GraphicsCommandBuffer, &GraphicsCommandBufferBeginInfo), label_IdleDestroyAndExit);
        
        VkRenderPassBeginInfo RenderPassBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = 0,
            .renderPass = VulkanGraphicsPipelineInfo.RenderPass,
            .framebuffer = AcquiredFramebuffer,
            .renderArea = RenderArea,
            .clearValueCount = ArrayCount(RenderClearValues),
            .pClearValues = RenderClearValues // NOTE(blackedout): For VK_ATTACHMENT_LOAD_OP_CLEAR
        };

        VkViewport Viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = (float)AcquiredImageExtent.width,
            .height = (float)AcquiredImageExtent.height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };

        VkRect2D Scissors = {
            .offset = { 0, 0 },
            .extent = AcquiredImageExtent,
        };

        ubo_mats *UboMats = VulkanShaders.UniformMats[AcquiredImageBufIndex];
        UboMats->M = TransposeM4(IdentityM4());
        UboMats->V = TransposeM4(ViewRotation);
        UboMats->P = TransposeM4(ProjectionPersp(1.1f, Viewport.width/Viewport.height, 0.01f, 1000.0f));

        vkCmdBeginRenderPass(GraphicsCommandBuffer, &RenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(GraphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, VulkanGraphicsPipelineInfo.Pipeline);
        vkCmdSetViewport(GraphicsCommandBuffer, 0, 1, &Viewport);
        vkCmdSetScissor(GraphicsCommandBuffer, 0, 1, &Scissors);
        //printf("A = %d\n", A);
        VkDeviceSize VertexBufferOffset = VerticesByteOffset; // TODO(blackedout): Are these byte offsets??
        vkCmdBindVertexBuffers(GraphicsCommandBuffer, 0, 1, &VulkanStaticBuffers.VertexHandle, &VertexBufferOffset);
        vkCmdBindIndexBuffer(GraphicsCommandBuffer, VulkanStaticBuffers.IndexHandle, IndicesByteOffset, VK_INDEX_TYPE_UINT32);
        //vkCmdDraw(GraphicsCommandBuffer, 3 + 3*A, 1, 0, 0);
        VkDescriptorSet Sets[] = { VulkanShaders.UniformMatsSets[AcquiredImageBufIndex], VulkanShaders.DefaultSamplerImageSet };
        vkCmdBindDescriptorSets(GraphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, VulkanGraphicsPipelineInfo.Layout, 0, ArrayCount(Sets), Sets, 0, 0);
        vkCmdDrawIndexed(GraphicsCommandBuffer, ArrayCount(Indices), 1, 0, 0, 0);
        ++A;
        if(A == 4) {
            A = 0;
        }
        vkCmdEndRenderPass(GraphicsCommandBuffer);
        VulkanCheckGoto(vkEndCommandBuffer(GraphicsCommandBuffer), label_IdleDestroyAndExit);

        CheckGoto(VulkanSubmitFinalAndPresent(VulkanSurfaceDevice, &VulkanSwapchainHandler, VulkanGraphicsQueue, GraphicsCommandBuffer, FramebufferExtent), label_IdleDestroyAndExit);
        
        //SleepMilliseconds(1000); // NOTE(blackedout): EDITING THIS MIGHT CAUSE IMAGE FLASHING
    }

    Result = 0;

//label_TODO:;
label_IdleDestroyAndExit:
    VulkanCheckGoto(vkDeviceWaitIdle(VulkanSurfaceDevice.Handle), label_IdleError);
label_IdleError:;
label_DestroySwapchainHandler:
    VulkanDestroySwapchainHandler(VulkanSurfaceDevice, &VulkanSwapchainHandler);
label_DestroyGraphicsPipeline:
    VulkanDestroyDefaultGraphicsPipeline(VulkanSurfaceDevice, VulkanGraphicsPipelineInfo);
label_DestroyShaders:
    DestroyShaders(VulkanSurfaceDevice, VulkanShaders);
label_DestroyStaticImagesAndBuffers:
    VulkanDestroyStaticImagesAndBuffers(VulkanSurfaceDevice, VulkanStaticBuffers);
label_DestroyCommandPool:
    vkDestroyCommandPool(VulkanSurfaceDevice.Handle, VulkanGraphicsCommandPool, 0);
label_DestroySurfaceDevice:
    VulkanDestroySurfaceDevice(VulkanInstance, VulkanSurfaceDevice);
label_DestroyVulkanInstance:
    vkDestroyInstance(VulkanInstance, 0);
label_TerminateGLFW:
    glfwTerminate(); // NOTE(blackedout): This will also destroy the window.
    Window = 0;
label_Exit:
    return Result;
}