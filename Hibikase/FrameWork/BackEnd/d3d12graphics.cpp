#include <BackEnd/d3d12backend.h>

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstring>
#include <sstream>

namespace HRHI::HD3D12
{
    namespace
    {
        constexpr ObjectType cD3D12RootSignatureObjectType = 0x00020009u;
        constexpr ObjectType cD3D12PipelineStateObjectType = 0x0002000au;

        D3D12_SHADER_BYTECODE GetShaderBytecode(IShader* shader)
        {
            if (shader == nullptr)
            {
                return {};
            }

            const void* bytecode = nullptr;
            size_t bytecodeSize = 0;
            shader->GetBytecode(&bytecode, &bytecodeSize);

            if (bytecode == nullptr || bytecodeSize == 0)
            {
                return {};
            }

            return { bytecode, bytecodeSize };
        }

        template <typename TContainer>
        bool ArraysAreDifferent(const TContainer& left, const TContainer& right)
        {
            if (left.size() != right.size())
            {
                return true;
            }

            for (size_t index = 0; index < left.size(); ++index)
            {
                if (left[index] != right[index])
                {
                    return true;
                }
            }

            return false;
        }

        template <typename TContainer>
        uint32_t ArrayDifferenceMask(const TContainer& left, const TContainer& right)
        {
            const size_t maxSize = std::max(left.size(), right.size());
            uint32_t differenceMask = 0;

            for (size_t index = 0; index < maxSize; ++index)
            {
                const bool isSame = index < left.size()
                    && index < right.size()
                    && left[index] == right[index];

                if (!isSame)
                {
                    differenceMask |= (1u << index);
                }
            }

            return differenceMask;
        }
    }

    HCommon::ZWObject ZWD3D12GraphicsPipeline::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case cD3D12RootSignatureObjectType:
            return rootSignature != nullptr ? rootSignature->GetNativeObject(objectType) : nullptr;

        case cD3D12PipelineStateObjectType:
            return HCommon::ZWObject(pipelineState.Get());

        default:
            return nullptr;
        }
    }

    HCommon::RefCountPtr<ID3D12PipelineState> ZWD3D12Device::CreatePipelineState(
        const ZWGraphicsPipelineDesc& state,
        ZWD3D12RootSignature* rootSignature,
        const ZWFramebufferInfo& framebufferInfo) const
    {
        if (rootSignature == nullptr)
        {
            return nullptr;
        }

        if (state.renderState.singlePassStereo.enabled && !SupportsFeature(EFeature::SinglePassStereo))
        {
            mContext.Error("Single-pass stereo is not supported by this device.");
            return nullptr;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {};
        pipelineStateDesc.pRootSignature = rootSignature->handle.Get();
        pipelineStateDesc.VS = GetShaderBytecode(state.VS.Get());
        pipelineStateDesc.HS = GetShaderBytecode(state.HS.Get());
        pipelineStateDesc.DS = GetShaderBytecode(state.DS.Get());
        pipelineStateDesc.GS = GetShaderBytecode(state.GS.Get());
        pipelineStateDesc.PS = GetShaderBytecode(state.PS.Get());

        TranslateBlendState(state.renderState.blendState, pipelineStateDesc.BlendState);

        const ZWDepthStencilState& depthState = state.renderState.depthStencilState;
        TranslateDepthStencilState(depthState, pipelineStateDesc.DepthStencilState);

        if ((depthState.depthTestEnable || depthState.stencilEnable) && framebufferInfo.depthFormat == EFormat::UNKNOWN)
        {
            pipelineStateDesc.DepthStencilState.DepthEnable = FALSE;
            pipelineStateDesc.DepthStencilState.StencilEnable = FALSE;

            if (mContext.messageCallback != nullptr)
            {
                mContext.messageCallback->message(
                    EMessageSeverity::Warning,
                    "depthTestEnable or stencilEnable is true, but no depth target is bound.");
            }
        }

        const ZWRasterState& rasterState = state.renderState.rasterState;
        TranslateRasterizerState(rasterState, pipelineStateDesc.RasterizerState);

        switch (state.primType)
        {
        case EPrimitiveType::PointList:
            pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            break;

        case EPrimitiveType::LineList:
        case EPrimitiveType::LineStrip:
            pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            break;

        case EPrimitiveType::TriangleList:
        case EPrimitiveType::TriangleStrip:
        case EPrimitiveType::TriangleFan:
        case EPrimitiveType::TriangleListWithAdjacency:
        case EPrimitiveType::TriangleStripWithAdjacency:
            pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            break;

        case EPrimitiveType::PatchList:
            pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
            break;

        default:
            mContext.Error("Primitive type is unsupported by the D3D12 graphics pipeline.");
            return nullptr;
        }

        pipelineStateDesc.DSVFormat = GetDxgiFormatMapping(framebufferInfo.depthFormat).rtvFormat;
        pipelineStateDesc.SampleDesc.Count = framebufferInfo.sampleCount;
        pipelineStateDesc.SampleDesc.Quality = framebufferInfo.sampleQuality;

        for (uint32_t renderTargetIndex = 0; renderTargetIndex < static_cast<uint32_t>(framebufferInfo.colorFormats.size()); ++renderTargetIndex)
        {
            pipelineStateDesc.RTVFormats[renderTargetIndex] =
                GetDxgiFormatMapping(framebufferInfo.colorFormats[renderTargetIndex]).rtvFormat;
        }

        ZWD3D12InputLayout* inputLayout = static_cast<ZWD3D12InputLayout*>(state.inputLayout.Get());
        if (inputLayout != nullptr && !inputLayout->inputElements.empty())
        {
            pipelineStateDesc.InputLayout.NumElements = static_cast<UINT>(inputLayout->inputElements.size());
            pipelineStateDesc.InputLayout.pInputElementDescs = inputLayout->inputElements.data();
        }

        pipelineStateDesc.NumRenderTargets = static_cast<UINT>(framebufferInfo.colorFormats.size());
        pipelineStateDesc.SampleMask = ~0u;

        HCommon::RefCountPtr<ID3D12PipelineState> pipelineState;
#if HRHI_D3D12_WITH_NVAPI
        std::vector<const NVAPI_D3D12_PSO_EXTENSION_DESC*> extensions;

        const auto appendShaderExtensions = [&extensions](IShader* shader)
        {
            ZWD3D12Shader* d3d12Shader = static_cast<ZWD3D12Shader*>(shader);
            if (d3d12Shader != nullptr && !d3d12Shader->extensions.empty())
            {
                extensions.insert(extensions.end(), d3d12Shader->extensions.begin(), d3d12Shader->extensions.end());
            }
        };

        appendShaderExtensions(state.VS.Get());
        appendShaderExtensions(state.HS.Get());
        appendShaderExtensions(state.DS.Get());
        appendShaderExtensions(state.GS.Get());
        appendShaderExtensions(state.PS.Get());

        const bool hasNvapiRasterizerState = rasterState.programmableSamplePositionsEnable || rasterState.quadFillEnable;
        const bool supportsNvapiRasterizerExtensions = SupportsFeature(EFeature::NvApiRasterizerExtensions);
        NVAPI_D3D12_PSO_RASTERIZER_STATE_DESC rasterizerDesc = {};
        if (supportsNvapiRasterizerExtensions && hasNvapiRasterizerState)
        {
            rasterizerDesc.baseVersion = NV_PSO_EXTENSION_DESC_VER;
            rasterizerDesc.psoExtension = NV_PSO_RASTER_EXTENSION;
            rasterizerDesc.version = NV_RASTERIZER_PSO_EXTENSION_DESC_VER;
            rasterizerDesc.ProgrammableSamplePositionsEnable = rasterState.programmableSamplePositionsEnable;
            rasterizerDesc.SampleCount = rasterState.forcedSampleCount;
            std::memcpy(rasterizerDesc.SamplePositionsX, rasterState.samplePositionsX, sizeof(rasterState.samplePositionsX));
            std::memcpy(rasterizerDesc.SamplePositionsY, rasterState.samplePositionsY, sizeof(rasterState.samplePositionsY));
            rasterizerDesc.QuadFillMode = rasterState.quadFillEnable
                ? NVAPI_QUAD_FILLMODE_BBOX
                : NVAPI_QUAD_FILLMODE_DISABLED;
            extensions.push_back(reinterpret_cast<const NVAPI_D3D12_PSO_EXTENSION_DESC*>(&rasterizerDesc));
        }

        if (!supportsNvapiRasterizerExtensions && hasNvapiRasterizerState && mContext.messageCallback != nullptr)
        {
            mContext.messageCallback->message(
                EMessageSeverity::Warning,
                "NVAPI rasterizer extensions are unavailable on this machine and will be ignored.");
        }

        if (!extensions.empty())
        {
            const NvAPI_Status status = NvAPI_D3D12_CreateGraphicsPipelineState(
                mContext.device.Get(),
                &pipelineStateDesc,
                static_cast<NvU32>(extensions.size()),
                extensions.data(),
                pipelineState.ReleaseAndGetAddressOf());

            if (status != NVAPI_OK || pipelineState == nullptr)
            {
                mContext.Error("Failed to create a graphics pipeline state object with NVAPI extensions.");
                return nullptr;
            }

            return pipelineState;
        }
#endif

        const HRESULT result = mContext.device->CreateGraphicsPipelineState(
            &pipelineStateDesc,
            IID_PPV_ARGS(pipelineState.ReleaseAndGetAddressOf()));

        if (FAILED(result))
        {
            std::stringstream messageBuilder;
            messageBuilder << "Failed to create a graphics pipeline state object, HRESULT = 0x"
                << std::hex << result;
            mContext.Error(messageBuilder.str());
            return nullptr;
        }

        return pipelineState;
    }

    ZWGraphicsPipelineHandle ZWD3D12Device::CreateGraphicsPipeline(const ZWGraphicsPipelineDesc& desc, ZWFramebufferInfo const& framebufferInfo)
    {
        HCommon::RefCountPtr<ZWD3D12RootSignature> rootSignature = GetRootSignature(desc.bindingLayouts, desc.inputLayout != nullptr);
        HCommon::RefCountPtr<ID3D12PipelineState> pipelineState = CreatePipelineState(desc, rootSignature.Get(), framebufferInfo);

        return CreateHandleForNativeGraphicsPipeline(rootSignature.Get(), pipelineState.Get(), desc, framebufferInfo);
    }

    ZWGraphicsPipelineHandle ZWD3D12Device::CreateHandleForNativeGraphicsPipeline(
        IRootSignature* rootSignature,
        ID3D12PipelineState* pipelineState,
        const ZWGraphicsPipelineDesc& desc,
        const ZWFramebufferInfo& framebufferInfo)
    {
        if (rootSignature == nullptr || pipelineState == nullptr)
        {
            return nullptr;
        }

        ZWD3D12GraphicsPipeline* graphicsPipeline = new ZWD3D12GraphicsPipeline();
        graphicsPipeline->desc = desc;
        graphicsPipeline->framebufferInfo = framebufferInfo;
        graphicsPipeline->rootSignature = static_cast<ZWD3D12RootSignature*>(rootSignature);
        graphicsPipeline->pipelineState = pipelineState;
        graphicsPipeline->requiresBlendFactor =
            desc.renderState.blendState.usesConstantColor(static_cast<uint32_t>(graphicsPipeline->framebufferInfo.colorFormats.size()));

        return ZWGraphicsPipelineHandle::Create(graphicsPipeline);
    }

    ZWFramebufferHandle ZWD3D12Device::CreateFramebuffer(const ZWFramebufferDesc& desc)
    {
        ZWD3D12Framebuffer* framebuffer = new ZWD3D12Framebuffer(mResources);
        framebuffer->desc = desc;
        framebuffer->framebufferInfo = ZWFramebufferInfoEx(desc);

        if (!desc.colorAttachments.empty())
        {
            ZWD3D12Texture* texture = static_cast<ZWD3D12Texture*>(desc.colorAttachments[0].texture);
            if (texture == nullptr)
            {
                delete framebuffer;
                return nullptr;
            }

            framebuffer->rtWidth = texture->desc.width;
            framebuffer->rtHeight = texture->desc.height;
        }
        else if (desc.depthAttachment.valid())
        {
            ZWD3D12Texture* texture = static_cast<ZWD3D12Texture*>(desc.depthAttachment.texture);
            if (texture == nullptr)
            {
                delete framebuffer;
                return nullptr;
            }

            framebuffer->rtWidth = texture->desc.width;
            framebuffer->rtHeight = texture->desc.height;
        }

        for (size_t renderTargetIndex = 0; renderTargetIndex < desc.colorAttachments.size(); ++renderTargetIndex)
        {
            const ZWFramebufferAttachment& attachment = desc.colorAttachments[renderTargetIndex];
            ZWD3D12Texture* texture = static_cast<ZWD3D12Texture*>(attachment.texture);
            if (texture == nullptr)
            {
                delete framebuffer;
                return nullptr;
            }

            assert(texture->desc.width == framebuffer->rtWidth);
            assert(texture->desc.height == framebuffer->rtHeight);

            const DescriptorIndex descriptorIndex = mResources.renderTargetViewHeap.AllocateDescriptor();
            const D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = mResources.renderTargetViewHeap.GetCpuHandle(descriptorIndex);

            texture->CreateRTV(descriptorHandle.ptr, attachment.format, attachment.subresources);
            framebuffer->RTVs.push_back(descriptorIndex);
            framebuffer->textures.push_back(texture);
        }

        if (desc.depthAttachment.valid())
        {
            ZWD3D12Texture* texture = static_cast<ZWD3D12Texture*>(desc.depthAttachment.texture);
            if (texture == nullptr)
            {
                delete framebuffer;
                return nullptr;
            }

            assert(texture->desc.width == framebuffer->rtWidth);
            assert(texture->desc.height == framebuffer->rtHeight);

            const DescriptorIndex descriptorIndex = mResources.depthStencilViewHeap.AllocateDescriptor();
            const D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = mResources.depthStencilViewHeap.GetCpuHandle(descriptorIndex);

            texture->CreateDSV(descriptorHandle.ptr, desc.depthAttachment.subresources, desc.depthAttachment.isReadOnly);
            framebuffer->DSV = descriptorIndex;
            framebuffer->textures.push_back(texture);
        }

        return ZWFramebufferHandle::Create(framebuffer);
    }

    ZWD3D12Framebuffer::~ZWD3D12Framebuffer()
    {
        for (DescriptorIndex renderTargetView : RTVs)
        {
            mResources.renderTargetViewHeap.ReleaseDescriptor(renderTargetView);
        }

        if (DSV != cInvalidDescriptorIndex)
        {
            mResources.depthStencilViewHeap.ReleaseDescriptor(DSV);
        }
    }

    void TranslateBlendState(const ZWBlendState& inState, D3D12_BLEND_DESC& outState)
    {
        outState.AlphaToCoverageEnable = inState.alphaToCoverageEnable ? TRUE : FALSE;
        outState.IndependentBlendEnable = TRUE;

        for (uint32_t index = 0; index < gMaxRenderTargets; ++index)
        {
            const ZWBlendState::ZWRenderTarget& source = inState.targets[index];
            D3D12_RENDER_TARGET_BLEND_DESC& target = outState.RenderTarget[index];

            target.BlendEnable = source.blendEnable ? TRUE : FALSE;
            target.LogicOpEnable = FALSE;
            target.SrcBlend = ConvertBlendValue(source.srcBlend);
            target.DestBlend = ConvertBlendValue(source.destBlend);
            target.BlendOp = ConvertBlendOp(source.blendOp);
            target.SrcBlendAlpha = ConvertBlendValue(source.srcBlendAlpha);
            target.DestBlendAlpha = ConvertBlendValue(source.destBlendAlpha);
            target.BlendOpAlpha = ConvertBlendOp(source.blendOpAlpha);
            target.LogicOp = D3D12_LOGIC_OP_NOOP;
            target.RenderTargetWriteMask = static_cast<D3D12_COLOR_WRITE_ENABLE>(source.colorWriteMask);
        }
    }

    void TranslateDepthStencilState(const ZWDepthStencilState& inState, D3D12_DEPTH_STENCIL_DESC& outState)
    {
        outState.DepthEnable = inState.depthTestEnable ? TRUE : FALSE;
        outState.DepthWriteMask = inState.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        outState.DepthFunc = ConvertComparisonFunc(inState.depthFunc);
        outState.StencilEnable = inState.stencilEnable ? TRUE : FALSE;
        outState.StencilReadMask = inState.stencilReadMask;
        outState.StencilWriteMask = inState.stencilWriteMask;
        outState.FrontFace.StencilFailOp = ConvertStencilOp(inState.frontFaceStencil.failOp);
        outState.FrontFace.StencilDepthFailOp = ConvertStencilOp(inState.frontFaceStencil.depthFailOp);
        outState.FrontFace.StencilPassOp = ConvertStencilOp(inState.frontFaceStencil.passOp);
        outState.FrontFace.StencilFunc = ConvertComparisonFunc(inState.frontFaceStencil.stencilFunc);
        outState.BackFace.StencilFailOp = ConvertStencilOp(inState.backFaceStencil.failOp);
        outState.BackFace.StencilDepthFailOp = ConvertStencilOp(inState.backFaceStencil.depthFailOp);
        outState.BackFace.StencilPassOp = ConvertStencilOp(inState.backFaceStencil.passOp);
        outState.BackFace.StencilFunc = ConvertComparisonFunc(inState.backFaceStencil.stencilFunc);
    }

    void TranslateRasterizerState(const ZWRasterState& inState, D3D12_RASTERIZER_DESC& outState)
    {
        switch (inState.fillMode)
        {
        case ERasterFillMode::Solid:
            outState.FillMode = D3D12_FILL_MODE_SOLID;
            break;
        case ERasterFillMode::Wireframe:
            outState.FillMode = D3D12_FILL_MODE_WIREFRAME;
            break;
        }

        switch (inState.cullMode)
        {
        case ERasterCullMode::Back:
            outState.CullMode = D3D12_CULL_MODE_BACK;
            break;
        case ERasterCullMode::Front:
            outState.CullMode = D3D12_CULL_MODE_FRONT;
            break;
        case ERasterCullMode::None:
            outState.CullMode = D3D12_CULL_MODE_NONE;
            break;
        }

        outState.FrontCounterClockwise = inState.frontCounterClockwise ? TRUE : FALSE;
        outState.DepthBias = inState.depthBias;
        outState.DepthBiasClamp = inState.depthBiasClamp;
        outState.SlopeScaledDepthBias = inState.slopeScaledDepthBias;
        outState.DepthClipEnable = inState.depthClipEnable ? TRUE : FALSE;
        outState.MultisampleEnable = inState.multisampleEnable ? TRUE : FALSE;
        outState.AntialiasedLineEnable = inState.antialiasedLineEnable ? TRUE : FALSE;
        outState.ForcedSampleCount = inState.forcedSampleCount;
        outState.ConservativeRaster = inState.conservativeRasterEnable
            ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON
            : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    }

    ZWD3D12ViewportState ConvertViewportState(const ZWRasterState& rasterState, const ZWFramebufferInfoEx& framebufferInfo, const ZWViewportState& vpState)
    {
        ZWD3D12ViewportState state = {};

        state.numViewports = static_cast<UINT>(vpState.viewports.size());
        for (size_t index = 0; index < vpState.viewports.size(); ++index)
        {
            state.viewports[index].TopLeftX = vpState.viewports[index].minX;
            state.viewports[index].TopLeftY = vpState.viewports[index].minY;
            state.viewports[index].Width = vpState.viewports[index].maxX - vpState.viewports[index].minX;
            state.viewports[index].Height = vpState.viewports[index].maxY - vpState.viewports[index].minY;
            state.viewports[index].MinDepth = vpState.viewports[index].minZ;
            state.viewports[index].MaxDepth = vpState.viewports[index].maxZ;
        }

        state.numScissorRects = static_cast<UINT>(vpState.scissorRects.size());
        for (size_t index = 0; index < vpState.scissorRects.size(); ++index)
        {
            if (rasterState.scissorEnable)
            {
                state.scissorRects[index].left = static_cast<LONG>(vpState.scissorRects[index].minX);
                state.scissorRects[index].top = static_cast<LONG>(vpState.scissorRects[index].minY);
                state.scissorRects[index].right = static_cast<LONG>(vpState.scissorRects[index].maxX);
                state.scissorRects[index].bottom = static_cast<LONG>(vpState.scissorRects[index].maxY);
            }
            else
            {
                state.scissorRects[index].left = static_cast<LONG>(vpState.viewports[index].minX);
                state.scissorRects[index].top = static_cast<LONG>(vpState.viewports[index].minY);
                state.scissorRects[index].right = static_cast<LONG>(vpState.viewports[index].maxX);
                state.scissorRects[index].bottom = static_cast<LONG>(vpState.viewports[index].maxY);

                if (framebufferInfo.width > 0)
                {
                    state.scissorRects[index].left = std::max(state.scissorRects[index].left, LONG(0));
                    state.scissorRects[index].top = std::max(state.scissorRects[index].top, LONG(0));
                    state.scissorRects[index].right = std::min(state.scissorRects[index].right, static_cast<LONG>(framebufferInfo.width));
                    state.scissorRects[index].bottom = std::min(state.scissorRects[index].bottom, static_cast<LONG>(framebufferInfo.height));
                }
            }
        }

        return state;
    }

    void ZWD3D12CommandList::BindFramebuffer(ZWD3D12Framebuffer* framebuffer)
    {
        HCommon::StaticVector<D3D12_CPU_DESCRIPTOR_HANDLE, 16> renderTargetViews;
        for (DescriptorIndex renderTargetView : framebuffer->RTVs)
        {
            renderTargetViews.push_back(m_Resources.renderTargetViewHeap.GetCpuHandle(renderTargetView));
        }

        D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView = {};
        if (framebuffer->desc.depthAttachment.valid())
        {
            depthStencilView = m_Resources.depthStencilViewHeap.GetCpuHandle(framebuffer->DSV);
        }

        mActiveCommandList->commandList->OMSetRenderTargets(
            static_cast<UINT>(renderTargetViews.size()),
            renderTargetViews.data(),
            FALSE,
            framebuffer->desc.depthAttachment.valid() ? &depthStencilView : nullptr);
    }

    void ZWD3D12CommandList::BindGraphicsPipeline(ZWD3D12GraphicsPipeline* pipeline, bool updateRootSignature) const
    {
        if (updateRootSignature)
        {
            mActiveCommandList->commandList->SetGraphicsRootSignature(pipeline->rootSignature->handle.Get());
        }

        mActiveCommandList->commandList->SetPipelineState(pipeline->pipelineState.Get());
        mActiveCommandList->commandList->IASetPrimitiveTopology(
            ConvertPrimitiveType(pipeline->desc.primType, pipeline->desc.patchControlPoints));
    }

    void ZWD3D12CommandList::UnbindShadingRateState()
    {
        if (mActiveCommandList == nullptr || mActiveCommandList->commandList6 == nullptr)
        {
            return;
        }

        if (mCurrentGraphicsStateValid && m_CurrentGraphicsState.shadingRateState.enabled)
        {
            mActiveCommandList->commandList6->RSSetShadingRateImage(nullptr);
            mActiveCommandList->commandList6->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr);
            m_CurrentGraphicsState.shadingRateState.enabled = false;
            m_CurrentGraphicsState.framebuffer = nullptr;
        }
    }

    void ZWD3D12CommandList::UpdateGraphicsVolatileBuffers()
    {
        if (!mAnyVolatileBufferWrites)
        {
            return;
        }

        for (VolatileConstantBufferBinding& parameter : mCurrentGraphicsVolatileCBs)
        {
            const D3D12_GPU_VIRTUAL_ADDRESS currentGpuAddress = mVolatileConstantBufferAddresses[parameter.buffer];
            if (currentGpuAddress != parameter.address)
            {
                mActiveCommandList->commandList->SetGraphicsRootConstantBufferView(parameter.bindingPoint, currentGpuAddress);
                parameter.address = currentGpuAddress;
            }
        }

        mAnyVolatileBufferWrites = false;
    }

    void ZWD3D12CommandList::SetGraphicsState(const ZWGraphicsState& state)
    {
        ZWD3D12GraphicsPipeline* pipeline = static_cast<ZWD3D12GraphicsPipeline*>(state.pipeline);
        ZWD3D12Framebuffer* framebuffer = static_cast<ZWD3D12Framebuffer*>(state.framebuffer);
        if (pipeline == nullptr || framebuffer == nullptr)
        {
            return;
        }

        const bool updateFramebuffer = !mCurrentGraphicsStateValid || m_CurrentGraphicsState.framebuffer != state.framebuffer;
        const bool updateRootSignature = !mCurrentGraphicsStateValid
            || m_CurrentGraphicsState.pipeline == nullptr
            || static_cast<ZWD3D12GraphicsPipeline*>(m_CurrentGraphicsState.pipeline)->rootSignature != pipeline->rootSignature;
        const bool updatePipeline = !mCurrentGraphicsStateValid || m_CurrentGraphicsState.pipeline != state.pipeline;
        const bool updateIndirectParams = !mCurrentGraphicsStateValid || m_CurrentGraphicsState.indirectParams != state.indirectParams;
        const bool updateIndirectCountBuffer = !mCurrentGraphicsStateValid || m_CurrentGraphicsState.indirectCountBuffer != state.indirectCountBuffer;
        const bool updateViewports = !mCurrentGraphicsStateValid
            || ArraysAreDifferent(m_CurrentGraphicsState.viewport.viewports, state.viewport.viewports)
            || ArraysAreDifferent(m_CurrentGraphicsState.viewport.scissorRects, state.viewport.scissorRects);
        const bool updateBlendFactor = !mCurrentGraphicsStateValid || m_CurrentGraphicsState.blendConstantColor != state.blendConstantColor;
        const uint8_t effectiveStencilRefValue = pipeline->desc.renderState.depthStencilState.dynamicStencilRef
            ? state.dynamicStencilRefValue
            : pipeline->desc.renderState.depthStencilState.stencilRefValue;
        const bool updateStencilRef = !mCurrentGraphicsStateValid || m_CurrentGraphicsState.dynamicStencilRefValue != effectiveStencilRefValue;
        const bool updateIndexBuffer = !mCurrentGraphicsStateValid || m_CurrentGraphicsState.indexBuffer != state.indexBuffer;
        const bool updateVertexBuffers = !mCurrentGraphicsStateValid || ArraysAreDifferent(m_CurrentGraphicsState.vertexBuffers, state.vertexBuffers);
        const bool updateShadingRate = !mCurrentGraphicsStateValid || m_CurrentGraphicsState.shadingRateState != state.shadingRateState;

        uint32_t bindingUpdateMask = 0;
        if (!mCurrentGraphicsStateValid || updateRootSignature)
        {
            bindingUpdateMask = ~0u;
        }

        if (CommitDescriptorHeaps())
        {
            bindingUpdateMask = ~0u;
        }

        if (bindingUpdateMask == 0)
        {
            bindingUpdateMask = ArrayDifferenceMask(m_CurrentGraphicsState.bindings, state.bindings);
        }

        if (updatePipeline)
        {
            BindGraphicsPipeline(pipeline, updateRootSignature);
            mInstance->referencedResources.push_back(pipeline);
        }

        if (pipeline->desc.renderState.depthStencilState.stencilEnable && (updatePipeline || updateStencilRef))
        {
            mActiveCommandList->commandList->OMSetStencilRef(effectiveStencilRefValue);
        }

        if (pipeline->requiresBlendFactor && updateBlendFactor)
        {
            mActiveCommandList->commandList->OMSetBlendFactor(&state.blendConstantColor.r);
        }

        if (updateFramebuffer)
        {
            BindFramebuffer(framebuffer);
            mInstance->referencedResources.push_back(framebuffer);
        }

        if (mEnableAutomaticBarriers && (mBindingStatesDirty || updateFramebuffer))
        {
            SetResourceStatesForFramebuffer(framebuffer);
        }

        SetGraphicsBindings(
            state.bindings,
            bindingUpdateMask,
            state.indirectParams,
            updateIndirectParams,
            state.indirectCountBuffer,
            updateIndirectCountBuffer,
            pipeline->rootSignature);

        if (updateIndexBuffer)
        {
            D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

            if (state.indexBuffer.buffer != nullptr)
            {
                ZWD3D12Buffer* buffer = static_cast<ZWD3D12Buffer*>(state.indexBuffer.buffer);
                indexBufferView.Format = GetDxgiFormatMapping(state.indexBuffer.format).srvFormat;
                indexBufferView.SizeInBytes = static_cast<UINT>(buffer->desc.byteSize - state.indexBuffer.offset);
                indexBufferView.BufferLocation = buffer->gpuVA + state.indexBuffer.offset;
                mInstance->referencedResources.push_back(buffer);
            }

            mActiveCommandList->commandList->IASetIndexBuffer(&indexBufferView);
        }

        if (mEnableAutomaticBarriers && state.indexBuffer.buffer != nullptr && (mBindingStatesDirty || updateIndexBuffer))
        {
            RequireBufferState(state.indexBuffer.buffer, EResourceStates::IndexBuffer);
        }

        if (updateVertexBuffers)
        {
            D3D12_VERTEX_BUFFER_VIEW vertexBufferViews[gMaxVertexAttributes] = {};
            uint32_t maxBindingSlot = 0;
            ZWD3D12InputLayout* inputLayout = static_cast<ZWD3D12InputLayout*>(pipeline->desc.inputLayout.Get());

            for (const ZWVertexBufferBinding& binding : state.vertexBuffers)
            {
                ZWD3D12Buffer* buffer = static_cast<ZWD3D12Buffer*>(binding.buffer);
                if (buffer == nullptr || binding.slot >= gMaxVertexAttributes)
                {
                    continue;
                }

                uint32_t stride = 0;
                if (inputLayout != nullptr)
                {
                    const auto strideIt = inputLayout->elementStrides.find(binding.slot);
                    if (strideIt != inputLayout->elementStrides.end())
                    {
                        stride = strideIt->second;
                    }
                }

                vertexBufferViews[binding.slot].StrideInBytes = stride;
                vertexBufferViews[binding.slot].SizeInBytes = static_cast<UINT>(
                    std::min<uint64_t>(buffer->desc.byteSize - binding.offset, static_cast<uint64_t>(ULONG_MAX)));
                vertexBufferViews[binding.slot].BufferLocation = buffer->gpuVA + binding.offset;
                maxBindingSlot = std::max(maxBindingSlot, binding.slot);

                mInstance->referencedResources.push_back(buffer);
            }

            if (mCurrentGraphicsStateValid)
            {
                for (const ZWVertexBufferBinding& binding : m_CurrentGraphicsState.vertexBuffers)
                {
                    if (binding.slot < gMaxVertexAttributes)
                    {
                        maxBindingSlot = std::max(maxBindingSlot, binding.slot);
                    }
                }
            }

            mActiveCommandList->commandList->IASetVertexBuffers(0, maxBindingSlot + 1u, vertexBufferViews);
        }

        if (mEnableAutomaticBarriers && (mBindingStatesDirty || updateVertexBuffers))
        {
            for (const ZWVertexBufferBinding& binding : state.vertexBuffers)
            {
                if (binding.buffer != nullptr)
                {
                    RequireBufferState(binding.buffer, EResourceStates::VertexBuffer);
                }
            }
        }

        if (mActiveCommandList->commandList6 != nullptr && (updateShadingRate || updateFramebuffer))
        {
            const ZWFramebufferDesc& framebufferDesc = framebuffer->GetDesc();
            const bool shouldEnableShadingRateImage = framebufferDesc.shadingRateAttachment.valid() && state.shadingRateState.enabled;
            const bool wasUsingShadingRateImage = mCurrentGraphicsStateValid
                && m_CurrentGraphicsState.framebuffer != nullptr
                && m_CurrentGraphicsState.framebuffer->GetDesc().shadingRateAttachment.valid()
                && m_CurrentGraphicsState.shadingRateState.enabled;

            if (shouldEnableShadingRateImage)
            {
                ZWD3D12Texture* shadingRateTexture = static_cast<ZWD3D12Texture*>(framebufferDesc.shadingRateAttachment.texture);
                mActiveCommandList->commandList6->RSSetShadingRateImage(shadingRateTexture->resource.Get());
            }
            else if (wasUsingShadingRateImage)
            {
                mActiveCommandList->commandList6->RSSetShadingRateImage(nullptr);
            }
        }

        if (mActiveCommandList->commandList6 != nullptr && updateShadingRate)
        {
            if (state.shadingRateState.enabled)
            {
                D3D12_SHADING_RATE_COMBINER combiners[D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT] = {};
                combiners[0] = ConvertShadingRateCombiner(state.shadingRateState.pipelinePrimitiveCombiner);
                combiners[1] = ConvertShadingRateCombiner(state.shadingRateState.imageCombiner);
                mActiveCommandList->commandList6->RSSetShadingRate(
                    ConvertPixelShadingRate(state.shadingRateState.shadingRate),
                    combiners);
            }
            else if (mCurrentGraphicsStateValid && m_CurrentGraphicsState.shadingRateState.enabled)
            {
                mActiveCommandList->commandList6->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr);
            }
        }

        CommitBarriers();

        if (updateViewports)
        {
            ZWD3D12ViewportState viewportState = ConvertViewportState(
                pipeline->desc.renderState.rasterState,
                framebuffer->framebufferInfo,
                state.viewport);

            if (viewportState.numViewports != 0)
            {
                mActiveCommandList->commandList->RSSetViewports(viewportState.numViewports, viewportState.viewports);
            }

            if (viewportState.numScissorRects != 0)
            {
                mActiveCommandList->commandList->RSSetScissorRects(viewportState.numScissorRects, viewportState.scissorRects);
            }
        }

        mCurrentGraphicsStateValid = true;
        mCurrentComputeStateValid = false;
        mCurrentMeshletStateValid = false;
        mCurrentRayTracingStateValid = false;
        m_CurrentGraphicsState = state;
        m_CurrentGraphicsState.dynamicStencilRefValue = effectiveStencilRefValue;
        mBindingStatesDirty = false;
    }

    void ZWD3D12CommandList::Draw(const ZWDrawArguments& args)
    {
        UpdateGraphicsVolatileBuffers();
        mActiveCommandList->commandList->DrawInstanced(
            args.vertexCount,
            args.instanceCount,
            args.startVertexLocation,
            args.startInstanceLocation);
    }

    void ZWD3D12CommandList::DrawIndexed(const ZWDrawArguments& args)
    {
        UpdateGraphicsVolatileBuffers();
        mActiveCommandList->commandList->DrawIndexedInstanced(
            args.vertexCount,
            args.instanceCount,
            args.startIndexLocation,
            args.startVertexLocation,
            args.startInstanceLocation);
    }

    void ZWD3D12CommandList::DrawIndirect(uint32_t offsetBytes, uint32_t drawCount)
    {
        ZWD3D12Buffer* indirectParams = static_cast<ZWD3D12Buffer*>(m_CurrentGraphicsState.indirectParams);
        if (indirectParams == nullptr)
        {
            return;
        }

        UpdateGraphicsVolatileBuffers();
        mActiveCommandList->commandList->ExecuteIndirect(
            m_Context.drawIndirectSignature.Get(),
            drawCount,
            indirectParams->resource.Get(),
            offsetBytes,
            nullptr,
            0);
    }

    void ZWD3D12CommandList::DrawIndexedIndirect(uint32_t offsetBytes, uint32_t drawCount)
    {
        ZWD3D12Buffer* indirectParams = static_cast<ZWD3D12Buffer*>(m_CurrentGraphicsState.indirectParams);
        if (indirectParams == nullptr)
        {
            return;
        }

        UpdateGraphicsVolatileBuffers();
        mActiveCommandList->commandList->ExecuteIndirect(
            m_Context.drawIndexedIndirectSignature.Get(),
            drawCount,
            indirectParams->resource.Get(),
            offsetBytes,
            nullptr,
            0);
    }

    void ZWD3D12CommandList::DrawIndexedIndirectCount(uint32_t paramOffsetBytes, uint32_t countOffsetBytes, uint32_t maxDrawCount)
    {
        ZWD3D12Buffer* paramBuffer = static_cast<ZWD3D12Buffer*>(m_CurrentGraphicsState.indirectParams);
        ZWD3D12Buffer* countBuffer = static_cast<ZWD3D12Buffer*>(m_CurrentGraphicsState.indirectCountBuffer);
        if (paramBuffer == nullptr || countBuffer == nullptr)
        {
            return;
        }

        UpdateGraphicsVolatileBuffers();
        mActiveCommandList->commandList->ExecuteIndirect(
            m_Context.drawIndexedIndirectSignature.Get(),
            maxDrawCount,
            paramBuffer->resource.Get(),
            paramOffsetBytes,
            countBuffer->resource.Get(),
            countOffsetBytes);
    }
}
