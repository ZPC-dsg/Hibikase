#include <BackEnd/vulkanbackend.h>

#include <cassert>

namespace HRHI
{
    ZWVKCommandList::ZWVKCommandList(
        ZWVKDevice* device,
        const ZWVKContext& context,
        const ZWCommandListParameters& parameters)
        : mDevice(device)
        , mContext(context)
        , mCommandListParameters(parameters)
        , mStateTracker(context.messageCallback)
        , m_UploadManager(std::make_unique<ZWVKUploadManager>(device, parameters.uploadChunkSize, 0, false))
        , m_ScratchManager(std::make_unique<ZWVKUploadManager>(device, parameters.scratchChunkSize, parameters.scratchMaxMemory, true))
    {
#if HRHI_WITH_AFTERMATH
        if (mDevice != nullptr && mDevice->IsAftermathEnabled())
        {
            mDevice->GetAftermathCrashDumpHelper().RegisterAftermathMarkerTracker(&mAftermathTracker);
        }
#endif
    }

    ZWVKCommandList::~ZWVKCommandList()
    {
#if HRHI_WITH_AFTERMATH
        if (mDevice != nullptr && mDevice->IsAftermathEnabled())
        {
            mDevice->GetAftermathCrashDumpHelper().UnRegisterAftermathMarkerTracker(&mAftermathTracker);
        }
#endif
    }

    HCommon::ZWObject ZWVKCommandList::GetNativeObject(ObjectType objectType)
    {
        if (objectType == HRHIObjectTypes::gVKCommandBuffer && mCurrentCmdBuf != nullptr)
        {
            return HCommon::ZWObject(VkCommandBuffer(mCurrentCmdBuf->cmdBuf));
        }

        return nullptr;
    }

    void ZWVKCommandList::Open()
    {
        assert(mDevice != nullptr);

        ZWVKQueue* queue = mDevice->GetQueue(mCommandListParameters.queueType);
        if (queue == nullptr)
        {
            mContext.Error("Failed to acquire a Vulkan queue for command list recording.");
            return;
        }

        mCurrentCmdBuf = queue->GetOrCreateCommandBuffer();
        if (mCurrentCmdBuf == nullptr)
        {
            mContext.Error("Failed to allocate a Vulkan command buffer.");
            return;
        }

        const vk::CommandBufferBeginInfo beginInfo =
            vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

        const vk::Result beginResult = mCurrentCmdBuf->cmdBuf.begin(&beginInfo);
        ASSERT_VK_OK(beginResult);
        if (beginResult != vk::Result::eSuccess)
        {
            mContext.Error("Failed to begin recording a Vulkan command buffer.");
            mCurrentCmdBuf = nullptr;
            return;
        }

        // Keep the command list alive while queued resources reference it.
        mCurrentCmdBuf->referencedResources.push_back(this);

        ClearState();
    }

    void ZWVKCommandList::Close()
    {
        if (mCurrentCmdBuf == nullptr)
        {
            return;
        }

        EndRenderPass();

        mStateTracker.KeepBufferInitialStates();
        mStateTracker.KeepTextureInitialStates();
        CommitBarriers();

#if HRHI_WITH_RTXMU
        if (mContext.rtxMemUtil != nullptr && !mCurrentCmdBuf->rtxmuBuildIds.empty())
        {
            mContext.rtxMemUtil->PopulateCompactionSizeCopiesCommandList(
                mCurrentCmdBuf->cmdBuf,
                mCurrentCmdBuf->rtxmuBuildIds);
        }
#endif

        mCurrentCmdBuf->cmdBuf.end();

        ClearState();

        FlushVolatileBufferWrites();
        m_UncachedShaderTableStates.clear();
    }

    void ZWVKCommandList::ClearState()
    {
        EndRenderPass();

        mCurrentPipelineLayout = vk::PipelineLayout();
        mCurrentPushConstantsVisibility = vk::ShaderStageFlags();

        mCurrentGraphicsState = ZWGraphicsState();
        mCurrentComputeState = ZWComputeState();
        mCurrentMeshletState = ZWMeshletState();
        mCurrentRayTracingState = Hrt::ZWState();

        mAnyVolatileBufferWrites = false;
        mBindingStatesDirty = false;
    }

    void ZWVKCommandList::SetPushConstants(const void* data, size_t byteSize)
    {
        assert(mCurrentCmdBuf != nullptr);

        mCurrentCmdBuf->cmdBuf.pushConstants(
            mCurrentPipelineLayout,
            mCurrentPushConstantsVisibility,
            0,
            static_cast<uint32_t>(byteSize),
            data);
    }

    void ZWVKCommandList::Executed(ZWVKQueue& queue, uint64_t submissionID)
    {
        assert(mCurrentCmdBuf != nullptr);

        mCurrentCmdBuf->submissionID = submissionID;

        const ECommandQueue queueId = queue.GetQueueID();
        const uint64_t recordingID = mCurrentCmdBuf->recordingID;

        mCurrentCmdBuf = nullptr;

        SubmitVolatileBuffers(recordingID, submissionID);

        mStateTracker.CommandListSubmitted();

        m_UploadManager->SubmitChunks(
            MakeVersion(recordingID, queueId, false),
            MakeVersion(submissionID, queueId, true));

        m_ScratchManager->SubmitChunks(
            MakeVersion(recordingID, queueId, false),
            MakeVersion(submissionID, queueId, true));

        mVolatileBufferStates.clear();
    }

    void ZWVKCommandList::ConvertCoopVecMatrices(HCoopVec::ZWConvertMatrixLayoutDesc const* convertDescs, size_t numDescs)
    {
#if HRHI_VULKAN_HAS_NV_COOPERATIVE_VECTOR
        if (convertDescs == nullptr || numDescs == 0 || mCurrentCmdBuf == nullptr)
        {
            return;
        }

        if (!mContext.extensions.NV_cooperative_vector)
        {
            mContext.Error("CoopVec matrix conversion is unavailable because VK_NV_cooperative_vector is not enabled on this Vulkan device.");
            return;
        }

        std::vector<vk::ConvertCooperativeVectorMatrixInfoNV> vkConvertDescs;
        vkConvertDescs.reserve(numDescs);

        std::vector<size_t> dstSizes;
        dstSizes.reserve(numDescs);

        for (size_t index = 0; index < numDescs; ++index)
        {
            const HCoopVec::ZWConvertMatrixLayoutDesc& desc = convertDescs[index];
            if (desc.src.buffer == nullptr || desc.dst.buffer == nullptr)
            {
                continue;
            }

            if (m_EnableAutomaticBarriers)
            {
                RequireBufferState(desc.src.buffer, EResourceStates::ConvertCoopVecMatrixInput);
                RequireBufferState(desc.dst.buffer, EResourceStates::ConvertCoopVecMatrixOutput);
                mBindingStatesDirty = true;
            }

            vk::ConvertCooperativeVectorMatrixInfoNV& vkDesc = vkConvertDescs.emplace_back();
            vkDesc.sType = vk::StructureType::eConvertCooperativeVectorMatrixInfoNV;
            vkDesc.srcSize = desc.src.size;
            vkDesc.srcData.deviceAddress = desc.src.buffer->GetGpuVirtualAddress() + desc.src.offset;
            vkDesc.pDstSize = &dstSizes.emplace_back(desc.dst.size);
            vkDesc.dstData.deviceAddress = desc.dst.buffer->GetGpuVirtualAddress() + desc.dst.offset;
            vkDesc.srcComponentType = ConvertCoopVecDataType(desc.src.type);
            vkDesc.dstComponentType = ConvertCoopVecDataType(desc.dst.type);
            vkDesc.numRows = desc.numRows;
            vkDesc.numColumns = desc.numColumns;

            vkDesc.srcLayout = ConvertCoopVecMatrixLayout(desc.src.layout);
            vkDesc.srcStride = desc.src.stride != 0
                ? desc.src.stride
                : HCoopVec::getOptimalMatrixStride(desc.src.type, desc.src.layout, desc.numRows, desc.numColumns);

            vkDesc.dstLayout = ConvertCoopVecMatrixLayout(desc.dst.layout);
            vkDesc.dstStride = desc.dst.stride != 0
                ? desc.dst.stride
                : HCoopVec::getOptimalMatrixStride(desc.dst.type, desc.dst.layout, desc.numRows, desc.numColumns);
        }

        CommitBarriers();

        if (!vkConvertDescs.empty())
        {
            mCurrentCmdBuf->cmdBuf.convertCooperativeVectorMatrixNV(vkConvertDescs);
        }
#else
        (void)convertDescs;
        (void)numDescs;
        if (numDescs != 0)
        {
            mContext.Error("CoopVec matrix conversion is not supported by this Vulkan backend build.");
        }
#endif
    }
}
