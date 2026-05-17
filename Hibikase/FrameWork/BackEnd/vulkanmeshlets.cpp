/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <algorithm>
#include <array>
#include <cmath>

#include <BackEnd/vulkanbackend.h>

namespace HRHI
{
    template <typename TContainer>
    static bool ArraysAreDifferent(const TContainer& left, const TContainer& right)
    {
        if (left.size() != right.size())
            return true;

        for (size_t i = 0; i < left.size(); i++)
        {
            if (left[i] != right[i])
                return true;
        }

        return false;
    }
    ZWMeshletPipelineHandle ZWVKDevice::CreateMeshletPipeline(const ZWMeshletPipelineDesc& desc, ZWFramebufferInfo const& fbinfo)
    {
        if (!mContext.extensions.EXT_mesh_shader)
        {
            mContext.Warning("Mesh shader is not supported by the Vulkan backend.");
            return nullptr;
        }

        vk::Result res;

        ZWVKMeshletPipeline* pso = new ZWVKMeshletPipeline(mContext);
        pso->desc = desc;
        pso->framebufferInfo = fbinfo;

        ZWVKShader* AS = static_cast<ZWVKShader*>(desc.AS.Get());
        ZWVKShader* MS = static_cast<ZWVKShader*>(desc.MS.Get());
        ZWVKShader* PS = static_cast<ZWVKShader*>(desc.PS.Get());

        size_t numShaders = 0;
        size_t numShadersWithSpecializations = 0;
        size_t numSpecializationConstants = 0;

        // Count the spec constants for all stages
        CountSpecializationConstants(AS, numShaders, numShadersWithSpecializations, numSpecializationConstants);
        CountSpecializationConstants(MS, numShaders, numShadersWithSpecializations, numSpecializationConstants);
        CountSpecializationConstants(PS, numShaders, numShadersWithSpecializations, numSpecializationConstants);

        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
        std::vector<vk::SpecializationInfo> specInfos;
        std::vector<vk::SpecializationMapEntry> specMapEntries;
        std::vector<uint32_t> specData;

        // Allocate buffers for specialization constants and related structures
        // so that shaderStageCreateInfo(...) can directly use pointers inside the vectors
        // because the vectors won't reallocate their buffers
        shaderStages.reserve(numShaders);
        specInfos.reserve(numShadersWithSpecializations);
        specMapEntries.reserve(numSpecializationConstants);
        specData.reserve(numSpecializationConstants);

        // Set up shader stages
        if (desc.AS)
        {
            shaderStages.push_back(MakeShaderStageCreateInfo(AS, 
                specInfos, specMapEntries, specData));
            pso->shaderMask = pso->shaderMask | EShaderType::Vertex;
        }

        if (desc.MS)
        {
            shaderStages.push_back(MakeShaderStageCreateInfo(MS, 
                specInfos, specMapEntries, specData));
            pso->shaderMask = pso->shaderMask | EShaderType::Hull;
        }
        
        if (desc.PS)
        {
            shaderStages.push_back(MakeShaderStageCreateInfo(PS, 
                specInfos, specMapEntries, specData));
            pso->shaderMask = pso->shaderMask | EShaderType::Pixel;
        }

        auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo()
            .setTopology(ConvertPrimitiveTopology(desc.primType));
        
        // fixed function state
        const auto& rasterState = desc.renderState.rasterState;
        const auto& depthStencilState = desc.renderState.depthStencilState;
        const auto& blendState = desc.renderState.blendState;

        auto viewportState = vk::PipelineViewportStateCreateInfo()
            .setViewportCount(1)
            .setScissorCount(1);

        auto rasterizer = vk::PipelineRasterizationStateCreateInfo()
                            // .setDepthClampEnable(??)
                            // .setRasterizerDiscardEnable(??)
                            .setPolygonMode(ConvertFillMode(rasterState.fillMode))
                            .setCullMode(ConvertCullMode(rasterState.cullMode))
                            .setFrontFace(rasterState.frontCounterClockwise ?
                                            vk::FrontFace::eCounterClockwise : vk::FrontFace::eClockwise)
                            .setDepthBiasEnable(rasterState.depthBias ? true : false)
                            .setDepthBiasConstantFactor(float(rasterState.depthBias))
                            .setDepthBiasClamp(rasterState.depthBiasClamp)
                            .setDepthBiasSlopeFactor(rasterState.slopeScaledDepthBias)
                            .setLineWidth(1.0f);
        
        auto multisample = vk::PipelineMultisampleStateCreateInfo()
                            .setRasterizationSamples(vk::SampleCountFlagBits(fbinfo.sampleCount))
                            .setAlphaToCoverageEnable(blendState.alphaToCoverageEnable);

        auto depthStencil = vk::PipelineDepthStencilStateCreateInfo()
                                .setDepthTestEnable(depthStencilState.depthTestEnable)
                                .setDepthWriteEnable(depthStencilState.depthWriteEnable)
                                .setDepthCompareOp(ConvertCompareOp(depthStencilState.depthFunc))
                                .setStencilTestEnable(depthStencilState.stencilEnable)
                                .setFront(ConvertStencilState(depthStencilState, depthStencilState.frontFaceStencil))
                                .setBack(ConvertStencilState(depthStencilState, depthStencilState.backFaceStencil));

        res = CreatePipelineLayout(
            pso->pipelineLayout,
            pso->pipelineBindingLayouts,
            pso->pushConstantVisibility,
            pso->descriptorSetIdxToBindingIdx,
            mContext,
            desc.bindingLayouts);
        CHECK_VK_FAIL(res)

        HCommon::StaticVector<vk::PipelineColorBlendAttachmentState, gMaxRenderTargets> colorBlendAttachments(fbinfo.colorFormats.size());

        for(uint32_t i = 0; i < uint32_t(fbinfo.colorFormats.size()); i++)
        {
            colorBlendAttachments[i] = ConvertBlendState(blendState.targets[i]);
        }

        auto colorBlend = vk::PipelineColorBlendStateCreateInfo()
                            .setAttachmentCount(uint32_t(colorBlendAttachments.size()))
                            .setPAttachments(colorBlendAttachments.data());

        pso->usesBlendConstants = blendState.usesConstantColor(uint32_t(fbinfo.colorFormats.size()));
        
        HCommon::StaticVector<vk::DynamicState, 4> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        if (pso->usesBlendConstants)
            dynamicStates.push_back(vk::DynamicState::eBlendConstants);
        if (pso->desc.renderState.depthStencilState.dynamicStencilRef)
            dynamicStates.push_back(vk::DynamicState::eStencilReference);

        auto dynamicStateInfo = vk::PipelineDynamicStateCreateInfo()
            .setDynamicStateCount(uint32_t(dynamicStates.size()))
            .setPDynamicStates(dynamicStates.data());
            
        std::array<vk::Format, gMaxRenderTargets> colorFormats;
        for (size_t i = 0; i < fbinfo.colorFormats.size(); i++)
            colorFormats[i] = vk::Format(ConvertFormat(fbinfo.colorFormats[i]));

        const ZWFormatInfo& depthStencilFormatInfo = GetFormatInfo(fbinfo.depthFormat);
        vk::Format depthStencilFormat = vk::Format(ConvertFormat(fbinfo.depthFormat));

        auto renderingInfo = vk::PipelineRenderingCreateInfo()
            .setColorAttachmentCount(uint32_t(fbinfo.colorFormats.size()))
            .setPColorAttachmentFormats(colorFormats.data())
            .setDepthAttachmentFormat(depthStencilFormatInfo.hasDepth ? depthStencilFormat : vk::Format::eUndefined)
            .setStencilAttachmentFormat(depthStencilFormatInfo.hasStencil ? depthStencilFormat : vk::Format::eUndefined);

        auto pipelineInfo = vk::GraphicsPipelineCreateInfo()
            .setPNext(&renderingInfo)
            .setStageCount(uint32_t(shaderStages.size()))
            .setPStages(shaderStages.data())
            .setPInputAssemblyState(&inputAssembly)
            .setPViewportState(&viewportState)
            .setPRasterizationState(&rasterizer)
            .setPMultisampleState(&multisample)
            .setPDepthStencilState(&depthStencil)
            .setPColorBlendState(&colorBlend)
            .setPDynamicState(&dynamicStateInfo)
            .setLayout(pso->pipelineLayout)
            .setBasePipelineHandle(nullptr)
            .setBasePipelineIndex(-1);

        res = mContext.device.createGraphicsPipelines(mContext.pipelineCache,
                                                     1, &pipelineInfo,
                                                     mContext.allocationCallbacks,
                                                     &pso->pipeline);

        ASSERT_VK_OK(res); // for debugging
        CHECK_VK_FAIL(res)
        
        return ZWMeshletPipelineHandle::Create(pso);
    }

    ZWVKMeshletPipeline::~ZWVKMeshletPipeline()
    {
        if (pipeline)
        {
            mContext.device.destroyPipeline(pipeline, mContext.allocationCallbacks);
            pipeline = nullptr;
        }

        if (pipelineLayout)
        {
            mContext.device.destroyPipelineLayout(pipelineLayout, mContext.allocationCallbacks);
            pipelineLayout = nullptr;
        }
    }

    HCommon::ZWObject ZWVKMeshletPipeline::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case HRHIObjectTypes::gVKPipelineLayout:
            return HCommon::ZWObject(pipelineLayout);
        case HRHIObjectTypes::gVKPipeline:
            return HCommon::ZWObject(pipeline);
        default:
            return nullptr;
        }
    }

    static vk::Viewport VKViewportWithDXCoords(const ZWViewport& v)
    {
        return vk::Viewport(v.minX, v.maxY, v.maxX - v.minX, -(v.maxY - v.minY), v.minZ, v.maxZ);
    }

    void ZWVKCommandList::SetMeshletState(const ZWMeshletState& state)
    {
        assert(mCurrentCmdBuf);

        ZWVKMeshletPipeline* pso = static_cast<ZWVKMeshletPipeline*>(state.pipeline);
        ZWVKFramebuffer* fb = static_cast<ZWVKFramebuffer*>(state.framebuffer);

        if (m_EnableAutomaticBarriers)
        {
            InsertMeshletResourceBarriers(state);
        }

        bool anyBarriers = this->AnyBarriers();
        bool updatePipeline = false;

        if (mCurrentMeshletState.pipeline != state.pipeline)
        {
            mCurrentCmdBuf->cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pso->pipeline);

            mCurrentCmdBuf->referencedResources.push_back(state.pipeline);
            updatePipeline = true;
        }

        if (mCurrentMeshletState.framebuffer != state.framebuffer || anyBarriers /* because barriers cannot be set inside a renderpass */)
        {
            EndRenderPass();
        }

        CommitBarriers();

        if (!mCurrentMeshletState.framebuffer)
        {
            BeginRenderPass(fb);
        }

        mCurrentPipelineLayout = pso->pipelineLayout;
        mCurrentPushConstantsVisibility = pso->pushConstantVisibility;

        if (ArraysAreDifferent(mCurrentComputeState.bindings, state.bindings) || mAnyVolatileBufferWrites)
        {
            BindBindingSets(vk::PipelineBindPoint::eGraphics, pso->pipelineLayout, state.bindings, pso->descriptorSetIdxToBindingIdx);
        }

        if (!state.viewport.viewports.empty() && ArraysAreDifferent(state.viewport.viewports, mCurrentMeshletState.viewport.viewports))
        {
            HCommon::StaticVector<vk::Viewport, gMaxViewports> viewports;
            for (const auto& vp : state.viewport.viewports)
            {
                viewports.push_back(VKViewportWithDXCoords(vp));
            }

            mCurrentCmdBuf->cmdBuf.setViewport(0, uint32_t(viewports.size()), viewports.data());
        }

        if (!state.viewport.scissorRects.empty() && ArraysAreDifferent(state.viewport.scissorRects, mCurrentMeshletState.viewport.scissorRects))
        {
            HCommon::StaticVector<vk::Rect2D, gMaxViewports> scissors;
            for (const auto& sc : state.viewport.scissorRects)
            {
                scissors.push_back(vk::Rect2D(vk::Offset2D(sc.minX, sc.minY),
                    vk::Extent2D(std::abs(sc.maxX - sc.minX), std::abs(sc.maxY - sc.minY))));
            }

            mCurrentCmdBuf->cmdBuf.setScissor(0, uint32_t(scissors.size()), scissors.data());
        }
        
        if (pso->desc.renderState.depthStencilState.dynamicStencilRef && (updatePipeline || mCurrentMeshletState.dynamicStencilRefValue != state.dynamicStencilRefValue))
        {
            mCurrentCmdBuf->cmdBuf.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, state.dynamicStencilRefValue);
        }

        if (pso->usesBlendConstants && (updatePipeline || mCurrentMeshletState.blendConstantColor != state.blendConstantColor))
        {
            mCurrentCmdBuf->cmdBuf.setBlendConstants(&state.blendConstantColor.r);
        }

        if (state.indirectParams)
        {
            mCurrentCmdBuf->referencedResources.push_back(state.indirectParams);
        }

        mCurrentComputeState = ZWComputeState();
        mCurrentGraphicsState = ZWGraphicsState();
        mCurrentMeshletState = state;
        mCurrentRayTracingState = Hrt::ZWState();
        mAnyVolatileBufferWrites = false;
    }

    void ZWVKCommandList::UpdateMeshletVolatileBuffers()
    {
        if (mAnyVolatileBufferWrites && mCurrentMeshletState.pipeline)
        {
            ZWVKMeshletPipeline* pso = static_cast<ZWVKMeshletPipeline*>(mCurrentMeshletState.pipeline);

            BindBindingSets(vk::PipelineBindPoint::eGraphics, pso->pipelineLayout, mCurrentMeshletState.bindings, pso->descriptorSetIdxToBindingIdx);

            mAnyVolatileBufferWrites = false;
        }
    }

    void ZWVKCommandList::DispatchMesh(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        assert(mCurrentCmdBuf);

        UpdateMeshletVolatileBuffers();

        mCurrentCmdBuf->cmdBuf.drawMeshTasksEXT(groupsX, groupsY, groupsZ);
    }

} // namespace HRHI
