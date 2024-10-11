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

// MARK: Shaders
static void DestroyShaders(vulkan_surface_device *Device, shaders *Shaders) {
    VkDevice DeviceHandle = Device->Handle;
    vkDestroyDescriptorPool(DeviceHandle, Shaders->DefaultDescriptorPool, 0);
    vkDestroySampler(DeviceHandle, Shaders->DefaultSampler, 0);

    vkDestroyDescriptorPool(DeviceHandle, Shaders->UniformDescriptorPool, 0);
    vkFreeMemory(DeviceHandle, Shaders->UniformBufferMemory, 0);
    for(uint32_t I = 0; I < ArrayCount(Shaders->UniformMatsBuffers); ++I) {
        vkDestroyBuffer(DeviceHandle, Shaders->UniformMatsBuffers[I], 0);
    }

    VulkanDestroyDescriptorSetLayouts(Device, Shaders->DescriptorSetLayouts, ArrayCount(Shaders->DescriptorSetLayouts));
    vkDestroyShaderModule(DeviceHandle, Shaders->Default.Frag, 0);
    vkDestroyShaderModule(DeviceHandle, Shaders->Default.Vert, 0);

    memset(Shaders, 0, sizeof(*Shaders));
}

static int LoadShaders(vulkan_surface_device *Device, shaders *Shaders, vulkan_image *Images) {
    VkDevice DeviceHandle = Device->Handle;
    int Result = 1;
    uint8_t *BytesVS = 0, *BytesFS = 0;
    uint64_t ByteCountVS, ByteCountFS;
    shaders LocalShaders = {0};
    {
        CheckGoto(LoadFileContentsCStd("bin/shaders/default.vert.spv", &BytesVS, &ByteCountVS), label_Exit);
        CheckGoto(LoadFileContentsCStd("bin/shaders/default.frag.spv", &BytesFS, &ByteCountFS), label_Exit);

        CheckGoto(VulkanCreateShaderModule(Device, BytesVS, ByteCountVS, &LocalShaders.Default.Vert), label_Exit);
        CheckGoto(VulkanCreateShaderModule(Device, BytesFS, ByteCountFS, &LocalShaders.Default.Frag), label_VS);

        // NOTE(blackedout): Create all descriptor set layouts
        VkDescriptorSetLayoutBinding DefaultUniformDescriptorSetLayoutBinding[] = {
            { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = 0 }
        };
        VkDescriptorSetLayoutBinding DefaultDescriptorSetLayoutBindings[] = {
            { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = 0 },
            { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = 0 }
        };
        // TODO(blackedout): Why does MSVC have to be so annoying ._. I just want to use array index initializers like in C
        vulkan_descriptor_set_layout_description DescriptorSetDescriptionUniform = { .Flags = 0, .Bindings = DefaultUniformDescriptorSetLayoutBinding, .BindingsCount = ArrayCount(DefaultUniformDescriptorSetLayoutBinding) };
        vulkan_descriptor_set_layout_description DescriptorSetDescriptionSamplerImage = { .Flags = 0, .Bindings = DefaultDescriptorSetLayoutBindings, .BindingsCount = ArrayCount(DefaultDescriptorSetLayoutBindings) };
        vulkan_descriptor_set_layout_description DescriptorSetDescriptions[DESCRIPTOR_SET_LAYOUT_COUNT] = {0};
        DescriptorSetDescriptions[DESCRIPTOR_SET_LAYOUT_DEFAULT_UNIFORM] = DescriptorSetDescriptionUniform;
        DescriptorSetDescriptions[DESCRIPTOR_SET_LAYOUT_DEFAULT_SAMPLER_IMAGE] = DescriptorSetDescriptionSamplerImage;            
        CheckGoto(VulkanCreateDescriptorSetLayouts(Device, DescriptorSetDescriptions, ArrayCount(DescriptorSetDescriptions), LocalShaders.DescriptorSetLayouts), label_FS);
        
        // NOTE(blackedout): Create all uniform buffers mapped with unique descriptor set pool and correctly initialized sets
        vulkan_shader_uniform_buffers_description UniformBufferDescriptions[] = {
            { LocalShaders.UniformMatsBuffers, (void **)LocalShaders.UniformMats, LocalShaders.UniformMatsSets, 0, sizeof(default_uniform_buffer1) }
        };
        CheckGoto(VulkanCreateShaderUniformBuffers(Device, LocalShaders.DescriptorSetLayouts[DESCRIPTOR_SET_LAYOUT_DEFAULT_UNIFORM], UniformBufferDescriptions,
                                                    ArrayCount(UniformBufferDescriptions), &LocalShaders.UniformBufferMemory, &LocalShaders.UniformDescriptorPool), label_DescriptorSetLayouts);
        //StaticAssert(ArrayCount(LocalShaders.UniformBufferDescriptions) == ArrayCount(UniformBufferDescriptions));
        //memcpy(LocalShaders.UniformBufferDescriptions, UniformBufferDescriptions, sizeof(UniformBufferDescriptions));

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
            .anisotropyEnable = Device->Features.samplerAnisotropy,
            .maxAnisotropy = Device->Properties.limits.maxSamplerAnisotropy, // NOTE(blackedout): Spec says ignored if not enabled, so this is fine
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE
        };
        VulkanCheckGoto(vkCreateSampler(DeviceHandle, &DefaultSamplerCreateInfo, 0, &LocalShaders.DefaultSampler), label_UniformBuffers);

        VkDescriptorPoolSize DescriptorPoolSizes[] = {
            { .type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 2 },
            { .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 2 }
        };
        VkDescriptorPoolCreateInfo DescriptorPoolCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .maxSets = 2,
            .poolSizeCount = ArrayCount(DescriptorPoolSizes),
            .pPoolSizes = DescriptorPoolSizes
        };
        VulkanCheckGoto(vkCreateDescriptorPool(DeviceHandle, &DescriptorPoolCreateInfo, 0, &LocalShaders.DefaultDescriptorPool), label_Sampler);
        
        VkDescriptorSetLayout DescriptorSetLayouts[] = {
            LocalShaders.DescriptorSetLayouts[DESCRIPTOR_SET_LAYOUT_DEFAULT_SAMPLER_IMAGE],
            LocalShaders.DescriptorSetLayouts[DESCRIPTOR_SET_LAYOUT_DEFAULT_SAMPLER_IMAGE],
        };
        VkDescriptorSetAllocateInfo DescriptorSetAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = 0,
            .descriptorPool = LocalShaders.DefaultDescriptorPool,
            .descriptorSetCount = ArrayCount(DescriptorSetLayouts),
            .pSetLayouts = DescriptorSetLayouts
        };
        VkDescriptorSet DefaultSets[2];
        VulkanCheckGoto(vkAllocateDescriptorSets(DeviceHandle, &DescriptorSetAllocateInfo, DefaultSets), label_DefaultDescriptorPool);
        LocalShaders.DefaultImageColorSet = DefaultSets[0];
        LocalShaders.DefaultImageTileSet = DefaultSets[1];

        VkImageView ImageViews[2] = { Images[0].ViewHandle, Images[1].ViewHandle };
        for(uint32_t I = 0; I < 2; ++I) {
            VkDescriptorImageInfo ImageInfo = {
                .sampler = VULKAN_NULL_HANDLE,
                .imageView = ImageViews[I],
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            VkDescriptorImageInfo SamplerInfo = {
                .sampler = LocalShaders.DefaultSampler,
            };
            VkWriteDescriptorSet WriteDescriptorSets[] = {
                {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .pNext = 0,
                    .dstSet = DefaultSets[I],
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
                    .dstSet = DefaultSets[I],
                    .dstBinding = 2,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .pImageInfo = &ImageInfo,
                    .pBufferInfo = 0,
                    .pTexelBufferView = 0
                }
            };
            vkUpdateDescriptorSets(DeviceHandle, ArrayCount(WriteDescriptorSets), WriteDescriptorSets, 0, 0);
        }

        *Shaders = LocalShaders;
    }

    Result = 0;
    goto label_Exit;

label_DefaultDescriptorPool:
    vkDestroyDescriptorPool(DeviceHandle, LocalShaders.DefaultDescriptorPool, 0);
label_Sampler:
    vkDestroySampler(DeviceHandle, LocalShaders.DefaultSampler, 0);
label_UniformBuffers:
    vkDestroyDescriptorPool(DeviceHandle, LocalShaders.UniformDescriptorPool, 0);
    vkFreeMemory(DeviceHandle, LocalShaders.UniformBufferMemory, 0);
    for(uint32_t I = 0; I < ArrayCount(LocalShaders.UniformMatsBuffers); ++I) {
        vkDestroyBuffer(DeviceHandle, LocalShaders.UniformMatsBuffers[I], 0);
    }
label_DescriptorSetLayouts:
    VulkanDestroyDescriptorSetLayouts(Device, LocalShaders.DescriptorSetLayouts, ArrayCount(LocalShaders.DescriptorSetLayouts));
label_FS:
    vkDestroyShaderModule(DeviceHandle, LocalShaders.Default.Frag, 0);
label_VS:
    vkDestroyShaderModule(DeviceHandle, LocalShaders.Default.Vert, 0);
label_Exit:
    free(BytesVS);
    free(BytesFS);
    return Result;
}

// MARK: Graphics Pipeline
static void VulkanDestroyDefaultGraphicsPipeline(vulkan_surface_device *Device, VkPipelineLayout PipelineLayout, VkRenderPass RenderPass, VkPipeline Pipeline) {
    VkDevice DeviceHandle = Device->Handle;
    vkDestroyPipeline(DeviceHandle, Pipeline, 0);
    vkDestroyRenderPass(DeviceHandle, RenderPass, 0);
    vkDestroyPipelineLayout(DeviceHandle, PipelineLayout, 0);
}

static int VulkanCreateDefaultGraphicsPipeline(vulkan_surface_device *Device, VkShaderModule ModuleVS, VkShaderModule ModuleFS, VkExtent2D InitialExtent, VkFormat SwapchainFormat, VkSampleCountFlagBits SampleCount, VkPipelineVertexInputStateCreateInfo PipelineVertexInputStateCreateInfo, VkDescriptorSetLayout *DescriptorSetLayouts, uint32_t DescriptorSetLayoutCount, VkPushConstantRange PushConstantRange, VkPipelineLayout *PipelineLayout, VkRenderPass *RenderPass, VkPipeline *Pipeline) {
    VkDevice DeviceHandle = Device->Handle;

    VkPipelineLayout LocalPipelineLayout = 0;
    VkRenderPass LocalRenderPass = 0;
    VkPipeline LocalPipeline = 0;
    {
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
            .cullMode = VK_CULL_MODE_BACK_BIT,
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
            .rasterizationSamples = SampleCount,
            .sampleShadingEnable = VK_FALSE, // TODO(blackedout): Enable this?
            .minSampleShading = 1.0f,
            .pSampleMask = 0,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE
        };
        
        VkStencilOpState EmptyStencilOpState;
        memset(&EmptyStencilOpState, 0, sizeof(EmptyStencilOpState));
        VkPipelineDepthStencilStateCreateInfo PipelineDepthStencilStateCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = EmptyStencilOpState,
            .back = EmptyStencilOpState,
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
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

        VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .setLayoutCount = DescriptorSetLayoutCount,
            .pSetLayouts = DescriptorSetLayouts,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &PushConstantRange,
        };
        VulkanCheckGoto(vkCreatePipelineLayout(DeviceHandle, &PipelineLayoutCreateInfo, 0, &LocalPipelineLayout), label_Error);

        VkAttachmentDescription AttachmentDescriptions[] = {
            {
                .flags = 0,
                .format = SwapchainFormat,
                .samples = SampleCount,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL //,
            },
            {
                .flags = 0,
                .format = Device->BestDepthFormat,
                .samples = SampleCount,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            },
            {
                .flags = 0,
                .format = SwapchainFormat,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            },
        };

        VkAttachmentReference AttachmentRefs[] = {
            {
                .attachment = 0, // NOTE(blackedout): Fragment shader layout index
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            },
            {
                .attachment = 1,
                .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            },
            {
                .attachment = 2,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            }
        };

        VkSubpassDescription SubpassDescription = {
            .flags = 0,
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount = 0,
            .pInputAttachments = 0,
            .colorAttachmentCount = 1,
            .pColorAttachments = AttachmentRefs + 0,
            .pResolveAttachments = AttachmentRefs + 2,
            .pDepthStencilAttachment = AttachmentRefs + 1, // NOTE(blackedout): No count because max one possible
            .preserveAttachmentCount = 0,
            .pPreserveAttachments = 0,
        };

        VkSubpassDependency RenderSubpassDependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0, // NOTE(blackedout): First subpass index
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
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
        
        VulkanCheckGoto(vkCreateRenderPass(DeviceHandle, &RenderPassCreateInfo, 0, &LocalRenderPass), label_PipelineLayout);

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
            .pDepthStencilState = &PipelineDepthStencilStateCreateInfo,
            .pColorBlendState = &PipelineColorBlendStateCreateInfo,
            .pDynamicState = &PipelineDynamicStateCreateInfo,
            .layout = LocalPipelineLayout,
            .renderPass = LocalRenderPass,
            .subpass = 0,
            .basePipelineHandle = VULKAN_NULL_HANDLE,
            .basePipelineIndex = -1
        };

        VulkanCheckGoto(vkCreateGraphicsPipelines(DeviceHandle, VULKAN_NULL_HANDLE, 1, &GraphicsPipelineCreateInfo, 0, &LocalPipeline), label_RenderPass);

        *PipelineLayout = LocalPipelineLayout;
        *RenderPass = LocalRenderPass;
        *Pipeline = LocalPipeline;
    }

    return 0;

label_RenderPass:
    vkDestroyRenderPass(DeviceHandle, LocalRenderPass, 0);
label_PipelineLayout:
    vkDestroyPipelineLayout(DeviceHandle, LocalPipelineLayout, 0);
label_Error:
    return 1;
}