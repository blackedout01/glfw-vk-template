// Original source in https://github.com/blackedout01/glfw-vk-template
//
// This is free and unencumbered software released into the public domain.
// Anyone is free to copy, modify, publish, use, compile, sell, or distribute
// this software, either in source code form or as a compiled binary, for any
// purpose, commercial or non-commercial, and by any means.
//
// In jurisdictions that recognize copyright laws, the author or authors of
// this software dedicate any and all copyright interest in the software to the
// public domain. We make this dedication for the benefit of the public at
// large and to the detriment of our heirs and successors. We intend this
// dedication to be an overt act of relinquishment in perpetuity of all present
// and future rights to this software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to https://unlicense.org

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <math.h>

#include "util.c" // NOTE(blackedout): util might include and use platform specific code, so do this before glfw to avoid errors.
#include "GLFW/glfw3.h"

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
    VkInstance VulkanInstance = 0;
    base_context Context = {0};
    vulkan_surface_device VulkanSurfaceDevice = {0};
    vulkan_swapchain_handler VulkanSwapchainHandler = {0};
    VkCommandBuffer GraphicsCommandBuffer = 0;
    VkQueue VulkanGraphicsQueue = 0;
    {
        // NOTE(blackedout):
        // VK_ADD_LAYER_PATH: path where vulkan will look for additional layers (neccessary for validation layers on linux and macOS).
        // VK_DRIVER_FILES: colon separated files that vulkan will look at to set the driver that will be used (neccessary on macOS for MoltenVK).
        // Removing the environment variables is not necessary, since they seem to have the same lifetime as the process.
#ifdef VULKAN_EXPLICIT_LAYERS_PATH
        AssertMessageGoto(setenv("VK_ADD_LAYER_PATH", VULKAN_EXPLICIT_LAYERS_PATH, 1) == 0, label_Exit, "Failed to set VK_ADD_LAYER_PATH.\n");
#endif
#ifdef VULKAN_DRIVER_FILES
        AssertMessageGoto(setenv("VK_DRIVER_FILES", VULKAN_DRIVER_FILES, 1) == 0, label_Exit, "Failed to set VULKAN_DRIVER_FILES.\n");
#endif

        glfwSetErrorCallback(ErrorCallbackGLFW);
        {
            int InitResultGLFW = glfwInit();
            AssertMessageGoto(InitResultGLFW, label_Exit, "GLFW initialization failed.\n");
        }
        AssertMessageGoto(glfwVulkanSupported(), label_TerminateGLFW, "GLFW says Vulkan is not supported on this platform.\n");

        uint32_t VulkanApiVersion = VK_API_VERSION_1_3;
        {
            uint32_t RequiredInstanceExtensionCount;
            const char **RequiredInstanceExtensionsGLFW = glfwGetRequiredInstanceExtensions(&RequiredInstanceExtensionCount);
            AssertMessageGoto(RequiredInstanceExtensionsGLFW, label_TerminateGLFW, "GLFW didn't return any Vulkan extensions. On macOS this might be because MoltenVK is not linked correctly.\n");

            CheckGoto(VulkanCreateInstance(RequiredInstanceExtensionsGLFW, RequiredInstanceExtensionCount, VulkanApiVersion, &VulkanInstance), label_TerminateGLFW);
        }

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
        
        {
            VkSurfaceKHR VulkanSurface;
            VulkanCheckGoto(glfwCreateWindowSurface(VulkanInstance, Context.Window, 0, &VulkanSurface), label_DestroyVulkanInstance);
            
            // NOTE(blackedout): The surface is freed inside of this function on failure.
            CheckGoto(VulkanCreateSurfaceDevice(VulkanInstance, VulkanSurface, VulkanApiVersion, &VulkanSurfaceDevice), label_DestroyVulkanInstance);
        }

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
            DeltaTime = Time - TimeStart;
            TimeStart = Time;   
        }
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