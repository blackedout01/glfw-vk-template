#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "util.c"

#include "GLFW/glfw3.h"

#include "vulkan.c"

typedef struct {
        v2 Position;
        v3 Color;
    } vertex;
static vertex Vertices[] = {
    { .Position = { -0.5f, 0.5f }, .Color = { 1.0f, 1.0f, 1.0f } },
    { .Position = { -0.5f, -0.5f }, .Color = { 1.0f, 0.0f, 0.0f } },
    { .Position = { 0.5f, -0.5f }, .Color = { 0.0f, 1.0f, 0.0f } },
    { .Position = { 0.5f, 0.5f }, .Color = { 0.0f, 0.0f, 1.0f } },
};
static uint32_t Indices[] = { 0, 3, 2, 2, 1, 0 };

typedef struct {
    int FramebufferWidth;
    int FramebufferHeight;
} context;

static void ErrorCallbackGLFW(int Code, const char *Description) {
    printf("GLFW error %d: %s\n", Code, Description);
}

static void ScrollCallbackGLFW(GLFWwindow *Window, double OffsetX, double OffsetY) {
    //printf("Scroll (%.2f, %.2f)\n", OffsetX, OffsetY);
}

static void FramebufferSizeCallbackGLFW(GLFWwindow *Window, int Width, int Height) {
    context *Context = (context *)glfwGetWindowUserPointer(Window);
    Context->FramebufferWidth = Width;
    Context->FramebufferHeight = Height;
    //printf("Framebuffer size (%d, %d)\n", Width, Height);
}

static int LoadShaders(vulkan_surface_device Device, VkShaderModule *ModuleVS, VkShaderModule *ModuleFS) {
    int Result = 1;
    uint8_t *BytesVS = 0, *BytesFS = 0;
    uint64_t ByteCountVS, ByteCountFS;
    CheckGoto(LoadFileContentsCStd("bin/shaders/default.vert.spv", &BytesVS, &ByteCountVS), label_Exit);
    CheckGoto(LoadFileContentsCStd("bin/shaders/default.frag.spv", &BytesFS, &ByteCountFS), label_Exit);
    VkShaderModule DefaultVS, DefaultFS;
    CheckGoto(VulkanCreateShaderModule(Device, (char *)BytesVS, ByteCountVS, &DefaultVS), label_Exit);
    CheckGoto(VulkanCreateShaderModule(Device, (char *)BytesFS, ByteCountFS, &DefaultFS), label_VS);
    
    *ModuleVS = DefaultVS;
    *ModuleFS = DefaultFS;

    Result = 0;
    goto label_Exit;

label_VS:
    vkDestroyShaderModule(Device.Handle, DefaultVS, 0);
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
    glfwSetWindowUserPointer(Window, &Context);
    glfwSetScrollCallback(Window, ScrollCallbackGLFW);
    glfwSetFramebufferSizeCallback(Window, FramebufferSizeCallbackGLFW);

    VkSurfaceKHR VulkanSurface;
    VulkanCheckGoto(glfwCreateWindowSurface(VulkanInstance, Window, 0, &VulkanSurface), label_DestroyWindow);

    vulkan_surface_device VulkanSurfaceDevice;
    CheckGoto(VulkanCreateSurfaceDevice(VulkanInstance, VulkanSurface, &VulkanSurfaceDevice), label_DestroyWindow);

    int Width, Height;
    glfwGetFramebufferSize(Window, &Width, &Height);
    Context.FramebufferWidth = Width;
    Context.FramebufferHeight = Height;

    VkShaderModule VulkanDefaultVS, VulkanDefaultFS;
    CheckGoto(LoadShaders(VulkanSurfaceDevice, &VulkanDefaultVS, &VulkanDefaultFS), label_DestroySurfaceDevice);

    VkVertexInputBindingDescription VertexInputBindingDescription = {
        .binding = 0,
        .stride = sizeof(vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription VertexAttributeDescriptions[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vertex, Position)
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vertex, Color)
        },
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

    vulkan_graphics_pipeline_info VulkanGraphicsPipelineInfo;
    CheckGoto(VulkanCreateDefaultGraphicsPipeline(VulkanSurfaceDevice, VulkanDefaultVS, VulkanDefaultFS, VulkanSurfaceDevice.InitialExtent, VulkanSurfaceDevice.InitialSurfaceFormat.format, PipelineVertexInputStateCreateInfo, &VulkanGraphicsPipelineInfo), label_DestroyShaders);

    vulkan_swapchain_handler VulkanSwapchainHandler;
    VkExtent2D InitialExtent = { .width = (uint32_t)Width, .height = (uint32_t)Height };
    CheckGoto(VulkanCreateSwapchainAndHandler(VulkanSurfaceDevice, InitialExtent, VulkanGraphicsPipelineInfo.RenderPass, &VulkanSwapchainHandler), label_DestroyGraphicsPipeline);
    
    VkCommandPoolCreateInfo GraphicsCommandPoolCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = 0,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = VulkanSurfaceDevice.GraphicsQueueFamilyIndex
    };
    VkCommandPool VulkanGraphicsCommandPool;
    VulkanCheckGoto(vkCreateCommandPool(VulkanSurfaceDevice.Handle, &GraphicsCommandPoolCreateInfo, 0, &VulkanGraphicsCommandPool), label_DestroySwapchainHandler);

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
    CheckGoto(VulkanCreateStaticBuffers(VulkanSurfaceDevice, MeshSubbufs, ArrayCount(MeshSubbufs), VulkanGraphicsCommandPool, VulkanGraphicsQueue, &VulkanStaticBuffers), label_DestroyCommandPool);

    VkExtent2D FramebufferExtent;
    int A = 0;
    while(glfwWindowShouldClose(Window) == 0) {
        glfwPollEvents();

        VkExtent2D FramebufferExtent = {
            .width = Context.FramebufferWidth,
            .height = Context.FramebufferHeight
        };

        VkExtent2D AcquiredImageExtent;
        VkFramebuffer AcquiredFramebuffer;
        CheckGoto(VulkanAcquireNextImage(VulkanSurfaceDevice, &VulkanSwapchainHandler, FramebufferExtent, &AcquiredImageExtent, &AcquiredFramebuffer), label_IdleDestroyAndExit);
        printf("current (%d, %d), acquired (%d, %d)\n", Context.FramebufferWidth, Context.FramebufferHeight, AcquiredImageExtent.width, AcquiredImageExtent.height);

        VulkanCheckGoto(vkResetCommandBuffer(GraphicsCommandBuffer, 0), label_IdleDestroyAndExit);

        VkRect2D RenderArea = {
            .offset = { 0, 0 },
            .extent = AcquiredImageExtent
        };
        VkClearValue RenderClearValue = {
            .color = { .float32 = { (A & 1), 0.5f*(A & 2), 0.0f, 1.0f } }
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
            .clearValueCount = 1,
            .pClearValues = &RenderClearValue // NOTE(blackedout): For VK_ATTACHMENT_LOAD_OP_CLEAR
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

        vkCmdBeginRenderPass(GraphicsCommandBuffer, &RenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(GraphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, VulkanGraphicsPipelineInfo.Pipeline);
        vkCmdSetViewport(GraphicsCommandBuffer, 0, 1, &Viewport);
        vkCmdSetScissor(GraphicsCommandBuffer, 0, 1, &Scissors);
        printf("A = %d\n", A);
        VkDeviceSize VertexBufferOffset = VerticesByteOffset; // TODO(blackedout): Are these byte offsets??
        vkCmdBindVertexBuffers(GraphicsCommandBuffer, 0, 1, &VulkanStaticBuffers.VertexHandle, &VertexBufferOffset);
        vkCmdBindIndexBuffer(GraphicsCommandBuffer, VulkanStaticBuffers.IndexHandle, IndicesByteOffset, VK_INDEX_TYPE_UINT32);
        //vkCmdDraw(GraphicsCommandBuffer, 3 + 3*A, 1, 0, 0);
        vkCmdDrawIndexed(GraphicsCommandBuffer, ArrayCount(Indices), 1, 0, 0, 0);
        ++A;
        if(A == 4) {
            A = 0;
        }
        vkCmdEndRenderPass(GraphicsCommandBuffer);
        VulkanCheckGoto(vkEndCommandBuffer(GraphicsCommandBuffer), label_IdleDestroyAndExit);

        CheckGoto(VulkanSubmitFinalAndPresent(VulkanSurfaceDevice, &VulkanSwapchainHandler, VulkanGraphicsQueue, GraphicsCommandBuffer, FramebufferExtent), label_IdleDestroyAndExit);

        SleepMilliseconds(1000);
    }

    Result = 0;

label_TODO:;
label_IdleDestroyAndExit:
    VulkanCheckGoto(vkDeviceWaitIdle(VulkanSurfaceDevice.Handle), label_IdleError);
label_IdleError:;
label_DestroyStaticBuffers:
    VulkanDestroyStaticBuffers(VulkanSurfaceDevice, VulkanStaticBuffers);
label_DestroyCommandPool:
    vkDestroyCommandPool(VulkanSurfaceDevice.Handle, VulkanGraphicsCommandPool, 0);
label_DestroySwapchainHandler:
    VulkanDestroySwapchainHandler(VulkanSurfaceDevice, &VulkanSwapchainHandler);
label_DestroyGraphicsPipeline:
    VulkanDestroyDefaultGraphicsPipeline(VulkanSurfaceDevice, VulkanGraphicsPipelineInfo);
label_DestroyShaders:
    vkDestroyShaderModule(VulkanSurfaceDevice.Handle, VulkanDefaultVS, 0);
    vkDestroyShaderModule(VulkanSurfaceDevice.Handle, VulkanDefaultFS, 0);
label_DestroySurfaceDevice:
    VulkanDestroySurfaceDevice(VulkanInstance, VulkanSurfaceDevice);
label_DestroyWindow:
    glfwDestroyWindow(Window);
label_DestroyVulkanInstance:
    vkDestroyInstance(VulkanInstance, 0);
label_TerminateGLFW:
    glfwTerminate();
label_Exit:
    return Result;
}