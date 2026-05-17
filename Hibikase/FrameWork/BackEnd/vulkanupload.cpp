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

namespace HRHI
{

    static uint64_t AlignUp(uint64_t value, uint64_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    std::shared_ptr<ZWVKBufferChunk> ZWVKUploadManager::CreateChunk(uint64_t size)
    {
        std::shared_ptr<ZWVKBufferChunk> chunk = std::make_shared<ZWVKBufferChunk>();

        if (mIsScratchBuffer)
        {
            ZWBufferDesc desc;
            desc.byteSize = size;
            desc.cpuAccess = ECpuAccessMode::None;
            desc.debugName = "ScratchBufferChunk";
            desc.canHaveUAVs = true;

            chunk->buffer = mDevice->CreateBuffer(desc);
            chunk->mappedMemory = nullptr;
            chunk->bufferSize = size;
        }
        else
        {
            ZWBufferDesc desc;
            desc.byteSize = size;
            desc.cpuAccess = ECpuAccessMode::Write;
            desc.debugName = "UploadChunk";

            // The upload manager buffers are used in buildTopLevelAccelStruct to store instance data, and SBT for shader entries
            desc.isAccelStructBuildInput = mDevice->QueryFeatureSupport(EFeature::RayTracingAccelStruct);
            desc.isShaderBindingTable = mDevice->QueryFeatureSupport(EFeature::RayTracingAccelStruct);

            chunk->buffer = mDevice->CreateBuffer(desc);
            chunk->mappedMemory = mDevice->MapBuffer(chunk->buffer, ECpuAccessMode::Write);
            chunk->bufferSize = size;
        }

        return chunk;
    }

    bool ZWVKUploadManager::SuballocateBuffer(uint64_t size, ZWVKBuffer** pBuffer, uint64_t* pOffset, void** pCpuVA,
        uint64_t currentVersion, uint32_t alignment)
    {
        std::shared_ptr<ZWVKBufferChunk> chunkToRetire;

        if (mCurrentChunk)
        {
            uint64_t alignedOffset = AlignUp(mCurrentChunk->writePointer, uint64_t(alignment));
            uint64_t endOfDataInChunk = alignedOffset + size;

            if (endOfDataInChunk <= mCurrentChunk->bufferSize)
            {
                mCurrentChunk->writePointer = endOfDataInChunk;

                *pBuffer = static_cast<ZWVKBuffer*>(mCurrentChunk->buffer.Get());
                *pOffset = alignedOffset;
                if (pCpuVA && mCurrentChunk->mappedMemory)
                    *pCpuVA = static_cast<char*>(mCurrentChunk->mappedMemory) + alignedOffset;

                return true;
            }

            chunkToRetire = mCurrentChunk;
            mCurrentChunk.reset();
        }

        ECommandQueue queue = VersionGetQueue(currentVersion);
        uint64_t completedInstance = mDevice->QueueGetCompletedInstance(queue);

        for (auto it = mChunkPool.begin(); it != mChunkPool.end(); ++it)
        {
            std::shared_ptr<ZWVKBufferChunk> chunk = *it;

            if (VersionGetSubmitted(chunk->version)
                && VersionGetInstance(chunk->version) <= completedInstance)
            {
                chunk->version = 0;
            }

            if (chunk->version == 0 && chunk->bufferSize >= size)
            {
                mChunkPool.erase(it);
                mCurrentChunk = chunk;
                break;
            }
        }

        if (chunkToRetire)
        {
            mChunkPool.push_back(chunkToRetire);
        }

        if (!mCurrentChunk)
        {
            uint64_t sizeToAllocate = AlignUp(std::max(size, mDefaultChunkSize), ZWVKBufferChunk::cSizeAlignment);

            if ((mMemoryLimit > 0) && (mAllocatedMemory + sizeToAllocate > mMemoryLimit))
                return false;

            mCurrentChunk = CreateChunk(sizeToAllocate);
        }

        mCurrentChunk->version = currentVersion;
        mCurrentChunk->writePointer = size;

        *pBuffer = static_cast<ZWVKBuffer*>(mCurrentChunk->buffer.Get());
        *pOffset = 0;
        if (pCpuVA)
            *pCpuVA = mCurrentChunk->mappedMemory;

        return true;
    }

    void ZWVKUploadManager::SubmitChunks(uint64_t currentVersion, uint64_t submittedVersion)
    {
        if (mCurrentChunk)
        {
            mChunkPool.push_back(mCurrentChunk);
            mCurrentChunk.reset();
        }

        for (const auto& chunk : mChunkPool)
        {
            if (chunk->version == currentVersion)
                chunk->version = submittedVersion;
        }
    }

}
