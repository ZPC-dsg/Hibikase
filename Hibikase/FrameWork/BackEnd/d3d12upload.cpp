#include <BackEnd/d3d12backend.h>
#include <BackEnd/versioning.h>

#include <algorithm>
#include <cassert>
#include <sstream>

namespace HRHI::HD3D12
{
    namespace
    {
        template <typename TValue, typename TAlignment>
        constexpr TValue AlignValue(TValue value, TAlignment alignment)
        {
            const TValue alignmentValue = static_cast<TValue>(alignment);
            return (value + alignmentValue - TValue(1)) & ~(alignmentValue - TValue(1));
        }
    }

    ZWD3D12BufferChunk::~ZWD3D12BufferChunk()
    {
        if (buffer != nullptr && cpuVA != nullptr)
        {
            buffer->Unmap(0, nullptr);
            cpuVA = nullptr;
        }
    }

    ZWD3D12UploadManager::ZWD3D12UploadManager(const ZWD3D12Context& context, ZWD3D12Queue* queue, size_t defaultChunkSize, uint64_t memoryLimit, bool isScratchBuffer)
        : mContext(context)
        , m_Queue(queue)
        , m_DefaultChunkSize(defaultChunkSize)
        , m_MemoryLimit(memoryLimit)
        , m_IsScratchBuffer(isScratchBuffer)
    {
        assert(queue != nullptr);
    }

    std::shared_ptr<ZWD3D12BufferChunk> ZWD3D12UploadManager::CreateChunk(size_t size) const
    {
        auto chunk = std::make_shared<ZWD3D12BufferChunk>();

        size = AlignValue(size, ZWD3D12BufferChunk::cSizeAlignment);

        D3D12_HEAP_PROPERTIES heapProperties = {};
        heapProperties.Type = m_IsScratchBuffer ? D3D12_HEAP_TYPE_DEFAULT : D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = size;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (m_IsScratchBuffer)
        {
            bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }

        const HRESULT result = mContext.device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            m_IsScratchBuffer ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(chunk->buffer.ReleaseAndGetAddressOf()));

        if (FAILED(result))
        {
            return nullptr;
        }

        if (!m_IsScratchBuffer)
        {
            void* cpuVa = nullptr;
            if (FAILED(chunk->buffer->Map(0, nullptr, &cpuVa)))
            {
                return nullptr;
            }

            chunk->cpuVA = cpuVa;
        }

        chunk->bufferSize = size;
        chunk->gpuVA = chunk->buffer->GetGPUVirtualAddress();
        chunk->identifier = static_cast<uint32_t>(mChunkPool.size());

        std::wstringstream nameBuilder;
        if (m_IsScratchBuffer)
        {
            nameBuilder << L"DXR Scratch Buffer " << chunk->identifier;
        }
        else
        {
            nameBuilder << L"Upload Buffer " << chunk->identifier;
        }
        chunk->buffer->SetName(nameBuilder.str().c_str());

        return chunk;
    }

    bool ZWD3D12UploadManager::SuballocateBuffer(uint64_t size, ID3D12GraphicsCommandList* commandList, ID3D12Resource** buffer, size_t* offset, void** cpuVa,
        D3D12_GPU_VIRTUAL_ADDRESS* gpuVa, uint64_t currentVersion, uint32_t alignment)
    {
        assert(!m_IsScratchBuffer || commandList != nullptr);

        std::shared_ptr<ZWD3D12BufferChunk> chunkToRetire;

        if (mCurrentChunk != nullptr)
        {
            const uint64_t alignedOffset = AlignValue(mCurrentChunk->writePointer, uint64_t(alignment));
            const uint64_t endOffset = alignedOffset + size;
            if (endOffset <= mCurrentChunk->bufferSize)
            {
                mCurrentChunk->writePointer = endOffset;

                if (buffer != nullptr) *buffer = mCurrentChunk->buffer.Get();
                if (offset != nullptr) *offset = static_cast<size_t>(alignedOffset);
                if (cpuVa != nullptr && mCurrentChunk->cpuVA != nullptr) *cpuVa = static_cast<char*>(mCurrentChunk->cpuVA) + alignedOffset;
                if (gpuVa != nullptr) *gpuVa = mCurrentChunk->gpuVA + alignedOffset;
                return true;
            }

            chunkToRetire = mCurrentChunk;
            mCurrentChunk.reset();
        }

        const uint64_t completedInstance = m_Queue->lastCompletedInstance;

        for (auto iterator = mChunkPool.begin(); iterator != mChunkPool.end(); ++iterator)
        {
            const std::shared_ptr<ZWD3D12BufferChunk>& candidate = *iterator;

            if (HRHI::VersionGetSubmitted(candidate->version) && HRHI::VersionGetInstance(candidate->version) <= completedInstance)
            {
                candidate->version = 0;
            }

            if (candidate->version == 0 && candidate->bufferSize >= size)
            {
                mCurrentChunk = candidate;
                mChunkPool.erase(iterator);
                break;
            }
        }

        if (chunkToRetire != nullptr)
        {
            mChunkPool.push_back(chunkToRetire);
        }

        if (mCurrentChunk == nullptr)
        {
            const uint64_t sizeToAllocate = AlignValue(std::max<uint64_t>(size, static_cast<uint64_t>(m_DefaultChunkSize)), ZWD3D12BufferChunk::cSizeAlignment);

            if (m_MemoryLimit > 0 && m_AllocatedMemory + sizeToAllocate > m_MemoryLimit)
            {
                if (!m_IsScratchBuffer)
                {
                    return false;
                }

                std::shared_ptr<ZWD3D12BufferChunk> bestChunk;
                for (const std::shared_ptr<ZWD3D12BufferChunk>& candidate : mChunkPool)
                {
                    if (candidate->bufferSize < sizeToAllocate)
                    {
                        continue;
                    }

                    if (bestChunk == nullptr)
                    {
                        bestChunk = candidate;
                        continue;
                    }

                    const bool candidateSubmitted = HRHI::VersionGetSubmitted(candidate->version);
                    const bool bestSubmitted = HRHI::VersionGetSubmitted(bestChunk->version);
                    const uint64_t candidateInstance = HRHI::VersionGetInstance(candidate->version);
                    const uint64_t bestInstance = HRHI::VersionGetInstance(bestChunk->version);

                    if ((candidateSubmitted && !bestSubmitted) ||
                        (candidateSubmitted == bestSubmitted && candidateInstance < bestInstance) ||
                        (candidateSubmitted == bestSubmitted && candidateInstance == bestInstance && candidate->bufferSize > bestChunk->bufferSize))
                    {
                        bestChunk = candidate;
                    }
                }

                if (bestChunk == nullptr)
                {
                    return false;
                }

                auto iterator = std::find(mChunkPool.begin(), mChunkPool.end(), bestChunk);
                if (iterator != mChunkPool.end())
                {
                    mChunkPool.erase(iterator);
                }
                mCurrentChunk = bestChunk;

                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.UAV.pResource = bestChunk->buffer.Get();
                commandList->ResourceBarrier(1, &barrier);
            }
            else
            {
                mCurrentChunk = CreateChunk(static_cast<size_t>(sizeToAllocate));
                if (mCurrentChunk == nullptr)
                {
                    return false;
                }

                m_AllocatedMemory += mCurrentChunk->bufferSize;
            }
        }

        mCurrentChunk->version = currentVersion;
        mCurrentChunk->writePointer = size;

        if (buffer != nullptr) *buffer = mCurrentChunk->buffer.Get();
        if (offset != nullptr) *offset = 0;
        if (cpuVa != nullptr) *cpuVa = mCurrentChunk->cpuVA;
        if (gpuVa != nullptr) *gpuVa = mCurrentChunk->gpuVA;

        return true;
    }

    void ZWD3D12UploadManager::SubmitChunks(uint64_t currentVersion, uint64_t submittedVersion)
    {
        if (mCurrentChunk != nullptr)
        {
            mChunkPool.push_back(mCurrentChunk);
            mCurrentChunk.reset();
        }

        for (const std::shared_ptr<ZWD3D12BufferChunk>& chunk : mChunkPool)
        {
            if (chunk->version == currentVersion)
            {
                chunk->version = submittedVersion;
            }
        }
    }
}
