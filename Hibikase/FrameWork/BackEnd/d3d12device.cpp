#include <BackEnd/d3d12backend.h>
#include <Utils/stringtranslatehelper.h>

#include <algorithm>
#include <cassert>
#include <iomanip>
#include <sstream>
#include <string>

#if HRHI_D3D12_WITH_NVAPI
#include <nvShaderExtnEnums.h>
#endif

namespace HRHI::HD3D12
{
    namespace
    {
        void DispatchMessage(IMessageCallback* messageCallback, EMessageSeverity severity, const std::string& message)
        {
            if (messageCallback != nullptr)
            {
                messageCallback->message(severity, message.c_str());
                return;
            }

            const std::string messageWithNewLine = message + "\n";
            ::OutputDebugStringA(messageWithNewLine.c_str());
        }

        void ReportDescriptorHeapAllocationFailure(const ZWD3D12Context& context, const char* heapName, HRESULT result)
        {
            std::stringstream messageBuilder;
            messageBuilder << "Failed to allocate the D3D12 " << heapName
                << " descriptor heap, HRESULT = 0x" << std::hex << std::setw(8) << result;
            context.Error(messageBuilder.str());
        }

        void ReportInvalidHeapType(const ZWD3D12Context& context)
        {
            context.Error("Invalid D3D12 heap type.");
        }
    }

    ZWDeviceHandle CreateDevice(const ZWDeviceDesc& desc)
    {
        if (desc.pDevice == nullptr)
        {
            return nullptr;
        }

        return ZWDeviceHandle::Create(new ZWD3D12Device(desc));
    }

    ZWD3D12DeviceResources::ZWD3D12DeviceResources(const ZWD3D12Context& context, const ZWDeviceDesc& desc)
        : renderTargetViewHeap(context)
        , depthStencilViewHeap(context)
        , shaderResourceViewHeap(context)
        , samplerHeap(context)
        , timerQueries(desc.maxTimerQueries, true)
        , mContext(context)
    {
    }

    ZWD3D12Queue::ZWD3D12Queue(const ZWD3D12Context& context, ID3D12CommandQueue* queue)
        : queue(queue)
        , mContext(context)
    {
        assert(queue != nullptr);
        mContext.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.ReleaseAndGetAddressOf()));
    }

    uint64_t ZWD3D12Queue::UpdateLastCompletedInstance()
    {
        if (lastCompletedInstance < lastSubmittedInstance && fence != nullptr)
        {
            lastCompletedInstance = fence->GetCompletedValue();
        }

        return lastCompletedInstance;
    }

    void WaitForFence(ID3D12Fence* fence, uint64_t value, HANDLE event)
    {
        if (fence == nullptr || event == nullptr)
        {
            return;
        }

        if (fence->GetCompletedValue() >= value)
        {
            return;
        }

        if (FAILED(fence->SetEventOnCompletion(value, event)))
        {
            return;
        }

        ::WaitForSingleObject(event, INFINITE);
    }

    uint32_t CalcSubresource(uint32_t mipSlice, uint32_t arraySlice, uint32_t planeSlice, uint32_t mipLevels, uint32_t arraySize)
    {
        return mipSlice + arraySlice * mipLevels + planeSlice * mipLevels * arraySize;
    }

    void ZWD3D12Context::Error(const std::string& message) const
    {
        DispatchMessage(messageCallback, EMessageSeverity::Error, message);
    }

    void ZWD3D12Context::Info(const std::string& message) const
    {
        DispatchMessage(messageCallback, EMessageSeverity::Info, message);
    }

    ZWD3D12Device::ZWD3D12Device(const ZWDeviceDesc& desc)
        : mResources(mContext, desc)
        , mFenceEvent(nullptr)
    {
        assert(desc.pDevice != nullptr);

        mContext.device = desc.pDevice;
        mContext.logBufferLifetime = desc.logBufferLifetime;
        mContext.messageCallback = desc.errorCB;

        if (desc.pGraphicsCommandQueue != nullptr)
        {
            mQueues[static_cast<size_t>(ECommandQueue::Graphics)] = std::make_unique<ZWD3D12Queue>(mContext, desc.pGraphicsCommandQueue);
        }

        if (desc.pComputeCommandQueue != nullptr)
        {
            mQueues[static_cast<size_t>(ECommandQueue::Compute)] = std::make_unique<ZWD3D12Queue>(mContext, desc.pComputeCommandQueue);
        }

        if (desc.pCopyCommandQueue != nullptr)
        {
            mQueues[static_cast<size_t>(ECommandQueue::Copy)] = std::make_unique<ZWD3D12Queue>(mContext, desc.pCopyCommandQueue);
        }

        HRESULT result = mResources.depthStencilViewHeap.AllocateResources(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, desc.depthStencilViewHeapSize, false);
        if (FAILED(result))
        {
            ReportDescriptorHeapAllocationFailure(mContext, "DSV", result);
        }

        result = mResources.renderTargetViewHeap.AllocateResources(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, desc.renderTargetViewHeapSize, false);
        if (FAILED(result))
        {
            ReportDescriptorHeapAllocationFailure(mContext, "RTV", result);
        }

        result = mResources.shaderResourceViewHeap.AllocateResources(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, desc.shaderResourceViewHeapSize, true);
        if (FAILED(result))
        {
            ReportDescriptorHeapAllocationFailure(mContext, "CBV/SRV/UAV", result);
        }

        result = mResources.samplerHeap.AllocateResources(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, desc.samplerHeapSize, true);
        if (FAILED(result))
        {
            ReportDescriptorHeapAllocationFailure(mContext, "sampler", result);
        }

        mContext.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &mOptions, sizeof(mOptions));
        mContext.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &mOptions1, sizeof(mOptions1));

        const bool hasOptions5 = SUCCEEDED(mContext.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &mOptions5, sizeof(mOptions5)));
        const bool hasOptions6 = SUCCEEDED(mContext.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &mOptions6, sizeof(mOptions6)));
        const bool hasOptions7 = SUCCEEDED(mContext.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &mOptions7, sizeof(mOptions7)));

        if (SUCCEEDED(mContext.device->QueryInterface(IID_PPV_ARGS(mContext.device5.ReleaseAndGetAddressOf()))) && hasOptions5)
        {
            mRayTracingSupported = mOptions5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
            mTraceRayInlineSupported = mOptions5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
            mOpacityMicromapSupported = mOptions5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_2;
#endif

#if HRHI_WITH_RTXMU
            if (mRayTracingSupported)
            {
                mContext.rtxMemUtil = std::make_unique<rtxmu::DxAccelStructManager>(mContext.device5.Get());
                mContext.rtxMemUtil->Initialize(8388608);
            }
#endif
        }

        if (SUCCEEDED(mContext.device->QueryInterface(IID_PPV_ARGS(mContext.device2.ReleaseAndGetAddressOf()))) && hasOptions7)
        {
            mMeshletsSupported = mOptions7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
        }

        if (SUCCEEDED(mContext.device->QueryInterface(IID_PPV_ARGS(mContext.device8.ReleaseAndGetAddressOf()))) && hasOptions7)
        {
            mSamplerFeedbackSupported = mOptions7.SamplerFeedbackTier >= D3D12_SAMPLER_FEEDBACK_TIER_0_9;
        }

#if HRHI_D3D12_WITH_COOPVEC
        if (SUCCEEDED(mContext.device->QueryInterface(IID_PPV_ARGS(mContext.devicePreview.ReleaseAndGetAddressOf()))))
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS_EXPERIMENTAL experimentalOptions = {};
            if (SUCCEEDED(mContext.device->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS_EXPERIMENTAL,
                &experimentalOptions,
                sizeof(experimentalOptions))))
            {
                mCoopVecInferencingSupported = experimentalOptions.CooperativeVectorTier >= D3D12_COOPERATIVE_VECTOR_TIER_1_0;
                mCoopVecTrainingSupported = experimentalOptions.CooperativeVectorTier >= D3D12_COOPERATIVE_VECTOR_TIER_1_1;
            }
        }
#endif

        if (hasOptions6)
        {
            mVariableRateShadingSupported = mOptions6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2;
        }

        D3D12_INDIRECT_ARGUMENT_DESC argumentDesc = {};
        D3D12_COMMAND_SIGNATURE_DESC signatureDesc = {};
        signatureDesc.NumArgumentDescs = 1;
        signatureDesc.pArgumentDescs = &argumentDesc;

        signatureDesc.ByteStride = 16;
        argumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        mContext.device->CreateCommandSignature(&signatureDesc, nullptr, IID_PPV_ARGS(mContext.drawIndirectSignature.ReleaseAndGetAddressOf()));

        signatureDesc.ByteStride = 20;
        argumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        mContext.device->CreateCommandSignature(&signatureDesc, nullptr, IID_PPV_ARGS(mContext.drawIndexedIndirectSignature.ReleaseAndGetAddressOf()));

        signatureDesc.ByteStride = 12;
        argumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        mContext.device->CreateCommandSignature(&signatureDesc, nullptr, IID_PPV_ARGS(mContext.dispatchIndirectSignature.ReleaseAndGetAddressOf()));

        mFenceEvent = ::CreateEvent(nullptr, false, false, nullptr);
        if (mFenceEvent == nullptr)
        {
            mContext.Error("Failed to create a D3D12 fence event.");
        }

        mCommandListsToExecute.reserve(64);

#if HRHI_D3D12_WITH_NVAPI
        mNvapiIsInitialized = NvAPI_Initialize() == NVAPI_OK;

        if (mNvapiIsInitialized)
        {
            NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS stereoParams = {};
            stereoParams.version = NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS_VER;

            if (NvAPI_D3D12_QuerySinglePassStereoSupport(mContext.device.Get(), &stereoParams) == NVAPI_OK
                && stereoParams.bSinglePassStereoSupported)
            {
                mSinglePassStereoSupported = true;
            }

            bool supported = false;
            if (NvAPI_D3D12_IsNvShaderExtnOpCodeSupported(mContext.device.Get(), NV_EXTN_OP_SHFL, &supported) == NVAPI_OK
                && supported)
            {
                mHlslExtensionsSupported = true;
            }

            supported = false;
            if (NvAPI_D3D12_IsNvShaderExtnOpCodeSupported(mContext.device.Get(), NV_EXTN_OP_FP16_ATOMIC, &supported) == NVAPI_OK
                && supported)
            {
                mFastGeometryShaderSupported = true;
            }

            NVAPI_D3D12_RAYTRACING_THREAD_REORDERING_CAPS serCaps = NVAPI_D3D12_RAYTRACING_THREAD_REORDERING_CAP_NONE;
            if (NvAPI_D3D12_GetRaytracingCaps(
                mContext.device.Get(),
                NVAPI_D3D12_RAYTRACING_CAPS_TYPE_THREAD_REORDERING,
                &serCaps,
                sizeof(serCaps)) == NVAPI_OK)
            {
                mShaderExecutionReorderingSupported =
                    (serCaps & NVAPI_D3D12_RAYTRACING_THREAD_REORDERING_CAP_STANDARD)
                    == NVAPI_D3D12_RAYTRACING_THREAD_REORDERING_CAP_STANDARD;
            }
        }

#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
#if HRHI_WITH_RTXMU
        mOpacityMicromapSupported = false;
#endif
#endif

#if HRHI_WITH_NVAPI_OPACITY_MICROMAP
#if HRHI_WITH_RTXMU
        mOpacityMicromapSupported = false;
#else
        if (mNvapiIsInitialized && mContext.device5 != nullptr)
        {
            NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAPS opacityMicromapCaps = NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAP_NONE;
            NvAPI_D3D12_GetRaytracingCaps(
                mContext.device5.Get(),
                NVAPI_D3D12_RAYTRACING_CAPS_TYPE_OPACITY_MICROMAP,
                &opacityMicromapCaps,
                sizeof(opacityMicromapCaps));
            mOpacityMicromapSupported = opacityMicromapCaps == NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAP_STANDARD;
        }
#endif
#endif

#if HRHI_WITH_NVAPI_CLUSTERS
        if (mNvapiIsInitialized && mContext.device5 != nullptr)
        {
            NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAPS clusterCaps = NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAP_NONE;
            NvAPI_D3D12_GetRaytracingCaps(
                mContext.device5.Get(),
                NVAPI_D3D12_RAYTRACING_CAPS_TYPE_CLUSTER_OPERATIONS,
                &clusterCaps,
                sizeof(clusterCaps));
            mRayTracingClustersSupported = clusterCaps == NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAP_STANDARD;
        }
#endif

#if HRHI_WITH_NVAPI_LSS
        if (mNvapiIsInitialized && mContext.device5 != nullptr)
        {
            NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAPS lssCaps = NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAP_NONE;
            NvAPI_D3D12_GetRaytracingCaps(
                mContext.device5.Get(),
                NVAPI_D3D12_RAYTRACING_CAPS_TYPE_LINEAR_SWEPT_SPHERES,
                &lssCaps,
                sizeof(lssCaps));
            mLinearSweptSpheresSupported = lssCaps == NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAP_STANDARD;

            NVAPI_D3D12_RAYTRACING_SPHERES_CAPS spheresCaps = NVAPI_D3D12_RAYTRACING_SPHERES_CAP_NONE;
            NvAPI_D3D12_GetRaytracingCaps(
                mContext.device5.Get(),
                NVAPI_D3D12_RAYTRACING_CAPS_TYPE_SPHERES,
                &spheresCaps,
                sizeof(spheresCaps));
            mSpheresSupported = spheresCaps == NVAPI_D3D12_RAYTRACING_SPHERES_CAP_STANDARD;
        }
#endif

#if HRHI_WITH_NVAPI_OPACITY_MICROMAP || HRHI_WITH_NVAPI_CLUSTERS || HRHI_WITH_NVAPI_LSS
        if (mContext.device5 != nullptr
            && (mOpacityMicromapSupported || mRayTracingClustersSupported || mLinearSweptSpheresSupported || mSpheresSupported))
        {
            NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS params = {};
            params.version = NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS_VER;
            params.flags = 0;
#if HRHI_WITH_NVAPI_OPACITY_MICROMAP
            params.flags |= mOpacityMicromapSupported ? NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_OMM_SUPPORT : 0;
#endif
#if HRHI_WITH_NVAPI_CLUSTERS
            params.flags |= mRayTracingClustersSupported ? NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_CLUSTER_SUPPORT : 0;
#endif
#if HRHI_WITH_NVAPI_LSS
            params.flags |= mLinearSweptSpheresSupported ? NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_LSS_SUPPORT : 0;
            params.flags |= mSpheresSupported ? NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_SPHERE_SUPPORT : 0;
#endif
            NvAPI_D3D12_SetCreatePipelineStateOptions(mContext.device5.Get(), &params);
        }
#endif
#endif

        if (desc.enableHeapDirectlyIndexed)
        {
            D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_6 };
            const bool hasShaderModel = SUCCEEDED(mContext.device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)));

            mHeapDirectlyIndexedEnabled =
                mOptions.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 &&
                hasShaderModel &&
                shaderModel.HighestShaderModel >= D3D_SHADER_MODEL_6_6;
        }
    }

    ZWD3D12Device::~ZWD3D12Device()
    {
        WaitForIdle();

        if (mFenceEvent != nullptr)
        {
            ::CloseHandle(mFenceEvent);
            mFenceEvent = nullptr;
        }
    }

    HCommon::ZWObject ZWD3D12Device::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case HRHIObjectTypes::gD3D12Device:
            return HCommon::ZWObject(mContext.device.Get());

        case HD3D12ObjectTypes::Nvrhi_D3D12_Device:
            return HCommon::ZWObject(this);

        case HRHIObjectTypes::gD3D12CommandQueue:
        {
            ZWD3D12Queue* graphicsQueue = GetQueue(ECommandQueue::Graphics);
            return graphicsQueue != nullptr ? HCommon::ZWObject(graphicsQueue->queue.Get()) : HCommon::ZWObject(nullptr);
        }

        default:
            return nullptr;
        }
    }

    ZWHeapHandle ZWD3D12Device::CreateHeap(const ZWHeapDesc& desc)
    {
        D3D12_HEAP_DESC heapDesc = {};
        heapDesc.SizeInBytes = desc.capacity;
        heapDesc.Alignment = D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;
        heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapDesc.Properties.CreationNodeMask = 1;
        heapDesc.Properties.VisibleNodeMask = 1;
        heapDesc.Flags = mOptions.ResourceHeapTier == D3D12_RESOURCE_HEAP_TIER_1
            ? D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES
            : D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;

        switch (desc.type)
        {
        case EHeapType::DeviceLocal:
            heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
            break;

        case EHeapType::Upload:
            heapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
            break;

        case EHeapType::Readback:
            heapDesc.Properties.Type = D3D12_HEAP_TYPE_READBACK;
            break;

        default:
            ReportInvalidHeapType(mContext);
            return nullptr;
        }

        HCommon::RefCountPtr<ID3D12Heap> d3d12Heap;
        const HRESULT result = mContext.device->CreateHeap(&heapDesc, IID_PPV_ARGS(d3d12Heap.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            std::stringstream messageBuilder;
            messageBuilder << "CreateHeap call failed for heap " << HApp::DebugNameToString(desc.debugName)
                << ", HRESULT = 0x" << std::hex << std::setw(8) << result;
            mContext.Error(messageBuilder.str());
            return nullptr;
        }

        if (!desc.debugName.empty())
        {
            const std::wstring debugName(desc.debugName.begin(), desc.debugName.end());
            d3d12Heap->SetName(debugName.c_str());
        }

        ZWD3D12Heap* heap = new ZWD3D12Heap();
        heap->heap = d3d12Heap;
        heap->desc = desc;
        return ZWHeapHandle::Create(heap);
    }

    HRHI::ZWCommandListHandle ZWD3D12Device::CreateCommandList(const ZWCommandListParameters& params)
    {
        if (GetQueue(params.queueType) == nullptr)
        {
            return nullptr;
        }

        return HRHI::ZWCommandListHandle::Create(new ZWD3D12CommandList(this, mContext, mResources, params));
    }

    uint64_t ZWD3D12Device::ExecuteCommandLists(HRHI::ICommandList* const* pCommandLists, size_t numCommandLists, ECommandQueue executionQueue)
    {
        if (numCommandLists == 0 || pCommandLists == nullptr)
        {
            return 0;
        }

        ZWD3D12Queue* queue = GetQueue(executionQueue);
        if (queue == nullptr || queue->queue == nullptr)
        {
            return 0;
        }

        mCommandListsToExecute.resize(numCommandLists);
        for (size_t commandListIndex = 0; commandListIndex < numCommandLists; ++commandListIndex)
        {
            ZWD3D12CommandList* commandList = static_cast<ZWD3D12CommandList*>(pCommandLists[commandListIndex]);
            mCommandListsToExecute[commandListIndex] = commandList != nullptr ? commandList->GetD3D12CommandList() : nullptr;
        }

        queue->queue->ExecuteCommandLists(static_cast<uint32_t>(mCommandListsToExecute.size()), mCommandListsToExecute.data());
        queue->lastSubmittedInstance += 1;
        queue->queue->Signal(queue->fence.Get(), queue->lastSubmittedInstance);

        for (size_t commandListIndex = 0; commandListIndex < numCommandLists; ++commandListIndex)
        {
            ZWD3D12CommandList* commandList = static_cast<ZWD3D12CommandList*>(pCommandLists[commandListIndex]);
            if (commandList != nullptr)
            {
                std::shared_ptr<CommandListInstance> instance = commandList->Executed(queue);
                if (instance != nullptr)
                {
                    queue->commandListsInFlight.push_front(instance);
                }
            }
        }

        if (FAILED(mContext.device->GetDeviceRemovedReason()))
        {
            mContext.Error("D3D12 device removed.");
        }

        return queue->lastSubmittedInstance;
    }

    void ZWD3D12Device::QueueWaitForCommandList(ECommandQueue waitQueue, ECommandQueue executionQueue, uint64_t instance)
    {
        ZWD3D12Queue* waitD3D12Queue = GetQueue(waitQueue);
        ZWD3D12Queue* executionD3D12Queue = GetQueue(executionQueue);
        if (waitD3D12Queue == nullptr || executionD3D12Queue == nullptr || instance > executionD3D12Queue->lastSubmittedInstance)
        {
            return;
        }

        waitD3D12Queue->queue->Wait(executionD3D12Queue->fence.Get(), instance);
    }

    bool ZWD3D12Device::WaitForIdle()
    {
        for (const std::unique_ptr<ZWD3D12Queue>& queue : mQueues)
        {
            if (queue == nullptr)
            {
                continue;
            }

            if (queue->UpdateLastCompletedInstance() < queue->lastSubmittedInstance)
            {
                WaitForFence(queue->fence.Get(), queue->lastSubmittedInstance, mFenceEvent);
            }
        }

        return true;
    }

    void ZWD3D12Device::RunGarbageCollection()
    {
        for (const std::unique_ptr<ZWD3D12Queue>& queue : mQueues)
        {
            if (queue == nullptr)
            {
                continue;
            }

            queue->UpdateLastCompletedInstance();

            while (!queue->commandListsInFlight.empty())
            {
                const std::shared_ptr<CommandListInstance>& instance = queue->commandListsInFlight.back();
                if (instance == nullptr || queue->lastCompletedInstance < instance->submittedInstance)
                {
                    break;
                }

#if HRHI_WITH_RTXMU
                if (!instance->rtxmuBuildIds.empty())
                {
                    std::lock_guard<std::mutex> lockGuard(mResources.asListMutex);
                    mResources.asBuildsCompleted.insert(
                        mResources.asBuildsCompleted.end(),
                        instance->rtxmuBuildIds.begin(),
                        instance->rtxmuBuildIds.end());
                    instance->rtxmuBuildIds.clear();
                }

                if (!instance->rtxmuCompactionIds.empty() && mContext.rtxMemUtil != nullptr)
                {
                    mContext.rtxMemUtil->GarbageCollection(instance->rtxmuCompactionIds);
                    instance->rtxmuCompactionIds.clear();
                }
#endif

                queue->commandListsInFlight.pop_back();
            }
        }
    }

    bool ZWD3D12Device::QueryFeatureSupport(EFeature feature, void* pInfo, size_t infoSize)
    {
        switch (feature)
        {
        case EFeature::DeferredCommandLists:
            return true;

        case EFeature::SinglePassStereo:
            return mSinglePassStereoSupported;

        case EFeature::RayTracingAccelStruct:
        case EFeature::RayTracingPipeline:
            return mRayTracingSupported;

        case EFeature::RayTracingOpacityMicromap:
            return mOpacityMicromapSupported;

        case EFeature::RayTracingClusters:
            return mRayTracingClustersSupported;

        case EFeature::RayQuery:
            return mTraceRayInlineSupported;

        case EFeature::FastGeometryShader:
            return mFastGeometryShaderSupported;

        case EFeature::ShaderExecutionReordering:
            return mShaderExecutionReorderingSupported;

        case EFeature::Spheres:
            return mSpheresSupported;

        case EFeature::LinearSweptSpheres:
            return mLinearSweptSpheresSupported;

        case EFeature::Meshlets:
            return mMeshletsSupported;

        case EFeature::VariableRateShading:
            if (pInfo != nullptr && infoSize == sizeof(ZWVariableRateShadingFeatureInfo))
            {
                auto* variableRateShadingInfo = static_cast<ZWVariableRateShadingFeatureInfo*>(pInfo);
                variableRateShadingInfo->shadingRateImageTileSize = mOptions6.ShadingRateImageTileSize;
            }
            return mVariableRateShadingSupported;

        case EFeature::VirtualResources:
            return true;

        case EFeature::ComputeQueue:
            return GetQueue(ECommandQueue::Compute) != nullptr;

        case EFeature::CopyQueue:
            return GetQueue(ECommandQueue::Copy) != nullptr;

        case EFeature::ConservativeRasterization:
            return true;

        case EFeature::ConstantBufferRanges:
            return true;

        case EFeature::HeapDirectlyIndexed:
            return mHeapDirectlyIndexedEnabled;

        case EFeature::SamplerFeedback:
            return mSamplerFeedbackSupported;

        case EFeature::HlslExtensionUAV:
            return mHlslExtensionsSupported;

        case EFeature::WaveLaneCountMinMax:
            if (mOptions1.WaveLaneCountMin == 0)
            {
                return false;
            }

            if (pInfo != nullptr && infoSize == sizeof(ZWWaveLaneCountMinMaxFeatureInfo))
            {
                auto* waveLaneInfo = static_cast<ZWWaveLaneCountMinMaxFeatureInfo*>(pInfo);
                waveLaneInfo->minWaveLaneCount = mOptions1.WaveLaneCountMin;
                waveLaneInfo->maxWaveLaneCount = mOptions1.WaveLaneCountMax;
            }
            return true;

        case EFeature::ShaderSpecializations:
            return false;

        case EFeature::CooperativeVectorInferencing:
            return mCoopVecInferencingSupported;

        case EFeature::CooperativeVectorTraining:
            return mCoopVecTrainingSupported;

        default:
            return false;
        }
    }

    EFormatSupport ZWD3D12Device::QueryFormatSupport(EFormat format)
    {
        const ZWDxgiFormatMapping& formatMapping = GetDxgiFormatMapping(format);
        EFormatSupport result = EFormatSupport::None;

        D3D12_FEATURE_DATA_FORMAT_SUPPORT featureData = {};
        featureData.Format = formatMapping.rtvFormat;
        if (FAILED(mContext.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &featureData, sizeof(featureData))))
        {
            return result;
        }

        if ((featureData.Support1 & D3D12_FORMAT_SUPPORT1_BUFFER) != 0)
            result = result | EFormatSupport::Buffer;
        if ((featureData.Support1 & (D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_TEXTURE3D | D3D12_FORMAT_SUPPORT1_TEXTURECUBE)) != 0)
            result = result | EFormatSupport::Texture;
        if ((featureData.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) != 0)
            result = result | EFormatSupport::DepthStencil;
        if ((featureData.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) != 0)
            result = result | EFormatSupport::RenderTarget;
        if ((featureData.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE) != 0)
            result = result | EFormatSupport::Blendable;

        if (formatMapping.srvFormat != featureData.Format)
        {
            featureData.Format = formatMapping.srvFormat;
            featureData.Support1 = D3D12_FORMAT_SUPPORT1_NONE;
            featureData.Support2 = D3D12_FORMAT_SUPPORT2_NONE;

            if (FAILED(mContext.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &featureData, sizeof(featureData))))
            {
                return result;
            }
        }

        if ((featureData.Support1 & D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER) != 0)
            result = result | EFormatSupport::IndexBuffer;
        if ((featureData.Support1 & D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER) != 0)
            result = result | EFormatSupport::VertexBuffer;
        if ((featureData.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) != 0)
            result = result | EFormatSupport::ShaderLoad;
        if ((featureData.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) != 0)
            result = result | EFormatSupport::ShaderSample;
        if ((featureData.Support2 & D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD) != 0)
            result = result | EFormatSupport::ShaderAtomic;
        if ((featureData.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0)
            result = result | EFormatSupport::ShaderUavLoad;
        if ((featureData.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0)
            result = result | EFormatSupport::ShaderUavStore;

        return result;
    }

    HCoopVec::ZWDeviceFeatures ZWD3D12Device::QueryCoopVecFeatures()
    {
        HCoopVec::ZWDeviceFeatures result;

#if HRHI_D3D12_WITH_COOPVEC
        D3D12_FEATURE_DATA_COOPERATIVE_VECTOR cooperativeVectorData = {};
        if (mContext.device->CheckFeatureSupport(
            D3D12_FEATURE_COOPERATIVE_VECTOR,
            &cooperativeVectorData,
            static_cast<UINT>(sizeof(cooperativeVectorData))) != S_OK)
        {
            return result;
        }

        std::vector<D3D12_COOPERATIVE_VECTOR_PROPERTIES_MUL> matMulProperties(cooperativeVectorData.MatrixVectorMulAddPropCount);
        std::vector<D3D12_COOPERATIVE_VECTOR_PROPERTIES_ACCUMULATE> outerProductAccumulateProperties(cooperativeVectorData.OuterProductAccumulatePropCount);
        std::vector<D3D12_COOPERATIVE_VECTOR_PROPERTIES_ACCUMULATE> vectorAccumulateProperties(cooperativeVectorData.VectorAccumulatePropCount);

        cooperativeVectorData.pMatrixVectorMulAddProperties = matMulProperties.data();
        cooperativeVectorData.pOuterProductAccumulateProperties = outerProductAccumulateProperties.data();
        cooperativeVectorData.pVectorAccumulateProperties = vectorAccumulateProperties.data();

        if (mContext.device->CheckFeatureSupport(
            D3D12_FEATURE_COOPERATIVE_VECTOR,
            &cooperativeVectorData,
            static_cast<UINT>(sizeof(cooperativeVectorData))) != S_OK)
        {
            return result;
        }

        result.matMulFormats.reserve(matMulProperties.size());
        for (const D3D12_COOPERATIVE_VECTOR_PROPERTIES_MUL& property : matMulProperties)
        {
            HCoopVec::ZWMatMulFormatCombo& combo = result.matMulFormats.emplace_back();
            combo.inputType = ConvertCoopVecDataType(property.InputType);
            combo.inputInterpretation = ConvertCoopVecDataType(property.InputInterpretation);
            combo.matrixInterpretation = ConvertCoopVecDataType(property.MatrixInterpretation);
            combo.biasInterpretation = ConvertCoopVecDataType(property.BiasInterpretation);
            combo.outputType = ConvertCoopVecDataType(property.OutputType);
            combo.transposeSupported = property.TransposeSupported != 0;
        }

        bool outerProductFloat16Supported = false;
        bool outerProductFloat32Supported = false;
        bool vectorAccumulateFloat16Supported = false;
        bool vectorAccumulateFloat32Supported = false;

        for (const D3D12_COOPERATIVE_VECTOR_PROPERTIES_ACCUMULATE& property : outerProductAccumulateProperties)
        {
            if (property.AccumulationType == D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT16)
            {
                outerProductFloat16Supported = true;
            }
            else if (property.AccumulationType == D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT32)
            {
                outerProductFloat32Supported = true;
            }
        }

        for (const D3D12_COOPERATIVE_VECTOR_PROPERTIES_ACCUMULATE& property : vectorAccumulateProperties)
        {
            if (property.AccumulationType == D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT16)
            {
                vectorAccumulateFloat16Supported = true;
            }
            else if (property.AccumulationType == D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT32)
            {
                vectorAccumulateFloat32Supported = true;
            }
        }

        result.trainingFloat16 = outerProductFloat16Supported && vectorAccumulateFloat16Supported;
        result.trainingFloat32 = outerProductFloat32Supported && vectorAccumulateFloat32Supported;
#endif

        return result;
    }

    size_t ZWD3D12Device::GetCoopVecMatrixSize(HCoopVec::EDataType type, HCoopVec::EMatrixLayout layout, int rows, int columns)
    {
#if HRHI_D3D12_WITH_COOPVEC
        if (mContext.devicePreview == nullptr)
        {
            return 0;
        }

        D3D12_LINEAR_ALGEBRA_MATRIX_CONVERSION_DEST_INFO destinationInfo = {};
        destinationInfo.DestLayout = ConvertCoopVecMatrixLayout(layout);
        destinationInfo.NumRows = rows;
        destinationInfo.NumColumns = columns;
        destinationInfo.DestDataType = ConvertCoopVecDataType(type);
        destinationInfo.DestStride = static_cast<UINT>(HCoopVec::getOptimalMatrixStride(type, layout, rows, columns));

        mContext.devicePreview->GetLinearAlgebraMatrixConversionDestinationInfo(&destinationInfo);
        return destinationInfo.DestSize;
#else
        (void)type;
        (void)layout;
        (void)rows;
        (void)columns;
        return 0;
#endif
    }

    HCommon::ZWObject ZWD3D12Device::GetNativeQueue(ObjectType objectType, ECommandQueue queue)
    {
        if (objectType != HRHIObjectTypes::gD3D12CommandQueue || queue >= ECommandQueue::Count)
        {
            return nullptr;
        }

        ZWD3D12Queue* d3d12Queue = GetQueue(queue);
        if (d3d12Queue == nullptr)
        {
            return nullptr;
        }

        return HCommon::ZWObject(d3d12Queue->queue.Get());
    }

    IDescriptorHeap* ZWD3D12Device::GetDescriptorHeap(EDescriptorHeapType heapType)
    {
        switch (heapType)
        {
        case EDescriptorHeapType::RenderTargetView:
            return &mResources.renderTargetViewHeap;

        case EDescriptorHeapType::DepthStencilView:
            return &mResources.depthStencilViewHeap;

        case EDescriptorHeapType::ShaderResourceView:
            return &mResources.shaderResourceViewHeap;

        case EDescriptorHeapType::Sampler:
            return &mResources.samplerHeap;

        default:
            return nullptr;
        }
    }

    ZWD3D12Sampler::ZWD3D12Sampler(const ZWD3D12Context& context, const ZWSamplerDesc& desc)
        : mContext(context)
        , mDesc(desc)
        , md3d12desc{}
    {
        const UINT reductionType = ConvertSamplerReductionType(desc.reductionType);

        if (mDesc.maxAnisotropy > 1.f)
        {
            md3d12desc.Filter = D3D12_ENCODE_ANISOTROPIC_FILTER(reductionType);
        }
        else
        {
            md3d12desc.Filter = D3D12_ENCODE_BASIC_FILTER(
                mDesc.minFilter ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
                mDesc.magFilter ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
                mDesc.mipFilter ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
                reductionType);
        }

        md3d12desc.AddressU = ConvertSamplerAddressMode(mDesc.addressU);
        md3d12desc.AddressV = ConvertSamplerAddressMode(mDesc.addressV);
        md3d12desc.AddressW = ConvertSamplerAddressMode(mDesc.addressW);
        md3d12desc.MipLODBias = mDesc.mipBias;
        md3d12desc.MaxAnisotropy = std::max(static_cast<UINT>(mDesc.maxAnisotropy), 1u);
        md3d12desc.ComparisonFunc = desc.reductionType == ESamplerReductionType::Comparison
            ? D3D12_COMPARISON_FUNC_LESS
            : D3D12_COMPARISON_FUNC_NEVER;
        md3d12desc.BorderColor[0] = mDesc.borderColor.r;
        md3d12desc.BorderColor[1] = mDesc.borderColor.g;
        md3d12desc.BorderColor[2] = mDesc.borderColor.b;
        md3d12desc.BorderColor[3] = mDesc.borderColor.a;
        md3d12desc.MinLOD = 0.f;
        md3d12desc.MaxLOD = D3D12_FLOAT32_MAX;
    }

    void ZWD3D12Sampler::CreateDescriptor(size_t descriptor) const
    {
        mContext.device->CreateSampler(&md3d12desc, { descriptor });
    }

    ZWSamplerHandle ZWD3D12Device::CreateSampler(const ZWSamplerDesc& desc)
    {
        return ZWSamplerHandle::Create(new ZWD3D12Sampler(mContext, desc));
    }

    EGraphicsAPI ZWD3D12Device::GetGraphicsAPI()
    {
        return EGraphicsAPI::D3D12;
    }
}
