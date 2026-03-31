#include <BackEnd/d3d12backend.h>
#include <BackEnd/versioning.h>

#include <cassert>
#include <cstring>

namespace HRHI::HD3D12
{
    namespace
    {
        constexpr ObjectType cD3D12GraphicsCommandListObjectType = 0x00020003u;
        constexpr ObjectType cD3D12CommandAllocatorObjectType = 0x0002000bu;

        void BeginEventMarker(ID3D12GraphicsCommandList* commandList, const char* name)
        {
            if (commandList == nullptr)
            {
                return;
            }

            const char* eventName = name != nullptr ? name : "";
            commandList->BeginEvent(0, eventName, static_cast<UINT>(std::strlen(eventName) + 1));
        }

        void ReportInvalidCommandQueueType(const ZWD3D12Context& context)
        {
            context.Error("Invalid D3D12 command queue type.");
        }
    }

    ZWD3D12CommandList::ZWD3D12CommandList(
        ZWD3D12Device* device,
        const ZWD3D12Context& context,
        ZWD3D12DeviceResources& resources,
        const ZWCommandListParameters& params)
        : m_Context(context)
        , m_Resources(resources)
        , mDevice(device)
        , mQueue(device != nullptr ? device->GetQueue(params.queueType) : nullptr)
        , mUploadManager(context, mQueue, params.uploadChunkSize, 0, false)
        , mDxrScratchManager(context, mQueue, params.scratchChunkSize, params.scratchMaxMemory, true)
        , mStateTracker(context.messageCallback)
        , mDesc(params)
    {
    }

    ZWD3D12CommandList::~ZWD3D12CommandList()
    {
    }

    HCommon::ZWObject ZWD3D12CommandList::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case cD3D12GraphicsCommandListObjectType:
            if (mActiveCommandList != nullptr)
            {
                return HCommon::ZWObject(mActiveCommandList->commandList.Get());
            }
            return nullptr;

        case cD3D12CommandAllocatorObjectType:
            if (mActiveCommandList != nullptr)
            {
                return HCommon::ZWObject(mActiveCommandList->allocator.Get());
            }
            return nullptr;

        case HD3D12ObjectTypes::Nvrhi_D3D12_CommandList:
            return HCommon::ZWObject(this);

        default:
            return nullptr;
        }
    }

    std::shared_ptr<ZWD3D12InternalCommandList> ZWD3D12CommandList::CreateInternalCommandList() const
    {
        auto commandList = std::make_shared<ZWD3D12InternalCommandList>();

        D3D12_COMMAND_LIST_TYPE d3d12CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
        switch (mDesc.queueType)
        {
        case ECommandQueue::Graphics:
            d3d12CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
            break;
        case ECommandQueue::Compute:
            d3d12CommandListType = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            break;
        case ECommandQueue::Copy:
            d3d12CommandListType = D3D12_COMMAND_LIST_TYPE_COPY;
            break;
        case ECommandQueue::Count:
        default:
            ReportInvalidCommandQueueType(m_Context);
            return nullptr;
        }

        if (FAILED(m_Context.device->CreateCommandAllocator(
            d3d12CommandListType,
            IID_PPV_ARGS(commandList->allocator.ReleaseAndGetAddressOf()))))
        {
            m_Context.Error("Failed to create a D3D12 command allocator.");
            return nullptr;
        }

        if (FAILED(m_Context.device->CreateCommandList(
            0,
            d3d12CommandListType,
            commandList->allocator.Get(),
            nullptr,
            IID_PPV_ARGS(commandList->commandList.ReleaseAndGetAddressOf()))))
        {
            m_Context.Error("Failed to create a D3D12 command list.");
            return nullptr;
        }

        commandList->commandList->QueryInterface(IID_PPV_ARGS(commandList->commandList4.ReleaseAndGetAddressOf()));
        commandList->commandList->QueryInterface(IID_PPV_ARGS(commandList->commandList6.ReleaseAndGetAddressOf()));
#if HRHI_D3D12_WITH_COOPVEC
        commandList->commandList->QueryInterface(IID_PPV_ARGS(commandList->commandListPreview.ReleaseAndGetAddressOf()));
#endif

        return commandList;
    }

    void ZWD3D12CommandList::RequireBufferState(IBuffer* buffer, EResourceStates state)
    {
        mStateTracker.RequireBufferState(static_cast<ZWD3D12Buffer*>(buffer), state);
    }

    bool ZWD3D12CommandList::CommitDescriptorHeaps()
    {
        ID3D12DescriptorHeap* shaderResourceViewHeap = m_Resources.shaderResourceViewHeap.GetShaderVisibleHeap();
        ID3D12DescriptorHeap* samplerHeap = m_Resources.samplerHeap.GetShaderVisibleHeap();

        if (shaderResourceViewHeap == mCurrentHeapSRVetc && samplerHeap == mCurrentHeapSamplers)
        {
            return false;
        }

        ID3D12DescriptorHeap* heaps[] = { shaderResourceViewHeap, samplerHeap };
        mActiveCommandList->commandList->SetDescriptorHeaps(2, heaps);

        mCurrentHeapSRVetc = shaderResourceViewHeap;
        mCurrentHeapSamplers = samplerHeap;

        mInstance->referencedNativeResources.push_back(shaderResourceViewHeap);
        mInstance->referencedNativeResources.push_back(samplerHeap);
        return true;
    }

    bool ZWD3D12CommandList::AllocateUploadBuffer(size_t size, void** pCpuAddress, D3D12_GPU_VIRTUAL_ADDRESS* pGpuAddress)
    {
        return mUploadManager.SuballocateBuffer(
            size,
            nullptr,
            nullptr,
            nullptr,
            pCpuAddress,
            pGpuAddress,
            mRecordingVersion,
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    }

    bool ZWD3D12CommandList::AllocateDxrScratchBuffer(size_t size, void** pCpuAddress, D3D12_GPU_VIRTUAL_ADDRESS* pGpuAddress)
    {
        if (mActiveCommandList == nullptr)
        {
            return false;
        }

        return mDxrScratchManager.SuballocateBuffer(
            size,
            mActiveCommandList->commandList.Get(),
            nullptr,
            nullptr,
            pCpuAddress,
            pGpuAddress,
            mRecordingVersion,
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    }

    D3D12_GPU_VIRTUAL_ADDRESS ZWD3D12CommandList::GetBufferGpuVA(IBuffer* buffer)
    {
        if (buffer == nullptr)
        {
            return 0;
        }

        ZWD3D12Buffer* d3d12Buffer = static_cast<ZWD3D12Buffer*>(buffer);
        if (d3d12Buffer->desc.isVolatile)
        {
            return mVolatileConstantBufferAddresses[d3d12Buffer];
        }

        return d3d12Buffer->gpuVA;
    }

    HRHI::IDevice* ZWD3D12CommandList::GetDevice()
    {
        return mDevice;
    }

    void ZWD3D12CommandList::BeginMarker(const char* name)
    {
        if (mActiveCommandList == nullptr || mActiveCommandList->commandList == nullptr)
        {
            return;
        }

        BeginEventMarker(mActiveCommandList->commandList.Get(), name);
    }

    void ZWD3D12CommandList::EndMarker()
    {
        if (mActiveCommandList == nullptr || mActiveCommandList->commandList == nullptr)
        {
            return;
        }

        mActiveCommandList->commandList->EndEvent();
    }

    void ZWD3D12CommandList::SetPushConstants(const void* data, size_t byteSize)
    {
        const ZWD3D12RootSignature* rootSignature = nullptr;
        bool isGraphics = false;

        if (mCurrentGraphicsStateValid && m_CurrentGraphicsState.pipeline != nullptr)
        {
            ZWD3D12GraphicsPipeline* pipeline = static_cast<ZWD3D12GraphicsPipeline*>(m_CurrentGraphicsState.pipeline);
            rootSignature = pipeline->rootSignature;
            isGraphics = true;
        }
        else if (mCurrentComputeStateValid && m_CurrentComputeState.pipeline != nullptr)
        {
            ComputePipeline* pipeline = static_cast<ComputePipeline*>(m_CurrentComputeState.pipeline);
            rootSignature = pipeline->rootSignature;
            isGraphics = false;
        }
        else if (mCurrentRayTracingStateValid && mCurrentRayTracingState.shaderTable != nullptr)
        {
            ZWD3D12ShaderTable* shaderTable = static_cast<ZWD3D12ShaderTable*>(mCurrentRayTracingState.shaderTable);
            ZWD3D12RayTracingPipeline* pipeline = static_cast<ZWD3D12RayTracingPipeline*>(shaderTable->GetPipeline());
            rootSignature = pipeline->globalRootSignature;
            isGraphics = false;
        }
        else if (mCurrentMeshletStateValid && m_CurrentMeshletState.pipeline != nullptr)
        {
            ZWD3D12MeshletPipeline* pipeline = static_cast<ZWD3D12MeshletPipeline*>(m_CurrentMeshletState.pipeline);
            rootSignature = pipeline->rootSignature;
            isGraphics = true;
        }

        if (rootSignature == nullptr || rootSignature->pushConstantByteSize == 0)
        {
            return;
        }

        assert(byteSize == rootSignature->pushConstantByteSize);

        if (isGraphics)
        {
            mActiveCommandList->commandList->SetGraphicsRoot32BitConstants(
                rootSignature->rootParameterPushConstants,
                static_cast<UINT>(byteSize / 4),
                data,
                0);
        }
        else
        {
            mActiveCommandList->commandList->SetComputeRoot32BitConstants(
                rootSignature->rootParameterPushConstants,
                static_cast<UINT>(byteSize / 4),
                data,
                0);
        }
    }

    void ZWD3D12CommandList::Open()
    {
        const uint64_t completedInstance = mQueue->UpdateLastCompletedInstance();

        std::shared_ptr<ZWD3D12InternalCommandList> commandList;
        if (!mCommandListPool.empty())
        {
            commandList = mCommandListPool.front();
            if (commandList->lastSubmittedInstance <= completedInstance)
            {
                commandList->allocator->Reset();
                commandList->commandList->Reset(commandList->allocator.Get(), nullptr);
                mCommandListPool.pop_front();
            }
            else
            {
                commandList = nullptr;
            }
        }

        if (commandList == nullptr)
        {
            commandList = CreateInternalCommandList();
        }

        if (commandList == nullptr)
        {
            return;
        }

        mActiveCommandList = commandList;

        mInstance = std::make_shared<CommandListInstance>();
        mInstance->commandAllocator = mActiveCommandList->allocator;
        mInstance->commandList = mActiveCommandList->commandList;
        mInstance->commandQueue = mDesc.queueType;

        mRecordingVersion = MakeVersion(mQueue->recordingInstance++, mDesc.queueType, false);
    }

    void ZWD3D12CommandList::ClearStateCache()
    {
        mAnyVolatileBufferWrites = false;
        mCurrentGraphicsStateValid = false;
        mCurrentComputeStateValid = false;
        mCurrentMeshletStateValid = false;
        mCurrentRayTracingStateValid = false;
        mCurrentHeapSRVetc = nullptr;
        mCurrentHeapSamplers = nullptr;
        mCurrentGraphicsVolatileCBs.resize(0);
        mCurrentComputeVolatileCBs.resize(0);
        mCurrentSinglePassStereoState = ZWSinglePassStereoState();
    }

    void ZWD3D12CommandList::ClearState()
    {
        if (mActiveCommandList == nullptr || mActiveCommandList->commandList == nullptr)
        {
            return;
        }

        mActiveCommandList->commandList->ClearState(nullptr);

        ClearStateCache();
        CommitDescriptorHeaps();
    }

    void ZWD3D12CommandList::Close()
    {
        if (mActiveCommandList == nullptr || mActiveCommandList->commandList == nullptr)
        {
            return;
        }

        mStateTracker.KeepBufferInitialStates();
        mStateTracker.KeepTextureInitialStates();
        CommitBarriers();

#if HRHI_WITH_RTXMU
        if (m_Context.rtxMemUtil != nullptr
            && mActiveCommandList->commandList4 != nullptr
            && mInstance != nullptr
            && !mInstance->rtxmuBuildIds.empty())
        {
            m_Context.rtxMemUtil->PopulateCompactionSizeCopiesCommandList(
                mActiveCommandList->commandList4.Get(),
                mInstance->rtxmuBuildIds);
        }
#endif

        mActiveCommandList->commandList->Close();

        ClearStateCache();
        mCurrentUploadBuffer = nullptr;
        mVolatileConstantBufferAddresses.clear();
        mUncachedShaderTableStates.clear();
    }

    std::shared_ptr<CommandListInstance> ZWD3D12CommandList::Executed(ZWD3D12Queue* pQueue)
    {
        if (mInstance == nullptr || pQueue == nullptr)
        {
            return nullptr;
        }

        std::shared_ptr<CommandListInstance> submittedInstance = mInstance;
        submittedInstance->fence = pQueue->fence;
        submittedInstance->submittedInstance = pQueue->lastSubmittedInstance;
        mInstance.reset();

        mActiveCommandList->lastSubmittedInstance = pQueue->lastSubmittedInstance;
        mCommandListPool.push_back(mActiveCommandList);
        mActiveCommandList.reset();

        for (const auto& texture : submittedInstance->referencedStagingTextures)
        {
            texture->lastUseFence = pQueue->fence;
            texture->lastUseFenceValue = submittedInstance->submittedInstance;
        }

        for (const auto& buffer : submittedInstance->referencedStagingBuffers)
        {
            buffer->lastUseFence = pQueue->fence;
            buffer->lastUseFenceValue = submittedInstance->submittedInstance;
        }

        for (const auto& timerQuery : submittedInstance->referencedTimerQueries)
        {
            timerQuery->started = true;
            timerQuery->resolved = false;
            timerQuery->fence = pQueue->fence;
            timerQuery->fenceCounter = submittedInstance->submittedInstance;
        }

        mStateTracker.CommandListSubmitted();

        const uint64_t submittedVersion = MakeVersion(submittedInstance->submittedInstance, mDesc.queueType, true);
        mUploadManager.SubmitChunks(mRecordingVersion, submittedVersion);
        mDxrScratchManager.SubmitChunks(mRecordingVersion, submittedVersion);
        mRecordingVersion = 0;

        return submittedInstance;
    }

    void ZWD3D12CommandList::CommitBarriers()
    {
        const std::vector<ZWTextureBarrier>& textureBarriers = mStateTracker.GetTextureBarriers();
        const std::vector<ZWBufferBarrier>& bufferBarriers = mStateTracker.GetBufferBarriers();
        if (textureBarriers.empty() && bufferBarriers.empty())
        {
            return;
        }

        mD3DBarriers.clear();
        mD3DBarriers.reserve(textureBarriers.size() + bufferBarriers.size());

        for (const ZWTextureBarrier& barrier : textureBarriers)
        {
            const ZWD3D12Texture* texture = nullptr;
            ID3D12Resource* resource = nullptr;

            if (barrier.texture->isSamplerFeedback)
            {
                resource = static_cast<const ZWD3D12SamplerFeedbackTexture*>(barrier.texture)->resource.Get();
            }
            else
            {
                texture = static_cast<const ZWD3D12Texture*>(barrier.texture);
                resource = texture->resource.Get();
            }

            const D3D12_RESOURCE_STATES stateBefore = ConvertResourceStates(barrier.stateBefore);
            const D3D12_RESOURCE_STATES stateAfter = ConvertResourceStates(barrier.stateAfter);

            if (stateBefore != stateAfter)
            {
                D3D12_RESOURCE_BARRIER d3dBarrier = {};
                d3dBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                d3dBarrier.Transition.pResource = resource;
                d3dBarrier.Transition.StateBefore = stateBefore;
                d3dBarrier.Transition.StateAfter = stateAfter;

                if (barrier.entireTexture || texture == nullptr)
                {
                    d3dBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    mD3DBarriers.push_back(d3dBarrier);
                }
                else
                {
                    for (uint8_t plane = 0; plane < texture->planeCount; ++plane)
                    {
                        d3dBarrier.Transition.Subresource = CalcSubresource(
                            barrier.mipLevel,
                            barrier.arraySlice,
                            plane,
                            texture->desc.mipLevels,
                            texture->desc.arraySize);
                        mD3DBarriers.push_back(d3dBarrier);
                    }
                }
            }
            else if ((stateAfter & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0)
            {
                D3D12_RESOURCE_BARRIER d3dBarrier = {};
                d3dBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                d3dBarrier.UAV.pResource = resource;
                mD3DBarriers.push_back(d3dBarrier);
            }
        }

        for (const ZWBufferBarrier& barrier : bufferBarriers)
        {
            const ZWD3D12Buffer* buffer = static_cast<const ZWD3D12Buffer*>(barrier.buffer);
            const D3D12_RESOURCE_STATES stateBefore = ConvertResourceStates(barrier.stateBefore);
            const D3D12_RESOURCE_STATES stateAfter = ConvertResourceStates(barrier.stateAfter);

            if (stateBefore != stateAfter &&
                (stateBefore & D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE) == 0 &&
                (stateAfter & D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE) == 0)
            {
                D3D12_RESOURCE_BARRIER d3dBarrier = {};
                d3dBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                d3dBarrier.Transition.pResource = buffer->resource.Get();
                d3dBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                d3dBarrier.Transition.StateBefore = stateBefore;
                d3dBarrier.Transition.StateAfter = stateAfter;
                mD3DBarriers.push_back(d3dBarrier);
            }
            else if ((barrier.stateBefore == EResourceStates::AccelStructWrite && (barrier.stateAfter & (EResourceStates::AccelStructRead | EResourceStates::AccelStructBuildBlas)) != 0) ||
                (barrier.stateAfter == EResourceStates::AccelStructWrite && (barrier.stateBefore & (EResourceStates::AccelStructRead | EResourceStates::AccelStructBuildBlas)) != 0) ||
                (barrier.stateBefore == EResourceStates::OpacityMicromapWrite && (barrier.stateAfter & EResourceStates::AccelStructBuildInput) != 0) ||
                (stateAfter & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0)
            {
                D3D12_RESOURCE_BARRIER d3dBarrier = {};
                d3dBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                d3dBarrier.UAV.pResource = buffer->resource.Get();
                mD3DBarriers.push_back(d3dBarrier);
            }
        }

        if (!mD3DBarriers.empty())
        {
            mActiveCommandList->commandList->ResourceBarrier(static_cast<uint32_t>(mD3DBarriers.size()), mD3DBarriers.data());
        }

        mStateTracker.ClearBarriers();
    }

    void ZWD3D12CommandList::ConvertCoopVecMatrices(HCoopVec::ZWConvertMatrixLayoutDesc const* convertDescs, size_t numDescs)
    {
#if HRHI_D3D12_WITH_COOPVEC
        if (numDescs == 0 || mActiveCommandList == nullptr || !mActiveCommandList->commandListPreview)
        {
            return;
        }

        std::vector<D3D12_LINEAR_ALGEBRA_MATRIX_CONVERSION_INFO> d3d12ConvertDescs;
        d3d12ConvertDescs.reserve(numDescs);

        for (size_t index = 0; index < numDescs; ++index)
        {
            const HCoopVec::ZWConvertMatrixLayoutDesc& desc = convertDescs[index];
            if (desc.src.buffer == nullptr || desc.dst.buffer == nullptr)
            {
                continue;
            }

            if (mEnableAutomaticBarriers)
            {
                RequireBufferState(desc.src.buffer, EResourceStates::ConvertCoopVecMatrixInput);
                RequireBufferState(desc.dst.buffer, EResourceStates::ConvertCoopVecMatrixOutput);
                mBindingStatesDirty = true;
            }

            D3D12_LINEAR_ALGEBRA_MATRIX_CONVERSION_INFO& d3d12Desc = d3d12ConvertDescs.emplace_back();

            d3d12Desc.SrcInfo.SrcSize = static_cast<UINT>(desc.src.size);
            d3d12Desc.SrcInfo.SrcDataType = ConvertCoopVecDataType(desc.src.type);
            d3d12Desc.SrcInfo.SrcLayout = ConvertCoopVecMatrixLayout(desc.src.layout);
            d3d12Desc.SrcInfo.SrcStride = desc.src.stride != 0
                ? static_cast<UINT>(desc.src.stride)
                : static_cast<UINT>(HCoopVec::getOptimalMatrixStride(desc.src.type, desc.src.layout, desc.numRows, desc.numColumns));

            d3d12Desc.DestInfo.DestSize = static_cast<UINT>(desc.dst.size);
            d3d12Desc.DestInfo.DestLayout = ConvertCoopVecMatrixLayout(desc.dst.layout);
            d3d12Desc.DestInfo.DestStride = desc.dst.stride != 0
                ? static_cast<UINT>(desc.dst.stride)
                : static_cast<UINT>(HCoopVec::getOptimalMatrixStride(desc.dst.type, desc.dst.layout, desc.numRows, desc.numColumns));
            d3d12Desc.DestInfo.NumColumns = desc.numColumns;
            d3d12Desc.DestInfo.NumRows = desc.numRows;
            d3d12Desc.DestInfo.DestDataType = ConvertCoopVecDataType(desc.dst.type);

            d3d12Desc.DataDesc.SrcVA = desc.src.buffer->GetGpuVirtualAddress() + desc.src.offset;
            d3d12Desc.DataDesc.DestVA = desc.dst.buffer->GetGpuVirtualAddress() + desc.dst.offset;
        }

        CommitBarriers();

        if (!d3d12ConvertDescs.empty())
        {
            mActiveCommandList->commandListPreview->ConvertLinearAlgebraMatrix(
                d3d12ConvertDescs.data(),
                static_cast<UINT>(d3d12ConvertDescs.size()));
        }
#else
        (void)convertDescs;
        (void)numDescs;
        m_Context.Error("CoopVec matrix conversion is not supported by this D3D12 backend build.");
#endif
    }
}
