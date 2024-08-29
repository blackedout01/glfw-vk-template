#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <math.h>

#include "GLFW/glfw3.h"

#include "util.c"
#include "vulkan_helpers.c"

#include "program.c"

typedef struct {
    GLFWwindow *Window;
    VkExtent2D FramebufferExtent;
    context ProgramContext;
} base_context;

static void ErrorCallbackGLFW(int Code, const char *Description) {
    printfc(CODE_RED, "GLFW error %d: %s\n", Code, Description);
}

static void KeyCallbackGLFW(GLFWwindow *Window, int Key, int Scancode, int Action, int Mods) {
    base_context *Context = (base_context *)glfwGetWindowUserPointer(Window);
    ProgramKeyCallback(&Context->ProgramContext, Key, Scancode, Action, Mods);
}

static void CursorPositionCallbackGLFW(GLFWwindow *Window, double PosX, double PosY) {
    base_context *Context = (base_context *)glfwGetWindowUserPointer(Window);
    ProgramCursorPositionCallback(&Context->ProgramContext, PosX, PosY);
}

static void MouseButtonCallbackGLFW(GLFWwindow *Window, int Button, int Action, int Mods) {
   base_context *Context = (base_context *)glfwGetWindowUserPointer(Window);
    ProgramMouseButtonCallback(&Context->ProgramContext, Button, Action, Mods);
}

static void ScrollCallbackGLFW(GLFWwindow *Window, double OffsetX, double OffsetY) {
    base_context *Context = (base_context *)glfwGetWindowUserPointer(Window);
    ProgramScrollCallback(&Context->ProgramContext, OffsetX, OffsetY);
}

static void FramebufferSizeCallbackGLFW(GLFWwindow *Window, int Width, int Height) {
    base_context *Context = (base_context *)glfwGetWindowUserPointer(Window);
    Context->FramebufferExtent.width = (uint32_t)Width;
    Context->FramebufferExtent.height = (uint32_t)Height;

    ProgramFramebufferSizeCallback(&Context->ProgramContext, Width, Height);
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

    VkInstance VulkanInstance;
    {
        uint32_t RequiredInstanceExtensionCount;
        const char **RequiredInstanceExtensionsGLFW = glfwGetRequiredInstanceExtensions(&RequiredInstanceExtensionCount);
        AssertMessageGoto(RequiredInstanceExtensionsGLFW > 0, label_TerminateGLFW, "GLFW didn't return any Vulkan extensions. On macOS this might be because MoltenVK is not linked correctly.\n");

        CheckGoto(VulkanCreateInstance(RequiredInstanceExtensionsGLFW, RequiredInstanceExtensionCount, &VulkanInstance), label_TerminateGLFW);
    }

    base_context Context = {0};
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    Context.Window = glfwCreateWindow(1280, 720, "glfw-vulkan-template", 0, 0);
    CheckGoto(Context.Window == 0, label_DestroyVulkanInstance);
    
    glfwSetWindowUserPointer(Context.Window, &Context);
    glfwSetKeyCallback(Context.Window, KeyCallbackGLFW);
    glfwSetCursorPosCallback(Context.Window, CursorPositionCallbackGLFW);
    glfwSetMouseButtonCallback(Context.Window, MouseButtonCallbackGLFW);
    glfwSetScrollCallback(Context.Window, ScrollCallbackGLFW);
    glfwSetFramebufferSizeCallback(Context.Window, FramebufferSizeCallbackGLFW);

    {
        int Width, Height;
        glfwGetFramebufferSize(Context.Window, &Width, &Height);
        Context.FramebufferExtent.width = (uint32_t)Width;
        Context.FramebufferExtent.height = (uint32_t)Height;
    }
    
    vulkan_surface_device VulkanSurfaceDevice;
    {
        VkSurfaceKHR VulkanSurface;
        VulkanCheckGoto(glfwCreateWindowSurface(VulkanInstance, Context.Window, 0, &VulkanSurface), label_DestroyVulkanInstance);
        
        // NOTE(blackedout): The surface is freed inside of this function on failure.
        CheckGoto(VulkanCreateSurfaceDevice(VulkanInstance, VulkanSurface, &VulkanSurfaceDevice), label_DestroyVulkanInstance);
    }

    vulkan_swapchain_handler VulkanSwapchainHandler;
    VkCommandBuffer GraphicsCommandBuffer;
    VkQueue VulkanGraphicsQueue;
    {
        VkRenderPass RenderPass;
        VkSampleCountFlagBits SampleCount;
        CheckGoto(ProgramSetup(&Context.ProgramContext, &VulkanSurfaceDevice, &GraphicsCommandBuffer, &VulkanGraphicsQueue, &RenderPass, &SampleCount), label_DestroySurfaceDevice);
        
        CheckGoto(VulkanCreateSwapchainAndHandler(&VulkanSurfaceDevice, Context.FramebufferExtent, SampleCount, RenderPass, &VulkanSwapchainHandler), label_ProgramSetdown);
    }

    double TimeStart = glfwGetTime(), DeltaTime = 0.0;
    while(glfwWindowShouldClose(Context.Window) == 0) {
        glfwPollEvents();

        CheckGoto(ProgramUpdate(&Context.ProgramContext, &VulkanSurfaceDevice, DeltaTime), label_IdleDestroyAndExit);

        vulkan_acquired_image AcquiredImage;
        CheckGoto(VulkanAcquireNextImage(&VulkanSurfaceDevice, &VulkanSwapchainHandler, Context.FramebufferExtent, &AcquiredImage), label_IdleDestroyAndExit);
        CheckGoto(ProgramRender(&Context.ProgramContext, &VulkanSurfaceDevice, AcquiredImage), label_IdleDestroyAndExit);
        CheckGoto(VulkanSubmitFinalAndPresent(&VulkanSurfaceDevice, &VulkanSwapchainHandler, VulkanGraphicsQueue, GraphicsCommandBuffer, Context.FramebufferExtent), label_IdleDestroyAndExit);
        
        //SleepMilliseconds(1000); // NOTE(blackedout): EDITING THIS MIGHT CAUSE IMAGE FLASHING

        double Time = glfwGetTime();
        DeltaTime = TimeStart - Time;
        TimeStart = Time;   
    }

    Result = 0;

//label_TODO:;
label_IdleDestroyAndExit:
    VulkanCheckGoto(vkDeviceWaitIdle(VulkanSurfaceDevice.Handle), label_IdleError);
label_IdleError:;
label_DestroySwapchainHandler:
    VulkanDestroySwapchainHandler(&VulkanSurfaceDevice, &VulkanSwapchainHandler);
label_ProgramSetdown:
    ProgramSetdown(&Context.ProgramContext, &VulkanSurfaceDevice);
label_DestroySurfaceDevice:
    VulkanDestroySurfaceDevice(VulkanInstance, &VulkanSurfaceDevice);
label_DestroyVulkanInstance:
    vkDestroyInstance(VulkanInstance, 0);
label_TerminateGLFW:
    glfwTerminate(); // NOTE(blackedout): This will also destroy the window.
    Context.Window = 0;
label_Exit:
    return Result;
}