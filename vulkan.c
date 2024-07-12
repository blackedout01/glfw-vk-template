#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#define VULKAN_NULL_HANDLE 0 // NOTE(blackedout): Somehow VK_NULL_HANDLE generates erros when compiling in cpp mode
#define VULKAN_INFO_PRINT
#define MAX_ACQUIRED_IMAGE_COUNT 1
#define MAX_SWAPCHAIN_COUNT (MAX_ACQUIRED_IMAGE_COUNT + 1)

typedef struct {
    VkDevice Handle;
    VkPhysicalDevice PhysicalDevice;
    VkSurfaceKHR Surface;

    uint32_t GraphicsQueueFamilyIndex;
    uint32_t PresentQueueFamilyIndex;

    VkExtent2D InitialExtent;
    VkSurfaceFormatKHR InitialSurfaceFormat;
} vulkan_surface_device;

typedef struct {
    VkSwapchainKHR Handle;

    VkExtent2D ImageExtent;
    VkFormat Format;

    uint32_t ImageCount;
    VkImage *Images;
    VkImageView *ImageViews;
    VkFramebuffer *Framebuffers;
    void *ImageBuf;

    uint32_t AcquiredImageCount;
} vulkan_swapchain;

typedef struct {
    uint32_t RenderPassCount;
    VkRenderPass RenderPass;

    uint32_t SwapchainIndexLastAcquired;
    buffer_indices SwapchainBufIndices;
    vulkan_swapchain Swapchains[MAX_SWAPCHAIN_COUNT];

    buffer_indices AcquiredImageBufIndices;
    uint32_t AcquiredImageIndices[MAX_ACQUIRED_IMAGE_COUNT];
    uint32_t AcquiredImageSwapchainIndices[MAX_ACQUIRED_IMAGE_COUNT];

    VkSemaphore ImageAvailableSemaphores[MAX_ACQUIRED_IMAGE_COUNT];
    VkSemaphore RenderFinishedSemaphores[MAX_ACQUIRED_IMAGE_COUNT];
    VkFence InFlightFences[MAX_ACQUIRED_IMAGE_COUNT];
} vulkan_swapchain_handler;

typedef struct {
    VkPipelineLayout Layout;
    VkRenderPass RenderPass;
    VkPipeline Pipeline;
} vulkan_graphics_pipeline_info;

// NOTE(blackedout): IncompleteActions: 0: error, 1: warn, 2: ignore
static int VulkanCheckFun(VkResult VulkanResult, int IncompleteAction, const char *CallString) {
    int Result = 1;
    if(VulkanResult == VK_SUCCESS) {
        return 0;
    }
    int DoWarn = 0;
    if(IncompleteAction > 0 && VulkanResult == VK_INCOMPLETE) {
        if(IncompleteAction == 2) {
            return 0;
        }
        Result = 0;
        DoWarn = 1;
    }
    if(CallString) {
        printfc(DoWarn? CODE_YELLOW : CODE_RED, "%s: %s\n", CallString, string_VkResult(VulkanResult));
    } else {
        printfc(DoWarn? CODE_YELLOW : CODE_RED, "Vulkan error: %s\n", string_VkResult(VulkanResult));
    }
    
    return Result;
}

#define VulkanCheck(Result) VulkanCheckFun(Result, 0, #Result)
#define VulkanCheckR1(Result) if(VulkanCheckFun(Result, 0, #Result)) return 1
#define VulkanCheckR1IncompleteOk(Result) if(VulkanCheckFun(Result, 2, #Result)) return 1
#define VulkanCheckR1IncompleteWarn(Result) if(VulkanCheckFun(Result, 1, #Result)) return 1
#define VulkanCheckGoto(Result, Label) if(VulkanCheckFun(Result, 0, #Result)) goto Label
#define VulkanCheckGotoIncompleteOk(Result, Label) if(VulkanCheckFun(Result, 2, #Result)) goto Label
#define VulkanCheckGotoIncompleteWarn(Result, Label) if(VulkanCheckFun(Result, 1, #Result)) goto Label

static int VulkanPickSurfaceFormat(VkPhysicalDevice PhysicalDevice, VkSurfaceKHR Surface, VkSurfaceFormatKHR *SurfaceFormat, uint32_t *SurfaceFormatScore) {
    VkSurfaceFormatKHR SurfaceFormats[128];
    uint32_t SurfaceFormatScores[ArrayCount(SurfaceFormats)];
    uint32_t SurfaceFormatCount = ArrayCount(SurfaceFormats);
    VulkanCheckR1IncompleteWarn(vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &SurfaceFormatCount, SurfaceFormats));
    AssertMessageGoto(SurfaceFormatCount > 0, label_Error, "vkGetPhysicalDeviceSurfaceFormatsKHR returned 0 formats even though that must not happen.\n");

    // NOTE(blackedout): From the Vulkan specification 34.5.2. Surface Format Support
    // "While the format of a presentable image refers to the encoding of each pixel, the colorSpace determines how the presentation engine interprets the pixel values."
    // "The color space selected for the swapchain image will not affect the processing of data written into the image by the implementation"
    //
    // Since VK_COLOR_SPACE_SRGB_NONLINEAR_KHR is the only non extension surface color space, search for a surface format that is _SRBG (we don't want shaders to output srgb manually).

    // https://vulkan.gpuinfo.org/listsurfaceformats.php (2024-07-08)
    // Prioritized picks: B8G8R8A8_SRGB, R8G8B8A8_SRGB, first _SRGB format, first remaining format
    uint32_t BestSurfaceFormatScore = 0;
    VkSurfaceFormatKHR BestSurfaceFormat = SurfaceFormats[0]; // TODO(blackedout): More elegant way to solve this first score problem?
    for(uint32_t I = 0; I < SurfaceFormatCount; ++I) {
        VkSurfaceFormatKHR SurfaceFormat = SurfaceFormats[I];
        uint32_t Score = 0;
        if(SurfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            if(SurfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB) {
                Score = 3;
            } else if(SurfaceFormat.format == VK_FORMAT_R8G8B8A8_SRGB) {
                Score = 2;
            } else if(strstr(string_VkFormat(SurfaceFormat.format), "_SRGB")) {
                Score = 1;
            }
        }
        if(Score > BestSurfaceFormatScore) {
            BestSurfaceFormatScore = Score;
            BestSurfaceFormat = SurfaceFormat;
        }
        SurfaceFormatScores[I] = Score;
    }

#ifdef VULKAN_INFO_PRINT
    // for(uint32_t J = 0; J < SurfaceFormatCount; ++J) {
    //     VkSurfaceFormatKHR SurfaceFormat = SurfaceFormats[J];
    //     printf("\t[%2d] (%d) %s %s\n", J, SurfaceFormatRanks[J], string_VkFormat(SurfaceFormat.format), string_VkColorSpaceKHR(SurfaceFormat.colorSpace));
    // }
#endif

    if(BestSurfaceFormatScore == 0) {
        printfc(CODE_YELLOW, "Picked suboptimal surface format %s %s as best.\n", string_VkFormat(BestSurfaceFormat.format), string_VkColorSpaceKHR(BestSurfaceFormat.colorSpace));
    }

    *SurfaceFormat = BestSurfaceFormat;
    if(SurfaceFormatScore) {
        *SurfaceFormatScore = BestSurfaceFormatScore;
    }

    return 0;

label_Error:
    return 1;
}

static int VulkanPickSurfacePresentMode(VkPhysicalDevice PhysicalDevice, VkSurfaceKHR Surface, VkPresentModeKHR *PresentMode, uint32_t *PresentModeScore) {
    VkPresentModeKHR PresentModes[8];
    uint32_t PresentModeScores[ArrayCount(PresentModes)];
    uint32_t PresentModeCount = ArrayCount(PresentModes);
    VulkanCheckR1IncompleteWarn(vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &PresentModeCount, PresentModes));
    AssertMessageGoto(PresentModeCount > 0, label_Error, "vkGetPhysicalDeviceSurfacePresentModesKHR returned 0 present modes.\n");

    uint32_t BestPresentModeScore = 0;
    VkPresentModeKHR BestPresentMode = PresentModes[0];
    for(uint32_t I = 0; I < PresentModeCount; ++I) {
        VkPresentModeKHR PresentMode = PresentModes[I];
        uint32_t Score = 0;
        if(PresentMode == VK_PRESENT_MODE_FIFO_KHR) {
            Score = 1;
        } else if(PresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            Score = 2;
        }

        if(Score > BestPresentModeScore) {
            BestPresentModeScore = Score;
            BestPresentMode = PresentModes[I];
        }
        PresentModeScores[I] = Score;
    }

    *PresentMode = BestPresentMode;
    if(PresentModeScore) {
        *PresentModeScore = BestPresentModeScore;
    }

    return 0;

label_Error:
    return 1;
}

static int VulkanCreateShaderModule(vulkan_surface_device Device, const char *Bytes, uint64_t ByteCount, VkShaderModule *Module) {
    AssertMessageGoto(ByteCount < (uint64_t)SIZE_T_MAX, label_Error, "Too many bytes in shader code.\n");
    VkShaderModuleCreateInfo CreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .codeSize = (size_t)ByteCount,
        .pCode = (uint32_t *)Bytes
    };
    VkShaderModule LocalModule;
    VulkanCheckGoto(vkCreateShaderModule(Device.Handle, &CreateInfo, 0, &LocalModule), label_Error);
    *Module = LocalModule;

    return 0;

label_Error:
    return 1;
}

// NOTE(blackedout): Input the instance extensions that are required by the platform.
static int VulkanCreateInstance(const char **PlatformInstanceExtensions, uint32_t PlatformInstanceExtensionCount, VkInstance *Instance) {
    const char *InstanceExtensions[16];
    // NOTE(blackedout): Leave room for additional 1 extensions (portability extension)
    AssertMessageGoto(PlatformInstanceExtensionCount <= 15, label_Error, "Too many required instance extensions (%d). Increase array buffer size to fix.\n", PlatformInstanceExtensionCount);
    
    uint32_t InstanceExtensionCount;
    for(InstanceExtensionCount = 0; InstanceExtensionCount < PlatformInstanceExtensionCount; ++InstanceExtensionCount) {
        InstanceExtensions[InstanceExtensionCount] = PlatformInstanceExtensions[InstanceExtensionCount];
    }

    VkApplicationInfo VulkanApplicationInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = 0,
        .pApplicationName = "",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    // TODO(blackedout): Check if these are actually available (and create custom error callback functions?)
    const char *ValidationLayers[] = {
        "VK_LAYER_KHRONOS_validation"
    };

    VkInstanceCreateInfo InstanceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .pApplicationInfo = &VulkanApplicationInfo,
        .enabledLayerCount = ArrayCount(ValidationLayers),
        .ppEnabledLayerNames = ValidationLayers,
        .enabledExtensionCount = InstanceExtensionCount,
        .ppEnabledExtensionNames = InstanceExtensions,
    };

    // TODO(blackedout): Check if version is supported
    VkResult CreateInstanceResult = vkCreateInstance(&InstanceCreateInfo, 0, Instance);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if(CreateInstanceResult == VK_ERROR_INCOMPATIBLE_DRIVER) {
        InstanceExtensions[InstanceExtensionCount++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        InstanceCreateInfo.enabledExtensionCount = InstanceExtensionCount;
        InstanceCreateInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

        CreateInstanceResult = vkCreateInstance(&InstanceCreateInfo, 0, Instance);
    }
#endif

#ifdef VULKAN_INFO_PRINT
    printf("Vulkan Instance Extensions (%d):\n", InstanceExtensionCount);
    for(uint32_t I = 0; I < InstanceExtensionCount; ++I) {
        printf("[%d] %s%s\n", I, InstanceExtensions[I], (I < PlatformInstanceExtensionCount)? " (platform)" : "");
    }
#endif
    
    VulkanCheckGoto(CreateInstanceResult, label_Error);

    return 0;

label_Error:
    return 1;
}

static void VulkanDestroySurfaceDevice(VkInstance Instance, vulkan_surface_device Device) {
    vkDestroySurfaceKHR(Instance, Device.Surface, 0);
    vkDestroyDevice(Device.Handle, 0);
}

static int VulkanCreateSurfaceDevice(VkInstance Instance, VkSurfaceKHR Surface, vulkan_surface_device *Device) {
    // NOTE(blackedout): This function will destroy the input surface on failure.
    // Returns a device whose physical device has at least one graphics queue, at least one surface presentation queue and supports the surface extension.
    // The physical device is picked by scoring its type, available surface formats and present modes.

    VkPhysicalDevice PhysicalDevices[16];
    uint32_t PhyiscalDeviceScores[ArrayCount(PhysicalDevices)];
    uint32_t PhysicalDeviceCount = ArrayCount(PhysicalDevices);
    VulkanCheckGotoIncompleteWarn(vkEnumeratePhysicalDevices(Instance, &PhysicalDeviceCount, PhysicalDevices), label_Error);
    AssertMessageGoto(PhysicalDeviceCount > 0, label_Error, "Vulkan says there are no GPUs.\n");

    uint32_t BestPhyiscalDeviceGraphicsQueueIndex;
    uint32_t BestPhyiscalDeviceSurfaceQueueIndex;
    int BestPhyiscalDeviceHasPortabilitySubsetExtension;
    VkSurfaceFormatKHR BestPhyiscalDeviceInitialSurfaceFormat;

    uint32_t BestPhyiscalDeviceScore = 0;
    VkPhysicalDevice BestPhyiscalDevice = PhysicalDevices[0];
    for(uint32_t I = 0; I < PhysicalDeviceCount; ++I) {
        VkPhysicalDevice PhysicalDevice = PhysicalDevices[I];

        VkPhysicalDeviceProperties Props;
        vkGetPhysicalDeviceProperties(PhysicalDevice, &Props);

        VkQueueFamilyProperties DeviceQueueFamilyProperties[8];
        uint32_t DeviceQueueFamilyPropertyCount = ArrayCount(DeviceQueueFamilyProperties);
        vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &DeviceQueueFamilyPropertyCount, DeviceQueueFamilyProperties);
        if(DeviceQueueFamilyPropertyCount == ArrayCount(DeviceQueueFamilyProperties)) {
            printfc(CODE_YELLOW, "vkGetPhysicalDeviceQueueFamilyProperties returned %d queue family properties, reaching buffer capacity.\n", DeviceQueueFamilyPropertyCount);
        }

        VkExtensionProperties ExtensionProperties[256];
        uint32_t ExtensionPropertyCount = ArrayCount(ExtensionProperties);
        VulkanCheckGotoIncompleteWarn(vkEnumerateDeviceExtensionProperties(PhysicalDevice, 0, &ExtensionPropertyCount, ExtensionProperties), label_Error);
        
        int IsUsable = 1;

        int HasGraphicsQueue = 0, HasSurfaceQueue = 0;
        uint32_t UsableQueueGraphicsIndex, UsableQueueSurfaceIndex;
        for(uint32_t J = 0; J < DeviceQueueFamilyPropertyCount; ++J) {
            VkQueueFamilyProperties QueueFamilyProps = DeviceQueueFamilyProperties[J];
            int IsGraphics = (QueueFamilyProps.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            
            VkBool32 IsSurfaceSupported;
            VulkanCheckGoto(vkGetPhysicalDeviceSurfaceSupportKHR(PhysicalDevice, J, Surface, &IsSurfaceSupported), label_Error);

            if(IsGraphics) {
                UsableQueueGraphicsIndex = J;
                HasGraphicsQueue = 1;
            }
            if(IsSurfaceSupported) {
                UsableQueueSurfaceIndex = J;
                HasSurfaceQueue = 1;
            }
        }
        IsUsable = IsUsable || (HasGraphicsQueue && HasSurfaceQueue);

        int HasSwapchainExtension = 0;
        int HasPortabilitySubsetExtension = 0;
        for(uint32_t J = 0; J < ExtensionPropertyCount; ++J) {
            if(strcmp(ExtensionProperties[J].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                HasSwapchainExtension = 1;
            }

            // NOTE(blackedout): This is done to remove a Vulkan warning.
            if(strcmp(ExtensionProperties[J].extensionName, "VK_KHR_portability_subset") == 0) {
                HasPortabilitySubsetExtension = 1;
            }
        }
        IsUsable = IsUsable && HasSwapchainExtension;

        uint32_t DeviceTypeScore;
        switch(Props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            DeviceTypeScore = 3; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            DeviceTypeScore = 2; break;
        default:
            DeviceTypeScore = 1; break;
        }
        
        VkSurfaceFormatKHR BestSurfaceFormat;
        uint32_t BestSurfaceFormatScore;
        IsUsable = IsUsable && (0 == VulkanPickSurfaceFormat(PhysicalDevice, Surface, &BestSurfaceFormat, &BestSurfaceFormatScore));

        VkPresentModeKHR BestPresentMode;
        uint32_t BestPresentModeScore;
        IsUsable = IsUsable && (0 == VulkanPickSurfacePresentMode(PhysicalDevice, Surface, &BestPresentMode, &BestPresentModeScore));

        uint32_t Score = 0;
        if(IsUsable) {
            // NOTE(blackedout): As long as they are usable, just pick the best once (hence the added 1)
            Score = 1 + 3*DeviceTypeScore + BestSurfaceFormatScore + BestPresentModeScore;
            if(Score > BestPhyiscalDeviceScore) {
                BestPhyiscalDeviceScore = Score;
                BestPhyiscalDevice = PhysicalDevice;

                BestPhyiscalDeviceGraphicsQueueIndex = UsableQueueGraphicsIndex;
                BestPhyiscalDeviceSurfaceQueueIndex = UsableQueueSurfaceIndex;
                BestPhyiscalDeviceHasPortabilitySubsetExtension = HasPortabilitySubsetExtension;
                BestPhyiscalDeviceInitialSurfaceFormat = BestSurfaceFormat;
            }
        }
        PhyiscalDeviceScores[I] = Score;
    }

#ifdef VULKAN_INFO_PRINT
    printf("Vulkan phyiscal devices (%d):\n", PhysicalDeviceCount);
    for(uint32_t I = 0; I < PhysicalDeviceCount; ++I) {
        VkPhysicalDeviceProperties Props;
        vkGetPhysicalDeviceProperties(PhysicalDevices[I], &Props);
        printf("[%d] (%d) %s (%s)\n", I, PhyiscalDeviceScores[I], Props.deviceName, string_VkPhysicalDeviceType(Props.deviceType));
        //printf("\t[%d] Queue (%d) %s %s\n", J, QueueFamilyProps.queueCount, IsGraphics? "VK_QUEUE_GRAPHICS_BIT" : "", IsSurfaceSupported? "SURFACE" : "");
        //printf("\tSurface image count range: %d to %d\n", SurfaceCapabilities.minImageCount, SurfaceCapabilities.maxImageCount);
        //printf("\tSurface image extents: (%d, %d) to (%d, %d)\n", SurfaceCapabilities.minImageExtent.width, SurfaceCapabilities.minImageExtent.height, SurfaceCapabilities.maxImageExtent.width, SurfaceCapabilities.maxImageExtent.height);
    }
#endif

    AssertMessageGoto(BestPhyiscalDeviceScore > 0, label_Error, "No usable physical device found.\n");

    float DeviceQueuePriorities[] = { 1.0f };
    VkDeviceQueueCreateInfo DeviceQueueCreateInfos[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .queueFamilyIndex = BestPhyiscalDeviceGraphicsQueueIndex,
            .queueCount = 1,
            .pQueuePriorities = DeviceQueuePriorities
        },
    };

    uint32_t DeviceQueueCreateInfoCount = 1;
    if(BestPhyiscalDeviceGraphicsQueueIndex != BestPhyiscalDeviceSurfaceQueueIndex) {
        DeviceQueueCreateInfos[1] = DeviceQueueCreateInfos[0];
        DeviceQueueCreateInfos[1].queueFamilyIndex = BestPhyiscalDeviceSurfaceQueueIndex;
        DeviceQueueCreateInfoCount = 2;
    }

    const char *ExtensionNames[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        "VK_KHR_portability_subset"
    };

    uint32_t ExtensionNameCount = ArrayCount(ExtensionNames);
    if(BestPhyiscalDeviceHasPortabilitySubsetExtension == 0) {
        ExtensionNameCount -= 1;
    }

    VkPhysicalDeviceFeatures PhyiscalDeviceFeatures = {};
    VkDeviceCreateInfo DeviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .queueCreateInfoCount = DeviceQueueCreateInfoCount,
        .pQueueCreateInfos = DeviceQueueCreateInfos,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = 0,
        .enabledExtensionCount = ExtensionNameCount,
        .ppEnabledExtensionNames = ExtensionNames,
        .pEnabledFeatures = &PhyiscalDeviceFeatures,
    };

    VkSurfaceCapabilitiesKHR BestPhysicalDeviceSurfaceCapabilities;
    VulkanCheckGoto(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(BestPhyiscalDevice, Surface, &BestPhysicalDeviceSurfaceCapabilities), label_Error);

    VkDevice DeviceHandle;
    VulkanCheckGoto(vkCreateDevice(BestPhyiscalDevice, &DeviceCreateInfo, 0, &DeviceHandle), label_Error);

    vulkan_surface_device LocalSurfaceDevice = {
        .Handle = DeviceHandle,
        .PhysicalDevice = BestPhyiscalDevice,
        .Surface = Surface,
        .GraphicsQueueFamilyIndex = BestPhyiscalDeviceGraphicsQueueIndex,
        .PresentQueueFamilyIndex = BestPhyiscalDeviceSurfaceQueueIndex,

        .InitialExtent = BestPhysicalDeviceSurfaceCapabilities.currentExtent,
        .InitialSurfaceFormat = BestPhyiscalDeviceInitialSurfaceFormat
    };

    *Device = LocalSurfaceDevice;

    return 0;

label_Error:
    vkDestroySurfaceKHR(Instance, Surface, 0);
    return 1;
}

static void VulkanDestroySwapchain(vulkan_surface_device Device, vulkan_swapchain Swapchain) {
    // NOTE(blackedout): Only destructible if none of its images are acquired.
    AssertMessage(Swapchain.AcquiredImageCount == 0, "Swapchain can't be destroyed because at least one of its imagess is still in use.\n");

    for(uint32_t I = 0; I < Swapchain.ImageCount; ++I) {
        vkDestroyFramebuffer(Device.Handle, Swapchain.Framebuffers[I], 0);
        vkDestroyImageView(Device.Handle, Swapchain.ImageViews[I], 0);
    }

    vkDestroySwapchainKHR(Device.Handle, Swapchain.Handle, 0);

    free(Swapchain.ImageBuf);
}

static int VulkanCreateSwapchain(vulkan_surface_device Device, VkExtent2D Extent, VkRenderPass RenderPass, vulkan_swapchain *OldSwapchain, vulkan_swapchain *Swapchain) {
    VkSwapchainKHR OldSwapchainHandle = VULKAN_NULL_HANDLE;
    if(OldSwapchain) {
        OldSwapchainHandle = OldSwapchain->Handle;
    }

    VkSurfaceCapabilitiesKHR SurfaceCapabilities;
    VulkanCheckGoto(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Device.PhysicalDevice, Device.Surface, &SurfaceCapabilities), label_Error);

    VkSurfaceFormatKHR SurfaceFormat;
    CheckGoto(VulkanPickSurfaceFormat(Device.PhysicalDevice, Device.Surface, &SurfaceFormat, 0), label_Error);

    VkPresentModeKHR PresentMode;
    CheckGoto(VulkanPickSurfacePresentMode(Device.PhysicalDevice, Device.Surface, &PresentMode, 0), label_Error);

    uint32_t MinImageCount = Max(2, SurfaceCapabilities.minImageCount);
    if(SurfaceCapabilities.maxImageCount > 0) {
        MinImageCount = Min(MinImageCount, SurfaceCapabilities.maxImageCount);
    }
    VkExtent2D ClampedImageExtent = {
        .width = Clamp(Extent.width, SurfaceCapabilities.minImageExtent.width, SurfaceCapabilities.maxImageExtent.width),
        .height = Clamp(Extent.height, SurfaceCapabilities.minImageExtent.height, SurfaceCapabilities.maxImageExtent.height)
    };

    uint32_t QueueFamilyIndices[] = { Device.GraphicsQueueFamilyIndex, Device.PresentQueueFamilyIndex };
    VkSharingMode QueueFamilySharingMode = VK_SHARING_MODE_EXCLUSIVE;
    uint32_t QueueFamilyIndexCount = 0;
    uint32_t *QueueFamilyIndicesOptional = 0;
    if(Device.GraphicsQueueFamilyIndex != Device.PresentQueueFamilyIndex) {
        QueueFamilySharingMode = VK_SHARING_MODE_CONCURRENT;
        QueueFamilyIndexCount = 2;
        QueueFamilyIndicesOptional = QueueFamilyIndices;
    }

    VkSwapchainCreateInfoKHR SwapchainCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = 0,
        .flags = 0,
        .surface = Device.Surface,
        .minImageCount = MinImageCount,
        .imageFormat = SurfaceFormat.format,
        .imageColorSpace = SurfaceFormat.colorSpace,
        .imageExtent = ClampedImageExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = QueueFamilySharingMode,
        .queueFamilyIndexCount = QueueFamilyIndexCount,
        .pQueueFamilyIndices = QueueFamilyIndices,
        .preTransform = SurfaceCapabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = PresentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = OldSwapchainHandle
    };

    VkSwapchainKHR SwapchainHandle;
    VulkanCheckGoto(vkCreateSwapchainKHR(Device.Handle, &SwapchainCreateInfo, 0, &SwapchainHandle), label_Error);

    uint32_t SwapchainImageCount = 0;
    VulkanCheckGoto(vkGetSwapchainImagesKHR(Device.Handle, SwapchainHandle, &SwapchainImageCount, 0), label_Swapchain);

    void *ImageBuf;
    VkImage *Images;
    VkImageView *ImageViews;
    VkFramebuffer *Framebuffers;
    {
        malloc_multiple_subbuf SwapchainSubbufs[] = {
            { &Images, SwapchainImageCount*sizeof(VkImage) },
            { &ImageViews, SwapchainImageCount*sizeof(VkImageView) },
            { &Framebuffers, SwapchainImageCount*sizeof(VkFramebuffer) }
        };
        CheckGoto(MallocMultiple(ArrayCount(SwapchainSubbufs), SwapchainSubbufs, &ImageBuf), label_Swapchain);
    }
    VulkanCheckGoto(vkGetSwapchainImagesKHR(Device.Handle, SwapchainHandle, &SwapchainImageCount, Images), label_ImageBuf);

    uint32_t CreatedImageViewCount;
    for(CreatedImageViewCount = 0; CreatedImageViewCount < SwapchainImageCount; ++CreatedImageViewCount) {
        VkComponentMapping ImageViewComponentMapping = {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY
        };
        VkImageSubresourceRange ImageViewSubresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };
        VkImageViewCreateInfo ImageViewCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .image = Images[CreatedImageViewCount],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = SurfaceFormat.format,
            .components = ImageViewComponentMapping,
            .subresourceRange = ImageViewSubresourceRange
        };

        VulkanCheckGoto(vkCreateImageView(Device.Handle, &ImageViewCreateInfo, 0, ImageViews + CreatedImageViewCount), label_ImageViews);
    }

    uint32_t CreatedFramebufferCount;
    for(CreatedFramebufferCount = 0; CreatedFramebufferCount < SwapchainImageCount; ++CreatedFramebufferCount) {
        VkFramebufferCreateInfo FramebufferCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .renderPass = RenderPass,
            .attachmentCount = 1,
            .pAttachments = ImageViews + CreatedFramebufferCount,
            .width = ClampedImageExtent.width,
            .height = ClampedImageExtent.height,
            .layers = 1
        };

        VulkanCheckGoto(vkCreateFramebuffer(Device.Handle, &FramebufferCreateInfo, 0, Framebuffers + CreatedFramebufferCount), label_Framebuffers);
    }

    vulkan_swapchain LocalSwapchain = {
        .Handle = SwapchainHandle,
        .ImageExtent = ClampedImageExtent,
        .Format = SurfaceFormat.format,
        .ImageCount = SwapchainImageCount,
        .Images = Images,
        .ImageViews = ImageViews,
        .Framebuffers = Framebuffers,
        .ImageBuf = ImageBuf,
        .AcquiredImageCount = 0,
    };

    *Swapchain = LocalSwapchain;

    return 0;

label_Framebuffers:
    for(uint32_t I = 0; I < CreatedFramebufferCount; ++I) {
        vkDestroyFramebuffer(Device.Handle, Framebuffers[I], 0);
    }

label_ImageViews:
    for(uint32_t I = 0; I < CreatedImageViewCount; ++I) {
        vkDestroyImageView(Device.Handle, ImageViews[I], 0);
    }

label_ImageBuf:
    free(ImageBuf);
label_Swapchain:
    vkDestroySwapchainKHR(Device.Handle, SwapchainHandle, 0);
label_Error:
    return 1;
}

static void VulkanDestroySwapchainHandler(vulkan_surface_device Device, vulkan_swapchain_handler *SwapchainHandler) {
    vulkan_swapchain_handler Handler = *SwapchainHandler;
    for(uint32_t I = 0; I < Handler.SwapchainBufIndices.Count; ++I) {
        uint32_t CircularIndex = IndicesCircularGet(&Handler.SwapchainBufIndices, I);
        VulkanDestroySwapchain(Device, Handler.Swapchains[CircularIndex]);
    }

    for(uint32_t I = 0; I < MAX_ACQUIRED_IMAGE_COUNT; ++I) {
        vkDestroySemaphore(Device.Handle, SwapchainHandler->ImageAvailableSemaphores[I], 0);
        vkDestroySemaphore(Device.Handle, SwapchainHandler->RenderFinishedSemaphores[I], 0);
        vkDestroyFence(Device.Handle, SwapchainHandler->InFlightFences[I], 0);
    }
}

static int VulkanCreateSwapchainAndHandler(vulkan_surface_device Device, VkExtent2D InitialExtent, VkRenderPass RenderPass, vulkan_swapchain_handler *SwapchainHandler) {
    vulkan_swapchain Swapchain;
    CheckGoto(VulkanCreateSwapchain(Device, InitialExtent, RenderPass, 0, &Swapchain), label_Error);

    vulkan_swapchain_handler Handler = {
        .RenderPassCount = 1,
        .RenderPass = RenderPass,

        .SwapchainIndexLastAcquired = UINT32_MAX,
        .SwapchainBufIndices = {
            .Cap = MAX_SWAPCHAIN_COUNT,
            .Count = 1,
            .Next = 1%MAX_SWAPCHAIN_COUNT
        },
        .Swapchains = { Swapchain },

        .AcquiredImageBufIndices = {
            .Cap = MAX_ACQUIRED_IMAGE_COUNT,
            .Count = 0,
            .Next = 0
        },
        .AcquiredImageIndices = {},
        .AcquiredImageSwapchainIndices = {},
    };

    VkSemaphoreCreateInfo UnsignaledSemaphoreCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = 0,
        .flags = 0
    };

    VkFenceCreateInfo UnsignaledFenceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = 0,
        .flags = 0
    };

    uint32_t SemaphoresCount, FencesCount;
    VkSemaphore Semaphores[2*MAX_ACQUIRED_IMAGE_COUNT];
    VkFence Fences[MAX_ACQUIRED_IMAGE_COUNT];
    
    for(SemaphoresCount = 0; SemaphoresCount < ArrayCount(Semaphores); ++SemaphoresCount) {
        VulkanCheckGoto(vkCreateSemaphore(Device.Handle, &UnsignaledSemaphoreCreateInfo, 0, Semaphores + SemaphoresCount), label_Semaphores);
    }
    
    for(FencesCount = 0; FencesCount < ArrayCount(Fences); ++FencesCount) {
        VulkanCheckGoto(vkCreateFence(Device.Handle, &UnsignaledFenceCreateInfo, 0, Fences + FencesCount), label_Fences);
    }

    memcpy(Handler.ImageAvailableSemaphores, Semaphores, MAX_ACQUIRED_IMAGE_COUNT*sizeof(*Semaphores));
    memcpy(Handler.RenderFinishedSemaphores, Semaphores + MAX_ACQUIRED_IMAGE_COUNT, MAX_ACQUIRED_IMAGE_COUNT*sizeof(*Semaphores));
    memcpy(Handler.InFlightFences, Fences, MAX_ACQUIRED_IMAGE_COUNT*sizeof(*Fences));

    *SwapchainHandler = Handler;

    return 0;

label_Fences:
    for(uint32_t I = 0; I < FencesCount; ++I) {
        vkDestroyFence(Device.Handle, Fences[I], 0);
    }
label_Semaphores:
    for(uint32_t I = 0; I < SemaphoresCount; ++I) {
        vkDestroySemaphore(Device.Handle, Semaphores[I], 0);
    }
    VulkanDestroySwapchain(Device, Swapchain);
label_Error:
    return 1;
}

static void VulkanDestroyDefaultGraphicsPipeline(vulkan_surface_device Device, vulkan_graphics_pipeline_info PipelineInfo) {
    vkDestroyPipeline(Device.Handle, PipelineInfo.Pipeline, 0);
    vkDestroyRenderPass(Device.Handle, PipelineInfo.RenderPass, 0);
    vkDestroyPipelineLayout(Device.Handle, PipelineInfo.Layout, 0);
}

static int VulkanCreateDefaultGraphicsPipeline(vulkan_surface_device Device, VkShaderModule ModuleVS, VkShaderModule ModuleFS, VkExtent2D InitialExtent, VkFormat SwapchainFormat, vulkan_graphics_pipeline_info *PipelineInfo) {
    VkPipelineShaderStageCreateInfo PipelineStageCreateInfos[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = ModuleVS,
            .pName = "main", // NOTE(blackedout): Entry point
            .pSpecializationInfo = 0
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = ModuleFS,
            .pName = "main",
            .pSpecializationInfo = 0
        }
    };

    VkDynamicState VulkanDynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo PipelineDynamicStateCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .dynamicStateCount = ArrayCount(VulkanDynamicStates),
        .pDynamicStates = VulkanDynamicStates
    };

    VkPipelineVertexInputStateCreateInfo PipelineVertexInputStateCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = 0,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = 0,
    };

    VkPipelineInputAssemblyStateCreateInfo PipelineInputAssemblyStateCreateInfo = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport Viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)InitialExtent.width,
        .height = (float)InitialExtent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };

    VkRect2D Scissors = {
        .offset = { 0, 0 },
        .extent = InitialExtent,
    };

    VkPipelineViewportStateCreateInfo PipelineViewportStateCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = &Viewport,
        .scissorCount = 1,
        .pScissors = &Scissors,
    };

    VkPipelineRasterizationStateCreateInfo PipelineRasterizationStateCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo PipelineMultiSampleStateCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = 0,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };

    VkPipelineColorBlendAttachmentState PipelineColorBlendAttachmentState = {
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo PipelineColorBlendStateCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_CLEAR,
        .attachmentCount = 1,
        .pAttachments = &PipelineColorBlendAttachmentState,
        .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}
    };

    VkPipelineLayout LocalPipelineLayout;
    VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .setLayoutCount = 0,
        .pSetLayouts = 0,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = 0,
    };
    VulkanCheckGoto(vkCreatePipelineLayout(Device.Handle, &PipelineLayoutCreateInfo, 0, &LocalPipelineLayout), label_Error);

    VkAttachmentDescription AttachmentDescriptions[] = {
        {
            .flags = 0,
            .format = SwapchainFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        }
    };

    VkAttachmentReference ColorAttachmentRef = {
        .attachment = 0, // NOTE(blackedout): Fragment shader layout index
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription SubpassDescription = {
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &ColorAttachmentRef,
        .pResolveAttachments = 0,
        .pDepthStencilAttachment = 0,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = 0,
    };

    VkSubpassDependency RenderSubpassDependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0, // NOTE(blackedout): First subpass index
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = 0,
    };

    VkRenderPassCreateInfo RenderPassCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .attachmentCount = ArrayCount(AttachmentDescriptions),
        .pAttachments = AttachmentDescriptions,
        .subpassCount = 1,
        .pSubpasses = &SubpassDescription,
        .dependencyCount = 1,
        .pDependencies = &RenderSubpassDependency,
    };
    VkRenderPass LocalRenderPass;
    VulkanCheckGoto(vkCreateRenderPass(Device.Handle, &RenderPassCreateInfo, 0, &LocalRenderPass), label_PipelineLayout);

    VkGraphicsPipelineCreateInfo GraphicsPipelineCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .stageCount = ArrayCount(PipelineStageCreateInfos),
        .pStages = PipelineStageCreateInfos,
        .pVertexInputState = &PipelineVertexInputStateCreateInfo,
        .pInputAssemblyState = &PipelineInputAssemblyStateCreateInfo,
        .pTessellationState = 0,
        .pViewportState = &PipelineViewportStateCreateInfo,
        .pRasterizationState = &PipelineRasterizationStateCreateInfo,
        .pMultisampleState = &PipelineMultiSampleStateCreateInfo,
        .pDepthStencilState = 0,
        .pColorBlendState = &PipelineColorBlendStateCreateInfo,
        .pDynamicState = &PipelineDynamicStateCreateInfo,
        .layout = LocalPipelineLayout,
        .renderPass = LocalRenderPass,
        .subpass = 0,
        .basePipelineHandle = VULKAN_NULL_HANDLE,
        .basePipelineIndex = -1
    };

    VkPipeline LocalPipeline;
    VulkanCheckGoto(vkCreateGraphicsPipelines(Device.Handle, VULKAN_NULL_HANDLE, 1, &GraphicsPipelineCreateInfo, 0, &LocalPipeline), label_RenderPass);

    vulkan_graphics_pipeline_info LocalPipelineInfo = {
        .Layout = LocalPipelineLayout,
        .RenderPass = LocalRenderPass,
        .Pipeline = LocalPipeline
    };
    *PipelineInfo = LocalPipelineInfo;

    return 0;

label_RenderPass:
    vkDestroyRenderPass(Device.Handle, LocalRenderPass, 0);
label_PipelineLayout:
    vkDestroyPipelineLayout(Device.Handle, LocalPipelineLayout, 0);
label_Error:
    return 1;
}

static int VulkanAcquireNextImage(vulkan_surface_device Device, vulkan_swapchain_handler *SwapchainHandler, VkExtent2D FramebufferExtent, VkExtent2D *ImageExtent, VkFramebuffer *Framebuffer) {
    vulkan_swapchain_handler Handler = *SwapchainHandler;

    uint32_t ImageIndex;
    uint32_t SwapchainIndex;
    vulkan_swapchain Swapchain;
    for(;;) {
        SwapchainIndex = IndicesCircularHead(&Handler.SwapchainBufIndices);
        Swapchain = Handler.Swapchains[SwapchainIndex];
        VkSemaphore ImageAvailableSemaphore = Handler.ImageAvailableSemaphores[Handler.AcquiredImageBufIndices.Next];
        VkResult AcquireResult = vkAcquireNextImageKHR(Device.Handle, Swapchain.Handle, UINT64_MAX, ImageAvailableSemaphore, VULKAN_NULL_HANDLE, &ImageIndex);
        if(AcquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            VkFence InFlightFences[ArrayCount(Handler.InFlightFences)];
            for(uint32_t I = 0; I < Handler.AcquiredImageBufIndices.Count; ++I) {
                uint32_t CircularIndex = IndicesCircularGet(&Handler.AcquiredImageBufIndices, I);
                InFlightFences[I] = Handler.InFlightFences[CircularIndex];
            }

            vkWaitForFences(Device.Handle, Handler.AcquiredImageBufIndices.Count, InFlightFences, VK_TRUE, UINT64_MAX);
            Handler.AcquiredImageBufIndices.Next = 0;
            Handler.AcquiredImageBufIndices.Count = 0;

            CheckGoto(VulkanCreateSwapchain(Device, FramebufferExtent, Handler.RenderPass, &Swapchain, &Swapchain), label_Error);
            for(uint32_t I = 0; I < Handler.SwapchainBufIndices.Count; ++I) {
                uint32_t CircularIndex = IndicesCircularGet(&Handler.SwapchainBufIndices, I);
                VulkanDestroySwapchain(Device, Handler.Swapchains[CircularIndex]);
            }
            Handler.SwapchainBufIndices.Next = 1;
            Handler.SwapchainBufIndices.Count = 1;
            Handler.Swapchains[0] = Swapchain;
        } else {
            if(AcquireResult == VK_SUBOPTIMAL_KHR) {
                // NOTE(blackedout): This is handled after queueing for presentation
            } else VulkanCheckGoto(AcquireResult, label_Error);

            // NOTE(blackedout): Image acquisition worked, so push
            uint32_t AcquiredImageBufIndex = IndicesCircularPush(&Handler.AcquiredImageBufIndices);
            Handler.AcquiredImageIndices[AcquiredImageBufIndex] = ImageIndex;
            Handler.AcquiredImageSwapchainIndices[AcquiredImageBufIndex] = SwapchainIndex;
            ++Handler.Swapchains[SwapchainIndex].AcquiredImageCount;

            Handler.SwapchainIndexLastAcquired = SwapchainIndex;
            printf("acquired image %d (framebuffer %p) from swapchain %d (buf index %d)\n", ImageIndex, Swapchain.Framebuffers[ImageIndex], SwapchainIndex, AcquiredImageBufIndex);

            break;
        }
    }

    *ImageExtent = Swapchain.ImageExtent;
    *Framebuffer = Swapchain.Framebuffers[ImageIndex];
    *SwapchainHandler = Handler;

    return 0;

label_Error:
    return 1;
}

static int VulkanSubmitFinalAndPresent(vulkan_surface_device Device, vulkan_swapchain_handler *SwapchainHandler, VkQueue GraphicsQueue, VkCommandBuffer GraphicsCommandBuffer, VkExtent2D FramebufferExtent) {
    vulkan_swapchain_handler Handler = *SwapchainHandler;

    uint32_t SwapchainIndex = Handler.SwapchainIndexLastAcquired;
    uint32_t AcquiredImagesIndex = IndicesCircularHead(&Handler.AcquiredImageBufIndices);

    VkPipelineStageFlags WaitDstStageMasks[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo GraphicsSubmitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = 0,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &Handler.ImageAvailableSemaphores[AcquiredImagesIndex],
        .pWaitDstStageMask = WaitDstStageMasks,
        .commandBufferCount = 1,
        .pCommandBuffers = &GraphicsCommandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &Handler.RenderFinishedSemaphores[AcquiredImagesIndex]
    };
    VulkanCheckGoto(vkQueueSubmit(GraphicsQueue, 1, &GraphicsSubmitInfo, Handler.InFlightFences[AcquiredImagesIndex]), label_Error);

    VkPresentInfoKHR PresentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = 0,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &Handler.RenderFinishedSemaphores[AcquiredImagesIndex],
        .swapchainCount = 1,
        .pSwapchains = &Handler.Swapchains[SwapchainIndex].Handle,
        .pImageIndices = &Handler.AcquiredImageIndices[AcquiredImagesIndex],
        .pResults = 0 // NOTE(blackedout): Only needed if multiple swapchains used
    };

    printf("Queueing image %d of swapchain %d for presentaton.\n", PresentInfo.pImageIndices[0], SwapchainIndex);
    double before = glfwGetTime();
    VkResult PresentResult = vkQueuePresentKHR(GraphicsQueue, &PresentInfo);
    double after = glfwGetTime();
    printf("Took %f s.\n", after - before);
    if(PresentResult == VK_SUBOPTIMAL_KHR) {
        // TODO(blackedout): There is still an issue where sometimes the next frame is not rendered (clear color only) when resizing multiple times in quick succession
        vulkan_swapchain NewSwapchain = Handler.Swapchains[SwapchainIndex];
        CheckGoto(VulkanCreateSwapchain(Device, FramebufferExtent, Handler.RenderPass, &NewSwapchain, &NewSwapchain), label_Error);
        uint32_t NewSwapchainIndex = IndicesCircularPush(&Handler.SwapchainBufIndices);
        Handler.Swapchains[NewSwapchainIndex] = NewSwapchain;

        printf("Swapchain %d pushed because suboptimal\n", NewSwapchainIndex);
    } else VulkanCheckGoto(PresentResult, label_Error); // NOTE(blackedout): VK_ERROR_OUT_OF_DATE_KHR shouldn't be happening here (?) since there was no event polling that could've changed the window

    // NOTE(blackedout): Remove acquired image from list
    uint32_t AcquiredImagesBaseIndex = IndicesCircularTake(&Handler.AcquiredImageBufIndices);
    VulkanCheckGoto(vkWaitForFences(Device.Handle, 1, &Handler.InFlightFences[AcquiredImagesBaseIndex], VK_TRUE, UINT64_MAX), label_Error);
    vkResetFences(Device.Handle, 1, &Handler.InFlightFences[AcquiredImagesBaseIndex]);
    Handler.AcquiredImageIndices[AcquiredImagesBaseIndex] = 0;
    Handler.AcquiredImageSwapchainIndices[AcquiredImagesBaseIndex] = 0;

    uint32_t SwapchainsBaseIndex = IndicesCircularGet(&Handler.SwapchainBufIndices, 0);
    AssertMessageGoto(Handler.Swapchains[SwapchainsBaseIndex].AcquiredImageCount > 0, label_Error, "Swapchain acquired image count zero.\n");
    --Handler.Swapchains[SwapchainsBaseIndex].AcquiredImageCount;
    if(Handler.SwapchainBufIndices.Count > 1 && Handler.Swapchains[SwapchainsBaseIndex].AcquiredImageCount == 0) {
        printf("Destructing swapchain %d.\n", SwapchainsBaseIndex);
        VulkanDestroySwapchain(Device, Handler.Swapchains[SwapchainsBaseIndex]);
        
        IndicesCircularTake(&Handler.SwapchainBufIndices);
        memset(&Handler.Swapchains[SwapchainsBaseIndex], 0, sizeof(*Handler.Swapchains));
        Handler.Swapchains[SwapchainsBaseIndex].Handle = VULKAN_NULL_HANDLE;
    }

    *SwapchainHandler = Handler;

    return 0;

label_Error:
    return 1;
}