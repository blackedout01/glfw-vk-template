#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#define VULKAN_NULL_HANDLE 0 // NOTE(blackedout): Somehow VK_NULL_HANDLE generates erros when compiling in cpp mode
#define VULKAN_INFO_PRINT
#define MAX_ACQUIRED_IMAGE_COUNT 1
#define MAX_SWAPCHAIN_COUNT (MAX_ACQUIRED_IMAGE_COUNT + 1)
#define MAX_SHADER_COUNT 1

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
    VkImage Handle;
    VkImageView ViewHandle;
    VkImageType Type;
    VkImageViewType ViewType;
    VkFormat Format;
    uint32_t Width;
    uint32_t Height;
    uint32_t Depth;

    uint64_t ByteCount;
    void *Source;
    uint64_t Offset;
} vulkan_image;

typedef struct {
    VkBuffer VertexHandle;
    VkBuffer IndexHandle;

    uint32_t MemoryCount;
    VkDeviceMemory MemoryBufs[3];

    uint32_t ImageCount;
    vulkan_image *Images;
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

// MARK: Scoring
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

// MARK: Memory, Buffers
static int VulkanGetBufferMemoryTypeIndex(vulkan_surface_device Device, uint32_t MemoryTypeBits, VkMemoryPropertyFlags MemoryPropertyFlags, uint32_t *MemoryTypeIndex) {
     // TODO(blackedout): Make this part of vulkan_surface_device?
    VkPhysicalDeviceMemoryProperties PhysicalDeviceMemoryProperties;
    vkGetPhysicalDeviceMemoryProperties(Device.PhysicalDevice, &PhysicalDeviceMemoryProperties);

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

static void VulkanDestroyBuffer(vulkan_surface_device Device, vulkan_buffer Buffer) {
    vkDestroyBuffer(Device.Handle, Buffer.Handle, 0);
    vkFreeMemory(Device.Handle, Buffer.Memory, 0);
}

static int VulkanCreateExlusiveBufferWithMemory(vulkan_surface_device Device, uint64_t ByteCount, VkBufferUsageFlags UsageFlags, VkMemoryPropertyFlags MemoryPropertyFlags, vulkan_buffer *Buffer) {
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

    VkBuffer BufferHandle;
    VulkanCheckGoto(vkCreateBuffer(Device.Handle, &BufferCreateInfo, 0, &BufferHandle), label_Error);

    VkMemoryRequirements BufferMemoryRequirements;
    vkGetBufferMemoryRequirements(Device.Handle, BufferHandle, &BufferMemoryRequirements);

    uint32_t BufferMemoryTypeIndex;
    CheckGoto(VulkanGetBufferMemoryTypeIndex(Device, BufferMemoryRequirements.memoryTypeBits, MemoryPropertyFlags, &BufferMemoryTypeIndex), label_Buffer);

    VkMemoryAllocateInfo MemoryAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = 0,
        .allocationSize = BufferMemoryRequirements.size,
        .memoryTypeIndex = BufferMemoryTypeIndex
    };

    VkDeviceMemory BufferMemoryHandle;
    VulkanCheckGoto(vkAllocateMemory(Device.Handle, &MemoryAllocateInfo, 0, &BufferMemoryHandle), label_Buffer);
    VulkanCheckGoto(vkBindBufferMemory(Device.Handle, BufferHandle, BufferMemoryHandle, 0), label_Memory);

    vulkan_buffer LocalBuffer = {
        .Handle = BufferHandle,
        .Memory = BufferMemoryHandle,
    };
    *Buffer = LocalBuffer;

    return 0;
    
label_Memory:
    vkFreeMemory(Device.Handle, BufferMemoryHandle, 0);
label_Buffer:
    vkDestroyBuffer(Device.Handle, BufferHandle, 0);
label_Error:
    return 1;
}

// MARK: Static Buffers
static void VulkanDestroyStaticImagesAndBuffers(vulkan_surface_device Device, vulkan_static_buffers StaticBuffers) {
    for(uint32_t I = 0; I < StaticBuffers.ImageCount; ++I) {
        vkDestroyImage(Device.Handle, StaticBuffers.Images[I].Handle, 0);
        vkDestroyImageView(Device.Handle, StaticBuffers.Images[I].ViewHandle, 0);
    }
    for(uint32_t I = 0; I < StaticBuffers.MemoryCount; ++I) {
        vkFreeMemory(Device.Handle, StaticBuffers.MemoryBufs[I], 0);
    }
    vkDestroyBuffer(Device.Handle, StaticBuffers.IndexHandle, 0);
    vkDestroyBuffer(Device.Handle, StaticBuffers.VertexHandle, 0);
}

static int VulkanCreateStaticImagesAndBuffers(vulkan_surface_device Device, vulkan_mesh_subbuf *MeshSubbufs, uint32_t MeshSubbufCount, vulkan_image *Images, uint32_t ImageCount, VkCommandPool TransferCommandPool, VkQueue TransferQueue, vulkan_static_buffers *StaticBuffers) {
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
    VkBuffer VertexBuffer;
    VulkanCheckGoto(vkCreateBuffer(Device.Handle, &BufferCreateInfo, 0, &VertexBuffer), label_Error);

    BufferCreateInfo.size = TotalIndexByteCount;
    BufferCreateInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkBuffer IndexBuffer;
    VulkanCheckGoto(vkCreateBuffer(Device.Handle, &BufferCreateInfo, 0, &IndexBuffer), label_VertexBuffer);

    // NOTE(blackedout): Allocate and bind uniform memory with joined vertex and index requirements to the respective buffers 
    VkMemoryRequirements VertexMemoryRequirements, IndexMemoryRequirements;
    vkGetBufferMemoryRequirements(Device.Handle, VertexBuffer, &VertexMemoryRequirements);
    vkGetBufferMemoryRequirements(Device.Handle, IndexBuffer, &IndexMemoryRequirements);

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
    VkDeviceMemory BufferMemoryHandle;
    VulkanCheckGoto(vkAllocateMemory(Device.Handle, &MemoryAllocateInfo, 0, &BufferMemoryHandle), label_IndexBuffer);

    VulkanCheckGoto(vkBindBufferMemory(Device.Handle, VertexBuffer, BufferMemoryHandle, VertexByteOffset), label_BufferMemory);
    VulkanCheckGoto(vkBindBufferMemory(Device.Handle, IndexBuffer, BufferMemoryHandle, IndexByteOffset), label_BufferMemory);

    // NOTE(blackedout): Create image handles, allocate and bind its memory, then create view handles
    uint64_t AlignedTotalImagesByteCount = 0;
    uint32_t ImageMemoryTypeBits = ~(uint32_t)0;
    uint32_t CreatedImageCount = 0;
    for(uint32_t I = 0; I < ImageCount; ++I) {
        vulkan_image Image = Images[I];
        VkImageCreateInfo ImageCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .imageType = Image.Type,
            .format = Image.Format,
            .extent.width = Image.Width,
            .extent.height = Image.Height,
            .extent.depth = Image.Depth,
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

        VulkanCheckGoto(vkCreateImage(Device.Handle, &ImageCreateInfo, 0, &Image.Handle), label_Images);
        ++CreatedImageCount;

        VkMemoryRequirements ImageMemoryRequirements;
        vkGetImageMemoryRequirements(Device.Handle, Image.Handle, &ImageMemoryRequirements);

        uint64_t ByteOffset = AlignAny(AlignedTotalImagesByteCount, uint64_t, ImageMemoryRequirements.alignment);
        AlignedTotalImagesByteCount += ByteOffset + ImageMemoryRequirements.size;
        Image.Offset = ByteOffset;

        // TODO(blackedout): Handle this getting too restrictive
        ImageMemoryTypeBits &= ImageMemoryRequirements.memoryTypeBits;

        Images[I] = Image;
    }
    uint32_t ImageMemoryTypeIndex;
    CheckGoto(VulkanGetBufferMemoryTypeIndex(Device, ImageMemoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ImageMemoryTypeIndex), label_Images);
    VkMemoryAllocateInfo ImageMemoryAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = 0,
        .allocationSize = AlignedTotalImagesByteCount,
        .memoryTypeIndex = ImageMemoryTypeIndex
    };
    VkDeviceMemory ImageMemoryHandle;
    VulkanCheckGoto(vkAllocateMemory(Device.Handle, &ImageMemoryAllocateInfo, 0, &ImageMemoryHandle), label_Images);
    for(uint32_t I = 0; I < ImageCount; ++I) {
        VulkanCheckGoto(vkBindImageMemory(Device.Handle, Images[I].Handle, ImageMemoryHandle, Images[I].Offset), label_ImageMemory);
    }

    uint32_t CreatedImageViewCount = 0;
    for(uint32_t I = 0; I < ImageCount; ++I) {
        vulkan_image Image = Images[I];
        VkImageViewCreateInfo ImageViewCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .image = Image.Handle,
            .viewType = Image.ViewType,
            .format = Image.Format,
            .components = { .r = VK_COMPONENT_SWIZZLE_IDENTITY, .g = VK_COMPONENT_SWIZZLE_IDENTITY, .b = VK_COMPONENT_SWIZZLE_IDENTITY, .a = VK_COMPONENT_SWIZZLE_IDENTITY },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };

        VulkanCheckGoto(vkCreateImageView(Device.Handle, &ImageViewCreateInfo, 0, &Image.ViewHandle), label_ImageViews);
        ++CreatedImageViewCount;

        Images[I] = Image;
    }

    // NOTE(blackedout): Create and fill staging buffer
    vulkan_buffer StagingBuffer;
    CheckGoto(VulkanCreateExlusiveBufferWithMemory(Device, AlignedTotalBuffersByteCount + AlignedTotalImagesByteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &StagingBuffer), label_ImageMemory);

    void *MappedStagingBuffer;
    VulkanCheckGoto(vkMapMemory(Device.Handle, StagingBuffer.Memory, 0, AlignedTotalBuffersByteCount, 0, &MappedStagingBuffer), label_StagingBuffer);
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
    uint64_t ImageOffset = 0;
    for(uint32_t I = 0; I < ImageCount; ++I) {
        memcpy(MappedStagingBuffer + AlignedTotalBuffersByteCount + ImageOffset, Images[I].Source, Images[I].ByteCount);
        ImageOffset = Images[I].ByteCount;
    }
    vkUnmapMemory(Device.Handle, StagingBuffer.Memory);

    // NOTE(blackedout): Allocate transfer command buffer, record transfer of data, submit and wait for completion
    VkCommandBufferAllocateInfo TransferCommandBufferAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = 0,
        .commandPool = TransferCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer TransferCommandBuffer;
    VulkanCheckGoto(vkAllocateCommandBuffers(Device.Handle, &TransferCommandBufferAllocateInfo, &TransferCommandBuffer), label_StagingBuffer);

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
    vkCmdCopyBuffer(TransferCommandBuffer, StagingBuffer.Handle, VertexBuffer, 1, &VertexBufferCopy);
    vkCmdCopyBuffer(TransferCommandBuffer, StagingBuffer.Handle, IndexBuffer, 1, &IndexBufferCopy);

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
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .subresourceRange.baseMipLevel = 0,
            .subresourceRange.levelCount = 1,
            .subresourceRange.baseArrayLayer = 0,
            .subresourceRange.layerCount = 1,
        };
        vkCmdPipelineBarrier(TransferCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &ImageMemoryBarrier);
    }
    for(uint32_t I = 0; I < ImageCount; ++I) {
        VkBufferImageCopy BufferImageCopy = {
            .bufferOffset = AlignedTotalBuffersByteCount + Images[I].Offset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .imageSubresource.mipLevel = 0,
            .imageSubresource.baseArrayLayer = 0,
            .imageSubresource.layerCount = 1,
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { Images[I].Width, Images[I].Height, Images[I].Depth } // TODO
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
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .subresourceRange.baseMipLevel = 0,
            .subresourceRange.levelCount = 1,
            .subresourceRange.baseArrayLayer = 0,
            .subresourceRange.layerCount = 1,
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

    vulkan_static_buffers LocalStaticBuffers = {
        .VertexHandle = VertexBuffer,
        .IndexHandle = IndexBuffer,
        .MemoryCount = 2,
        .MemoryBufs = { BufferMemoryHandle, ImageMemoryHandle, VULKAN_NULL_HANDLE },

        .ImageCount = ImageCount,
        .Images = Images
    };
    *StaticBuffers = LocalStaticBuffers;

    vkFreeCommandBuffers(Device.Handle, TransferCommandPool, 1, &TransferCommandBuffer);
    VulkanDestroyBuffer(Device, StagingBuffer);

    return 0;

label_CommandBuffer:
    vkFreeCommandBuffers(Device.Handle, TransferCommandPool, 1, &TransferCommandBuffer);
label_StagingBuffer:
    VulkanDestroyBuffer(Device, StagingBuffer);
label_ImageViews:
    for(uint32_t I = 0; I < CreatedImageViewCount; ++I) {
        vkDestroyImageView(Device.Handle, Images[I].ViewHandle, 0);
    }
label_ImageMemory:
    vkFreeMemory(Device.Handle, ImageMemoryHandle, 0);
label_Images:
    for(uint32_t I = 0; I < CreatedImageCount; ++I) {
        vkDestroyImage(Device.Handle, Images[I].Handle, 0);
    }
label_BufferMemory:
    vkFreeMemory(Device.Handle, BufferMemoryHandle, 0);
label_IndexBuffer:
    vkDestroyBuffer(Device.Handle, IndexBuffer, 0);
label_VertexBuffer:
    vkDestroyBuffer(Device.Handle, VertexBuffer, 0);
label_Error:
    return 1;
}

// MARK: Shaders
static int VulkanCreateShaderModule(vulkan_surface_device Device, const uint8_t *Bytes, uint64_t ByteCount, VkShaderModule *Module) {
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

typedef struct {
    VkDescriptorSetLayoutCreateFlags Flags;
    VkDescriptorSetLayoutBinding *Bindings;
    uint32_t BindingsCount;
} vulkan_descriptor_set_layout_description;

static void VulkanDestroyDescriptorSetLayouts(vulkan_surface_device Device, VkDescriptorSetLayout *DescriptorSetLayouts, uint32_t Count) {
    for(uint32_t I = 0; I < Count; ++I) {
        vkDestroyDescriptorSetLayout(Device.Handle, DescriptorSetLayouts[I], 0);
    }
}

static int VulkanCreateDescriptorSetLayouts(vulkan_surface_device Device, vulkan_descriptor_set_layout_description *Descriptions, uint32_t Count, VkDescriptorSetLayout *DescriptorSetLayouts) {
    VkDescriptorSetLayoutCreateInfo DescriptorSetLayoutCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .bindingCount = 0,
        .pBindings = 0
    };

    uint32_t CreatedCount;
    for(CreatedCount = 0; CreatedCount < Count; ++CreatedCount) {
        vulkan_descriptor_set_layout_description Description = Descriptions[CreatedCount];
        DescriptorSetLayoutCreateInfo.flags = Description.Flags;
        DescriptorSetLayoutCreateInfo.bindingCount = Description.BindingsCount;
        DescriptorSetLayoutCreateInfo.pBindings = Description.Bindings;
        
        VulkanCheckGoto(vkCreateDescriptorSetLayout(Device.Handle, &DescriptorSetLayoutCreateInfo, 0, DescriptorSetLayouts + CreatedCount), label_Error);
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

static int VulkanCreateShaderUniformBuffers(vulkan_surface_device Device, VkDescriptorSetLayout DescriptorSetLayout, vulkan_shader_uniform_buffers_description *Descriptions, uint32_t Count, VkDeviceMemory *BufferMemory, VkDescriptorPool *DescriptorPool) {
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

    uint32_t CreatedCount = 0;
    VkDeviceSize TotalByteCount = 0;
    uint32_t CommonMemoryTypeBits = ~(uint32_t)0; // TODO(blackedout): Do multiple allocations if needed.
    for(uint32_t I = 0; I < Count; ++I) {
        vulkan_shader_uniform_buffers_description Description = Descriptions[I];
        BufferCreateInfo.size = Description.Size;
        
        for(uint32_t J = 0; J < MAX_ACQUIRED_IMAGE_COUNT; ++J) {
            VkBuffer Buffer;
            VulkanCheckGoto(vkCreateBuffer(Device.Handle, &BufferCreateInfo, 0, &Buffer), label_Buffers);
            Description.Buffers[J] = Buffer;
            ++CreatedCount;
        }

        VkMemoryRequirements MemoryRequirements;
        vkGetBufferMemoryRequirements(Device.Handle, Description.Buffers[0], &MemoryRequirements);

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

    VkDeviceMemory Memory;
    VulkanCheckGoto(vkAllocateMemory(Device.Handle, &MemoryAllocateInfo, 0, &Memory), label_Buffers);

    // TODO(blackedout): Map whole buffer or individual chunks?
    uint8_t *MappedMemory;
    VulkanCheckGoto(vkMapMemory(Device.Handle, Memory, 0, TotalByteCount, 0, (void **)&MappedMemory), label_Memory);

    for(uint32_t I = 0; I < Count; ++I) {
        for(uint32_t J = 0; J < MAX_ACQUIRED_IMAGE_COUNT; ++J) {
            vulkan_shader_uniform_buffers_description Description = Descriptions[I];

            uint64_t Offset = (uint64_t)Description.MappedBuffers[J];
            VulkanCheckGoto(vkBindBufferMemory(Device.Handle, Description.Buffers[J], Memory, Offset), label_Memory);
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

    VkDescriptorPool LocalDescriptorPool;
    VulkanCheckGoto(vkCreateDescriptorPool(Device.Handle, &DescriptorPoolCreateInfo, 0, &LocalDescriptorPool), label_Memory);    
    
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

        VkDescriptorSet DescriptorSets[ArrayCount(DescriptorSetLayouts)];
        VulkanCheckGoto(vkAllocateDescriptorSets(Device.Handle, &DescriptorSetAllocateInfo, DescriptorSets), label_DescriptorPool);

        VkDescriptorBufferInfo BufferInfo = {
            .buffer = VULKAN_NULL_HANDLE,
            .offset = 0,
            .range = Description.Size
        };
        for(uint32_t J = 0; J < ArrayCount(DescriptorSets); ++J) {
            BufferInfo.buffer = Description.Buffers[J];

            VkWriteDescriptorSet WriteDescriptorSets[] = {
                {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .pNext = 0,
                    .dstSet = DescriptorSets[I],
                    .dstBinding = Description.BindingIndex, // NOTE(blackedout): Uniform binding index (?)
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pImageInfo = 0,
                    .pBufferInfo = &BufferInfo,
                    .pTexelBufferView = 0
                },
            };

            vkUpdateDescriptorSets(Device.Handle, ArrayCount(WriteDescriptorSets), WriteDescriptorSets, 0, 0);
        }
        memcpy(Description.DescriptorSets, DescriptorSets, sizeof(DescriptorSets));
    }

    *BufferMemory = Memory;
    *DescriptorPool = LocalDescriptorPool;
    return 0;

label_DescriptorPool:
    vkDestroyDescriptorPool(Device.Handle, LocalDescriptorPool, 0);
    *DescriptorPool = VULKAN_NULL_HANDLE;
label_Memory:
    vkFreeMemory(Device.Handle, Memory, 0);
    *BufferMemory = VULKAN_NULL_HANDLE;
label_Buffers:
    for(uint32_t I = 0; I < CreatedCount; ++I) {
        vulkan_shader_uniform_buffers_description Description = Descriptions[I / MAX_ACQUIRED_IMAGE_COUNT];
        uint32_t BufferIndex = (I % MAX_ACQUIRED_IMAGE_COUNT);

        vkDestroyBuffer(Device.Handle, Description.Buffers[BufferIndex], 0);
        Description.Buffers[BufferIndex] = VULKAN_NULL_HANDLE;
        Description.MappedBuffers[BufferIndex] = 0;
    }
    return 1;
}

// MARK: Instace
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

// MARK: Surface Device
static void VulkanDestroySurfaceDevice(VkInstance Instance, vulkan_surface_device Device) {
    vkDestroySurfaceKHR(Instance, Device.Surface, 0);
    vkDestroyDevice(Device.Handle, 0);
}

static int VulkanCreateSurfaceDevice(VkInstance Instance, VkSurfaceKHR Surface, vulkan_surface_device *Device) {
    // NOTE(blackedout): This function will destroy the input surface on failure.
    // Returns a device whose physical device has at least one graphics queue, at least one surface presentation queue and supports the surface extension.
    // The physical device is picked by scoring its type, available surface formats and present modes.

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

    vulkan_surface_device LocalSurfaceDevice = {
        .Handle = DeviceHandle,
        .PhysicalDevice = BestPhysicalDevice,
        .Surface = Surface,
        .GraphicsQueueFamilyIndex = BestPhysicalDeviceGraphicsQueueIndex,
        .PresentQueueFamilyIndex = BestPhysicalDeviceSurfaceQueueIndex,

        .InitialExtent = BestPhysicalDeviceSurfaceCapabilities.currentExtent,
        .InitialSurfaceFormat = BestPhysicalDeviceInitialSurfaceFormat,

        .Features = BestPhysicalDeviceFeatures,
        .Properties = BestPhysicalDeviceProperties
    };

    *Device = LocalSurfaceDevice;

    return 0;

label_Error:
    vkDestroySurfaceKHR(Instance, Surface, 0);
    return 1;
}

// MARK: Swapchain
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
        VkImageViewCreateInfo ImageViewCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .image = Images[CreatedImageViewCount],
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

static void VulkanDestroyDefaultGraphicsPipeline(vulkan_surface_device Device, vulkan_graphics_pipeline_info PipelineInfo) {
    vkDestroyPipeline(Device.Handle, PipelineInfo.Pipeline, 0);
    vkDestroyRenderPass(Device.Handle, PipelineInfo.RenderPass, 0);
    vkDestroyPipelineLayout(Device.Handle, PipelineInfo.Layout, 0);
    
}

// MARK: Graphics Pipeline
static int VulkanCreateDefaultGraphicsPipeline(vulkan_surface_device Device, VkShaderModule ModuleVS, VkShaderModule ModuleFS, VkExtent2D InitialExtent, VkFormat SwapchainFormat, VkPipelineVertexInputStateCreateInfo PipelineVertexInputStateCreateInfo, VkDescriptorSetLayout *DescriptorSetLayouts, uint32_t DescriptorSetLayoutCount, vulkan_graphics_pipeline_info *PipelineInfo) {
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
        .setLayoutCount = DescriptorSetLayoutCount,
        .pSetLayouts = DescriptorSetLayouts,
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

// MARK: Swapch. Handler
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
        .AcquiredImageIndices = {0},
        .AcquiredImageSwapchainIndices = {0},
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

static int VulkanAcquireNextImage(vulkan_surface_device Device, vulkan_swapchain_handler *SwapchainHandler, VkExtent2D FramebufferExtent, VkExtent2D *ImageExtent, VkFramebuffer *Framebuffer, uint32_t *UserImageIndex) {
    vulkan_swapchain_handler Handler = *SwapchainHandler;

    // TODO(blackedout): Think of better names for ImageIndex and AcquiredImageBufIndex
    // ImageIndex is the index of the acquired image in the array of swapchain images (max is runtime dependent)
    // AcquiredImageBufIndex is the index into the array of all acquired images (max is the max number of acquired images)
    uint32_t ImageIndex;
    uint32_t AcquiredImageBufIndex;
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
                printfc(CODE_YELLOW, "Acquire suboptimal.\n");
            } else VulkanCheckGoto(AcquireResult, label_Error);

            // NOTE(blackedout): Image acquisition worked, so push
            AcquiredImageBufIndex = IndicesCircularPush(&Handler.AcquiredImageBufIndices);
            Handler.AcquiredImageIndices[AcquiredImageBufIndex] = ImageIndex;
            Handler.AcquiredImageSwapchainIndices[AcquiredImageBufIndex] = SwapchainIndex;
            ++Handler.Swapchains[SwapchainIndex].AcquiredImageCount;

            Handler.SwapchainIndexLastAcquired = SwapchainIndex;
            //printf("acquired image %d (framebuffer %p) from swapchain %d (buf index %d)\n", ImageIndex, Swapchain.Framebuffers[ImageIndex], SwapchainIndex, AcquiredImageBufIndex);

            break;
        }
    }

    *ImageExtent = Swapchain.ImageExtent;
    *Framebuffer = Swapchain.Framebuffers[ImageIndex];
    *UserImageIndex = AcquiredImageBufIndex;
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

    //printf("Queueing image %d of swapchain %d for presentaton.\n", PresentInfo.pImageIndices[0], SwapchainIndex);
    double before = glfwGetTime();
    VkResult PresentResult = vkQueuePresentKHR(GraphicsQueue, &PresentInfo);
    double after = glfwGetTime();
    //printf("Took %f s.\n", after - before);
    if(PresentResult == VK_SUBOPTIMAL_KHR || PresentResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // TODO(blackedout): There is still an issue where sometimes the next frame is not rendered (clear color only) when resizing multiple times in quick succession
        vulkan_swapchain NewSwapchain = Handler.Swapchains[SwapchainIndex];
        CheckGoto(VulkanCreateSwapchain(Device, FramebufferExtent, Handler.RenderPass, &NewSwapchain, &NewSwapchain), label_Error);
        uint32_t NewSwapchainIndex = IndicesCircularPush(&Handler.SwapchainBufIndices);
        Handler.Swapchains[NewSwapchainIndex] = NewSwapchain;

        // TODO(blackedout): VK_ERROR_OUT_OF_DATE_KHR shouldn't be happening here (?) since there was no event polling that could've changed the window
        // Apparently this ^ is wrong, because on windows PresentResult is VK_ERROR_OUT_OF_DATE_KHR without any prior info (from acquiring)
        printf("Swapchain %d pushed because %s.\n", NewSwapchainIndex, string_VkResult(PresentResult));
    } else VulkanCheckGoto(PresentResult, label_Error);

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