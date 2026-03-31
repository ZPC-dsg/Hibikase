#include <BackEnd/d3d12backend.h>

#include <mutex>

namespace HRHI::HD3D12
{
    ZWD3D12TimerQuery::~ZWD3D12TimerQuery()
    {
        mResources.timerQueries.release(static_cast<int>(beginQueryIndex) / 2);
    }

    ZWEventQueryHandle ZWD3D12Device::CreateEventQuery()
    {
        return ZWEventQueryHandle::Create(new ZWD3D12EventQuery());
    }

    void ZWD3D12Device::SetEventQuery(IEventQuery* query, ECommandQueue queue)
    {
        ZWD3D12EventQuery* d3d12Query = static_cast<ZWD3D12EventQuery*>(query);
        ZWD3D12Queue* d3d12Queue = GetQueue(queue);
        if (d3d12Query == nullptr || d3d12Queue == nullptr)
        {
            return;
        }

        d3d12Query->started = true;
        d3d12Query->resolved = false;
        d3d12Query->fence = d3d12Queue->fence;
        d3d12Query->fenceCounter = d3d12Queue->lastSubmittedInstance;
    }

    bool ZWD3D12Device::PollEventQuery(IEventQuery* query)
    {
        ZWD3D12EventQuery* d3d12Query = static_cast<ZWD3D12EventQuery*>(query);
        if (d3d12Query == nullptr || !d3d12Query->started)
        {
            return false;
        }

        if (d3d12Query->resolved)
        {
            return true;
        }

        if (d3d12Query->fence != nullptr && d3d12Query->fence->GetCompletedValue() >= d3d12Query->fenceCounter)
        {
            d3d12Query->resolved = true;
            d3d12Query->fence = nullptr;
        }

        return d3d12Query->resolved;
    }

    void ZWD3D12Device::WaitEventQuery(IEventQuery* query)
    {
        ZWD3D12EventQuery* d3d12Query = static_cast<ZWD3D12EventQuery*>(query);
        if (d3d12Query == nullptr || !d3d12Query->started || d3d12Query->resolved || d3d12Query->fence == nullptr)
        {
            return;
        }

        WaitForFence(d3d12Query->fence.Get(), d3d12Query->fenceCounter, mFenceEvent);
        d3d12Query->resolved = true;
        d3d12Query->fence = nullptr;
    }

    void ZWD3D12Device::ResetEventQuery(IEventQuery* query)
    {
        ZWD3D12EventQuery* d3d12Query = static_cast<ZWD3D12EventQuery*>(query);
        if (d3d12Query == nullptr)
        {
            return;
        }

        d3d12Query->started = false;
        d3d12Query->resolved = false;
        d3d12Query->fence = nullptr;
        d3d12Query->fenceCounter = 0;
    }

    ZWTimerQueryHandle ZWD3D12Device::CreateTimerQuery()
    {
        if (mContext.timerQueryHeap == nullptr)
        {
            std::lock_guard<std::mutex> lockGuard(mMutex);

            if (mContext.timerQueryHeap == nullptr)
            {
                D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
                queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
                queryHeapDesc.Count = static_cast<uint32_t>(mResources.timerQueries.getCapacity()) * 2u;

                if (FAILED(mContext.device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(mContext.timerQueryHeap.ReleaseAndGetAddressOf()))))
                {
                    mContext.Error("Failed to create a D3D12 timestamp query heap.");
                    return nullptr;
                }

                ZWBufferDesc bufferDesc = {};
                bufferDesc.byteSize = static_cast<uint64_t>(queryHeapDesc.Count) * sizeof(uint64_t);
                bufferDesc.cpuAccess = ECpuAccessMode::Read;

                mContext.timerQueryResolveBuffer = CreateBuffer(bufferDesc);
                if (mContext.timerQueryResolveBuffer == nullptr)
                {
                    mContext.Error("Failed to create a D3D12 timer query resolve buffer.");
                    return nullptr;
                }
            }
        }

        const int queryIndex = mResources.timerQueries.allocate();
        if (queryIndex < 0)
        {
            return nullptr;
        }

        ZWD3D12TimerQuery* query = new ZWD3D12TimerQuery(mResources);
        query->beginQueryIndex = static_cast<uint32_t>(queryIndex) * 2u;
        query->endQueryIndex = query->beginQueryIndex + 1u;
        query->resolved = false;
        query->time = 0.f;
        return ZWTimerQueryHandle::Create(query);
    }

    bool ZWD3D12Device::PollTimerQuery(ITimerQuery* query)
    {
        ZWD3D12TimerQuery* d3d12Query = static_cast<ZWD3D12TimerQuery*>(query);
        if (d3d12Query == nullptr || !d3d12Query->started)
        {
            return false;
        }

        if (d3d12Query->fence == nullptr)
        {
            return true;
        }

        if (d3d12Query->fence->GetCompletedValue() >= d3d12Query->fenceCounter)
        {
            d3d12Query->fence = nullptr;
            return true;
        }

        return false;
    }

    float ZWD3D12Device::GetTimerQueryTime(ITimerQuery* query)
    {
        ZWD3D12TimerQuery* d3d12Query = static_cast<ZWD3D12TimerQuery*>(query);
        if (d3d12Query == nullptr)
        {
            return 0.f;
        }

        if (!d3d12Query->resolved)
        {
            if (d3d12Query->fence != nullptr)
            {
                WaitForFence(d3d12Query->fence.Get(), d3d12Query->fenceCounter, mFenceEvent);
                d3d12Query->fence = nullptr;
            }

            uint64_t frequency = 0;
            ZWD3D12Queue* graphicsQueue = GetQueue(ECommandQueue::Graphics);
            if (graphicsQueue == nullptr || graphicsQueue->queue == nullptr)
            {
                return 0.f;
            }

            graphicsQueue->queue->GetTimestampFrequency(&frequency);

            ZWD3D12Buffer* resolveBuffer = static_cast<ZWD3D12Buffer*>(mContext.timerQueryResolveBuffer.Get());
            if (resolveBuffer == nullptr || resolveBuffer->resource == nullptr)
            {
                return 0.f;
            }

            D3D12_RANGE readRange = {
                d3d12Query->beginQueryIndex * sizeof(uint64_t),
                (d3d12Query->beginQueryIndex + 2u) * sizeof(uint64_t)
            };

            uint64_t* queryData = nullptr;
            if (FAILED(resolveBuffer->resource->Map(0, &readRange, reinterpret_cast<void**>(&queryData))))
            {
                mContext.Error("Failed to map the D3D12 timer query resolve buffer.");
                return 0.f;
            }

            d3d12Query->resolved = true;
            d3d12Query->time = static_cast<float>(
                static_cast<double>(queryData[d3d12Query->endQueryIndex] - queryData[d3d12Query->beginQueryIndex]) /
                static_cast<double>(frequency));

            resolveBuffer->resource->Unmap(0, nullptr);
        }

        return d3d12Query->time;
    }

    void ZWD3D12Device::ResetTimerQuery(ITimerQuery* query)
    {
        ZWD3D12TimerQuery* d3d12Query = static_cast<ZWD3D12TimerQuery*>(query);
        if (d3d12Query == nullptr)
        {
            return;
        }

        d3d12Query->started = false;
        d3d12Query->resolved = false;
        d3d12Query->time = 0.f;
        d3d12Query->fence = nullptr;
        d3d12Query->fenceCounter = 0;
    }

    void ZWD3D12CommandList::BeginTimerQuery(ITimerQuery* query)
    {
        ZWD3D12TimerQuery* d3d12Query = static_cast<ZWD3D12TimerQuery*>(query);
        if (d3d12Query == nullptr || m_Context.timerQueryHeap == nullptr)
        {
            return;
        }

        mInstance->referencedTimerQueries.push_back(d3d12Query);

        mActiveCommandList->commandList->EndQuery(
            m_Context.timerQueryHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            d3d12Query->beginQueryIndex);

        // Two timestamps within the same command list are reliably comparable,
        // so we only mark the query as started after the command list is submitted.
    }

    void ZWD3D12CommandList::EndTimerQuery(ITimerQuery* query)
    {
        ZWD3D12TimerQuery* d3d12Query = static_cast<ZWD3D12TimerQuery*>(query);
        ZWD3D12Buffer* resolveBuffer = static_cast<ZWD3D12Buffer*>(m_Context.timerQueryResolveBuffer.Get());
        if (d3d12Query == nullptr || m_Context.timerQueryHeap == nullptr || resolveBuffer == nullptr || resolveBuffer->resource == nullptr)
        {
            return;
        }

        mInstance->referencedTimerQueries.push_back(d3d12Query);

        mActiveCommandList->commandList->EndQuery(
            m_Context.timerQueryHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            d3d12Query->endQueryIndex);

        mActiveCommandList->commandList->ResolveQueryData(
            m_Context.timerQueryHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            d3d12Query->beginQueryIndex,
            2,
            resolveBuffer->resource.Get(),
            static_cast<uint64_t>(d3d12Query->beginQueryIndex) * sizeof(uint64_t));
    }
}
