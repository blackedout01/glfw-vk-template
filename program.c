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

typedef struct {
    v3 Position;
    v3 Normal;
    v2 TexCoord;
} vertex;

typedef struct {
    m4 V, P;
    v4 L;
} default_uniform_buffer1;

typedef struct {
    m4 M;
    m2 TexM;
    v2 TexT;
} default_push_constants;

enum {
    DESCRIPTOR_SET_LAYOUT_DEFAULT_UNIFORM,
    DESCRIPTOR_SET_LAYOUT_DEFAULT_SAMPLER_IMAGE,

    DESCRIPTOR_SET_LAYOUT_COUNT
};

enum {
    STATIC_IMAGE_COLOR,
    STATIC_IMAGE_TILE,

    STATIC_IMAGE_COUNT
};

typedef struct {
    vulkan_shader Default;
    VkDescriptorSetLayout DescriptorSetLayouts[DESCRIPTOR_SET_LAYOUT_COUNT];
    VkDeviceMemory UniformBufferMemory;
    
    VkBuffer UniformMatsBuffers[MAX_ACQUIRED_IMAGE_COUNT];
    default_uniform_buffer1 *UniformMats[MAX_ACQUIRED_IMAGE_COUNT];
    VkDescriptorSet UniformMatsSets[MAX_ACQUIRED_IMAGE_COUNT];

    VkSampler DefaultSampler;

    VkDescriptorPool DefaultDescriptorPool;
    VkDescriptorPool UniformDescriptorPool;

    VkDescriptorSet DefaultImageTileSet;
    VkDescriptorSet DefaultImageColorSet;
} shaders;

#include "vulkan_custom.c"

typedef struct {
    int IsSuperDown;

    int IsDragging;
    double LastCursorX, LastCursorY;

    float CamAzi, CamPol;
    float CamZoom;

    int ImagesInitialized;

    shaders Shaders;
    VkCommandPool GraphicsCommandPool;
    VkCommandBuffer GraphicsCommandBuffer;
    VkQueue GraphicsQueue;

    vulkan_static_buffers StaticBuffers;

    VkPipelineLayout GraphicsPipelineLayout;
    VkRenderPass RenderPass;
    VkPipeline GraphicsPipeline;
    VkSampleCountFlagBits SampleCount;

    vulkan_image Images[STATIC_IMAGE_COUNT];

    uint64_t PlaneVerticesByteOffset;
    uint64_t PlaneIndicesByteOffset;
    uint64_t CubeVerticesByteOffset;
    uint64_t CubeIndicesByteOffset;
} context;

static void ProgramCursorPositionCallback(context *Context, double PosX, double PosY) {
    double CursorDeltaX = Context->LastCursorX - PosX;
    double CursorDeltaY = Context->LastCursorY - PosY;

    if(Context->IsDragging) {
        Context->CamAzi += (float)(0.01*CursorDeltaX);
        Context->CamPol += (float)(0.01*CursorDeltaY);
    }

    Context->LastCursorX = PosX;
    Context->LastCursorY = PosY;
}

static void ProgramMouseButtonCallback(context *Context, int Button, int Action, int Mods) {
    if(Button == GLFW_MOUSE_BUTTON_LEFT) {
        Context->IsDragging = Action != GLFW_RELEASE;
    }
}

static void ProgramKeyCallback(context *Context, int Key, int Scancode, int Action, int Mods) {
    if(Key == GLFW_KEY_LEFT_SUPER) {
        Context->IsSuperDown = Action != GLFW_RELEASE;
    }
}

static void ProgramScrollCallback(context *Context, double OffsetX, double OffsetY) {
    float NewZoom = Max(0.01f, Min(100.0f, Context->CamZoom*(float)pow(1.1, OffsetY)));
#ifdef __APPLE__
    if(Context->IsSuperDown) {
        Context->CamZoom = NewZoom;
    } else {
        Context->CamAzi -= 0.1f*OffsetX;
        Context->CamPol -= 0.1f*OffsetY;
    }
#else
    Context->CamZoom = NewZoom;
#endif
}

static void ProgramFramebufferSizeCallback(context *Context, int Width, int Height) {
}

static vertex PlaneVertices[] = {
    { .Position = { -0.5f, 0.0f, -0.5f }, .Normal = { 0.0f, 1.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { 0.5f, 0.0f, -0.5f }, .Normal = { 0.0f, 1.0f, 0.0f }, .TexCoord = { 0.5f, 0.0f } },
    { .Position = { 0.5f, 0.0f, 0.5f }, .Normal = { 0.0f, 1.0f, 0.0f }, .TexCoord = { 0.5f, 0.5f } },
    { .Position = { -0.5f, 0.0f, 0.5f }, .Normal = { 0.0f, 1.0f, 0.0f }, .TexCoord = { 0.0f, 0.5f } },
};

static uint32_t PlaneIndices[] = {
    0, 1, 2, 2, 3, 0
};

static vertex CubeVertices[] = {
    { .Position = { -0.5f, -0.5f, -0.5f }, .Normal = { 0.0f, -1.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { -0.5f, -0.5f, 0.5f }, .Normal = { 0.0f, -1.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { 0.5f, -0.5f, 0.5f }, .Normal = { 0.0f, -1.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { 0.5f, -0.5f, -0.5f }, .Normal = { 0.0f, -1.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },

    { .Position = { -0.5f, 0.5f, -0.5f }, .Normal = { 0.0f, 1.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { 0.5f, 0.5f, -0.5f }, .Normal = { 0.0f, 1.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { 0.5f, 0.5f, 0.5f }, .Normal = { 0.0f, 1.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { -0.5f, 0.5f, 0.5f }, .Normal = { 0.0f, 1.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },

    { .Position = { -0.5f, -0.5f, 0.5f }, .Normal = { -1.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { -0.5f, -0.5f, -0.5f }, .Normal = { -1.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { -0.5f, 0.5f, -0.5f }, .Normal = { -1.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { -0.5f, 0.5f, 0.5f }, .Normal = { -1.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },

    { .Position = { 0.5f, -0.5f, -0.5f }, .Normal = { 1.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { 0.5f, -0.5f, 0.5f }, .Normal = { 1.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { 0.5f, 0.5f, 0.5f }, .Normal = { 1.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { 0.5f, 0.5f, -0.5f }, .Normal = { 1.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },

    { .Position = { -0.5f, -0.5f, -0.5f }, .Normal = { 0.0f, 0.0f, -1.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { 0.5f, -0.5f, -0.5f }, .Normal = { 0.0f, 0.0f, -1.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { 0.5f, 0.5f, -0.5f }, .Normal = { 0.0f, 0.0f, -1.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { -0.5f, 0.5f, -0.5f }, .Normal = { 0.0f, 0.0f, -1.0f }, .TexCoord = { 0.0f, 0.0f } },

    { .Position = { 0.5f, -0.5f, 0.5f }, .Normal = { 0.0f, 0.0f, 1.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { -0.5f, -0.5f, 0.5f }, .Normal = { 0.0f, 0.0f, 1.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { -0.5f, 0.5f, 0.5f }, .Normal = { 0.0f, 0.0f, 1.0f }, .TexCoord = { 0.0f, 0.0f } },
    { .Position = { 0.5f, 0.5f, 0.5f }, .Normal = { 0.0f, 0.0f, 1.0f }, .TexCoord = { 0.0f, 0.0f } },
};

static uint32_t CubeIndices[] = {
    0, 1, 2, 2, 3, 0,
    4, 5, 6, 6, 7, 4,
    8, 9, 10, 10, 11, 8,
    12, 13, 14, 14, 15, 12,
    16, 17, 18, 18, 19, 16,
    20, 21, 22, 22, 23, 20,
    24, 25, 26, 26, 27, 24
};

static void ProgramSetdown(context *Context, vulkan_surface_device *Device) {
    // NOTE(blackedout): This function is built to be called at any stage of the setup process, meaning any initialization of setup might not have been done yet.
    VkDevice DeviceHandle = Device->Handle;
    if(Context->GraphicsPipelineLayout) {
        vkDestroyPipelineLayout(DeviceHandle, Context->GraphicsPipelineLayout, 0);
    }
    if(Context->RenderPass) {
        vkDestroyRenderPass(DeviceHandle, Context->RenderPass, 0);
    }
    if(Context->GraphicsPipeline) {
        vkDestroyPipeline(DeviceHandle, Context->GraphicsPipeline, 0);
    }
    if(Context->Shaders.UniformBufferMemory) {
        DestroyShaders(Device, &Context->Shaders);
    }
    if(Context->ImagesInitialized) {
        VulkanDestroyStaticBuffersAndImages(Device, &Context->StaticBuffers, Context->Images, STATIC_IMAGE_COUNT);
    }
    if(Context->GraphicsCommandPool) {
        vkDestroyCommandPool(DeviceHandle, Context->GraphicsCommandPool, 0);
    }
}

static int ProgramSetup(context *Context, vulkan_surface_device *Device, VkCommandBuffer *GraphicsCommandBuffer, VkQueue *GraphicsQueue, VkRenderPass *RenderPass, VkSampleCountFlagBits *SampleCount) {
    VkDevice DeviceHandle = Device->Handle;

    context LocalContext = {
        .CamPol = -0.01f,
        .CamZoom = 1.0f,
    };

    {
        // NOTE(blackedout): Create command pools, buffer and get queue
        VkCommandPoolCreateInfo GraphicsCommandPoolCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = 0,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = Device->GraphicsQueueFamilyIndex
        };
        VulkanCheckGoto(vkCreateCommandPool(DeviceHandle, &GraphicsCommandPoolCreateInfo, 0, &LocalContext.GraphicsCommandPool), label_Error);

        VkCommandBufferAllocateInfo GraphicsCommandBufferAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = 0,
            .commandPool = LocalContext.GraphicsCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VulkanCheckGoto(vkAllocateCommandBuffers(DeviceHandle, &GraphicsCommandBufferAllocateInfo, &LocalContext.GraphicsCommandBuffer), label_Error);

        vkGetDeviceQueue(DeviceHandle, Device->GraphicsQueueFamilyIndex, 0, &LocalContext.GraphicsQueue);

        vulkan_mesh_subbuf MeshSubbufs[] = {
            {
                .Vertices = { .Source = PlaneVertices, .ByteCount = sizeof(PlaneVertices), .OffsetPointer = &LocalContext.PlaneVerticesByteOffset },
                .Indices = { .Source = PlaneIndices, .ByteCount = sizeof(PlaneIndices), .OffsetPointer = &LocalContext.PlaneIndicesByteOffset }
            },
            {
                .Vertices = { .Source = CubeVertices, .ByteCount = sizeof(CubeVertices), .OffsetPointer = &LocalContext.CubeVerticesByteOffset },
                .Indices = { .Source = CubeIndices, .ByteCount = sizeof(CubeIndices), .OffsetPointer = &LocalContext.CubeIndicesByteOffset }
            }
        };

        uint8_t ColorImageBytes[] = {
            0xff, 0x20, 0x20, 0xff,
            0x20, 0xff, 0x20, 0xff,
            0x20, 0x20, 0xff, 0xff,
        };
        uint8_t TileImageBytes[] = {
            0xff, 0xff, 0xff, 0xff,
            0xf9, 0xf9, 0xfd, 0xff,
            0xf9, 0xf9, 0xfd, 0xff,
            0xff, 0xff, 0xff, 0xff,
        };
        vulkan_image_description StaticImageColor = { .Type = VK_IMAGE_TYPE_2D, .ViewType = VK_IMAGE_VIEW_TYPE_2D, .Format = VK_FORMAT_R8G8B8A8_SRGB, .Width = ArrayCount(ColorImageBytes)/4, .Height = 1, .Depth = 1, .ByteCount = sizeof(ColorImageBytes), .Source = ColorImageBytes };
        vulkan_image_description StaticImageTile = { .Type = VK_IMAGE_TYPE_2D, .ViewType = VK_IMAGE_VIEW_TYPE_2D, .Format = VK_FORMAT_R8G8B8A8_SRGB, .Width = 2, .Height = 2, .Depth = 1, .ByteCount = sizeof(TileImageBytes), .Source = TileImageBytes };
        vulkan_image_description ImageDescriptions[STATIC_IMAGE_COUNT];
        memset(ImageDescriptions, 0, sizeof(ImageDescriptions));
        ImageDescriptions[STATIC_IMAGE_COLOR] = StaticImageColor;
        ImageDescriptions[STATIC_IMAGE_TILE] = StaticImageTile;
        CheckGoto(VulkanCreateStaticBuffersAndImages(Device, MeshSubbufs, ArrayCount(MeshSubbufs), ImageDescriptions, ArrayCount(LocalContext.Images), LocalContext.GraphicsCommandPool, LocalContext.GraphicsQueue, &LocalContext.StaticBuffers, LocalContext.Images), label_Error);
        LocalContext.ImagesInitialized = 1;

        CheckGoto(LoadShaders(Device, &LocalContext.Shaders, LocalContext.Images), label_Error);

        VkVertexInputBindingDescription VertexInputBindingDescription = {
            .binding = 0,
            .stride = sizeof(vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        VkVertexInputAttributeDescription VertexAttributeDescriptions[] = {
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vertex, Position) },
            { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vertex, Normal) },
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

        VkPushConstantRange PushConstantRange = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(default_push_constants),
        };

        VkSampleCountFlagBits LocalSampleCount = Min(Device->MaxSampleCount, VK_SAMPLE_COUNT_4_BIT);
        CheckGoto(VulkanCreateDefaultGraphicsPipeline(Device, LocalContext.Shaders.Default.Vert, LocalContext.Shaders.Default.Frag, Device->InitialExtent, Device->InitialSurfaceFormat.format, LocalSampleCount, PipelineVertexInputStateCreateInfo, LocalContext.Shaders.DescriptorSetLayouts, ArrayCount(LocalContext.Shaders.DescriptorSetLayouts), PushConstantRange, &LocalContext.GraphicsPipelineLayout, &LocalContext.RenderPass, &LocalContext.GraphicsPipeline), label_Error);

        *Context = LocalContext;
        *GraphicsCommandBuffer = LocalContext.GraphicsCommandBuffer;
        *GraphicsQueue = LocalContext.GraphicsQueue;
        *RenderPass = LocalContext.RenderPass;
        *SampleCount = LocalSampleCount;
    }

    return 0;
    
label_Error:
    ProgramSetdown(&LocalContext, Device);
    return 1;
}

static int ProgramUpdate(context *Context, vulkan_surface_device *Device, double DeltaTime) {
    //Context.CamAzi += 0.1f;
    return 0;
}

static int ProgramRender(context *Context, vulkan_surface_device *Device, vulkan_acquired_image AcquiredImage) {
    {
        VulkanCheckGoto(vkResetCommandBuffer(Context->GraphicsCommandBuffer, 0), label_Error);
        int A = 0;
        VkRect2D RenderArea = {
            .offset = { 0, 0 },
            .extent = AcquiredImage.Extent
        };
        VkClearValue RenderClearValues[] = {
            {
                //.color = { .float32 = { (A & 1), 0.5f*(A & 2), 0.0f, 1.0f } }
                .color = { .float32 = { 0.6f, 0.8f, 0.99f - (0.1f*A), 1.0f } }
                //.color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } }
            },
            { .depthStencil = { .depth = 1.0f, .stencil = 0 } }
        };
        //++A;
        if(A >= 4) A = 0;

        VkCommandBufferBeginInfo GraphicsCommandBufferBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = 0,
            .flags = 0,
            .pInheritanceInfo = 0
        };

        VulkanCheckGoto(vkBeginCommandBuffer(Context->GraphicsCommandBuffer, &GraphicsCommandBufferBeginInfo), label_Error);
        
        VkRenderPassBeginInfo RenderPassBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = 0,
            .renderPass = Context->RenderPass,
            .framebuffer = AcquiredImage.Framebuffer,
            .renderArea = RenderArea,
            .clearValueCount = ArrayCount(RenderClearValues),
            .pClearValues = RenderClearValues // NOTE(blackedout): For VK_ATTACHMENT_LOAD_OP_CLEAR
        };

        VkViewport Viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = (float)AcquiredImage.Extent.width,
            .height = (float)AcquiredImage.Extent.height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };

        VkRect2D Scissors = {
            .offset = { 0, 0 },
            .extent = AcquiredImage.Extent,
        };

        v3 AxisX = {1.0f, 0.0f, 0.0f};
        v3 AxisY = {0.0f, 1.0f, 0.0f};
        m4 ViewRotation = MultiplyM4M4(TranslationM4(0.0f, 0.0f, -8.0f/Context->CamZoom), MultiplyM4M4(RotationM4(AxisX, -Context->CamPol), RotationM4(AxisY, -Context->CamAzi)));
        default_uniform_buffer1 DefaultUniformBuffer1 = {
            .V = TransposeM4(ViewRotation),
            .P = TransposeM4(ProjectionPersp(1.1f, Viewport.width/Viewport.height, 0.01f, 1000.0f)),
            .L = { 0.2f, -1.0f, -0.4f, 0.0f }
        };

        *Context->Shaders.UniformMats[AcquiredImage.DataIndex] = DefaultUniformBuffer1;
        vkCmdBeginRenderPass(Context->GraphicsCommandBuffer, &RenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(Context->GraphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Context->GraphicsPipeline);
        vkCmdSetViewport(Context->GraphicsCommandBuffer, 0, 1, &Viewport);
        vkCmdSetScissor(Context->GraphicsCommandBuffer, 0, 1, &Scissors);

        // Draw plane mesh
        VkDescriptorSet PlaneSets[] = { Context->Shaders.UniformMatsSets[AcquiredImage.DataIndex], Context->Shaders.DefaultImageTileSet };
        vkCmdBindDescriptorSets(Context->GraphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Context->GraphicsPipelineLayout, 0, ArrayCount(PlaneSets), PlaneSets, 0, 0);
        float PlaneScale = 16.0f;
        default_push_constants DefaultPlanePushConstants = {
            .M = {
                PlaneScale, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, PlaneScale, 0.0f,
                0.0f, -0.5f, 0.0f, 1.0f
            },
            .TexM = {
                PlaneScale, 0.0f,
                0.0f, PlaneScale
            },
            .TexT  = { 0.0f, 0.0f }
        };
        vkCmdBindVertexBuffers(Context->GraphicsCommandBuffer, 0, 1, &Context->StaticBuffers.VertexHandle, &Context->PlaneVerticesByteOffset);
        vkCmdBindIndexBuffer(Context->GraphicsCommandBuffer, Context->StaticBuffers.IndexHandle, Context->PlaneIndicesByteOffset, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(Context->GraphicsCommandBuffer, Context->GraphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DefaultPlanePushConstants), &DefaultPlanePushConstants);
        vkCmdDrawIndexed(Context->GraphicsCommandBuffer, ArrayCount(PlaneIndices), 1, 0, 0, 0);

        // Draw cube meshes
        VkDescriptorSet CubeSets[] = { Context->Shaders.UniformMatsSets[AcquiredImage.DataIndex], Context->Shaders.DefaultImageColorSet };
        vkCmdBindDescriptorSets(Context->GraphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Context->GraphicsPipelineLayout, 0, ArrayCount(CubeSets), CubeSets, 0, 0);
        vkCmdBindVertexBuffers(Context->GraphicsCommandBuffer, 0, 1, &Context->StaticBuffers.VertexHandle, &Context->CubeVerticesByteOffset);
        vkCmdBindIndexBuffer(Context->GraphicsCommandBuffer, Context->StaticBuffers.IndexHandle, Context->CubeIndicesByteOffset, VK_INDEX_TYPE_UINT32);
        
        float CubeTexOffsets[] = { 0.25f, 0.5f, 0.75f };
        v2 CubePositions[] = { { -2.5f, -2.5f }, { -0.5f, -0.5f }, { 2.5f, 2.5f }, };
        float CubeHeights[] = { 1.0f, 2.0f, 3.0f };
        for(uint32_t I = 0; I < 3; ++I) {
            default_push_constants DefaultCubePushConstants = {
                .M = {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, CubeHeights[I], 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    CubePositions[I].E[0], 0.5f*(CubeHeights[I] - 1.0f), CubePositions[I].E[1], 1.0f
                },
                .TexM = {
                    0.0f, 0.0f,
                    0.0f, 0.0f
                },
                .TexT  = { CubeTexOffsets[I], 0.0f }
            };
            
            vkCmdPushConstants(Context->GraphicsCommandBuffer, Context->GraphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DefaultCubePushConstants), &DefaultCubePushConstants);
            vkCmdDrawIndexed(Context->GraphicsCommandBuffer, ArrayCount(CubeIndices), 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(Context->GraphicsCommandBuffer);
        VulkanCheckGoto(vkEndCommandBuffer(Context->GraphicsCommandBuffer), label_Error);
    }

    return 0;

label_Error:
    return 1;
}