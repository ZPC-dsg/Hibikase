#include <BackEnd/d3d12backend.h>

#include <algorithm>
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

    HCommon::ZWObject ZWD3D12MeshletPipeline::GetNativeObject(ObjectType objectType)
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
        const ZWMeshletPipelineDesc& desc,
        ZWD3D12RootSignature* rootSignature,
        const ZWFramebufferInfo& framebufferInfo) const
    {
        if (rootSignature == nullptr)
        {
            return nullptr;
        }

        if (mContext.device2 == nullptr)
        {
            mContext.Error("Failed to create a meshlet pipeline state object because ID3D12Device2 is unavailable.");
            return nullptr;
        }

        HCommon::RefCountPtr<ID3D12PipelineState> pipelineState;

#pragma warning(push)
#pragma warning(disable: 4324)
        struct PipelineStateStream
        {
            typedef __declspec(align(sizeof(void*))) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE AlignedType;

            AlignedType rootSignatureType;
            ID3D12RootSignature* rootSignature;
            AlignedType primitiveTopologyTypeType;
            D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopologyType;
            AlignedType amplificationShaderType;
            D3D12_SHADER_BYTECODE amplificationShader;
            AlignedType meshShaderType;
            D3D12_SHADER_BYTECODE meshShader;
            AlignedType pixelShaderType;
            D3D12_SHADER_BYTECODE pixelShader;
            AlignedType rasterizerStateType;
            D3D12_RASTERIZER_DESC rasterizerState;
            AlignedType depthStencilStateType;
            D3D12_DEPTH_STENCIL_DESC depthStencilState;
            AlignedType blendStateType;
            D3D12_BLEND_DESC blendState;
            AlignedType sampleDescType;
            DXGI_SAMPLE_DESC sampleDesc;
            AlignedType sampleMaskType;
            UINT sampleMask;
            AlignedType renderTargetsType;
            D3D12_RT_FORMAT_ARRAY renderTargets;
            AlignedType depthStencilFormatType;
            DXGI_FORMAT depthStencilFormat;
        } pipelineStateStream = {};
#pragma warning(pop)

        pipelineStateStream.rootSignatureType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
        pipelineStateStream.primitiveTopologyTypeType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY;
        pipelineStateStream.amplificationShaderType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS;
        pipelineStateStream.meshShaderType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
        pipelineStateStream.pixelShaderType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
        pipelineStateStream.rasterizerStateType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER;
        pipelineStateStream.depthStencilStateType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL;
        pipelineStateStream.blendStateType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND;
        pipelineStateStream.sampleDescType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC;
        pipelineStateStream.sampleMaskType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK;
        pipelineStateStream.renderTargetsType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
        pipelineStateStream.depthStencilFormatType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT;

        pipelineStateStream.rootSignature = rootSignature->handle.Get();

        TranslateBlendState(desc.renderState.blendState, pipelineStateStream.blendState);

        const ZWDepthStencilState& depthState = desc.renderState.depthStencilState;
        TranslateDepthStencilState(depthState, pipelineStateStream.depthStencilState);

        if ((depthState.depthTestEnable || depthState.stencilEnable) && framebufferInfo.depthFormat == EFormat::UNKNOWN)
        {
            pipelineStateStream.depthStencilState.DepthEnable = FALSE;
            pipelineStateStream.depthStencilState.StencilEnable = FALSE;

            if (mContext.messageCallback != nullptr)
            {
                mContext.messageCallback->message(
                    EMessageSeverity::Warning,
                    "depthTestEnable or stencilEnable is true, but no depth target is bound.");
            }
        }

        const ZWRasterState& rasterState = desc.renderState.rasterState;
        TranslateRasterizerState(rasterState, pipelineStateStream.rasterizerState);

        switch (desc.primType)
        {
        case EPrimitiveType::PointList:
            pipelineStateStream.primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            break;

        case EPrimitiveType::LineList:
            pipelineStateStream.primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            break;

        case EPrimitiveType::TriangleList:
        case EPrimitiveType::TriangleStrip:
            pipelineStateStream.primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            break;

        default:
            mContext.Error("Primitive type is unsupported by the D3D12 meshlet pipeline.");
            return nullptr;
        }

        pipelineStateStream.sampleDesc.Count = framebufferInfo.sampleCount;
        pipelineStateStream.sampleDesc.Quality = framebufferInfo.sampleQuality;
        pipelineStateStream.sampleMask = ~0u;

        for (uint32_t renderTargetIndex = 0; renderTargetIndex < static_cast<uint32_t>(framebufferInfo.colorFormats.size()); ++renderTargetIndex)
        {
            pipelineStateStream.renderTargets.RTFormats[renderTargetIndex] =
                GetDxgiFormatMapping(framebufferInfo.colorFormats[renderTargetIndex]).rtvFormat;
        }
        pipelineStateStream.renderTargets.NumRenderTargets = static_cast<UINT>(framebufferInfo.colorFormats.size());

        pipelineStateStream.depthStencilFormat = GetDxgiFormatMapping(framebufferInfo.depthFormat).rtvFormat;

        pipelineStateStream.amplificationShader = GetShaderBytecode(desc.AS.Get());
        pipelineStateStream.meshShader = GetShaderBytecode(desc.MS.Get());
        pipelineStateStream.pixelShader = GetShaderBytecode(desc.PS.Get());

        D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
        streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;
        streamDesc.SizeInBytes = sizeof(pipelineStateStream);

        const HRESULT result = mContext.device2->CreatePipelineState(
            &streamDesc,
            IID_PPV_ARGS(pipelineState.ReleaseAndGetAddressOf()));

        if (FAILED(result))
        {
            std::stringstream messageBuilder;
            messageBuilder << "Failed to create a meshlet pipeline state object, HRESULT = 0x" << std::hex << result;
            mContext.Error(messageBuilder.str());
            return nullptr;
        }

        return pipelineState;
    }

    ZWMeshletPipelineHandle ZWD3D12Device::CreateMeshletPipeline(const ZWMeshletPipelineDesc& desc, ZWFramebufferInfo const& framebufferInfo)
    {
        HCommon::RefCountPtr<ZWD3D12RootSignature> rootSignature = GetRootSignature(desc.bindingLayouts, false);
        HCommon::RefCountPtr<ID3D12PipelineState> pipelineState = CreatePipelineState(desc, rootSignature.Get(), framebufferInfo);

        return CreateHandleForNativeMeshletPipeline(rootSignature.Get(), pipelineState.Get(), desc, framebufferInfo);
    }

    ZWMeshletPipelineHandle ZWD3D12Device::CreateHandleForNativeMeshletPipeline(
        IRootSignature* rootSignature,
        ID3D12PipelineState* pipelineState,
        const ZWMeshletPipelineDesc& desc,
        const ZWFramebufferInfo& framebufferInfo)
    {
        if (rootSignature == nullptr || pipelineState == nullptr)
        {
            return nullptr;
        }

        ZWD3D12MeshletPipeline* meshletPipeline = new ZWD3D12MeshletPipeline();
        meshletPipeline->desc = desc;
        meshletPipeline->framebufferInfo = framebufferInfo;
        meshletPipeline->rootSignature = static_cast<ZWD3D12RootSignature*>(rootSignature);
        meshletPipeline->pipelineState = pipelineState;
        meshletPipeline->requiresBlendFactor =
            desc.renderState.blendState.usesConstantColor(static_cast<uint32_t>(meshletPipeline->framebufferInfo.colorFormats.size()));

        return ZWMeshletPipelineHandle::Create(meshletPipeline);
    }

    void ZWD3D12CommandList::BindMeshletPipeline(ZWD3D12MeshletPipeline* pipeline, bool updateRootSignature) const
    {
        if (updateRootSignature)
        {
            mActiveCommandList->commandList->SetGraphicsRootSignature(pipeline->rootSignature->handle.Get());
        }

        mActiveCommandList->commandList->SetPipelineState(pipeline->pipelineState.Get());
        mActiveCommandList->commandList->IASetPrimitiveTopology(ConvertPrimitiveType(pipeline->desc.primType, 0));

        if (pipeline->viewportState.numViewports != 0)
        {
            mActiveCommandList->commandList->RSSetViewports(pipeline->viewportState.numViewports, pipeline->viewportState.viewports);
        }

        if (pipeline->viewportState.numScissorRects != 0)
        {
            mActiveCommandList->commandList->RSSetScissorRects(pipeline->viewportState.numScissorRects, pipeline->viewportState.scissorRects);
        }
    }

    void ZWD3D12CommandList::SetMeshletState(const ZWMeshletState& state)
    {
        ZWD3D12MeshletPipeline* pipeline = static_cast<ZWD3D12MeshletPipeline*>(state.pipeline);
        ZWD3D12Framebuffer* framebuffer = static_cast<ZWD3D12Framebuffer*>(state.framebuffer);
        if (pipeline == nullptr || framebuffer == nullptr)
        {
            return;
        }

        UnbindShadingRateState();

        const bool updateFramebuffer = !mCurrentMeshletStateValid || m_CurrentMeshletState.framebuffer != state.framebuffer;
        const bool updateRootSignature = !mCurrentMeshletStateValid
            || m_CurrentMeshletState.pipeline == nullptr
            || static_cast<ZWD3D12MeshletPipeline*>(m_CurrentMeshletState.pipeline)->rootSignature != pipeline->rootSignature;
        const bool updatePipeline = !mCurrentMeshletStateValid || m_CurrentMeshletState.pipeline != state.pipeline;
        const bool updateIndirectParams = !mCurrentMeshletStateValid || m_CurrentMeshletState.indirectParams != state.indirectParams;
        const bool updateViewports = !mCurrentMeshletStateValid
            || ArraysAreDifferent(m_CurrentMeshletState.viewport.viewports, state.viewport.viewports)
            || ArraysAreDifferent(m_CurrentMeshletState.viewport.scissorRects, state.viewport.scissorRects);
        const bool updateBlendFactor = !mCurrentMeshletStateValid || m_CurrentMeshletState.blendConstantColor != state.blendConstantColor;
        const uint8_t effectiveStencilRefValue = pipeline->desc.renderState.depthStencilState.dynamicStencilRef
            ? state.dynamicStencilRefValue
            : pipeline->desc.renderState.depthStencilState.stencilRefValue;
        const bool updateStencilRef = !mCurrentMeshletStateValid || m_CurrentMeshletState.dynamicStencilRefValue != effectiveStencilRefValue;

        uint32_t bindingUpdateMask = 0;
        if (!mCurrentMeshletStateValid || updateRootSignature)
        {
            bindingUpdateMask = ~0u;
        }

        if (CommitDescriptorHeaps())
        {
            bindingUpdateMask = ~0u;
        }

        if (bindingUpdateMask == 0)
        {
            bindingUpdateMask = ArrayDifferenceMask(m_CurrentMeshletState.bindings, state.bindings);
        }

        if (updatePipeline)
        {
            BindMeshletPipeline(pipeline, updateRootSignature);
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
            nullptr,
            false,
            pipeline->rootSignature);

        CommitBarriers();

        if (updateViewports)
        {
            ZWD3D12ViewportState viewportState = ConvertViewportState(
                pipeline->desc.renderState.rasterState,
                framebuffer->framebufferInfo,
                state.viewport);

            if (viewportState.numViewports != 0 && pipeline->viewportState.numViewports == 0)
            {
                mActiveCommandList->commandList->RSSetViewports(viewportState.numViewports, viewportState.viewports);
            }

            if (viewportState.numScissorRects != 0 && pipeline->viewportState.numScissorRects == 0)
            {
                mActiveCommandList->commandList->RSSetScissorRects(viewportState.numScissorRects, viewportState.scissorRects);
            }
        }

        mCurrentGraphicsStateValid = false;
        mCurrentComputeStateValid = false;
        mCurrentMeshletStateValid = true;
        mCurrentRayTracingStateValid = false;
        m_CurrentMeshletState = state;
        m_CurrentMeshletState.dynamicStencilRefValue = effectiveStencilRefValue;
        mBindingStatesDirty = false;
    }

    void ZWD3D12CommandList::DispatchMesh(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        if (mActiveCommandList == nullptr || mActiveCommandList->commandList6 == nullptr)
        {
            m_Context.Error("Mesh shader dispatch is not supported because ID3D12GraphicsCommandList6 is unavailable.");
            return;
        }

        UpdateGraphicsVolatileBuffers();
        mActiveCommandList->commandList6->DispatchMesh(groupsX, groupsY, groupsZ);
    }
}
