// Original source in https://github.com/blackedout01/glfw-vk-template
//
// zlib License
//
// (C) 2024 blackedout01
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#define VULKAN_NULL_HANDLE 0 // NOTE(blackedout): Somehow VK_NULL_HANDLE generates erros when compiling in cpp mode
#define VULKAN_INFO_PRINT

#ifndef MAX_ACQUIRED_IMAGE_COUNT
#define MAX_ACQUIRED_IMAGE_COUNT 1
#endif
#define MAX_SWAPCHAIN_COUNT (MAX_ACQUIRED_IMAGE_COUNT + 1)

static const char *VULKAN_REQUESTED_INSTANCE_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};

typedef struct {
    VkDevice Handle;
    VkSurfaceKHR Surface;

    VkPhysicalDevice PhysicalDevice;
    //VkPhysicalDeviceMemoryProperties PhysicalMemoryProperties;

    uint32_t GraphicsQueueFamilyIndex;
    uint32_t PresentQueueFamilyIndex;

    VkExtent2D InitialExtent;
    VkSurfaceFormatKHR InitialSurfaceFormat;

    VkPhysicalDeviceFeatures Features;
    VkPhysicalDeviceProperties Properties;

    VkFormat BestDepthFormat;
    VkSampleCountFlagBits MaxSampleCount;
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

    VkImage DepthImage;
    VkImageView DepthImageView;
    VkDeviceMemory DepthImageMemory;

    VkImage MultiSampleColorImage;
    VkImageView MultiSampleColorImageView;
    VkDeviceMemory MultiSampleColorImageMemory;

    uint32_t AcquiredImageCount;
} vulkan_swapchain;

typedef struct {
    uint32_t RenderPassCount;
    VkRenderPass RenderPass;
    VkSampleCountFlagBits SampleCount;

    uint32_t SwapchainIndexLastAcquired;
    buffer_indices SwapchainBufIndices;
    vulkan_swapchain Swapchains[MAX_SWAPCHAIN_COUNT];

    buffer_indices AcquiredImageDataIndices;
    uint32_t AcquiredSwapchainImageIndices[MAX_ACQUIRED_IMAGE_COUNT];
    uint32_t AcquiredSwapchainIndices[MAX_ACQUIRED_IMAGE_COUNT];

    VkSemaphore ImageAvailableSemaphores[MAX_ACQUIRED_IMAGE_COUNT];
    VkSemaphore RenderFinishedSemaphores[MAX_ACQUIRED_IMAGE_COUNT];
    VkFence InFlightFences[MAX_ACQUIRED_IMAGE_COUNT];
} vulkan_swapchain_handler;

typedef struct {
    VkFramebuffer Framebuffer;
    VkExtent2D Extent;
    uint32_t DataIndex;
} vulkan_acquired_image;

typedef struct {
    VkShaderModule Vert, Frag;
} vulkan_shader;

typedef struct {
    VkBuffer Handle;
    VkDeviceMemory Memory;
} vulkan_buffer;

typedef struct {
    void *Source;
    uint64_t ByteCount;
    uint64_t *OffsetPointer;
} vulkan_subbuf;

typedef struct {
    VkImageType Type;
    VkImageViewType ViewType;
    VkFormat Format;
    uint32_t Width, Height, Depth;

    uint64_t ByteCount;
    void *Source;
} vulkan_image_description;

typedef struct {
    VkImage Handle;
    VkImageView ViewHandle;
    uint64_t Offset;
} vulkan_image;

typedef struct {
    VkBuffer VertexHandle;
    VkBuffer IndexHandle;

    uint32_t MemoryCount;
    VkDeviceMemory MemoryBufs[3];
} vulkan_static_buffers;

typedef struct {
    vulkan_subbuf Vertices;
    vulkan_subbuf Indices;
} vulkan_mesh_subbuf;

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
    const char *Title = CallString? CallString : "Vulkan error";
    printfc(DoWarn? CODE_YELLOW : CODE_RED, "%s: %s\n", Title, string_VkResult(VulkanResult));
    
    return Result;
}

#define VulkanCheck(Result) VulkanCheckFun(Result, 0, #Result)
#define VulkanCheckGoto(Result, Label) if(VulkanCheckFun(Result, 0, #Result)) goto Label
#define VulkanCheckGotoIncompleteOk(Result, Label) if(VulkanCheckFun(Result, 2, #Result)) goto Label
#define VulkanCheckGotoIncompleteWarn(Result, Label) if(VulkanCheckFun(Result, 1, #Result)) goto Label

// MARK: Scoring
static int VulkanPickSurfaceFormat(VkPhysicalDevice PhysicalDevice, VkSurfaceKHR Surface, VkSurfaceFormatKHR *SurfaceFormat, uint32_t *SurfaceFormatScore) {
    VkSurfaceFormatKHR SurfaceFormats[128];
    uint32_t SurfaceFormatScores[ArrayCount(SurfaceFormats)];
    uint32_t SurfaceFormatCount = ArrayCount(SurfaceFormats);
    VulkanCheckGotoIncompleteWarn(vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &SurfaceFormatCount, SurfaceFormats), label_Error);
    AssertMessageGoto(SurfaceFormatCount > 0, label_Error, "vkGetPhysicalDeviceSurfaceFormatsKHR returned 0 formats even though that must not happen.\n");

    // NOTE(blackedout): From the Vulkan specification 34.5.2. Surface Format Support
    // "While the format of a presentable image refers to the encoding of each pixel, the colorSpace determines how the presentation engine interprets the pixel values."
    // "The color space selected for the swapchain image will not affect the processing of data written into the image by the implementation"
    //
    // Since VK_COLOR_SPACE_SRGB_NONLINEAR_KHR is the only non extension surface color space, search for a surface format that is _SRBG (we don't want shaders to output srgb manually).

    // https://vulkan.gpuinfo.org/listsurfaceformats.php (2024-07-08)
    // Prioritized picks: B8G8R8A8_SRGB, R8G8B8A8_SRGB, first _SRGB format, first remaining format
    {
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
    }

    return 0;

label_Error:
    return 1;
}

static int VulkanPickSurfacePresentMode(VkPhysicalDevice PhysicalDevice, VkSurfaceKHR Surface, VkPresentModeKHR *PresentMode, uint32_t *PresentModeScore) {
    VkPresentModeKHR PresentModes[8];
    uint32_t PresentModeScores[ArrayCount(PresentModes)];
    uint32_t PresentModeCount = ArrayCount(PresentModes);
    VulkanCheckGotoIncompleteWarn(vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &PresentModeCount, PresentModes), label_Error);
    AssertMessageGoto(PresentModeCount > 0, label_Error, "vkGetPhysicalDeviceSurfacePresentModesKHR returned 0 present modes.\n");

    {
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
    }

    return 0;

label_Error:
    return 1;
}

// MARK: Memory, Buffers
static int VulkanGetBufferMemoryTypeIndex(vulkan_surface_device *Device, uint32_t MemoryTypeBits, VkMemoryPropertyFlags MemoryPropertyFlags, uint32_t *MemoryTypeIndex) {
    // TODO(blackedout): Make this part of vulkan_surface_device?
    VkPhysicalDeviceMemoryProperties PhysicalDeviceMemoryProperties;
    vkGetPhysicalDeviceMemoryProperties(Device->PhysicalDevice, &PhysicalDeviceMemoryProperties);

    int HasMemoryType = 0;
    uint32_t BestBufferMemoryTypeIndex;
    for(uint32_t I = 0; I < PhysicalDeviceMemoryProperties.memoryTypeCount; ++I) {
        int TypeUsable = (MemoryTypeBits & (1 << I)) &&
                        (PhysicalDeviceMemoryProperties.memoryTypes[I].propertyFlags & MemoryPropertyFlags) == MemoryPropertyFlags;
        if(TypeUsable) {
            HasMemoryType = 1;
            BestBufferMemoryTypeIndex = I;
        }
    }
    if(HasMemoryType == 0) {
        printfc(CODE_RED, "No suitable memory type was found for bits %x.\n", MemoryTypeBits);
        return 1;
    }

    *MemoryTypeIndex = BestBufferMemoryTypeIndex;
    return 0;
}

static void VulkanDestroyBuffer(vulkan_surface_device *Device, vulkan_buffer *Buffer) {
    VkDevice DeviceHandle = Device->Handle;
    vkDestroyBuffer(DeviceHandle, Buffer->Handle, 0);
    vkFreeMemory(DeviceHandle, Buffer->Memory, 0);
    memset(Buffer, 0, sizeof(*Buffer));
}

static int VulkanCreateExclusiveBufferWithMemory(vulkan_surface_device *Device, uint64_t ByteCount, VkBufferUsageFlags UsageFlags, VkMemoryPropertyFlags MemoryPropertyFlags, vulkan_buffer *Buffer) {
    VkDevice DeviceHandle = Device->Handle;

    vulkan_buffer LocalBuffer = {0};
    {
        VkBufferCreateInfo BufferCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .size = ByteCount,
            .usage = UsageFlags,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE, // NOTE(blackedout): Sharing between queue families
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = 0
        };

        VulkanCheckGoto(vkCreateBuffer(DeviceHandle, &BufferCreateInfo, 0, &LocalBuffer.Handle), label_Error);

        VkMemoryRequirements BufferMemoryRequirements;
        vkGetBufferMemoryRequirements(DeviceHandle, LocalBuffer.Handle, &BufferMemoryRequirements);

        uint32_t BufferMemoryTypeIndex;
        CheckGoto(VulkanGetBufferMemoryTypeIndex(Device, BufferMemoryRequirements.memoryTypeBits, MemoryPropertyFlags, &BufferMemoryTypeIndex), label_Buffer);

        VkMemoryAllocateInfo MemoryAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = 0,
            .allocationSize = BufferMemoryRequirements.size,
            .memoryTypeIndex = BufferMemoryTypeIndex
        };

        VulkanCheckGoto(vkAllocateMemory(DeviceHandle, &MemoryAllocateInfo, 0, &LocalBuffer.Memory), label_Buffer);
        VulkanCheckGoto(vkBindBufferMemory(DeviceHandle, LocalBuffer.Handle, LocalBuffer.Memory, 0), label_Memory);

        *Buffer = LocalBuffer;
    }

    return 0;
    
label_Memory:
    vkFreeMemory(DeviceHandle, LocalBuffer.Memory, 0);
    LocalBuffer.Memory = 0;
label_Buffer:
    vkDestroyBuffer(DeviceHandle, LocalBuffer.Handle, 0);
    LocalBuffer.Handle = 0;
label_Error:
    return 1;
}

static void VulkanDestroyImageWidthMemoryAndView(vulkan_surface_device *Device, VkImage *Image, VkDeviceMemory *ImageMemory, VkImageView *ImageView) {
    VkDevice DeviceHandle = Device->Handle;
    vkDestroyImageView(DeviceHandle, *ImageView, 0);
    *ImageView = 0;
    vkFreeMemory(DeviceHandle, *ImageMemory, 0);
    *ImageMemory = 0;
    vkDestroyImage(DeviceHandle, *Image, 0);
    *Image = 0;
}

static int VulkanCreateExclusiveImageWithMemoryAndView(vulkan_surface_device *Device, VkImageType Type, VkFormat Format, uint32_t Width, uint32_t Height, uint32_t Depth, VkSampleCountFlagBits SampleCount, VkImageUsageFlags Usage, VkMemoryPropertyFlags MemoryProperties, VkImageViewType ViewType, VkImageAspectFlags ViewAspect, VkImage *Image, VkDeviceMemory *ImageMemory, VkImageView *ImageView) {
    VkDevice DeviceHandle = Device->Handle;

    VkImage ImageHandle = 0;
    VkDeviceMemory Memory = 0;
    VkImageView ViewHandle = 0;
    {
        VkImageCreateInfo CreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .imageType = Type,
            .format = Format,
            .extent = {
                .width = Width,
                .height = Height,
                .depth = Depth,
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = SampleCount,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = Usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = 0,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        
        VulkanCheckGoto(vkCreateImage(DeviceHandle, &CreateInfo, 0, &ImageHandle), label_Error);

        VkMemoryRequirements MemoryRequirements;
        vkGetImageMemoryRequirements(DeviceHandle, ImageHandle, &MemoryRequirements);

        uint32_t MemoryTypeIndex;
        CheckGoto(VulkanGetBufferMemoryTypeIndex(Device, MemoryRequirements.memoryTypeBits, MemoryProperties, &MemoryTypeIndex), label_Image);

        VkMemoryAllocateInfo MemoryAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = 0,
            .allocationSize = MemoryRequirements.size,
            .memoryTypeIndex = MemoryTypeIndex
        };
        
        VulkanCheckGoto(vkAllocateMemory(DeviceHandle, &MemoryAllocateInfo, 0, &Memory), label_Image);
        VulkanCheckGoto(vkBindImageMemory(DeviceHandle, ImageHandle, Memory, 0), label_Memory);

        VkImageViewCreateInfo ViewCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .image = ImageHandle,
            .viewType = ViewType,
            .format = Format,
            .components = { .r = VK_COMPONENT_SWIZZLE_IDENTITY, .g = VK_COMPONENT_SWIZZLE_IDENTITY, .b = VK_COMPONENT_SWIZZLE_IDENTITY, .a = VK_COMPONENT_SWIZZLE_IDENTITY },
            .subresourceRange = {
                .aspectMask = ViewAspect,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        
        VulkanCheckGoto(vkCreateImageView(DeviceHandle, &ViewCreateInfo, 0, &ViewHandle), label_Memory);

        *Image = ImageHandle;
        *ImageMemory = Memory;
        *ImageView = ViewHandle;
    }

    return 0;

label_Memory:
    vkFreeMemory(DeviceHandle, Memory, 0);
    Memory = 0;
label_Image:
    vkDestroyImage(DeviceHandle, ImageHandle, 0);
    ImageHandle = 0;
label_Error:
    return 1;
}

// MARK: Static Buffers
static void VulkanDestroyStaticBuffersAndImages(vulkan_surface_device *Device, vulkan_static_buffers *StaticBuffers, vulkan_image *Images, uint32_t ImageCount) {
    VkDevice DeviceHandle = Device->Handle;

    for(uint32_t I = 0; I < ImageCount; ++I) {
        vkDestroyImage(DeviceHandle, Images[I].Handle, 0);
        vkDestroyImageView(DeviceHandle, Images[I].ViewHandle, 0);
    }
    for(uint32_t I = 0; I < StaticBuffers->MemoryCount; ++I) {
        vkFreeMemory(DeviceHandle, StaticBuffers->MemoryBufs[I], 0);
    }
    vkDestroyBuffer(DeviceHandle, StaticBuffers->IndexHandle, 0);
    vkDestroyBuffer(DeviceHandle, StaticBuffers->VertexHandle, 0);

    memset(StaticBuffers, 0, sizeof(*StaticBuffers));
    memset(Images, 0, sizeof(*Images)*ImageCount);
}

static int VulkanCreateStaticBuffersAndImages(vulkan_surface_device *Device, vulkan_mesh_subbuf *MeshSubbufs, uint32_t MeshSubbufCount, vulkan_image_description *ImageDescriptions, uint32_t ImageCount, VkCommandPool TransferCommandPool, VkQueue TransferQueue, vulkan_static_buffers *StaticBuffers, vulkan_image *Images) {
    VkDevice DeviceHandle = Device->Handle;

    vulkan_static_buffers LocalStaticBuffers = {0};
    vulkan_buffer StagingBuffer = {0};
    uint32_t CreatedImageCount = 0;
    uint32_t CreatedImageViewCount = 0;
    VkCommandBuffer TransferCommandBuffer = 0;
    {
        uint64_t TotalVertexByteCount = 0;
        uint64_t TotalIndexByteCount = 0;
        for(uint32_t I = 0; I < MeshSubbufCount; ++I) {
            TotalVertexByteCount += MeshSubbufs[I].Vertices.ByteCount;
            TotalIndexByteCount += MeshSubbufs[I].Indices.ByteCount;
        }

        // NOTE(blackedout): Create vertex and index buffers using similar create info
        VkBufferCreateInfo BufferCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .size = TotalVertexByteCount,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = 0
        };
        VulkanCheckGoto(vkCreateBuffer(DeviceHandle, &BufferCreateInfo, 0, &LocalStaticBuffers.VertexHandle), label_Error);

        BufferCreateInfo.size = TotalIndexByteCount;
        BufferCreateInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VulkanCheckGoto(vkCreateBuffer(DeviceHandle, &BufferCreateInfo, 0, &LocalStaticBuffers.IndexHandle), label_VertexBuffer);

        // NOTE(blackedout): Allocate and bind uniform memory with joined vertex and index requirements to the respective buffers 
        VkMemoryRequirements VertexMemoryRequirements, IndexMemoryRequirements;
        vkGetBufferMemoryRequirements(DeviceHandle, LocalStaticBuffers.VertexHandle, &VertexMemoryRequirements);
        vkGetBufferMemoryRequirements(DeviceHandle, LocalStaticBuffers.IndexHandle, &IndexMemoryRequirements);

        // TODO(blackedout): Handle non uniform device memory necessity
        uint32_t BufferMemoryTypeIndex;
        CheckGoto(VulkanGetBufferMemoryTypeIndex(Device, VertexMemoryRequirements.memoryTypeBits & IndexMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &BufferMemoryTypeIndex), label_IndexBuffer);
        
        uint64_t VertexByteOffset = 0;
        uint64_t IndexByteOffset = AlignAny(VertexMemoryRequirements.size, uint64_t, IndexMemoryRequirements.alignment);
        uint64_t AlignedTotalBuffersByteCount = IndexByteOffset + IndexMemoryRequirements.size;

        VkMemoryAllocateInfo MemoryAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = 0,
            .allocationSize = AlignedTotalBuffersByteCount,
            .memoryTypeIndex = BufferMemoryTypeIndex
        };
        VulkanCheckGoto(vkAllocateMemory(DeviceHandle, &MemoryAllocateInfo, 0, &LocalStaticBuffers.MemoryBufs[0]), label_IndexBuffer);
        ++LocalStaticBuffers.MemoryCount;

        VulkanCheckGoto(vkBindBufferMemory(DeviceHandle, LocalStaticBuffers.VertexHandle, LocalStaticBuffers.MemoryBufs[0], VertexByteOffset), label_BufferMemory);
        VulkanCheckGoto(vkBindBufferMemory(DeviceHandle, LocalStaticBuffers.IndexHandle, LocalStaticBuffers.MemoryBufs[0], IndexByteOffset), label_BufferMemory);

        // NOTE(blackedout): Create image handles, allocate and bind its memory, then create view handles
        uint64_t AlignedTotalImagesByteCount = 0;
        uint64_t FirstImageAlignment = 0;
        uint32_t ImageMemoryTypeBits = ~(uint32_t)0;
        for(uint32_t I = 0; I < ImageCount; ++I) {
            vulkan_image_description ImageDescription = ImageDescriptions[I];
            VkImageCreateInfo ImageCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .pNext = 0,
                .flags = 0,
                .imageType = ImageDescription.Type,
                .format = ImageDescription.Format,
                .extent = {
                    .width = ImageDescription.Width,
                    .height = ImageDescription.Height,
                    .depth = ImageDescription.Depth
                },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = 0,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
            };
            
            VulkanCheckGoto(vkCreateImage(DeviceHandle, &ImageCreateInfo, 0, &Images[I].Handle), label_Images);
            ++CreatedImageCount;

            VkMemoryRequirements ImageMemoryRequirements;
            vkGetImageMemoryRequirements(DeviceHandle, Images[I].Handle, &ImageMemoryRequirements);

            uint64_t ByteOffset = AlignAny(AlignedTotalImagesByteCount, uint64_t, ImageMemoryRequirements.alignment);
            AlignedTotalImagesByteCount = ByteOffset + ImageMemoryRequirements.size;
            Images[I].Offset = ByteOffset;

            // TODO(blackedout): Handle this getting too restrictive
            ImageMemoryTypeBits &= ImageMemoryRequirements.memoryTypeBits;

            if(I == 0) {
                FirstImageAlignment = ImageMemoryRequirements.alignment;
            }
        }
        uint32_t ImageMemoryTypeIndex;
        CheckGoto(VulkanGetBufferMemoryTypeIndex(Device, ImageMemoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ImageMemoryTypeIndex), label_Images);
        VkMemoryAllocateInfo ImageMemoryAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = 0,
            .allocationSize = AlignedTotalImagesByteCount,
            .memoryTypeIndex = ImageMemoryTypeIndex
        };
        VulkanCheckGoto(vkAllocateMemory(DeviceHandle, &ImageMemoryAllocateInfo, 0, &LocalStaticBuffers.MemoryBufs[1]), label_Images);
        ++LocalStaticBuffers.MemoryCount;
        for(uint32_t I = 0; I < ImageCount; ++I) {
            VulkanCheckGoto(vkBindImageMemory(DeviceHandle, Images[I].Handle, LocalStaticBuffers.MemoryBufs[1], Images[I].Offset), label_Images);
        }

        for(uint32_t I = 0; I < ImageCount; ++I) {
            vulkan_image_description ImageDescription = ImageDescriptions[I];
            VkImageViewCreateInfo ImageViewCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = 0,
                .flags = 0,
                .image = Images[I].Handle,
                .viewType = ImageDescription.ViewType,
                .format = ImageDescription.Format,
                .components = { .r = VK_COMPONENT_SWIZZLE_IDENTITY, .g = VK_COMPONENT_SWIZZLE_IDENTITY, .b = VK_COMPONENT_SWIZZLE_IDENTITY, .a = VK_COMPONENT_SWIZZLE_IDENTITY },
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            };

            VulkanCheckGoto(vkCreateImageView(DeviceHandle, &ImageViewCreateInfo, 0, &Images[I].ViewHandle), label_ImageViews);
            ++CreatedImageViewCount;
        }

        // NOTE(blackedout): Create and fill staging buffer
        // Staging buffer sections are created with alignments of destination buffers, because I'm not sure
        // what alignment rules apply to transfer operations.
        AlignedTotalBuffersByteCount = AlignAny(AlignedTotalBuffersByteCount, uint64_t, FirstImageAlignment);
        CheckGoto(VulkanCreateExclusiveBufferWithMemory(Device, AlignedTotalBuffersByteCount + AlignedTotalImagesByteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &StagingBuffer), label_Images);

        uint8_t *MappedStagingBuffer;
        VulkanCheckGoto(vkMapMemory(DeviceHandle, StagingBuffer.Memory, 0, AlignedTotalBuffersByteCount + AlignedTotalImagesByteCount, 0, (void **)&MappedStagingBuffer), label_StagingBuffer);
        uint64_t VertexOffset = 0, IndexOffset = 0;
        for(uint32_t I = 0; I < MeshSubbufCount; ++I) {
            vulkan_mesh_subbuf Subbuf = MeshSubbufs[I];
            if(Subbuf.Vertices.Source && Subbuf.Vertices.ByteCount > 0) {
                memcpy(MappedStagingBuffer + VertexOffset, Subbuf.Vertices.Source, Subbuf.Vertices.ByteCount);
                *Subbuf.Vertices.OffsetPointer = VertexOffset;
                VertexOffset += Subbuf.Vertices.ByteCount;
            }
            if(Subbuf.Indices.Source && Subbuf.Indices.ByteCount > 0) {
                memcpy(MappedStagingBuffer + IndexByteOffset + IndexOffset, Subbuf.Indices.Source, Subbuf.Indices.ByteCount);
                *Subbuf.Indices.OffsetPointer = IndexOffset;
                IndexOffset += Subbuf.Indices.ByteCount;
            }
        }
        for(uint32_t I = 0; I < ImageCount; ++I) {
            vulkan_image_description ImageDescription = ImageDescriptions[I];
            memcpy(MappedStagingBuffer + AlignedTotalBuffersByteCount + Images[I].Offset, ImageDescription.Source, ImageDescription.ByteCount);
        }
        vkUnmapMemory(DeviceHandle, StagingBuffer.Memory);

        // NOTE(blackedout): Allocate transfer command buffer, record transfer of data, submit and wait for completion
        VkCommandBufferAllocateInfo TransferCommandBufferAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = 0,
            .commandPool = TransferCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VulkanCheckGoto(vkAllocateCommandBuffers(DeviceHandle, &TransferCommandBufferAllocateInfo, &TransferCommandBuffer), label_StagingBuffer);

        VkCommandBufferBeginInfo TransferBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = 0,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = 0
        };
        VulkanCheckGoto(vkBeginCommandBuffer(TransferCommandBuffer, &TransferBeginInfo), label_CommandBuffer);
        VkBufferCopy VertexBufferCopy = {
            .srcOffset = VertexByteOffset,
            .dstOffset = 0,
            .size = TotalVertexByteCount
        };
        VkBufferCopy IndexBufferCopy = {
            .srcOffset = IndexByteOffset,
            .dstOffset = 0,
            .size = TotalIndexByteCount
        };
        vkCmdCopyBuffer(TransferCommandBuffer, StagingBuffer.Handle, LocalStaticBuffers.VertexHandle, 1, &VertexBufferCopy);
        vkCmdCopyBuffer(TransferCommandBuffer, StagingBuffer.Handle, LocalStaticBuffers.IndexHandle, 1, &IndexBufferCopy);

        // TODO(blackedout): Get a better understanding of access synchronization
        // https://www.cg.tuwien.ac.at/courses/EinfCG/slides/VulkanLectureSeries/ECG2021_VK05_PipelinesAndStages.pdf
        // https://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/
        for(uint32_t I = 0; I < ImageCount; ++I) {
            VkImageMemoryBarrier ImageMemoryBarrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = 0,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = Images[I].Handle,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };
            vkCmdPipelineBarrier(TransferCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &ImageMemoryBarrier);
        }
        
        for(uint32_t I = 0; I < ImageCount; ++I) {
            VkBufferImageCopy BufferImageCopy = {
                .bufferOffset = AlignedTotalBuffersByteCount + Images[I].Offset,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .imageOffset = { 0, 0, 0 },
                .imageExtent = { ImageDescriptions[I].Width, ImageDescriptions[I].Height, ImageDescriptions[I].Depth } // TODO
            };
            vkCmdCopyBufferToImage(TransferCommandBuffer, StagingBuffer.Handle, Images[I].Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &BufferImageCopy);
        }
        for(uint32_t I = 0; I < ImageCount; ++I) {
            VkImageMemoryBarrier ImageMemoryBarrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = 0,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = Images[I].Handle,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            };
            vkCmdPipelineBarrier(TransferCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1, &ImageMemoryBarrier);
        }
        VulkanCheckGoto(vkEndCommandBuffer(TransferCommandBuffer), label_CommandBuffer);

        VkSubmitInfo TransferSubmitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = 0,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = 0,
            .pWaitDstStageMask = 0,
            .commandBufferCount = 1,
            .pCommandBuffers = &TransferCommandBuffer,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = 0
        };

        VulkanCheckGoto(vkQueueSubmit(TransferQueue, 1, &TransferSubmitInfo, VULKAN_NULL_HANDLE), label_CommandBuffer);
        VulkanCheckGoto(vkQueueWaitIdle(TransferQueue), label_CommandBuffer);

        *StaticBuffers = LocalStaticBuffers;

        vkFreeCommandBuffers(DeviceHandle, TransferCommandPool, 1, &TransferCommandBuffer);
        TransferCommandBuffer = 0;
        VulkanDestroyBuffer(Device, &StagingBuffer);
    }

    return 0;

label_CommandBuffer:
    vkFreeCommandBuffers(DeviceHandle, TransferCommandPool, 1, &TransferCommandBuffer);
    TransferCommandBuffer = 0;
label_StagingBuffer:
    VulkanDestroyBuffer(Device, &StagingBuffer);
label_ImageViews:
    for(uint32_t I = 0; I < CreatedImageViewCount; ++I) {
        vkDestroyImageView(DeviceHandle, Images[I].ViewHandle, 0);
        Images[I].ViewHandle = 0;
    }
label_Images:
    for(uint32_t I = 0; I < CreatedImageCount; ++I) {
        vkDestroyImage(DeviceHandle, Images[I].Handle, 0);
        Images[I].Handle = 0;
    }
label_BufferMemory:
    for(uint32_t I = 0; I < LocalStaticBuffers.MemoryCount; ++I) {
        vkFreeMemory(DeviceHandle, LocalStaticBuffers.MemoryBufs[I], 0);
        LocalStaticBuffers.MemoryBufs[I] = 0;
    }
label_IndexBuffer:
    vkDestroyBuffer(DeviceHandle, LocalStaticBuffers.IndexHandle, 0);
    LocalStaticBuffers.IndexHandle = 0;
label_VertexBuffer:
    vkDestroyBuffer(DeviceHandle, LocalStaticBuffers.VertexHandle, 0);
    LocalStaticBuffers.VertexHandle = 0;
label_Error:
    return 1;
}

// MARK: Shaders
static int VulkanCreateShaderModule(vulkan_surface_device *Device, const uint8_t *Bytes, uint64_t ByteCount, VkShaderModule *Module) {
    VkDevice DeviceHandle = Device->Handle;

    VkShaderModule LocalModule = 0;
    {
        AssertMessageGoto(ByteCount < (uint64_t)SIZE_T_MAX, label_Error, "Too many bytes in shader code.\n");
        VkShaderModuleCreateInfo CreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .codeSize = (size_t)ByteCount,
            .pCode = (uint32_t *)Bytes
        };
        
        VulkanCheckGoto(vkCreateShaderModule(DeviceHandle, &CreateInfo, 0, &LocalModule), label_Error);
        *Module = LocalModule;
    }

    return 0;

label_Error:
    return 1;
}

typedef struct {
    VkDescriptorSetLayoutCreateFlags Flags;
    VkDescriptorSetLayoutBinding *Bindings;
    uint32_t BindingsCount;
} vulkan_descriptor_set_layout_description;

static void VulkanDestroyDescriptorSetLayouts(vulkan_surface_device *Device, VkDescriptorSetLayout *DescriptorSetLayouts, uint32_t Count) {
    VkDevice DeviceHandle = Device->Handle;
    for(uint32_t I = 0; I < Count; ++I) {
        vkDestroyDescriptorSetLayout(DeviceHandle, DescriptorSetLayouts[I], 0);
        DescriptorSetLayouts[I] = 0;
    }
}

static int VulkanCreateDescriptorSetLayouts(vulkan_surface_device *Device, vulkan_descriptor_set_layout_description *Descriptions, uint32_t Count, VkDescriptorSetLayout *DescriptorSetLayouts) {
    VkDevice DeviceHandle = Device->Handle;

    uint32_t CreatedCount = 0;
    {
        VkDescriptorSetLayoutCreateInfo DescriptorSetLayoutCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .bindingCount = 0,
            .pBindings = 0
        };
        
        for(; CreatedCount < Count; ++CreatedCount) {
            vulkan_descriptor_set_layout_description Description = Descriptions[CreatedCount];
            DescriptorSetLayoutCreateInfo.flags = Description.Flags;
            DescriptorSetLayoutCreateInfo.bindingCount = Description.BindingsCount;
            DescriptorSetLayoutCreateInfo.pBindings = Description.Bindings;
            
            VulkanCheckGoto(vkCreateDescriptorSetLayout(DeviceHandle, &DescriptorSetLayoutCreateInfo, 0, DescriptorSetLayouts + CreatedCount), label_Error);
        }
    }

    return 0;

label_Error:
    VulkanDestroyDescriptorSetLayouts(Device, DescriptorSetLayouts, CreatedCount);
    return 1;
}

typedef struct {
    VkBuffer *Buffers;
    void **MappedBuffers;
    VkDescriptorSet *DescriptorSets;
    uint32_t BindingIndex;
    VkDeviceSize Size;
} vulkan_shader_uniform_buffers_description;

static int VulkanCreateShaderUniformBuffers(vulkan_surface_device *Device, VkDescriptorSetLayout DescriptorSetLayout, vulkan_shader_uniform_buffers_description *Descriptions, uint32_t Count, VkDeviceMemory *BufferMemory, VkDescriptorPool *DescriptorPool) {
    VkDevice DeviceHandle = Device->Handle;

    VkDeviceMemory Memory = 0;
    VkDescriptorPool LocalDescriptorPool = 0;

    uint32_t CreatedBufferCount = 0;
    {
        VkBufferCreateInfo BufferCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .size = 0,
            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = 0
        };
        
        VkDeviceSize TotalByteCount = 0;
        uint32_t CommonMemoryTypeBits = ~(uint32_t)0; // TODO(blackedout): Do multiple allocations if needed.
        for(uint32_t I = 0; I < Count; ++I) {
            vulkan_shader_uniform_buffers_description Description = Descriptions[I];
            BufferCreateInfo.size = Description.Size;
            
            for(uint32_t J = 0; J < MAX_ACQUIRED_IMAGE_COUNT; ++J) {
                VkBuffer Buffer;
                VulkanCheckGoto(vkCreateBuffer(DeviceHandle, &BufferCreateInfo, 0, &Buffer), label_Buffers);
                Description.Buffers[J] = Buffer;
                ++CreatedBufferCount;
            }

            VkMemoryRequirements MemoryRequirements;
            vkGetBufferMemoryRequirements(DeviceHandle, Description.Buffers[0], &MemoryRequirements);

            CommonMemoryTypeBits &= MemoryRequirements.memoryTypeBits;

            for(uint32_t J = 0; J < MAX_ACQUIRED_IMAGE_COUNT; ++J) {
                uint64_t UniformBufferOffset = AlignAny(TotalByteCount, uint64_t, MemoryRequirements.alignment);
                TotalByteCount = UniformBufferOffset + MemoryRequirements.size;

                // NOTE(blackedout): First, just store offset in pointer. Later, add base address
                Description.MappedBuffers[J] = (void *)UniformBufferOffset;
            }
        }

        uint32_t MemoryTypeIndex;
        CheckGoto(VulkanGetBufferMemoryTypeIndex(Device, CommonMemoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &MemoryTypeIndex), label_Buffers);

        VkMemoryAllocateInfo MemoryAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = 0,
            .allocationSize = TotalByteCount,
            .memoryTypeIndex = MemoryTypeIndex
        };
        
        VulkanCheckGoto(vkAllocateMemory(DeviceHandle, &MemoryAllocateInfo, 0, &Memory), label_Buffers);

        // TODO(blackedout): Map whole buffer or individual chunks?
        uint8_t *MappedMemory;
        VulkanCheckGoto(vkMapMemory(DeviceHandle, Memory, 0, TotalByteCount, 0, (void **)&MappedMemory), label_Memory);

        for(uint32_t I = 0; I < Count; ++I) {
            for(uint32_t J = 0; J < MAX_ACQUIRED_IMAGE_COUNT; ++J) {
                vulkan_shader_uniform_buffers_description Description = Descriptions[I];

                uint64_t Offset = (uint64_t)Description.MappedBuffers[J];
                VulkanCheckGoto(vkBindBufferMemory(DeviceHandle, Description.Buffers[J], Memory, Offset), label_Memory);
                Description.MappedBuffers[J] = (void *)(MappedMemory + Offset);
            }
        }

        // NOTE(blackedout): Create uniform descriptor pool and sets
        uint32_t TotalBufferCount = Count*MAX_ACQUIRED_IMAGE_COUNT;
        VkDescriptorPoolSize DescriptorPoolSizes[] = {
            {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                // NOTE(blackedout): This seems to be the total number per pool, not per set
                .descriptorCount = TotalBufferCount
            },
        };

        VkDescriptorPoolCreateInfo DescriptorPoolCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .maxSets = TotalBufferCount,
            .poolSizeCount = ArrayCount(DescriptorPoolSizes),
            .pPoolSizes = DescriptorPoolSizes
        };
        
        VulkanCheckGoto(vkCreateDescriptorPool(DeviceHandle, &DescriptorPoolCreateInfo, 0, &LocalDescriptorPool), label_Memory);    
        
        VkDescriptorSetLayout DescriptorSetLayouts[MAX_ACQUIRED_IMAGE_COUNT];
        for(uint32_t J = 0; J < ArrayCount(DescriptorSetLayouts); ++J) {
            DescriptorSetLayouts[J] = DescriptorSetLayout;
        }
        for(uint32_t I = 0; I < Count; ++I) {
            vulkan_shader_uniform_buffers_description Description = Descriptions[I];

            VkDescriptorSetAllocateInfo DescriptorSetAllocateInfo = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .pNext = 0,
                .descriptorPool = LocalDescriptorPool,
                .descriptorSetCount = ArrayCount(DescriptorSetLayouts),
                .pSetLayouts = DescriptorSetLayouts
            };

            // NOTE(blackedout): The length of Description.DescriptorSets must be ArrayCount(DescriptorSetLayouts)
            VulkanCheckGoto(vkAllocateDescriptorSets(DeviceHandle, &DescriptorSetAllocateInfo, Description.DescriptorSets), label_DescriptorPool);

            VkDescriptorBufferInfo BufferInfos[ArrayCount(DescriptorSetLayouts)];
            VkWriteDescriptorSet WriteDescriptorSets[ArrayCount(DescriptorSetLayouts)];
            for(uint32_t J = 0; J < ArrayCount(DescriptorSetLayouts); ++J) {
                VkDescriptorBufferInfo BufferInfo = {
                    .buffer = Description.Buffers[J],
                    .offset = 0,
                    .range = Description.Size
                };

                VkWriteDescriptorSet WriteDescriptorSet = {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .pNext = 0,
                    .dstSet = Description.DescriptorSets[J],
                    .dstBinding = Description.BindingIndex, // NOTE(blackedout): Uniform binding index (?)
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pImageInfo = 0,
                    .pBufferInfo = BufferInfos + J,
                    .pTexelBufferView = 0
                };

                BufferInfos[J] = BufferInfo;
                WriteDescriptorSets[J] = WriteDescriptorSet;
            }
            vkUpdateDescriptorSets(DeviceHandle, ArrayCount(WriteDescriptorSets), WriteDescriptorSets, 0, 0);
        }

        *BufferMemory = Memory;
        *DescriptorPool = LocalDescriptorPool;
    }
    
    return 0;

label_DescriptorPool:
    vkDestroyDescriptorPool(DeviceHandle, LocalDescriptorPool, 0);
    LocalDescriptorPool = 0;
label_Memory:
    vkFreeMemory(DeviceHandle, Memory, 0);
    Memory = 0;
label_Buffers:
    for(uint32_t I = 0; I < CreatedBufferCount; ++I) {
        vulkan_shader_uniform_buffers_description Description = Descriptions[I / MAX_ACQUIRED_IMAGE_COUNT];
        uint32_t BufferIndex = (I % MAX_ACQUIRED_IMAGE_COUNT);

        vkDestroyBuffer(DeviceHandle, Description.Buffers[BufferIndex], 0);
        Description.Buffers[BufferIndex] = 0;
        Description.MappedBuffers[BufferIndex] = 0;
    }
    return 1;
}

// MARK: Instace
static int VulkanCreateInstance(const char **PlatformRequiredInstanceExtensions, uint32_t PlatformRequiredInstanceExtensionCount, VkInstance *Instance) {
    {
        const char *InstanceExtensions[16];
        // NOTE(blackedout): Leave room for additional 1 extensions (portability extension)
        AssertMessageGoto(PlatformRequiredInstanceExtensionCount <= 15, label_Error,
                        "Too many required instance extensions (%d). Increase array buffer size to fix.", PlatformRequiredInstanceExtensionCount);
        
        uint32_t InstanceExtensionCount;
        for(InstanceExtensionCount = 0; InstanceExtensionCount < PlatformRequiredInstanceExtensionCount; ++InstanceExtensionCount) {
            InstanceExtensions[InstanceExtensionCount] = PlatformRequiredInstanceExtensions[InstanceExtensionCount];
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

        uint32_t InstanceLayerPropertyCount;
        VulkanCheckGoto(vkEnumerateInstanceLayerProperties(&InstanceLayerPropertyCount, 0), label_Error);

        VkLayerProperties InstanceLayerProperties[16];
        AssertMessageGoto(InstanceLayerPropertyCount <= ArrayCount(InstanceLayerProperties), label_Error,
                        "Too many available instance layers (%d). Increase buffer size to fix.", InstanceLayerPropertyCount);
        InstanceLayerPropertyCount = ArrayCount(InstanceLayerProperties);
        VulkanCheckGoto(vkEnumerateInstanceLayerProperties(&InstanceLayerPropertyCount, InstanceLayerProperties), label_Error);

        // TODO(blackedout): Create custom error callback functions?

        const char *AvaiableRequestedValidationLayers[ArrayCount(VULKAN_REQUESTED_INSTANCE_LAYERS)];
        uint32_t AvaiableRequestedValidationLayerCount = 0;
        for(uint32_t I = 0; I < ArrayCount(VULKAN_REQUESTED_INSTANCE_LAYERS); ++I) {
            const char *Layer = VULKAN_REQUESTED_INSTANCE_LAYERS[I];

            int Found = 0;
            for(uint32_t J = 0; J < InstanceLayerPropertyCount; ++J) {
                if(strcmp(Layer, InstanceLayerProperties[J].layerName) == 0) {
                    AvaiableRequestedValidationLayers[AvaiableRequestedValidationLayerCount++] = Layer;
                    Found = 1;
                    break;
                }
            }
            if(Found == 0) {
                printfc(CODE_YELLOW, "Validation layer %s is not available.\n", Layer);
            }
        }

        VkInstanceCreateInfo InstanceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .pApplicationInfo = &VulkanApplicationInfo,
            .enabledLayerCount = AvaiableRequestedValidationLayerCount,
            .ppEnabledLayerNames = AvaiableRequestedValidationLayers,
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
        printf("Vulkan instance extensions (%d):\n", InstanceExtensionCount);
        for(uint32_t I = 0; I < InstanceExtensionCount; ++I) {
            printf("[%d] %s%s\n", I, InstanceExtensions[I], (I < PlatformRequiredInstanceExtensionCount)? " (platform)" : "");
        }

        printf("Vulkan available instance layers (%d):\n", InstanceLayerPropertyCount);
        for(uint32_t I = 0; I < InstanceLayerPropertyCount; ++I) {
            int Found = 0;
            for(uint32_t J = 0; J < AvaiableRequestedValidationLayerCount; ++J) {
                if(strcmp(InstanceLayerProperties[I].layerName, AvaiableRequestedValidationLayers[J]) == 0) {
                    Found = 1;
                    break;
                }
            }
            printf("[%d] %s%s\n", I, InstanceLayerProperties[I].layerName, Found? " (requested)" : "");
        }
#endif
        
        VulkanCheckGoto(CreateInstanceResult, label_Error);
    }

    return 0;

label_Error:
    return 1;
}

// MARK: Surface Device
static void VulkanDestroySurfaceDevice(VkInstance Instance, vulkan_surface_device *Device) {
    vkDestroySurfaceKHR(Instance, Device->Surface, 0);
    vkDestroyDevice(Device->Handle, 0);
}

static int VulkanCreateSurfaceDevice(VkInstance Instance, VkSurfaceKHR Surface, vulkan_surface_device *Device) {
    // NOTE(blackedout): This function will destroy the input surface on failure.
    // Returns a device whose physical device has at least one graphics queue, at least one surface presentation queue and supports the surface extension.
    // The physical device is picked by scoring its type, available surface formats and present modes.

    {    
        VkPhysicalDevice PhysicalDevices[16];
        uint32_t PhysicalDeviceScores[ArrayCount(PhysicalDevices)];
        uint32_t PhysicalDeviceCount = ArrayCount(PhysicalDevices);
        VulkanCheckGotoIncompleteWarn(vkEnumeratePhysicalDevices(Instance, &PhysicalDeviceCount, PhysicalDevices), label_Error);
        AssertMessageGoto(PhysicalDeviceCount > 0, label_Error, "Vulkan says there are no GPUs.\n");

        uint32_t BestPhysicalDeviceGraphicsQueueIndex;
        uint32_t BestPhysicalDeviceSurfaceQueueIndex;
        int BestPhysicalDeviceHasPortabilitySubsetExtension;
        VkSurfaceFormatKHR BestPhysicalDeviceInitialSurfaceFormat;
        VkPhysicalDeviceProperties BestPhysicalDeviceProperties;
        VkPhysicalDeviceFeatures BestPhysicalDeviceFeatures;
        VkFormat BestPhysicalDeviceDepthFormat;

        uint32_t BestPhysicalDeviceScore = 0;
        VkPhysicalDevice BestPhysicalDevice = PhysicalDevices[0];
        for(uint32_t I = 0; I < PhysicalDeviceCount; ++I) {
            VkPhysicalDevice PhysicalDevice = PhysicalDevices[I];

            VkPhysicalDeviceFeatures Features;
            vkGetPhysicalDeviceFeatures(PhysicalDevice, &Features);
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

            int HasBestDepthFormat = 0;
            VkFormat BestDepthFormat;
            VkFormat DepthFormats[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
            for(uint32_t J = 0; J < ArrayCount(DepthFormats); ++J) {
                VkFormatProperties FormatProperties;
                vkGetPhysicalDeviceFormatProperties(PhysicalDevice, DepthFormats[I], &FormatProperties);

                if(FormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                    BestDepthFormat = DepthFormats[I];
                    HasBestDepthFormat = 1;
                    break;
                }
            }
            IsUsable = IsUsable && HasBestDepthFormat;

            uint32_t FeatureScore = 0;
            if(Features.samplerAnisotropy) {
                ++FeatureScore;
            }

            uint32_t Score = 0;
            if(IsUsable) {
                // NOTE(blackedout): As long as they are usable, just pick the best once (hence the added 1)
                Score = 1 + 3*DeviceTypeScore + BestSurfaceFormatScore + BestPresentModeScore + FeatureScore;
                if(Score > BestPhysicalDeviceScore) {
                    BestPhysicalDeviceScore = Score;
                    BestPhysicalDevice = PhysicalDevice;

                    BestPhysicalDeviceGraphicsQueueIndex = UsableQueueGraphicsIndex;
                    BestPhysicalDeviceSurfaceQueueIndex = UsableQueueSurfaceIndex;
                    BestPhysicalDeviceHasPortabilitySubsetExtension = HasPortabilitySubsetExtension;
                    BestPhysicalDeviceInitialSurfaceFormat = BestSurfaceFormat;

                    BestPhysicalDeviceProperties = Props;
                    BestPhysicalDeviceFeatures = Features;
                    BestPhysicalDeviceDepthFormat = BestDepthFormat;
                }
            }
            PhysicalDeviceScores[I] = Score;
        }

    #ifdef VULKAN_INFO_PRINT
        printf("Vulkan physical devices (%d):\n", PhysicalDeviceCount);
        for(uint32_t I = 0; I < PhysicalDeviceCount; ++I) {
            VkPhysicalDeviceProperties Props;
            vkGetPhysicalDeviceProperties(PhysicalDevices[I], &Props);
            printf("[%d] (%d) %s (%s)\n", I, PhysicalDeviceScores[I], Props.deviceName, string_VkPhysicalDeviceType(Props.deviceType));
            //printf("\t[%d] Queue (%d) %s %s\n", J, QueueFamilyProps.queueCount, IsGraphics? "VK_QUEUE_GRAPHICS_BIT" : "", IsSurfaceSupported? "SURFACE" : "");
            //printf("\tSurface image count range: %d to %d\n", SurfaceCapabilities.minImageCount, SurfaceCapabilities.maxImageCount);
            //printf("\tSurface image extents: (%d, %d) to (%d, %d)\n", SurfaceCapabilities.minImageExtent.width, SurfaceCapabilities.minImageExtent.height, SurfaceCapabilities.maxImageExtent.width, SurfaceCapabilities.maxImageExtent.height);
        }
    #endif

        AssertMessageGoto(BestPhysicalDeviceScore > 0, label_Error, "No usable physical device found.\n");

        float DeviceQueuePriorities[] = { 1.0f };
        VkDeviceQueueCreateInfo DeviceQueueCreateInfos[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .pNext = 0,
                .flags = 0,
                .queueFamilyIndex = BestPhysicalDeviceGraphicsQueueIndex,
                .queueCount = 1,
                .pQueuePriorities = DeviceQueuePriorities
            },
        };

        uint32_t DeviceQueueCreateInfoCount = 1;
        if(BestPhysicalDeviceGraphicsQueueIndex != BestPhysicalDeviceSurfaceQueueIndex) {
            DeviceQueueCreateInfos[1] = DeviceQueueCreateInfos[0];
            DeviceQueueCreateInfos[1].queueFamilyIndex = BestPhysicalDeviceSurfaceQueueIndex;
            DeviceQueueCreateInfoCount = 2;
        }

        const char *ExtensionNames[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            "VK_KHR_portability_subset"
        };

        uint32_t ExtensionNameCount = ArrayCount(ExtensionNames);
        if(BestPhysicalDeviceHasPortabilitySubsetExtension == 0) {
            ExtensionNameCount -= 1;
        }

        // NOTE(blackedout): Disable all features by default first, then enable using supported features
        VkPhysicalDeviceFeatures PhysicalDeviceFeatures = {0};
        PhysicalDeviceFeatures.samplerAnisotropy = BestPhysicalDeviceFeatures.samplerAnisotropy;

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
            .pEnabledFeatures = &PhysicalDeviceFeatures,
        };

        VkSurfaceCapabilitiesKHR BestPhysicalDeviceSurfaceCapabilities;
        VulkanCheckGoto(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(BestPhysicalDevice, Surface, &BestPhysicalDeviceSurfaceCapabilities), label_Error);

        VkDevice DeviceHandle;
        VulkanCheckGoto(vkCreateDevice(BestPhysicalDevice, &DeviceCreateInfo, 0, &DeviceHandle), label_Error);

        VkSampleCountFlags PossibleSampleCountFlags = BestPhysicalDeviceProperties.limits.sampledImageColorSampleCounts & BestPhysicalDeviceProperties.limits.sampledImageDepthSampleCounts;
        VkSampleCountFlagBits MaxPhysicalDeviceSampleCount = VK_SAMPLE_COUNT_1_BIT;
        for(VkSampleCountFlagBits SampleCount = VK_SAMPLE_COUNT_64_BIT; SampleCount > 0; SampleCount = (VkSampleCountFlagBits)(SampleCount >> 1)) {
            if(SampleCount & PossibleSampleCountFlags) {
                MaxPhysicalDeviceSampleCount = SampleCount;
                break;
            }
        }

        vulkan_surface_device LocalSurfaceDevice = {
            .Handle = DeviceHandle,
            .Surface = Surface,
            .PhysicalDevice = BestPhysicalDevice,
            
            .GraphicsQueueFamilyIndex = BestPhysicalDeviceGraphicsQueueIndex,
            .PresentQueueFamilyIndex = BestPhysicalDeviceSurfaceQueueIndex,

            .InitialExtent = BestPhysicalDeviceSurfaceCapabilities.currentExtent,
            .InitialSurfaceFormat = BestPhysicalDeviceInitialSurfaceFormat,

            .Features = BestPhysicalDeviceFeatures,
            .Properties = BestPhysicalDeviceProperties,

            .BestDepthFormat = BestPhysicalDeviceDepthFormat,
            .MaxSampleCount = MaxPhysicalDeviceSampleCount
        };

        *Device = LocalSurfaceDevice;
    }

    return 0;

label_Error:
    vkDestroySurfaceKHR(Instance, Surface, 0);
    Surface = 0;
    return 1;
}

// MARK: Swapchain
static void VulkanDestroySwapchain(vulkan_surface_device *Device, vulkan_swapchain *Swapchain) {
    VkDevice DeviceHandle = Device->Handle;

    // NOTE(blackedout): Only destructible if none of its images are acquired.
    AssertMessage(Swapchain->AcquiredImageCount == 0, "Swapchain can't be destroyed because at least one of its imagess is still in use.\n");

    VulkanDestroyImageWidthMemoryAndView(Device, &Swapchain->MultiSampleColorImage, &Swapchain->MultiSampleColorImageMemory, &Swapchain->MultiSampleColorImageView);
    VulkanDestroyImageWidthMemoryAndView(Device, &Swapchain->DepthImage, &Swapchain->DepthImageMemory, &Swapchain->DepthImageView);

    for(uint32_t I = 0; I < Swapchain->ImageCount; ++I) {
        vkDestroyFramebuffer(DeviceHandle, Swapchain->Framebuffers[I], 0);
        vkDestroyImageView(DeviceHandle, Swapchain->ImageViews[I], 0);
    }

    vkDestroySwapchainKHR(DeviceHandle, Swapchain->Handle, 0);

    free(Swapchain->ImageBuf);
    memset(Swapchain, 0, sizeof(*Swapchain));
}

static int VulkanCreateSwapchain(vulkan_surface_device *Device, VkExtent2D Extent, VkSampleCountFlagBits SampleCount, VkRenderPass RenderPass, vulkan_swapchain *OldSwapchain, vulkan_swapchain *Swapchain) {
    VkDevice DeviceHandle = Device->Handle;
    VkSurfaceKHR DeviceSurface = Device->Surface;
    VkSwapchainKHR OldSwapchainHandle = VULKAN_NULL_HANDLE;
    if(OldSwapchain) {
        OldSwapchainHandle = OldSwapchain->Handle;
    }

    vulkan_swapchain LocalSwapchain = {0};

    uint32_t CreatedImageViewCount = 0;
    uint32_t CreatedFramebufferCount = 0;
    {
        VkSurfaceCapabilitiesKHR SurfaceCapabilities;
        VkSurfaceFormatKHR SurfaceFormat;
        VkPresentModeKHR PresentMode;
        {
            VkPhysicalDevice PhysicalDeviceHandle = Device->PhysicalDevice;
            VulkanCheckGoto(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysicalDeviceHandle, DeviceSurface, &SurfaceCapabilities), label_Error);
            CheckGoto(VulkanPickSurfaceFormat(PhysicalDeviceHandle, DeviceSurface, &SurfaceFormat, 0), label_Error);
            CheckGoto(VulkanPickSurfacePresentMode(PhysicalDeviceHandle, DeviceSurface, &PresentMode, 0), label_Error);
            LocalSwapchain.Format = SurfaceFormat.format;
        }

        uint32_t MinImageCount = Max(2, SurfaceCapabilities.minImageCount);
        if(SurfaceCapabilities.maxImageCount > 0) {
            MinImageCount = Min(MinImageCount, SurfaceCapabilities.maxImageCount);
        }
        VkExtent2D ClampedImageExtent = {
            .width = Clamp(Extent.width, SurfaceCapabilities.minImageExtent.width, SurfaceCapabilities.maxImageExtent.width),
            .height = Clamp(Extent.height, SurfaceCapabilities.minImageExtent.height, SurfaceCapabilities.maxImageExtent.height)
        };
        LocalSwapchain.ImageExtent = ClampedImageExtent;

        uint32_t QueueFamilyIndices[] = { Device->GraphicsQueueFamilyIndex, Device->PresentQueueFamilyIndex };
        VkSharingMode QueueFamilySharingMode = VK_SHARING_MODE_EXCLUSIVE;
        uint32_t QueueFamilyIndexCount = 0;
        uint32_t *QueueFamilyIndicesOptional = 0;
        if(Device->GraphicsQueueFamilyIndex != Device->PresentQueueFamilyIndex) {
            QueueFamilySharingMode = VK_SHARING_MODE_CONCURRENT;
            QueueFamilyIndexCount = 2;
            QueueFamilyIndicesOptional = QueueFamilyIndices;
        }

        VkSwapchainCreateInfoKHR SwapchainCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = 0,
            .flags = 0,
            .surface = DeviceSurface,
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

        VulkanCheckGoto(vkCreateSwapchainKHR(DeviceHandle, &SwapchainCreateInfo, 0, &LocalSwapchain.Handle), label_Error);

        VulkanCheckGoto(vkGetSwapchainImagesKHR(DeviceHandle, LocalSwapchain.Handle, &LocalSwapchain.ImageCount, 0), label_Swapchain);

        {
            malloc_multiple_subbuf SwapchainSubbufs[] = {
                { &LocalSwapchain.Images, LocalSwapchain.ImageCount*sizeof(VkImage) },
                { &LocalSwapchain.ImageViews, LocalSwapchain.ImageCount*sizeof(VkImageView) },
                { &LocalSwapchain.Framebuffers, LocalSwapchain.ImageCount*sizeof(VkFramebuffer) }
            };
            CheckGoto(MallocMultiple(ArrayCount(SwapchainSubbufs), SwapchainSubbufs, &LocalSwapchain.ImageBuf), label_Swapchain);
        }
        VulkanCheckGoto(vkGetSwapchainImagesKHR(DeviceHandle, LocalSwapchain.Handle, &LocalSwapchain.ImageCount, LocalSwapchain.Images), label_ImageBuf);

        for(; CreatedImageViewCount < LocalSwapchain.ImageCount; ++CreatedImageViewCount) {
            VkImageViewCreateInfo ImageViewCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = 0,
                .flags = 0,
                .image = LocalSwapchain.Images[CreatedImageViewCount],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = SurfaceFormat.format,
                .components = { .r = VK_COMPONENT_SWIZZLE_IDENTITY, .g = VK_COMPONENT_SWIZZLE_IDENTITY, .b = VK_COMPONENT_SWIZZLE_IDENTITY, .a = VK_COMPONENT_SWIZZLE_IDENTITY },
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            VulkanCheckGoto(vkCreateImageView(DeviceHandle, &ImageViewCreateInfo, 0, LocalSwapchain.ImageViews + CreatedImageViewCount), label_ImageViews);
        }

        VkSampleCountFlagBits UsedSampleCount = SampleCount;
        CheckGoto(VulkanCreateExclusiveImageWithMemoryAndView(Device, VK_IMAGE_TYPE_2D, Device->BestDepthFormat, ClampedImageExtent.width, ClampedImageExtent.height, 1,
                                                            UsedSampleCount, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT,
                                                            &LocalSwapchain.DepthImage, &LocalSwapchain.DepthImageMemory, &LocalSwapchain.DepthImageView), label_ImageViews);
        CheckGoto(VulkanCreateExclusiveImageWithMemoryAndView(Device, VK_IMAGE_TYPE_2D, SurfaceFormat.format, ClampedImageExtent.width, ClampedImageExtent.height, 1,
                                                            UsedSampleCount, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT,
                                                            &LocalSwapchain.MultiSampleColorImage, &LocalSwapchain.MultiSampleColorImageMemory, &LocalSwapchain.MultiSampleColorImageView), label_DepthImage);

        
        for(; CreatedFramebufferCount < LocalSwapchain.ImageCount; ++CreatedFramebufferCount) {
            VkImageView FramebufferAttachments[] = { LocalSwapchain.MultiSampleColorImageView, LocalSwapchain.DepthImageView, LocalSwapchain.ImageViews[CreatedFramebufferCount] };
            VkFramebufferCreateInfo FramebufferCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .pNext = 0,
                .flags = 0,
                .renderPass = RenderPass,
                .attachmentCount = ArrayCount(FramebufferAttachments),
                .pAttachments = FramebufferAttachments,
                .width = ClampedImageExtent.width,
                .height = ClampedImageExtent.height,
                .layers = 1
            };

            VulkanCheckGoto(vkCreateFramebuffer(DeviceHandle, &FramebufferCreateInfo, 0, LocalSwapchain.Framebuffers + CreatedFramebufferCount), label_Framebuffers);
        }

        

        *Swapchain = LocalSwapchain;
    }

    return 0;

label_Framebuffers:
    for(uint32_t I = 0; I < CreatedFramebufferCount; ++I) {
        vkDestroyFramebuffer(DeviceHandle, LocalSwapchain.Framebuffers[I], 0);
        LocalSwapchain.Framebuffers[I] = 0;
    }
    VulkanDestroyImageWidthMemoryAndView(Device, &LocalSwapchain.MultiSampleColorImage, &LocalSwapchain.MultiSampleColorImageMemory, &LocalSwapchain.MultiSampleColorImageView);
label_DepthImage:
    VulkanDestroyImageWidthMemoryAndView(Device, &LocalSwapchain.DepthImage, &LocalSwapchain.DepthImageMemory, &LocalSwapchain.DepthImageView);
label_ImageViews:
    for(uint32_t I = 0; I < CreatedImageViewCount; ++I) {
        vkDestroyImageView(DeviceHandle, LocalSwapchain.ImageViews[I], 0);
        LocalSwapchain.ImageViews[I] = 0;
    }
label_ImageBuf:
    free(LocalSwapchain.ImageBuf);
    LocalSwapchain.ImageBuf = 0;
label_Swapchain:
    vkDestroySwapchainKHR(DeviceHandle, LocalSwapchain.Handle, 0);
    LocalSwapchain.Handle = 0;
label_Error:
    return 1;
}

// MARK: Swapch. Handler
static void VulkanDestroySwapchainHandler(vulkan_surface_device *Device, vulkan_swapchain_handler *SwapchainHandler) {
    VkDevice DeviceHandle = Device->Handle;
    vulkan_swapchain_handler Handler = *SwapchainHandler;
    for(uint32_t I = 0; I < Handler.SwapchainBufIndices.Count; ++I) {
        uint32_t CircularIndex = IndicesCircularGet(&Handler.SwapchainBufIndices, I);
        VulkanDestroySwapchain(Device, Handler.Swapchains + CircularIndex);
    }

    for(uint32_t I = 0; I < MAX_ACQUIRED_IMAGE_COUNT; ++I) {
        vkDestroySemaphore(DeviceHandle, SwapchainHandler->ImageAvailableSemaphores[I], 0);
        vkDestroySemaphore(DeviceHandle, SwapchainHandler->RenderFinishedSemaphores[I], 0);
        vkDestroyFence(DeviceHandle, SwapchainHandler->InFlightFences[I], 0);
    }

    memset(SwapchainHandler, 0, sizeof(*SwapchainHandler));
}

static int VulkanCreateSwapchainAndHandler(vulkan_surface_device *Device, VkExtent2D InitialExtent, VkSampleCountFlagBits SampleCount, VkRenderPass RenderPass, vulkan_swapchain_handler *SwapchainHandler) {
    VkDevice DeviceHandle = Device->Handle;

    vulkan_swapchain_handler Handler = {
        .RenderPassCount = 1,
        .RenderPass = RenderPass,
        .SampleCount = SampleCount,

        .SwapchainIndexLastAcquired = UINT32_MAX,
        .SwapchainBufIndices = {
            .Cap = MAX_SWAPCHAIN_COUNT,
            .Count = 1,
            .Next = 1%MAX_SWAPCHAIN_COUNT
        },
        .Swapchains = {0},

        .AcquiredImageDataIndices = {
            .Cap = MAX_ACQUIRED_IMAGE_COUNT,
            .Count = 0,
            .Next = 0
        },
        .AcquiredSwapchainImageIndices = {0},
        .AcquiredSwapchainIndices = {0},
    };

    uint32_t CreatedInFlightFenceCount = 0;
    uint32_t CreatedImageAvailableSemaphoreCount = 0;
    uint32_t CreatedRenderFinishedSemaphoreCount = 0;
    {
        CheckGoto(VulkanCreateSwapchain(Device, InitialExtent, SampleCount, RenderPass, 0, &Handler.Swapchains[0]), label_Error);

        // NOTE(blackedout): Both of these are unsignaled.
        VkSemaphoreCreateInfo SemaphoreCreateInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = 0, .flags = 0 };
        VkFenceCreateInfo FenceCreateInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = 0, .flags = 0 };
        
        for(; CreatedImageAvailableSemaphoreCount < ArrayCount(Handler.ImageAvailableSemaphores); ++CreatedImageAvailableSemaphoreCount) {
            VulkanCheckGoto(vkCreateSemaphore(DeviceHandle, &SemaphoreCreateInfo, 0, Handler.ImageAvailableSemaphores + CreatedImageAvailableSemaphoreCount), label_Arrays);
        }

        for(; CreatedRenderFinishedSemaphoreCount < ArrayCount(Handler.RenderFinishedSemaphores); ++CreatedRenderFinishedSemaphoreCount) {
            VulkanCheckGoto(vkCreateSemaphore(DeviceHandle, &SemaphoreCreateInfo, 0, Handler.RenderFinishedSemaphores + CreatedRenderFinishedSemaphoreCount), label_Arrays);
        }
        
        for(; CreatedInFlightFenceCount < ArrayCount(Handler.InFlightFences); ++CreatedInFlightFenceCount) {
            VulkanCheckGoto(vkCreateFence(DeviceHandle, &FenceCreateInfo, 0, Handler.InFlightFences + CreatedInFlightFenceCount), label_Arrays);
        }

        *SwapchainHandler = Handler;
    }
    
    return 0;

label_Arrays:
    for(uint32_t I = 0; I < CreatedInFlightFenceCount; ++I) {
        vkDestroyFence(DeviceHandle, Handler.InFlightFences[I], 0);
        Handler.InFlightFences[I] = 0;
    }
    for(uint32_t I = 0; I < CreatedRenderFinishedSemaphoreCount; ++I) {
        vkDestroySemaphore(DeviceHandle, Handler.RenderFinishedSemaphores[I], 0);
        Handler.RenderFinishedSemaphores[I] = 0;
    }
    for(uint32_t I = 0; I < CreatedImageAvailableSemaphoreCount; ++I) {
        vkDestroySemaphore(DeviceHandle, Handler.ImageAvailableSemaphores[I], 0);
        Handler.ImageAvailableSemaphores[I] = 0;
    }
    VulkanDestroySwapchain(Device, &Handler.Swapchains[0]);
label_Error:
    return 1;
}

static int VulkanAcquireNextImage(vulkan_surface_device *Device, vulkan_swapchain_handler *SwapchainHandler, VkExtent2D FramebufferExtent, vulkan_acquired_image *AcquiredImage) {
    VkDevice DeviceHandle = Device->Handle;

    {
        vulkan_swapchain_handler Handler = *SwapchainHandler;

        // NOTE(blackedout):
        // SwapchainImageIndex is the index of the acquired image in the array of swapchain images (max is runtime dependent)
        // AcquiredImageDataIndex is the index into the array of all acquired images (max is the max number of acquired images)
        uint32_t SwapchainImageIndex;
        uint32_t AcquiredImageDataIndex;
        uint32_t SwapchainIndex;
        vulkan_swapchain Swapchain;
        for(;;) {
            SwapchainIndex = IndicesCircularHead(&Handler.SwapchainBufIndices);
            Swapchain = Handler.Swapchains[SwapchainIndex];
            VkSemaphore ImageAvailableSemaphore = Handler.ImageAvailableSemaphores[Handler.AcquiredImageDataIndices.Next];
            VkResult AcquireResult = vkAcquireNextImageKHR(DeviceHandle, Swapchain.Handle, UINT64_MAX, ImageAvailableSemaphore, VULKAN_NULL_HANDLE, &SwapchainImageIndex);
            if(AcquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
                VkFence InFlightFences[ArrayCount(Handler.InFlightFences)];
                for(uint32_t I = 0; I < Handler.AcquiredImageDataIndices.Count; ++I) {
                    uint32_t CircularIndex = IndicesCircularGet(&Handler.AcquiredImageDataIndices, I);
                    InFlightFences[I] = Handler.InFlightFences[CircularIndex];
                }

                vkWaitForFences(DeviceHandle, Handler.AcquiredImageDataIndices.Count, InFlightFences, VK_TRUE, UINT64_MAX);
                Handler.AcquiredImageDataIndices.Next = 0;
                Handler.AcquiredImageDataIndices.Count = 0;

                CheckGoto(VulkanCreateSwapchain(Device, FramebufferExtent, Handler.SampleCount, Handler.RenderPass, &Swapchain, &Swapchain), label_Error);
                for(uint32_t I = 0; I < Handler.SwapchainBufIndices.Count; ++I) {
                    uint32_t CircularIndex = IndicesCircularGet(&Handler.SwapchainBufIndices, I);
                    VulkanDestroySwapchain(Device, Handler.Swapchains + CircularIndex);
                }
                Handler.SwapchainBufIndices.Next = 1;
                Handler.SwapchainBufIndices.Count = 1;
                Handler.Swapchains[0] = Swapchain;
            } else {
                if(AcquireResult == VK_SUBOPTIMAL_KHR) {
                    // NOTE(blackedout): This is handled after queueing for presentation
                    printfc(CODE_YELLOW, "Acquire suboptimal.\n");
                } else VulkanCheckGoto(AcquireResult, label_Error);

                // NOTE(blackedout): Image acquisition worked, so push
                AcquiredImageDataIndex = IndicesCircularPush(&Handler.AcquiredImageDataIndices);
                Handler.AcquiredSwapchainImageIndices[AcquiredImageDataIndex] = SwapchainImageIndex;
                Handler.AcquiredSwapchainIndices[AcquiredImageDataIndex] = SwapchainIndex;
                ++Handler.Swapchains[SwapchainIndex].AcquiredImageCount;

                Handler.SwapchainIndexLastAcquired = SwapchainIndex;
                //printf("acquired image %d (framebuffer %p) from swapchain %d (buf index %d)\n", SwapchainImageIndex, Swapchain.Framebuffers[SwapchainImageIndex], SwapchainIndex, AcquiredImageDataIndex);

                break;
            }
        }

        vulkan_acquired_image LocalAcquiredImage = {
            .Framebuffer = Swapchain.Framebuffers[SwapchainImageIndex],
            .Extent = Swapchain.ImageExtent,
            .DataIndex = AcquiredImageDataIndex
        };
        *AcquiredImage = LocalAcquiredImage;
        *SwapchainHandler = Handler;

        //printf("current (%d, %d), acquired (%d, %d)\n", Context.FramebufferWidth, Context.FramebufferHeight, AcquiredImage.Extent.width, AcquiredImage.Extent.height);
    }

    return 0;

label_Error:
    return 1;
}

static int VulkanSubmitFinalAndPresent(vulkan_surface_device *Device, vulkan_swapchain_handler *SwapchainHandler, VkQueue GraphicsQueue, VkCommandBuffer GraphicsCommandBuffer, VkExtent2D FramebufferExtent) {
    VkDevice DeviceHandle = Device->Handle;

    {
        vulkan_swapchain_handler Handler = *SwapchainHandler;

        uint32_t SwapchainIndex = Handler.SwapchainIndexLastAcquired;
        uint32_t AcquiredImageDataIndex = IndicesCircularHead(&Handler.AcquiredImageDataIndices);

        VkPipelineStageFlags WaitDstStageMasks[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSubmitInfo GraphicsSubmitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = 0,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &Handler.ImageAvailableSemaphores[AcquiredImageDataIndex],
            .pWaitDstStageMask = WaitDstStageMasks,
            .commandBufferCount = 1,
            .pCommandBuffers = &GraphicsCommandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &Handler.RenderFinishedSemaphores[AcquiredImageDataIndex]
        };
        VulkanCheckGoto(vkQueueSubmit(GraphicsQueue, 1, &GraphicsSubmitInfo, Handler.InFlightFences[AcquiredImageDataIndex]), label_Error);

        VkPresentInfoKHR PresentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = 0,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &Handler.RenderFinishedSemaphores[AcquiredImageDataIndex],
            .swapchainCount = 1,
            .pSwapchains = &Handler.Swapchains[SwapchainIndex].Handle,
            .pImageIndices = &Handler.AcquiredSwapchainImageIndices[AcquiredImageDataIndex],
            .pResults = 0 // NOTE(blackedout): Only needed if multiple swapchains used
        };

        //printf("Queueing image %d of swapchain %d for presentaton.\n", PresentInfo.pImageIndices[0], SwapchainIndex);
        VkResult PresentResult = vkQueuePresentKHR(GraphicsQueue, &PresentInfo);
        if(PresentResult == VK_SUBOPTIMAL_KHR || PresentResult == VK_ERROR_OUT_OF_DATE_KHR) {
            // TODO(blackedout): There is still an issue where sometimes the next frame is not rendered (clear color only) when resizing multiple times in quick succession
            vulkan_swapchain NewSwapchain = Handler.Swapchains[SwapchainIndex];
            CheckGoto(VulkanCreateSwapchain(Device, FramebufferExtent, Handler.SampleCount, Handler.RenderPass, &NewSwapchain, &NewSwapchain), label_Error);
            uint32_t NewSwapchainIndex = IndicesCircularPush(&Handler.SwapchainBufIndices);
            Handler.Swapchains[NewSwapchainIndex] = NewSwapchain;

            // TODO(blackedout): VK_ERROR_OUT_OF_DATE_KHR shouldn't be happening here (?) since there was no event polling that could've changed the window
            // Apparently this ^ is wrong, because on windows PresentResult is VK_ERROR_OUT_OF_DATE_KHR without any prior info (from acquiring)
            printf("Swapchain %d pushed because %s.\n", NewSwapchainIndex, string_VkResult(PresentResult));
        } else VulkanCheckGoto(PresentResult, label_Error);

        // NOTE(blackedout): Remove acquired image from list
        uint32_t AcquiredImageDataBaseIndex = IndicesCircularTake(&Handler.AcquiredImageDataIndices);
        VulkanCheckGoto(vkWaitForFences(DeviceHandle, 1, &Handler.InFlightFences[AcquiredImageDataBaseIndex], VK_TRUE, UINT64_MAX), label_Error);
        vkResetFences(DeviceHandle, 1, &Handler.InFlightFences[AcquiredImageDataBaseIndex]);
        Handler.AcquiredSwapchainImageIndices[AcquiredImageDataBaseIndex] = 0;
        Handler.AcquiredSwapchainIndices[AcquiredImageDataBaseIndex] = 0;

        uint32_t SwapchainsBaseIndex = IndicesCircularGet(&Handler.SwapchainBufIndices, 0);
        AssertMessageGoto(Handler.Swapchains[SwapchainsBaseIndex].AcquiredImageCount > 0, label_Error, "Swapchain acquired image count zero.\n");
        --Handler.Swapchains[SwapchainsBaseIndex].AcquiredImageCount;
        if(Handler.SwapchainBufIndices.Count > 1 && Handler.Swapchains[SwapchainsBaseIndex].AcquiredImageCount == 0) {
            printf("Destructing swapchain %d.\n", SwapchainsBaseIndex);
            VulkanDestroySwapchain(Device, Handler.Swapchains + SwapchainsBaseIndex);
            
            IndicesCircularTake(&Handler.SwapchainBufIndices);
            memset(&Handler.Swapchains[SwapchainsBaseIndex], 0, sizeof(*Handler.Swapchains));
            Handler.Swapchains[SwapchainsBaseIndex].Handle = VULKAN_NULL_HANDLE;
        }

        *SwapchainHandler = Handler;
    }

    return 0;

label_Error:
    return 1;
}