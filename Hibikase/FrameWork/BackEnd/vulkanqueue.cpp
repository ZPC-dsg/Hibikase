#include <BackEnd/vulkanbackend.h>

#include <cassert>
#include <vector>

namespace HRHI
{

vk::ImageAspectFlags GuessImageAspectFlags(vk::Format format);

ZWVKTrackedCommandBuffer::~ZWVKTrackedCommandBuffer()
{
    if (cmdPool)
    {
        mContext.device.destroyCommandPool(cmdPool, mContext.allocationCallbacks);
        cmdPool = vk::CommandPool();
    }
}

ZWVKQueue::ZWVKQueue(const ZWVKContext& context, ECommandQueue queueID, vk::Queue queue, uint32_t queueFamilyIndex)
    : mContext(context)
    , mQueue(queue)
    , mQueueID(queueID)
    , mQueueFamilyIndex(queueFamilyIndex)
{
    const vk::SemaphoreTypeCreateInfo semaphoreTypeInfo =
        vk::SemaphoreTypeCreateInfo().setSemaphoreType(vk::SemaphoreType::eTimeline);
    const vk::SemaphoreCreateInfo semaphoreInfo =
        vk::SemaphoreCreateInfo().setPNext(&semaphoreTypeInfo);

    trackingSemaphore = context.device.createSemaphore(semaphoreInfo, context.allocationCallbacks);
}

ZWVKQueue::~ZWVKQueue()
{
    if (trackingSemaphore)
    {
        mContext.device.destroySemaphore(trackingSemaphore, mContext.allocationCallbacks);
        trackingSemaphore = vk::Semaphore();
    }
}

ZWVKTrackedCommandBufferPtr ZWVKQueue::CreateCommandBuffer()
{
    ZWVKTrackedCommandBufferPtr cmdBuffer = std::make_shared<ZWVKTrackedCommandBuffer>(mContext);

    const vk::CommandPoolCreateInfo poolInfo =
        vk::CommandPoolCreateInfo()
            .setQueueFamilyIndex(mQueueFamilyIndex)
            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                vk::CommandPoolCreateFlagBits::eTransient);

    vk::Result result = mContext.device.createCommandPool(&poolInfo, mContext.allocationCallbacks, &cmdBuffer->cmdPool);
    CHECK_VK_FAIL(result)

    const vk::CommandBufferAllocateInfo allocInfo =
        vk::CommandBufferAllocateInfo()
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandPool(cmdBuffer->cmdPool)
            .setCommandBufferCount(1);

    result = mContext.device.allocateCommandBuffers(&allocInfo, &cmdBuffer->cmdBuf);
    CHECK_VK_FAIL(result)

    return cmdBuffer;
}

ZWVKTrackedCommandBufferPtr ZWVKQueue::GetOrCreateCommandBuffer()
{
    std::lock_guard lockGuard(mMutex);

    const uint64_t recordingID = ++mLastRecordingID;
    ZWVKTrackedCommandBufferPtr cmdBuf;

    if (mCommandBuffersPool.empty())
    {
        cmdBuf = CreateCommandBuffer();
    }
    else
    {
        cmdBuf = mCommandBuffersPool.front();
        mCommandBuffersPool.pop_front();
    }

    if (cmdBuf != nullptr)
    {
        cmdBuf->recordingID = recordingID;
    }

    return cmdBuf;
}

void ZWVKQueue::AddWaitSemaphore(vk::Semaphore semaphore, uint64_t value)
{
    if (!semaphore)
    {
        return;
    }

    mWaitSemaphores.push_back(semaphore);
    mWaitSemaphoreValues.push_back(value);
}

void ZWVKQueue::AddSignalSemaphore(vk::Semaphore semaphore, uint64_t value)
{
    if (!semaphore)
    {
        return;
    }

    mSignalSemaphores.push_back(semaphore);
    mSignalSemaphoreValues.push_back(value);
}

uint64_t ZWVKQueue::Submit(ICommandList* const* ppCmd, size_t numCmd)
{
    std::vector<vk::PipelineStageFlags> waitStageArray(mWaitSemaphores.size());
    std::vector<vk::CommandBuffer> commandBuffers(numCmd);

    for (size_t index = 0; index < mWaitSemaphores.size(); ++index)
    {
        waitStageArray[index] = vk::PipelineStageFlagBits::eTopOfPipe;
    }

    mLastSubmittedID++;

    for (size_t index = 0; index < numCmd; ++index)
    {
        ZWVKCommandList* commandList = static_cast<ZWVKCommandList*>(ppCmd[index]);
        ZWVKTrackedCommandBufferPtr commandBuffer = commandList != nullptr ? commandList->GetCurrentCmdBuf() : nullptr;
        if (commandBuffer == nullptr)
        {
            continue;
        }

        commandBuffers[index] = commandBuffer->cmdBuf;
        mCommandBuffersInFlight.push_back(commandBuffer);

        for (const auto& stagingBufferHandle : commandBuffer->referencedStagingBuffers)
        {
            ZWVKBuffer* stagingBuffer = static_cast<ZWVKBuffer*>(stagingBufferHandle.Get());
            if (stagingBuffer != nullptr)
            {
                stagingBuffer->lastUseQueue = mQueueID;
                stagingBuffer->lastUseCommandListID = mLastSubmittedID;
            }
        }
    }

    mSignalSemaphores.push_back(trackingSemaphore);
    mSignalSemaphoreValues.push_back(mLastSubmittedID);

    vk::TimelineSemaphoreSubmitInfo timelineSemaphoreInfo =
        vk::TimelineSemaphoreSubmitInfo()
            .setSignalSemaphoreValueCount(static_cast<uint32_t>(mSignalSemaphoreValues.size()))
            .setPSignalSemaphoreValues(mSignalSemaphoreValues.data());

    if (!mWaitSemaphoreValues.empty())
    {
        timelineSemaphoreInfo.setWaitSemaphoreValueCount(static_cast<uint32_t>(mWaitSemaphoreValues.size()));
        timelineSemaphoreInfo.setPWaitSemaphoreValues(mWaitSemaphoreValues.data());
    }

    const vk::SubmitInfo submitInfo =
        vk::SubmitInfo()
            .setPNext(&timelineSemaphoreInfo)
            .setCommandBufferCount(static_cast<uint32_t>(numCmd))
            .setPCommandBuffers(commandBuffers.data())
            .setWaitSemaphoreCount(static_cast<uint32_t>(mWaitSemaphores.size()))
            .setPWaitSemaphores(mWaitSemaphores.empty() ? nullptr : mWaitSemaphores.data())
            .setPWaitDstStageMask(waitStageArray.data())
            .setSignalSemaphoreCount(static_cast<uint32_t>(mSignalSemaphores.size()))
            .setPSignalSemaphores(mSignalSemaphores.empty() ? nullptr : mSignalSemaphores.data());

    try
    {
        mQueue.submit(submitInfo);
    }
    catch (const vk::DeviceLostError&)
    {
        mContext.Error("Vulkan device removed while submitting command buffers.");
    }

    mWaitSemaphores.clear();
    mWaitSemaphoreValues.clear();
    mSignalSemaphores.clear();
    mSignalSemaphoreValues.clear();

    return mLastSubmittedID;
}

void ZWVKQueue::UpdateTextureTileMappings(ITexture* texture, const ZWTextureTilesMapping* tileMappings, uint32_t numTileMappings)
{
    ZWVKTexture* vkTexture = static_cast<ZWVKTexture*>(texture);
    if (vkTexture == nullptr)
    {
        return;
    }

    std::vector<vk::SparseImageMemoryBind> sparseImageMemoryBinds;
    std::vector<vk::SparseMemoryBind> sparseMemoryBinds;

    vk::ImageCreateInfo& imageInfo = vkTexture->imageInfo;
    const vk::ImageAspectFlags textureAspectFlags = GuessImageAspectFlags(imageInfo.format);

    uint32_t tileWidth = 1;
    uint32_t tileHeight = 1;
    uint32_t tileDepth = 1;

    vk::DeviceSize imageMipTailOffset = 0;

    const std::vector<vk::SparseImageFormatProperties> formatProperties =
        mContext.physicalDevice.getSparseImageFormatProperties(
            imageInfo.format, imageInfo.imageType, imageInfo.samples, imageInfo.usage, imageInfo.tiling);
    const std::vector<vk::SparseImageMemoryRequirements> memoryRequirements =
        mContext.device.getImageSparseMemoryRequirements(vkTexture->image);

    if (!formatProperties.empty())
    {
        tileWidth = formatProperties[0].imageGranularity.width;
        tileHeight = formatProperties[0].imageGranularity.height;
        tileDepth = formatProperties[0].imageGranularity.depth;
    }

    if (!memoryRequirements.empty())
    {
        imageMipTailOffset = memoryRequirements[0].imageMipTailOffset;
    }

    for (size_t mappingIndex = 0; mappingIndex < numTileMappings; ++mappingIndex)
    {
        const uint32_t numRegions = tileMappings[mappingIndex].numTextureRegions;
        ZWVKHeap* heap = tileMappings[mappingIndex].heap
            ? static_cast<ZWVKHeap*>(tileMappings[mappingIndex].heap)
            : nullptr;
        vk::DeviceMemory deviceMemory = heap ? heap->memory : VK_NULL_HANDLE;

        for (uint32_t regionIndex = 0; regionIndex < numRegions; ++regionIndex)
        {
            const ZWTiledTextureCoordinate& tiledTextureCoordinate = tileMappings[mappingIndex].tiledTextureCoordinates[regionIndex];
            const ZWTiledTextureRegion& tiledTextureRegion = tileMappings[mappingIndex].tiledTextureRegions[regionIndex];

            if (tiledTextureRegion.tilesNum)
            {
                sparseMemoryBinds.push_back(
                    vk::SparseMemoryBind()
                        .setResourceOffset(imageMipTailOffset + tiledTextureCoordinate.arrayLevel * imageMipTailOffset)
                        .setSize(tiledTextureRegion.tilesNum * ZWVKTexture::tileByteSize)
                        .setMemory(deviceMemory)
                        .setMemoryOffset(deviceMemory ? tileMappings[mappingIndex].byteOffsets[regionIndex] : 0));
            }
            else
            {
                vk::ImageSubresource subresource = {};
                subresource.arrayLayer = tiledTextureCoordinate.arrayLevel;
                subresource.mipLevel = tiledTextureCoordinate.mipLevel;
                subresource.aspectMask = textureAspectFlags;

                vk::Offset3D offset3D;
                offset3D.x = tiledTextureCoordinate.x * tileWidth;
                offset3D.y = tiledTextureCoordinate.y * tileHeight;
                offset3D.z = tiledTextureCoordinate.z * tileHeight;

                vk::Extent3D extent3D;
                extent3D.width = tiledTextureRegion.width * tileWidth;
                extent3D.height = tiledTextureRegion.height * tileHeight;
                extent3D.depth = tiledTextureRegion.depth * tileDepth;

                sparseImageMemoryBinds.push_back(
                    vk::SparseImageMemoryBind()
                        .setSubresource(subresource)
                        .setOffset(offset3D)
                        .setExtent(extent3D)
                        .setMemory(deviceMemory)
                        .setMemoryOffset(deviceMemory ? tileMappings[mappingIndex].byteOffsets[regionIndex] : 0));
            }
        }
    }

    vk::BindSparseInfo bindSparseInfo = {};

    if (!sparseImageMemoryBinds.empty())
    {
        vk::SparseImageMemoryBindInfo sparseImageMemoryBindInfo;
        sparseImageMemoryBindInfo.setImage(vkTexture->image);
        sparseImageMemoryBindInfo.setBinds(sparseImageMemoryBinds);
        bindSparseInfo.setImageBinds(sparseImageMemoryBindInfo);
    }

    if (!sparseMemoryBinds.empty())
    {
        vk::SparseImageOpaqueMemoryBindInfo sparseImageOpaqueMemoryBindInfo;
        sparseImageOpaqueMemoryBindInfo.setImage(vkTexture->image);
        sparseImageOpaqueMemoryBindInfo.setBinds(sparseMemoryBinds);
        bindSparseInfo.setImageOpaqueBinds(sparseImageOpaqueMemoryBindInfo);
    }

    mQueue.bindSparse(bindSparseInfo, vk::Fence());
}

uint64_t ZWVKQueue::UpdateLastFinishedID()
{
    mLastFinishedID = mContext.device.getSemaphoreCounterValue(trackingSemaphore);
    return mLastFinishedID;
}

void ZWVKQueue::RetireCommandBuffers()
{
    std::list<ZWVKTrackedCommandBufferPtr> submissions = std::move(mCommandBuffersInFlight);
    const uint64_t lastFinishedID = UpdateLastFinishedID();

    for (const ZWVKTrackedCommandBufferPtr& cmd : submissions)
    {
        if (cmd->submissionID <= lastFinishedID)
        {
            cmd->referencedResources.clear();
            cmd->referencedStagingBuffers.clear();
            cmd->submissionID = 0;
            mCommandBuffersPool.push_back(cmd);

#if HRHI_WITH_RTXMU
            if (!cmd->rtxmuBuildIds.empty())
            {
                std::lock_guard lockGuard(mContext.rtxMuResources->asListMutex);
                mContext.rtxMuResources->asBuildsCompleted.insert(
                    mContext.rtxMuResources->asBuildsCompleted.end(),
                    cmd->rtxmuBuildIds.begin(),
                    cmd->rtxmuBuildIds.end());
                cmd->rtxmuBuildIds.clear();
            }

            if (!cmd->rtxmuCompactionIds.empty())
            {
                mContext.rtxMemUtil->GarbageCollection(cmd->rtxmuCompactionIds);
                cmd->rtxmuCompactionIds.clear();
            }
#endif
        }
        else
        {
            mCommandBuffersInFlight.push_back(cmd);
        }
    }
}

ZWVKTrackedCommandBufferPtr ZWVKQueue::GetCommandBufferInFlight(uint64_t submissionID)
{
    for (const ZWVKTrackedCommandBufferPtr& cmd : mCommandBuffersInFlight)
    {
        if (cmd->submissionID == submissionID)
        {
            return cmd;
        }
    }

    return nullptr;
}

bool ZWVKQueue::PollCommandList(uint64_t commandListID)
{
    if (commandListID > mLastSubmittedID || commandListID == 0)
    {
        return false;
    }

    if (GetLastFinishedID() >= commandListID)
    {
        return true;
    }

    return UpdateLastFinishedID() >= commandListID;
}

bool ZWVKQueue::WaitCommandList(uint64_t commandListID, uint64_t timeout)
{
    if (commandListID > mLastSubmittedID || commandListID == 0)
    {
        return false;
    }

    if (PollCommandList(commandListID))
    {
        return true;
    }

    const std::array<const vk::Semaphore, 1> semaphores = { trackingSemaphore };
    const std::array<uint64_t, 1> waitValues = { commandListID };

    const vk::SemaphoreWaitInfo waitInfo =
        vk::SemaphoreWaitInfo()
            .setSemaphores(semaphores)
            .setValues(waitValues);

    const vk::Result result = mContext.device.waitSemaphores(waitInfo, timeout);
    return result == vk::Result::eSuccess;
}

VkSemaphore ZWVKDevice::GetQueueSemaphore(ECommandQueue queue)
{
    ZWVKQueue* vkQueue = GetQueue(queue);
    return vkQueue != nullptr ? VkSemaphore(vkQueue->trackingSemaphore) : VkSemaphore();
}

void ZWVKDevice::QueueWaitForSemaphore(ECommandQueue waitQueue, VkSemaphore semaphore, uint64_t value)
{
    ZWVKQueue* vkQueue = GetQueue(waitQueue);
    if (vkQueue != nullptr)
    {
        vkQueue->AddWaitSemaphore(vk::Semaphore(semaphore), value);
    }
}

void ZWVKDevice::QueueSignalSemaphore(ECommandQueue executionQueue, VkSemaphore semaphore, uint64_t value)
{
    ZWVKQueue* vkQueue = GetQueue(executionQueue);
    if (vkQueue != nullptr)
    {
        vkQueue->AddSignalSemaphore(vk::Semaphore(semaphore), value);
    }
}

void ZWVKDevice::QueueWaitForCommandList(ECommandQueue waitQueue, ECommandQueue executionQueue, uint64_t instance)
{
    QueueWaitForSemaphore(waitQueue, GetQueueSemaphore(executionQueue), instance);
}

void ZWVKDevice::UpdateTextureTileMappings(
    ITexture* texture,
    const ZWTextureTilesMapping* tileMappings,
    uint32_t numTileMappings,
    ECommandQueue executionQueue)
{
    ZWVKQueue* vkQueue = GetQueue(executionQueue);
    if (vkQueue != nullptr)
    {
        vkQueue->UpdateTextureTileMappings(texture, tileMappings, numTileMappings);
    }
}

uint64_t ZWVKDevice::QueueGetCompletedInstance(ECommandQueue queue)
{
    return mContext.device.getSemaphoreCounterValue(vk::Semaphore(GetQueueSemaphore(queue)));
}

}
