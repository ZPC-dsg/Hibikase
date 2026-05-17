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

#include <BackEnd/vulkanbackend.h>
#include <Utils/stringtranslatehelper.h>
#include <sstream>


namespace HRHI
{

    ZWBufferHandle ZWVKDevice::CreateBuffer(const ZWBufferDesc& desc)
    {
        // Check some basic constraints first - the validation layer is expected to handle them too

        if (desc.isVolatile && desc.maxVersions == 0)
            return nullptr;

        if (desc.isVolatile && !desc.isConstantBuffer)
            return nullptr;

        if (desc.byteSize == 0)
            return nullptr;


        ZWVKBuffer* buffer = new ZWVKBuffer(mContext, mAllocator);
        buffer->desc = desc;

        vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eTransferSrc |
                                          vk::BufferUsageFlagBits::eTransferDst;

        if (desc.isVertexBuffer)
            usageFlags |= vk::BufferUsageFlagBits::eVertexBuffer;
        
        if (desc.isIndexBuffer)
            usageFlags |= vk::BufferUsageFlagBits::eIndexBuffer;
        
        if (desc.isDrawIndirectArgs)
            usageFlags |= vk::BufferUsageFlagBits::eIndirectBuffer;
        
        if (desc.isConstantBuffer)
            usageFlags |= vk::BufferUsageFlagBits::eUniformBuffer;

        if (desc.structStride != 0 || desc.canHaveUAVs || desc.canHaveRawViews)
            usageFlags |= vk::BufferUsageFlagBits::eStorageBuffer;
        
        if (desc.canHaveTypedViews)
            usageFlags |= vk::BufferUsageFlagBits::eUniformTexelBuffer;

        if (desc.canHaveTypedViews && desc.canHaveUAVs)
            usageFlags |= vk::BufferUsageFlagBits::eStorageTexelBuffer;

        if (desc.isAccelStructBuildInput)
            usageFlags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;

        if (desc.isAccelStructStorage)
            usageFlags |= vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;

        if (desc.isShaderBindingTable)
            usageFlags |= vk::BufferUsageFlagBits::eShaderBindingTableKHR;

        if (mContext.extensions.buffer_device_address)
            usageFlags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;

        uint64_t size = desc.byteSize;

        if (desc.isVolatile)
        {
            assert(!desc.isVirtual);

            uint64_t alignment = mContext.physicalDeviceProperties.limits.minUniformBufferOffsetAlignment;

            uint64_t atomSize = mContext.physicalDeviceProperties.limits.nonCoherentAtomSize;
            alignment = std::max(alignment, atomSize);

            assert((alignment & (alignment - 1)) == 0); // check if it's a power of 2
            
            size = (size + alignment - 1) & ~(alignment - 1);
            buffer->desc.byteSize = size;

            size *= desc.maxVersions;

            buffer->versionTracking.resize(desc.maxVersions);
            std::fill(buffer->versionTracking.begin(), buffer->versionTracking.end(), 0);

            buffer->desc.cpuAccess = ECpuAccessMode::Write; // to get the right memory type allocated
        }
        else if (desc.byteSize < 65536)
        {
            // vulkan allows for <= 64kb buffer updates to be done inline via vkCmdUpdateBuffer,
            // but the data size must always be a multiple of 4
            // enlarge the buffer slightly to allow for this
            size = (size + 3) & ~3ull;
        }

        auto bufferInfo = vk::BufferCreateInfo()
            .setSize(size)
            .setUsage(usageFlags)
            .setSharingMode(vk::SharingMode::eExclusive);

#if _WIN32
        const auto handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
#else
        const auto handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
#endif
        vk::ExternalMemoryBufferCreateInfo externalBuffer{ handleType };
        if ((desc.sharedResourceFlags & ESharedResourceFlags::Shared) != 0)
            bufferInfo.setPNext(&externalBuffer);

        vk::Result res = mContext.device.createBuffer(&bufferInfo, mContext.allocationCallbacks, &buffer->buffer);
        CHECK_VK_FAIL(res);

        mContext.NameVKObject(VkBuffer(buffer->buffer), vk::ObjectType::eBuffer, vk::DebugReportObjectTypeEXT::eBuffer, desc.debugName.c_str());

        if (!desc.isVirtual)
        {
            res = mAllocator.AllocateBufferMemory(buffer, (usageFlags & vk::BufferUsageFlagBits::eShaderDeviceAddress) != vk::BufferUsageFlags(0));
            CHECK_VK_FAIL(res)

            mContext.NameVKObject(buffer->memory, vk::ObjectType::eDeviceMemory, vk::DebugReportObjectTypeEXT::eDeviceMemory, desc.debugName.c_str());

            if (desc.isVolatile)
            {
                buffer->mappedMemory = mContext.device.mapMemory(buffer->memory, 0, size);
                assert(buffer->mappedMemory);
            }

            if (mContext.extensions.buffer_device_address)
            {
                auto addressInfo = vk::BufferDeviceAddressInfo().setBuffer(buffer->buffer);

                buffer->deviceAddress = mContext.device.getBufferAddress(addressInfo);
            }

            if ((desc.sharedResourceFlags & ESharedResourceFlags::Shared) != 0)
            {
#ifdef _WIN32
                buffer->sharedHandle = mContext.device.getMemoryWin32HandleKHR({ buffer->memory, vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32 });
#else
                buffer->sharedHandle = (void*)(size_t)mContext.device.getMemoryFdKHR({ buffer->memory, vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd });
#endif
            }
        }

        if (mContext.logBufferLifetime)
        {
            size_t byteDisplay = desc.byteSize;
            const char* byteUnit = "B";

            if (desc.byteSize > (1 << 20))
            {
                byteDisplay = desc.byteSize >> 20;
                byteUnit = "MB";
            }
            else if (desc.byteSize > (1 << 10))
            {
                byteDisplay = desc.byteSize >> 10;
                byteUnit = "KB";
            }

            std::stringstream ss;
            ss << "Create buffer: " << desc.debugName
                << " Buf:0x" << std::hex << reinterpret_cast<uintptr_t>(VkBuffer(buffer->buffer))
                << " Gpu:0x" << std::hex << buffer->GetGpuVirtualAddress() << "->0x" << std::hex << buffer->GetGpuVirtualAddress() + desc.byteSize;

            if (desc.structStride)
            {
                ss << " (n:" << std::dec << (desc.structStride ? desc.byteSize / desc.structStride : 0)
                    << " stride:" << std::dec << desc.structStride
                    << "B size:" << std::dec << byteDisplay << byteUnit << ")";
            }
            else
            {
                ss << " (size:" << std::dec << byteDisplay << byteUnit << ")";
            }

            mContext.Info(ss.str());
        }

        return ZWBufferHandle::Create(buffer);
    }

    ZWBufferHandle ZWVKDevice::CreateHandleForNativeBuffer(ObjectType objectType, HCommon::ZWObject _buffer, const ZWBufferDesc& desc)
    {
        if (!_buffer.pointer)
            return nullptr;

        if (objectType != HRHIObjectTypes::gVKBuffer)
            return nullptr;
        
        ZWVKBuffer* buffer = new ZWVKBuffer(mContext, mAllocator);
        buffer->buffer = VkBuffer(_buffer.integer);
        buffer->desc = desc;
        buffer->managed = false;
        
        if (mContext.extensions.buffer_device_address)
        {
            auto addressInfo = vk::BufferDeviceAddressInfo().setBuffer(buffer->buffer);

            buffer->deviceAddress = mContext.device.getBufferAddress(addressInfo);
        }

        return ZWBufferHandle::Create(buffer);
    }

    void ZWVKCommandList::CopyBuffer(IBuffer* _dest, uint64_t destOffsetBytes,
                                             IBuffer* _src, uint64_t srcOffsetBytes,
                                             uint64_t dataSizeBytes)
    {
        ZWVKBuffer* dest = static_cast<ZWVKBuffer*>(_dest);
        ZWVKBuffer* src = static_cast<ZWVKBuffer*>(_src);

        assert(destOffsetBytes + dataSizeBytes <= dest->desc.byteSize);
        assert(srcOffsetBytes + dataSizeBytes <= src->desc.byteSize);

        assert(mCurrentCmdBuf);

        if (dest->desc.cpuAccess != ECpuAccessMode::None)
            mCurrentCmdBuf->referencedStagingBuffers.push_back(dest);
        else
            mCurrentCmdBuf->referencedResources.push_back(dest);

        if (src->desc.cpuAccess != ECpuAccessMode::None)
            mCurrentCmdBuf->referencedStagingBuffers.push_back(src);
        else
            mCurrentCmdBuf->referencedResources.push_back(src);

        if (m_EnableAutomaticBarriers)
        {
            RequireBufferState(src, EResourceStates::CopySource);
            RequireBufferState(dest, EResourceStates::CopyDest);
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        auto copyRegion = vk::BufferCopy()
            .setSize(dataSizeBytes)
            .setSrcOffset(srcOffsetBytes)
            .setDstOffset(destOffsetBytes);

        mCurrentCmdBuf->cmdBuf.copyBuffer(src->buffer, dest->buffer, { copyRegion });
    }

    static uint64_t getQueueLastFinishedID(ZWVKDevice* device, ECommandQueue queueIndex)
    {
        ZWVKQueue* queue = device->GetQueue(queueIndex);
        if (queue)
            return queue->GetLastFinishedID();
        return 0;
    }

    void ZWVKCommandList::WriteVolatileBuffer(ZWVKBuffer* buffer, const void* data, size_t dataSize)
    {
        ZWVKVolatileBufferState& state = mVolatileBufferStates[buffer];

        if (!state.initialized)
        {
            state.minVersion = int(buffer->desc.maxVersions);
            state.maxVersion = -1;
            state.initialized = true;
        }
        
        std::array<uint64_t, uint32_t(ECommandQueue::Count)> queueCompletionValues = {
            getQueueLastFinishedID(mDevice, ECommandQueue::Graphics),
            getQueueLastFinishedID(mDevice, ECommandQueue::Compute),
            getQueueLastFinishedID(mDevice, ECommandQueue::Copy)
        };

        uint32_t searchStart = buffer->versionSearchStart;
        uint32_t maxVersions = buffer->desc.maxVersions;
        uint32_t version = 0;

        uint64_t originalVersionInfo = 0;

        // Since versionTracking[] can be accessed by multiple threads concurrently,
        // perform the search in a loop ending with compare_exchange until the exchange is successful.
        while (true)
        {
            bool found = false;

            // Search through the versions of this buffer, looking for either unused (0)
            // or submitted and already finished versions

            for (uint32_t searchIndex = 0; searchIndex < maxVersions; searchIndex++)
            {
                version = searchIndex + searchStart;
                version = (version >= maxVersions) ? (version - maxVersions) : version;

                originalVersionInfo = buffer->versionTracking[version];

                if (originalVersionInfo == 0)
                {
                    // Previously unused version - definitely available
                    found = true;
                    break;
                }

                // Decode the bitfield
                bool isSubmitted = (originalVersionInfo & cVersionSubmittedFlag) != 0;
                uint32_t queueIndex = uint32_t(originalVersionInfo >> cVersionQueueShift) & cVersionQueueMask;
                uint64_t id = originalVersionInfo & cVersionIDMask;

                // If the version is in a recorded but not submitted command list,
                // we can't use it. So, only compare the version ID for submitted CLs.
                if (isSubmitted)
                {
                    // Versions can potentially be used in CLs submitted to different queues.
                    // So we store the queue index and use look at the last finished CL in that queue.

                    if (queueIndex >= uint32_t(ECommandQueue::Count))
                    {
                        // If the version points at an invalid queue, assume it's available. Signal the error too.
                        assert(false);
                        found = true;
                        break;
                    }

                    if (id <= queueCompletionValues[queueIndex])
                    {
                        // If the version was used in a completed CL, it's available.
                        found = true;
                        break;
                    }
                }
            }

            if (!found)
            {
                // Not enough versions - need to relay this information to the developer.
                // This has to be a real message and not assert, because asserts only happen in the
                // debug mode, and buffer versioning will behave differently in debug vs. release,
                // or validation on vs. off, because it is timing related.

                std::stringstream ss;
                ss << "Volatile constant buffer " << HApp::DebugNameToString(buffer->desc.debugName) <<
                    " has maxVersions = " << buffer->desc.maxVersions << ", which is insufficient.";

                mContext.Error(ss.str());
                return;
            }

            // Encode the current CL ID for this version of the buffer, in a "pending" state
            uint64_t newVersionInfo = (uint64_t(mCommandListParameters.queueType) << cVersionQueueShift) | mCurrentCmdBuf->recordingID;

            // Try to store the new version info, end the loop if we actually won this version, i.e. no other thread has claimed it
            if (buffer->versionTracking[version].compare_exchange_weak(originalVersionInfo, newVersionInfo))
                break;
        }

        buffer->versionSearchStart = (version + 1 < maxVersions) ? (version + 1) : 0;

        // Store the current version and expand the version range in this CL
        state.latestVersion = int(version);
        state.minVersion = std::min(int(version), state.minVersion);
        state.maxVersion = std::max(int(version), state.maxVersion);

        // Finally, write the actual data
        void* hostData = (char*)buffer->mappedMemory + version * buffer->desc.byteSize;
        memcpy(hostData, data, dataSize);

        mAnyVolatileBufferWrites = true;
    }

    void ZWVKCommandList::FlushVolatileBufferWrites()
    {
        // The volatile CBs are permanently mapped with the eHostVisible flag, but not eHostCoherent,
        // so before using the data on the GPU, we need to make sure it's available there.
        // Go over all the volatile CBs that were used in this CL and flush their written versions.

        std::vector<vk::MappedMemoryRange> ranges;

        for (auto& iter : mVolatileBufferStates)
        {
            ZWVKBuffer* buffer = iter.first;
            const ZWVKVolatileBufferState& state = iter.second;

            if (state.maxVersion < state.minVersion || !state.initialized)
                continue;

            // Flush all the versions between min and max - that might be too conservative,
            // but that should be fine - better than using potentially hundreds of ranges.
            int numVersions = state.maxVersion - state.minVersion + 1;

            auto range = vk::MappedMemoryRange()
                .setMemory(buffer->memory)
                .setOffset(state.minVersion * buffer->desc.byteSize)
                .setSize(numVersions * buffer->desc.byteSize);

            ranges.push_back(range);
        }

        if (!ranges.empty())
        {
            mContext.device.flushMappedMemoryRanges(ranges);
        }
    }

    void ZWVKCommandList::SubmitVolatileBuffers(uint64_t recordingID, uint64_t submittedID)
    {
        // For each volatile CB that was written in this command list, and for every version thereof,
        // we need to replace the tracking information from "pending" to "submitted".
        // This is potentially slow as there might be hundreds of versions of a buffer,
        // but at least the find-and-replace operation is constrained to the min/max version range.

        uint64_t stateToFind = (uint64_t(mCommandListParameters.queueType) << cVersionQueueShift) | (recordingID & cVersionIDMask);
        uint64_t stateToReplace = (uint64_t(mCommandListParameters.queueType) << cVersionQueueShift) | (submittedID & cVersionIDMask) | cVersionSubmittedFlag;

        for (auto& iter : mVolatileBufferStates)
        {
            ZWVKBuffer* buffer = iter.first;
            const ZWVKVolatileBufferState& state = iter.second;

            if (!state.initialized)
                continue;

            for (int version = state.minVersion; version <= state.maxVersion; version++)
            {
                // Use compare_exchange to conditionally replace the entries equal to stateToFind with stateToReplace.
                uint64_t expected = stateToFind;
                buffer->versionTracking[version].compare_exchange_strong(expected, stateToReplace);
            }
        }
    }

    void ZWVKCommandList::WriteBuffer(IBuffer* _buffer, const void *data, size_t dataSize, uint64_t destOffsetBytes)
    {
        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);

        assert(dataSize <= buffer->desc.byteSize);

        assert(mCurrentCmdBuf);

        mCurrentCmdBuf->referencedResources.push_back(buffer);

        if (buffer->desc.isVolatile)
        {
            assert(destOffsetBytes == 0);

            WriteVolatileBuffer(buffer, data, dataSize);
            
            return;
        }

        // Per Vulkan spec, vkCmdUpdateBuffer is only allowed outside of a render pass, so end it here.
        // Note that WriteVolatileBuffer above is permitted so don't end the render pass for that case.
        EndRenderPass();

        const size_t vkCmdUpdateBufferLimit = 65536;

        // Per Vulkan spec, vkCmdUpdateBuffer requires that the data size is smaller than or equal to 64 kB,
        // and that the offset and data size are a multiple of 4. We can't change the offset, but data size
        // is rounded up later.
        if (dataSize <= vkCmdUpdateBufferLimit && (destOffsetBytes & 3) == 0)
        {
            if (m_EnableAutomaticBarriers)
            {
                RequireBufferState(buffer, EResourceStates::CopyDest);
                mBindingStatesDirty = true;
            }
            CommitBarriers();

            // Round up the write size to a multiple of 4
            const size_t sizeToWrite = (dataSize + 3) & ~3ull;

            mCurrentCmdBuf->cmdBuf.updateBuffer(buffer->buffer, destOffsetBytes, sizeToWrite, data);
        }
        else
        {
            if (buffer->desc.cpuAccess != ECpuAccessMode::Write)
            {
                // use the upload manager
                ZWVKBuffer* uploadBuffer;
                uint64_t uploadOffset;
                void* uploadCpuVA;
                m_UploadManager->SuballocateBuffer(dataSize, &uploadBuffer, &uploadOffset, &uploadCpuVA, MakeVersion(mCurrentCmdBuf->recordingID, mCommandListParameters.queueType, false));

                memcpy(uploadCpuVA, data, dataSize);

                CopyBuffer(buffer, destOffsetBytes, uploadBuffer, uploadOffset, dataSize);
            }
            else
            {
                mContext.Error("Using WriteBuffer on mappable buffers is invalid");
            }
        }
    }

    void ZWVKCommandList::ClearBufferUInt(IBuffer* b, uint32_t clearValue)
    {
        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(b);

        assert(mCurrentCmdBuf);

        EndRenderPass();

        if (m_EnableAutomaticBarriers)
        {
            RequireBufferState(buffer, EResourceStates::CopyDest);
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        mCurrentCmdBuf->cmdBuf.fillBuffer(buffer->buffer, 0, buffer->desc.byteSize, clearValue);
        mCurrentCmdBuf->referencedResources.push_back(b);
    }

    ZWVKBuffer::~ZWVKBuffer()
    {
        if (mContext.logBufferLifetime)
        {
            std::stringstream ss;
            ss << "Release buffer: " << desc.debugName << " 0x" << std::hex << GetGpuVirtualAddress();
            mContext.Info(ss.str());
        }

        if (mappedMemory)
        {
            mContext.device.unmapMemory(memory);
            mappedMemory = nullptr;
        }

        for (auto&& iter : viewCache)
        {
            mContext.device.destroyBufferView(iter.second, mContext.allocationCallbacks);
        }

        viewCache.clear();

        if (managed)
        {
            assert(buffer != vk::Buffer());

            mContext.device.destroyBuffer(buffer, mContext.allocationCallbacks);
            buffer = vk::Buffer();

            if (memory)
            {
                mAllocator.FreeBufferMemory(this);
                memory = vk::DeviceMemory();
            }
        }
    }

    HCommon::ZWObject ZWVKBuffer::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case HRHIObjectTypes::gVKBuffer:
            return HCommon::ZWObject(buffer);
        case HRHIObjectTypes::gVKDeviceMemory:
            return HCommon::ZWObject(memory);
        case HRHIObjectTypes::gSharedHandle:
            return HCommon::ZWObject(sharedHandle);
        default:
            return nullptr;
        }
    }

    void *ZWVKDevice::MapBuffer(IBuffer* _buffer, ECpuAccessMode flags, uint64_t offset, size_t size) const
    {
        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);

        assert(flags != ECpuAccessMode::None);

        // If the buffer has been used in a command list before, wait for that CL to complete
        if (buffer->lastUseCommandListID != 0)
        {
            auto& queue = mQueues[uint32_t(buffer->lastUseQueue)];
            queue->WaitCommandList(buffer->lastUseCommandListID, ~0ull);
        }

        vk::AccessFlags accessFlags;

        switch(flags)
        {
            case ECpuAccessMode::Read:
                accessFlags = vk::AccessFlagBits::eHostRead;
                break;

            case ECpuAccessMode::Write:
                accessFlags = vk::AccessFlagBits::eHostWrite;
                break;
                
            case ECpuAccessMode::None:
            default:
                assert(false);
                break;
        }

        // TODO: there should be a barrier... But there can't be a command list here
        // buffer->barrier(cmd, vk::PipelineStageFlagBits::eHost, accessFlags);

        void* ptr = nullptr;
        [[maybe_unused]] const vk::Result res = mContext.device.mapMemory(buffer->memory, offset, size, vk::MemoryMapFlags(), &ptr);
        assert(res == vk::Result::eSuccess);

        return ptr;
    }

    void *ZWVKDevice::MapBuffer(IBuffer* _buffer, ECpuAccessMode flags)
    {
        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);

        return MapBuffer(buffer, flags, 0, buffer->desc.byteSize);
    }

    void ZWVKDevice::UnmapBuffer(IBuffer* _buffer)
    {
        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);

        mContext.device.unmapMemory(buffer->memory);

        // TODO: there should be a barrier
        // buffer->barrier(cmd, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
    }

    ZWMemoryRequirements ZWVKDevice::GetBufferMemoryRequirements(IBuffer* _buffer)
    {
        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);

        const vk::MemoryRequirements memReq = mContext.device.getBufferMemoryRequirements(buffer->buffer);

        ZWMemoryRequirements requirements;
        requirements.alignment = memReq.alignment;
        requirements.size = memReq.size;
        return requirements;
    }

    bool ZWVKDevice::BindBufferMemory(IBuffer* _buffer, IHeap* _heap, uint64_t offset)
    {
        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);
        ZWVKHeap* heap = static_cast<ZWVKHeap*>(_heap);

        if (buffer->heap)
            return false;

        if (!buffer->desc.isVirtual)
            return false;
        
        mContext.device.bindBufferMemory(buffer->buffer, heap->memory, offset);

        buffer->heap = heap;

        if (mContext.extensions.buffer_device_address)
        {
            auto addressInfo = vk::BufferDeviceAddressInfo().setBuffer(buffer->buffer);

            buffer->deviceAddress = mContext.device.getBufferAddress(addressInfo);
        }

        return true;
    }

} // namespace HRHI














