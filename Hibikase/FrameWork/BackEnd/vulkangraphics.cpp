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

    static ETextureDimension getDimensionForFramebuffer(ETextureDimension dimension, bool isArray)
    {
        // Can't render into cubes and 3D textures directly, convert them to 2D arrays
        if (dimension == ETextureDimension::TextureCube || dimension == ETextureDimension::TextureCubeArray || dimension == ETextureDimension::Texture3D)
            dimension = ETextureDimension::Texture2DArray;

        if (!isArray)
        {
            // Demote arrays to single textures if we just need one layer
            switch(dimension)  // NOLINT(clang-diagnostic-switch-enum)
            {
            case ETextureDimension::Texture1DArray:
                dimension = ETextureDimension::Texture1D;
                break;
            case ETextureDimension::Texture2DArray:
                dimension = ETextureDimension::Texture2D;
                break;
            case ETextureDimension::Texture2DMSArray:
                dimension = ETextureDimension::Texture2DMS;
                break;
            default:
                break;
            }
        }

        return dimension;
    }

    ZWFramebufferHandle ZWVKDevice::CreateFramebuffer(const ZWFramebufferDesc& desc)
    {
        ZWVKFramebuffer* fb = new ZWVKFramebuffer();
        fb->desc = desc;
        fb->framebufferInfo = ZWFramebufferInfoEx(desc);

        for(uint32_t i = 0; i < desc.colorAttachments.size(); i++)
        {
            const auto& rt = desc.colorAttachments[i];
            ZWVKTexture* t = static_cast<ZWVKTexture*>(rt.texture);

            assert(fb->framebufferInfo.width == std::max(t->desc.width >> rt.subresources.baseMipLevel, 1u));
            assert(fb->framebufferInfo.height == std::max(t->desc.height >> rt.subresources.baseMipLevel, 1u));

            ZWTextureSubresourceSet subresources = rt.subresources.resolve(t->desc, true);

            ETextureDimension dimension = getDimensionForFramebuffer(t->desc.dimension, subresources.numArraySlices > 1);

            const auto& view = t->GetSubresourceView(subresources, dimension, rt.format, vk::ImageUsageFlagBits::eColorAttachment);
            
            vk::RenderingAttachmentInfo& attachmentInfo = fb->colorAttachments.emplace_back();
            attachmentInfo = vk::RenderingAttachmentInfo()
                .setImageView(view.view)
                .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setLoadOp(vk::AttachmentLoadOp::eLoad)
                .setStoreOp(vk::AttachmentStoreOp::eStore);

            fb->resources.push_back(rt.texture);
        }

        // add depth/stencil attachment if present
        if (desc.depthAttachment.valid())
        {
            const auto& att = desc.depthAttachment;

            ZWVKTexture* texture = static_cast<ZWVKTexture*>(att.texture);

            assert(fb->framebufferInfo.width == std::max(texture->desc.width >> att.subresources.baseMipLevel, 1u));
            assert(fb->framebufferInfo.height == std::max(texture->desc.height >> att.subresources.baseMipLevel, 1u));

            vk::ImageLayout depthLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            if (desc.depthAttachment.isReadOnly)
            {
                depthLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
            }

            ZWTextureSubresourceSet subresources = att.subresources.resolve(texture->desc, true);

            ETextureDimension dimension = getDimensionForFramebuffer(texture->desc.dimension, subresources.numArraySlices > 1);

            const auto& view = texture->GetSubresourceView(subresources, dimension, att.format, vk::ImageUsageFlagBits::eDepthStencilAttachment);

            fb->depthAttachment = vk::RenderingAttachmentInfo()
                .setImageView(view.view)
                .setImageLayout(depthLayout)
                .setLoadOp(vk::AttachmentLoadOp::eLoad)
                .setStoreOp(vk::AttachmentStoreOp::eStore);

            if (GetFormatInfo(texture->desc.format).hasStencil)
                fb->stencilAttachment = fb->depthAttachment;
                
            fb->resources.push_back(att.texture);
        }

        // add VRS attachment
        if (desc.shadingRateAttachment.valid())
        {
            const auto& vrsAttachment = desc.shadingRateAttachment;
            ZWVKTexture* vrsTexture = static_cast<ZWVKTexture*>(vrsAttachment.texture);
            assert(vrsTexture->imageInfo.format == vk::Format::eR8Uint);
            assert(vrsTexture->imageInfo.samples == vk::SampleCountFlagBits::e1);

            ZWTextureSubresourceSet subresources = vrsAttachment.subresources.resolve(vrsTexture->desc, true);
            ETextureDimension dimension = getDimensionForFramebuffer(vrsTexture->desc.dimension, subresources.numArraySlices > 1);

            const auto& view = vrsTexture->GetSubresourceView(subresources, dimension, vrsAttachment.format, vk::ImageUsageFlagBits::eFragmentShadingRateAttachmentKHR);

            auto rateProps = vk::PhysicalDeviceFragmentShadingRatePropertiesKHR();
            auto props = vk::PhysicalDeviceProperties2();
            props.pNext = &rateProps;
            mContext.physicalDevice.getProperties2(&props);

            fb->shadingRateAttachment = vk::RenderingFragmentShadingRateAttachmentInfoKHR()
                .setImageLayout(vk::ImageLayout::eFragmentShadingRateAttachmentOptimalKHR)
                .setImageView(view.view)
                .setShadingRateAttachmentTexelSize(rateProps.minFragmentShadingRateAttachmentTexelSize);

            fb->resources.push_back(vrsAttachment.texture);
        }
        
        return ZWFramebufferHandle::Create(fb);
    }

    void CountSpecializationConstants(
        ZWVKShader* shader,
        size_t& numShaders,
        size_t& numShadersWithSpecializations,
        size_t& numSpecializationConstants)
    {
        if (!shader)
            return;

        numShaders += 1;

        if (shader->specializationConstants.empty())
            return;

        numShadersWithSpecializations += 1;
        numSpecializationConstants += shader->specializationConstants.size();
    }

    vk::PipelineShaderStageCreateInfo MakeShaderStageCreateInfo(
        ZWVKShader* shader,
        std::vector<vk::SpecializationInfo>& specInfos,
        std::vector<vk::SpecializationMapEntry>& specMapEntries,
        std::vector<uint32_t>& specData)
    {
        auto shaderStageCreateInfo = vk::PipelineShaderStageCreateInfo()
            .setStage(shader->stageFlagBits)
            .setModule(shader->shaderModule)
            .setPName(shader->desc.entryName.c_str());

        if (!shader->specializationConstants.empty())
        {
            // For specializations, this functions allocates:
            //  - One entry in specInfos per shader
            //  - One entry in specMapEntries and specData each per constant
            // The vectors are pre-allocated, so it's safe to use .data() before writing the data

            assert(specInfos.data());
            assert(specMapEntries.data());
            assert(specData.data());

            shaderStageCreateInfo.setPSpecializationInfo(specInfos.data() + specInfos.size());
            
            auto specInfo = vk::SpecializationInfo()
                .setPMapEntries(specMapEntries.data() + specMapEntries.size())
                .setMapEntryCount(static_cast<uint32_t>(shader->specializationConstants.size()))
                .setPData(specData.data() + specData.size())
                .setDataSize(shader->specializationConstants.size() * sizeof(uint32_t));

            size_t dataOffset = 0;
            for (const auto& constant : shader->specializationConstants)
            {
                auto specMapEntry = vk::SpecializationMapEntry()
                    .setConstantID(constant.constantID)
                    .setOffset(static_cast<uint32_t>(dataOffset))
                    .setSize(sizeof(uint32_t));

                specMapEntries.push_back(specMapEntry);
                specData.push_back(constant.value.u);
                dataOffset += specMapEntry.size;
            }

            specInfos.push_back(specInfo);
        }

        return shaderStageCreateInfo;
    }

    ZWGraphicsPipelineHandle ZWVKDevice::CreateGraphicsPipeline(const ZWGraphicsPipelineDesc& desc, ZWFramebufferInfo const& fbinfo)
    {
        if (desc.renderState.singlePassStereo.enabled)
        {
            mContext.Error("Single-pass stereo is not supported by the Vulkan backend");
            return nullptr;
        }

        vk::Result res;

        ZWVKInputLayout* inputLayout = static_cast<ZWVKInputLayout*>(desc.inputLayout.Get());

        ZWVKGraphicsPipeline* pso = new ZWVKGraphicsPipeline(mContext);
        pso->desc = desc;
        pso->framebufferInfo = fbinfo;

        ZWVKShader* VS = static_cast<ZWVKShader*>(desc.VS.Get());
        ZWVKShader* HS = static_cast<ZWVKShader*>(desc.HS.Get());
        ZWVKShader* DS = static_cast<ZWVKShader*>(desc.DS.Get());
        ZWVKShader* GS = static_cast<ZWVKShader*>(desc.GS.Get());
        ZWVKShader* PS = static_cast<ZWVKShader*>(desc.PS.Get());

        size_t numShaders = 0;
        size_t numShadersWithSpecializations = 0;
        size_t numSpecializationConstants = 0;

        // Count the spec constants for all stages
        CountSpecializationConstants(VS, numShaders, numShadersWithSpecializations, numSpecializationConstants);
        CountSpecializationConstants(HS, numShaders, numShadersWithSpecializations, numSpecializationConstants);
        CountSpecializationConstants(DS, numShaders, numShadersWithSpecializations, numSpecializationConstants);
        CountSpecializationConstants(GS, numShaders, numShadersWithSpecializations, numSpecializationConstants);
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
        if (desc.VS)
        {
            shaderStages.push_back(MakeShaderStageCreateInfo(VS, 
                specInfos, specMapEntries, specData));
            pso->shaderMask = pso->shaderMask | EShaderType::Vertex;
        }

        if (desc.HS)
        {
            shaderStages.push_back(MakeShaderStageCreateInfo(HS, 
                specInfos, specMapEntries, specData));
            pso->shaderMask = pso->shaderMask | EShaderType::Hull;
        }

        if (desc.DS)
        {
            shaderStages.push_back(MakeShaderStageCreateInfo(DS, 
                specInfos, specMapEntries, specData));
            pso->shaderMask = pso->shaderMask | EShaderType::Domain;
        }

        if (desc.GS)
        {
            shaderStages.push_back(MakeShaderStageCreateInfo(GS, 
                specInfos, specMapEntries, specData));
            pso->shaderMask = pso->shaderMask | EShaderType::Geometry;
        }

        if (desc.PS)
        {
            shaderStages.push_back(MakeShaderStageCreateInfo(PS, 
                specInfos, specMapEntries, specData));
            pso->shaderMask = pso->shaderMask | EShaderType::Pixel;
        }

        // set up vertex input state
        auto vertexInput = vk::PipelineVertexInputStateCreateInfo();
        if (inputLayout)
        {
            vertexInput.setVertexBindingDescriptionCount(uint32_t(inputLayout->bindingDesc.size()))
                       .setPVertexBindingDescriptions(inputLayout->bindingDesc.data())
                       .setVertexAttributeDescriptionCount(uint32_t(inputLayout->attributeDesc.size()))
                       .setPVertexAttributeDescriptions(inputLayout->attributeDesc.data());
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
        
        // Conservative raster state
        auto conservativeRasterState = vk::PipelineRasterizationConservativeStateCreateInfoEXT()
            .setConservativeRasterizationMode(vk::ConservativeRasterizationModeEXT::eOverestimate);
		if (rasterState.conservativeRasterEnable)
		{
			rasterizer.setPNext(&conservativeRasterState);
		}

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

        // VRS state
        std::array<vk::FragmentShadingRateCombinerOpKHR, 2> combiners = {
            ConvertShadingRateCombiner(desc.shadingRateState.pipelinePrimitiveCombiner),
            ConvertShadingRateCombiner(desc.shadingRateState.imageCombiner)
        };
        auto shadingRateState = vk::PipelineFragmentShadingRateStateCreateInfoKHR()
            .setCombinerOps(combiners)
            .setFragmentSize(ConvertFragmentShadingRate(desc.shadingRateState.shadingRate));

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

        HCommon::StaticVector<vk::DynamicState, 5> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        if (pso->usesBlendConstants)
            dynamicStates.push_back(vk::DynamicState::eBlendConstants);
        if (pso->desc.renderState.depthStencilState.dynamicStencilRef)
            dynamicStates.push_back(vk::DynamicState::eStencilReference);
        if (pso->desc.shadingRateState.enabled)
            dynamicStates.push_back(vk::DynamicState::eFragmentShadingRateKHR);

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
            .setPVertexInputState(&vertexInput)
            .setPInputAssemblyState(&inputAssembly)
            .setPViewportState(&viewportState)
            .setPRasterizationState(&rasterizer)
            .setPMultisampleState(&multisample)
            .setPDepthStencilState(&depthStencil)
            .setPColorBlendState(&colorBlend)
            .setPDynamicState(&dynamicStateInfo)
            .setLayout(pso->pipelineLayout)
            .setBasePipelineHandle(nullptr)
            .setBasePipelineIndex(-1)
            .setPTessellationState(nullptr);

        if (pso->desc.shadingRateState.enabled)
            renderingInfo.setPNext(&shadingRateState);

        auto tessellationState = vk::PipelineTessellationStateCreateInfo();

        if (desc.primType == EPrimitiveType::PatchList)
        {
            tessellationState.setPatchControlPoints(desc.patchControlPoints);
            pipelineInfo.setPTessellationState(&tessellationState);
        }

        res = mContext.device.createGraphicsPipelines(mContext.pipelineCache,
                                                     1, &pipelineInfo,
                                                     mContext.allocationCallbacks,
                                                     &pso->pipeline);
        ASSERT_VK_OK(res); // for debugging
        CHECK_VK_FAIL(res);
        
        return ZWGraphicsPipelineHandle::Create(pso);
    }

    ZWVKGraphicsPipeline::~ZWVKGraphicsPipeline()
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

    HCommon::ZWObject ZWVKGraphicsPipeline::GetNativeObject(ObjectType objectType)
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

    void ZWVKCommandList::BeginRenderPass(HRHI::IFramebuffer* _framebuffer)
    {
        EndRenderPass();

        ZWVKFramebuffer* framebuffer = static_cast<ZWVKFramebuffer*>(_framebuffer);

        if (!framebuffer)
            return;

        mCurrentGraphicsState.framebuffer = framebuffer;
        mCurrentMeshletState.framebuffer = framebuffer;

        vk::RenderingInfo renderingInfo = vk::RenderingInfo()
            .setRenderArea(vk::Rect2D()
                .setOffset(vk::Offset2D(0, 0))
                .setExtent(vk::Extent2D(framebuffer->framebufferInfo.width, framebuffer->framebufferInfo.height)))
            .setLayerCount(framebuffer->framebufferInfo.arraySize)
            .setColorAttachmentCount(uint32_t(framebuffer->colorAttachments.size()))
            .setPColorAttachments(framebuffer->colorAttachments.data())
            .setPDepthAttachment(framebuffer->depthAttachment.imageView ? &framebuffer->depthAttachment : nullptr)
            .setPStencilAttachment(framebuffer->stencilAttachment.imageView ? &framebuffer->stencilAttachment : nullptr);
        
        mCurrentCmdBuf->cmdBuf.beginRendering(renderingInfo);
        mCurrentCmdBuf->referencedResources.push_back(framebuffer);
    }

    void ZWVKCommandList::EndRenderPass()
    {
        if (mCurrentGraphicsState.framebuffer || mCurrentMeshletState.framebuffer)
        {
            mCurrentCmdBuf->cmdBuf.endRendering();
            mCurrentGraphicsState.framebuffer = nullptr;
            mCurrentMeshletState.framebuffer = nullptr;
        }
    }

    static vk::Viewport VKViewportWithDXCoords(const ZWViewport& v)
    {
        return vk::Viewport(v.minX, v.maxY, v.maxX - v.minX, -(v.maxY - v.minY), v.minZ, v.maxZ);
    }

    void ZWVKCommandList::SetGraphicsState(const ZWGraphicsState& state)
    {
        assert(mCurrentCmdBuf);

        ZWVKGraphicsPipeline* pso = static_cast<ZWVKGraphicsPipeline*>(state.pipeline);
        ZWVKFramebuffer* fb = static_cast<ZWVKFramebuffer*>(state.framebuffer);

        if (m_EnableAutomaticBarriers)
        {
            InsertGraphicsResourceBarriers(state);
        }

        bool anyBarriers = this->AnyBarriers();
        bool updatePipeline = false;

        if (mCurrentGraphicsState.pipeline != state.pipeline)
        {
            mCurrentCmdBuf->cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pso->pipeline);

            mCurrentCmdBuf->referencedResources.push_back(state.pipeline);
            updatePipeline = true;
        }

        if (mCurrentGraphicsState.framebuffer != state.framebuffer || anyBarriers /* because barriers cannot be set inside a renderpass */)
        {
            EndRenderPass();
        }

        CommitBarriers();

        if (!mCurrentGraphicsState.framebuffer)
        {
            BeginRenderPass(fb);
        }

        mCurrentPipelineLayout = pso->pipelineLayout;
        mCurrentPushConstantsVisibility = pso->pushConstantVisibility;

        if (ArraysAreDifferent(mCurrentGraphicsState.bindings, state.bindings) || mAnyVolatileBufferWrites)
        {
            BindBindingSets(vk::PipelineBindPoint::eGraphics, pso->pipelineLayout, state.bindings, pso->descriptorSetIdxToBindingIdx);
        }

        if (!state.viewport.viewports.empty() && ArraysAreDifferent(state.viewport.viewports, mCurrentGraphicsState.viewport.viewports))
        {
            HCommon::StaticVector<vk::Viewport, gMaxViewports> viewports;
            for (const auto& vp : state.viewport.viewports)
            {
                viewports.push_back(VKViewportWithDXCoords(vp));
            }

            mCurrentCmdBuf->cmdBuf.setViewport(0, uint32_t(viewports.size()), viewports.data());
        }

        if (!state.viewport.scissorRects.empty() && ArraysAreDifferent(state.viewport.scissorRects, mCurrentGraphicsState.viewport.scissorRects))
        {
            HCommon::StaticVector<vk::Rect2D, gMaxViewports> scissors;
            for (const auto& sc : state.viewport.scissorRects)
            {
                scissors.push_back(vk::Rect2D(vk::Offset2D(sc.minX, sc.minY),
                    vk::Extent2D(std::abs(sc.maxX - sc.minX), std::abs(sc.maxY - sc.minY))));
            }

            mCurrentCmdBuf->cmdBuf.setScissor(0, uint32_t(scissors.size()), scissors.data());
        }

        if (pso->desc.renderState.depthStencilState.dynamicStencilRef && (updatePipeline || mCurrentGraphicsState.dynamicStencilRefValue != state.dynamicStencilRefValue))
        {
            mCurrentCmdBuf->cmdBuf.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, state.dynamicStencilRefValue);
        }

        if (pso->usesBlendConstants && (updatePipeline || mCurrentGraphicsState.blendConstantColor != state.blendConstantColor))
        {
            mCurrentCmdBuf->cmdBuf.setBlendConstants(&state.blendConstantColor.r);
        }

        if (state.indexBuffer.buffer && mCurrentGraphicsState.indexBuffer != state.indexBuffer)
        {
            mCurrentCmdBuf->cmdBuf.bindIndexBuffer(static_cast<ZWVKBuffer*>(state.indexBuffer.buffer)->buffer,
                state.indexBuffer.offset,
                state.indexBuffer.format == EFormat::R16_UINT ?
                vk::IndexType::eUint16 : vk::IndexType::eUint32);

            mCurrentCmdBuf->referencedResources.push_back(state.indexBuffer.buffer);
        }

        if (!state.vertexBuffers.empty() && ArraysAreDifferent(state.vertexBuffers, mCurrentGraphicsState.vertexBuffers))
        {
            vk::Buffer vertexBuffers[gMaxVertexAttributes];
            vk::DeviceSize vertexBufferOffsets[gMaxVertexAttributes];
            uint32_t maxVbIndex = 0;

            for (const auto& binding : state.vertexBuffers)
            {
                // This is tested by the validation layer, skip invalid slots here if VL is not used.
                if (binding.slot >= gMaxVertexAttributes)
                    continue;

                vertexBuffers[binding.slot] = static_cast<ZWVKBuffer*>(binding.buffer)->buffer;
                vertexBufferOffsets[binding.slot] = vk::DeviceSize(binding.offset);
                maxVbIndex = std::max(maxVbIndex, binding.slot);

                mCurrentCmdBuf->referencedResources.push_back(binding.buffer);
            }

            mCurrentCmdBuf->cmdBuf.bindVertexBuffers(0, maxVbIndex + 1, vertexBuffers, vertexBufferOffsets);
        }

        if (state.indirectParams)
        {
            mCurrentCmdBuf->referencedResources.push_back(state.indirectParams);
        }
        
        if (state.indirectCountBuffer && state.indirectCountBuffer != state.indirectParams)
        {
            mCurrentCmdBuf->referencedResources.push_back(state.indirectCountBuffer);
        }

        if (state.shadingRateState.enabled)
        {
            vk::FragmentShadingRateCombinerOpKHR combiners[2] = { ConvertShadingRateCombiner(state.shadingRateState.pipelinePrimitiveCombiner), ConvertShadingRateCombiner(state.shadingRateState.imageCombiner) };
            vk::Extent2D shadingRate = ConvertFragmentShadingRate(state.shadingRateState.shadingRate);
            mCurrentCmdBuf->cmdBuf.setFragmentShadingRateKHR(&shadingRate, combiners);
        }

        mCurrentGraphicsState = state;
        mCurrentComputeState = ZWComputeState();
        mCurrentMeshletState = ZWMeshletState();
        mCurrentRayTracingState = Hrt::ZWState();
        mAnyVolatileBufferWrites = false;
    }

    void ZWVKCommandList::UpdateGraphicsVolatileBuffers()
    {
        if (mAnyVolatileBufferWrites && mCurrentGraphicsState.pipeline)
        {
            ZWVKGraphicsPipeline* pso = static_cast<ZWVKGraphicsPipeline*>(mCurrentGraphicsState.pipeline);

            BindBindingSets(vk::PipelineBindPoint::eGraphics, pso->pipelineLayout, mCurrentGraphicsState.bindings, pso->descriptorSetIdxToBindingIdx);

            mAnyVolatileBufferWrites = false;
        }
    }

    void ZWVKCommandList::Draw(const ZWDrawArguments& args)
    {
        assert(mCurrentCmdBuf);

        UpdateGraphicsVolatileBuffers();

        mCurrentCmdBuf->cmdBuf.draw(args.vertexCount,
            args.instanceCount,
            args.startVertexLocation,
            args.startInstanceLocation);
    }

    void ZWVKCommandList::DrawIndexed(const ZWDrawArguments& args)
    {
        assert(mCurrentCmdBuf);

        UpdateGraphicsVolatileBuffers();

        mCurrentCmdBuf->cmdBuf.drawIndexed(args.vertexCount,
            args.instanceCount,
            args.startIndexLocation,
            args.startVertexLocation,
            args.startInstanceLocation);
    }

    void ZWVKCommandList::DrawIndirect(uint32_t offsetBytes, uint32_t drawCount)
    {
        assert(mCurrentCmdBuf);

        UpdateGraphicsVolatileBuffers();

        ZWVKBuffer* indirectParams = static_cast<ZWVKBuffer*>(mCurrentGraphicsState.indirectParams);
        assert(indirectParams);

        mCurrentCmdBuf->cmdBuf.drawIndirect(indirectParams->buffer, offsetBytes, drawCount, sizeof(ZWDrawIndirectArguments));
    }

    void ZWVKCommandList::DrawIndexedIndirect(uint32_t offsetBytes, uint32_t drawCount)
    {
        assert(mCurrentCmdBuf);

        UpdateGraphicsVolatileBuffers();

        ZWVKBuffer* indirectParams = static_cast<ZWVKBuffer*>(mCurrentGraphicsState.indirectParams);
        assert(indirectParams);

        mCurrentCmdBuf->cmdBuf.drawIndexedIndirect(indirectParams->buffer, offsetBytes, drawCount, sizeof(ZWDrawIndexedIndirectArguments));
    }

    void ZWVKCommandList::DrawIndexedIndirectCount(uint32_t paramOffsetBytes, uint32_t countOffsetBytes, uint32_t maxDrawCount)
    {
        assert(mCurrentCmdBuf);

        UpdateGraphicsVolatileBuffers();

        ZWVKBuffer* paramBuffer = static_cast<ZWVKBuffer*>(mCurrentGraphicsState.indirectParams);
        ZWVKBuffer* countBuffer = static_cast<ZWVKBuffer*>(mCurrentGraphicsState.indirectCountBuffer);
        assert(paramBuffer);
        assert(countBuffer);

        mCurrentCmdBuf->cmdBuf.drawIndexedIndirectCount(
            paramBuffer->buffer,
            paramOffsetBytes,
            countBuffer->buffer,
            countOffsetBytes,
            maxDrawCount,
            sizeof(ZWDrawIndexedIndirectArguments)
        );
    }

} // namespace HRHI
