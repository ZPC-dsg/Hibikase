#include <BackEnd/d3d12backend.h>

#include <algorithm>
#include <sstream>

namespace HRHI::HD3D12
{
    namespace
    {
        constexpr ObjectType cD3D12RootSignatureObjectType = 0x00020009u;
        constexpr ObjectType cD3D12PipelineStateObjectType = 0x0002000au;

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

    HCommon::ZWObject ComputePipeline::GetNativeObject(ObjectType objectType)
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
        const ZWComputePipelineDesc& desc,
        ZWD3D12RootSignature* rootSignature) const
    {
        HCommon::RefCountPtr<ID3D12PipelineState> pipelineState;
        if (rootSignature == nullptr || desc.CS == nullptr)
        {
            return nullptr;
        }

        ZWD3D12Shader* shader = static_cast<ZWD3D12Shader*>(desc.CS.Get());
        if (shader == nullptr || shader->bytecode.empty())
        {
            mContext.Error("Failed to create a compute pipeline state object because the compute shader bytecode is unavailable.");
            return nullptr;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc = {};
        pipelineStateDesc.pRootSignature = rootSignature->handle.Get();
        pipelineStateDesc.CS = { shader->bytecode.data(), shader->bytecode.size() };

#if HRHI_D3D12_WITH_NVAPI
        if (!shader->extensions.empty())
        {
            NvAPI_Status status = NvAPI_D3D12_CreateComputePipelineState(
                mContext.device.Get(),
                &pipelineStateDesc,
                static_cast<NvU32>(shader->extensions.size()),
                const_cast<const NVAPI_D3D12_PSO_EXTENSION_DESC**>(shader->extensions.data()),
                pipelineState.ReleaseAndGetAddressOf());

            if (status != NVAPI_OK || pipelineState == nullptr)
            {
                mContext.Error("Failed to create a compute pipeline state object with NVAPI extensions.");
                return nullptr;
            }

            return pipelineState;
        }
#endif

        const HRESULT result = mContext.device->CreateComputePipelineState(
            &pipelineStateDesc,
            IID_PPV_ARGS(pipelineState.ReleaseAndGetAddressOf()));

        if (FAILED(result))
        {
            std::stringstream messageBuilder;
            messageBuilder << "Failed to create a compute pipeline state object, HRESULT = 0x" << std::hex << result;
            mContext.Error(messageBuilder.str());
            return nullptr;
        }

        return pipelineState;
    }

    ZWComputePipelineHandle ZWD3D12Device::CreateComputePipeline(const ZWComputePipelineDesc& desc)
    {
        HCommon::RefCountPtr<ZWD3D12RootSignature> rootSignature = GetRootSignature(desc.bindingLayouts, false);
        HCommon::RefCountPtr<ID3D12PipelineState> pipelineState = CreatePipelineState(desc, rootSignature.Get());
        if (!rootSignature || !pipelineState)
        {
            return nullptr;
        }

        ComputePipeline* pipeline = new ComputePipeline();
        pipeline->desc = desc;
        pipeline->rootSignature = rootSignature;
        pipeline->pipelineState = pipelineState;
        return ZWComputePipelineHandle::Create(pipeline);
    }

    void ZWD3D12CommandList::SetComputeState(const ZWComputeState& state)
    {
        ComputePipeline* pipeline = static_cast<ComputePipeline*>(state.pipeline);
        if (pipeline == nullptr)
        {
            return;
        }

        const bool updateRootSignature = !mCurrentComputeStateValid
            || m_CurrentComputeState.pipeline == nullptr
            || static_cast<ComputePipeline*>(m_CurrentComputeState.pipeline)->rootSignature != pipeline->rootSignature;
        const bool updatePipeline = !mCurrentComputeStateValid || m_CurrentComputeState.pipeline != state.pipeline;
        const bool updateIndirectParams = !mCurrentComputeStateValid || m_CurrentComputeState.indirectParams != state.indirectParams;

        uint32_t bindingUpdateMask = 0;
        if (!mCurrentComputeStateValid || updateRootSignature)
        {
            bindingUpdateMask = ~0u;
        }

        if (CommitDescriptorHeaps())
        {
            bindingUpdateMask = ~0u;
        }

        if (bindingUpdateMask == 0)
        {
            bindingUpdateMask = ArrayDifferenceMask(m_CurrentComputeState.bindings, state.bindings);
        }

        if (updateRootSignature)
        {
            mActiveCommandList->commandList->SetComputeRootSignature(pipeline->rootSignature->handle.Get());
        }

        if (updatePipeline)
        {
            mActiveCommandList->commandList->SetPipelineState(pipeline->pipelineState.Get());
            mInstance->referencedResources.push_back(pipeline);
        }

        SetComputeBindings(state.bindings, bindingUpdateMask, state.indirectParams, updateIndirectParams, pipeline->rootSignature);
        UnbindShadingRateState();

        mCurrentGraphicsStateValid = false;
        mCurrentComputeStateValid = true;
        mCurrentMeshletStateValid = false;
        mCurrentRayTracingStateValid = false;
        m_CurrentComputeState = state;
        mBindingStatesDirty = false;

        CommitBarriers();
    }

    void ZWD3D12CommandList::UpdateComputeVolatileBuffers()
    {
        if (!mAnyVolatileBufferWrites)
        {
            return;
        }

        for (VolatileConstantBufferBinding& parameter : mCurrentComputeVolatileCBs)
        {
            const D3D12_GPU_VIRTUAL_ADDRESS currentGpuAddress = mVolatileConstantBufferAddresses[parameter.buffer];
            if (currentGpuAddress != parameter.address)
            {
                mActiveCommandList->commandList->SetComputeRootConstantBufferView(parameter.bindingPoint, currentGpuAddress);
                parameter.address = currentGpuAddress;
            }
        }

        mAnyVolatileBufferWrites = false;
    }

    void ZWD3D12CommandList::Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        UpdateComputeVolatileBuffers();
        mActiveCommandList->commandList->Dispatch(groupsX, groupsY, groupsZ);
    }

    void ZWD3D12CommandList::DispatchIndirect(uint32_t offsetBytes)
    {
        ZWD3D12Buffer* indirectParams = static_cast<ZWD3D12Buffer*>(m_CurrentComputeState.indirectParams);
        if (indirectParams == nullptr)
        {
            return;
        }

        UpdateComputeVolatileBuffers();
        mActiveCommandList->commandList->ExecuteIndirect(
            m_Context.dispatchIndirectSignature.Get(),
            1,
            indirectParams->resource.Get(),
            offsetBytes,
            nullptr,
            0);
    }
}
