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

    ZWEventQueryHandle ZWVKDevice::CreateEventQuery()
    {
        ZWVKEventQuery* query = new ZWVKEventQuery();
        return ZWEventQueryHandle::Create(query);
    }

    void ZWVKDevice::SetEventQuery(IEventQuery* _query, ECommandQueue queue)
    {
        ZWVKEventQuery* query = static_cast<ZWVKEventQuery*>(_query);

        assert(query->commandListID == 0);

        query->queue = queue;
        query->commandListID = mQueues[uint32_t(queue)]->GetLastSubmittedID();
    }

    bool ZWVKDevice::PollEventQuery(IEventQuery* _query)
    {
        ZWVKEventQuery* query = static_cast<ZWVKEventQuery*>(_query);
        
        auto& queue = *mQueues[uint32_t(query->queue)];

        return queue.PollCommandList(query->commandListID);
    }

    void ZWVKDevice::WaitEventQuery(IEventQuery* _query)
    {
        ZWVKEventQuery* query = static_cast<ZWVKEventQuery*>(_query);

        if (query->commandListID == 0)
            return;

        auto& queue = *mQueues[uint32_t(query->queue)];

        bool success = queue.WaitCommandList(query->commandListID, ~0ull);
        assert(success);
        (void)success;
    }

    void ZWVKDevice::ResetEventQuery(IEventQuery* _query)
    {
        ZWVKEventQuery* query = static_cast<ZWVKEventQuery*>(_query);

        query->commandListID = 0;
    }


    ZWTimerQueryHandle ZWVKDevice::CreateTimerQuery()
    {
        if (!mTimerQueryPool)
        {
            std::lock_guard lockGuard(mMutex);

            if (!mTimerQueryPool)
            {
                // set up the timer query pool on first use
                auto poolInfo = vk::QueryPoolCreateInfo()
                    .setQueryType(vk::QueryType::eTimestamp)
                    .setQueryCount(uint32_t(mTimerQueryAllocator.getCapacity()) * 2); // use 2 Vulkan queries per 1 TimerQuery

                const vk::Result res = mContext.device.createQueryPool(&poolInfo, mContext.allocationCallbacks, &mTimerQueryPool);
                CHECK_VK_FAIL(res)
            }
        }

        int queryIndex = mTimerQueryAllocator.allocate();

        if (queryIndex < 0)
        {
            mContext.Error("Insufficient query pool space, increase Device::numTimerQueries");
            return nullptr;
        }

        ZWVKTimerQuery* query = new ZWVKTimerQuery(mTimerQueryAllocator);
        query->beginQueryIndex = queryIndex * 2;
        query->endQueryIndex = queryIndex * 2 + 1;

        return ZWTimerQueryHandle::Create(query);
    }

    ZWVKTimerQuery::~ZWVKTimerQuery()
    {
        mQueryAllocator.release(beginQueryIndex / 2);
        beginQueryIndex = -1;
        endQueryIndex = -1;
    }

    void ZWVKCommandList::BeginTimerQuery(ITimerQuery* _query)
    {
        EndRenderPass();

        ZWVKTimerQuery* query = static_cast<ZWVKTimerQuery*>(_query);

        assert(query->beginQueryIndex >= 0);
        assert(!query->started);
        assert(mCurrentCmdBuf);

        query->resolved = false;

        mCurrentCmdBuf->cmdBuf.resetQueryPool(mDevice->GetTimerQueryPool(), query->beginQueryIndex, 2);
        mCurrentCmdBuf->cmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, mDevice->GetTimerQueryPool(), query->beginQueryIndex);
    }

    void ZWVKCommandList::EndTimerQuery(ITimerQuery* _query)
    {
        EndRenderPass();

        ZWVKTimerQuery* query = static_cast<ZWVKTimerQuery*>(_query);

        assert(query->endQueryIndex >= 0);
        assert(!query->started);
        assert(!query->resolved);

        assert(mCurrentCmdBuf);

        mCurrentCmdBuf->cmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, mDevice->GetTimerQueryPool(), query->endQueryIndex);
        query->started = true;
    }

    bool ZWVKDevice::PollTimerQuery(ITimerQuery* _query)
    {
        ZWVKTimerQuery* query = static_cast<ZWVKTimerQuery*>(_query);

        if (!query->started)
        {
            return false;
        }

        if (query->resolved)
        {
            return true;
        }

        uint32_t timestamps[2] = { 0, 0 };

        vk::Result res;
        res = mContext.device.getQueryPoolResults(mTimerQueryPool,
                                                 query->beginQueryIndex, 2,
                                                 sizeof(timestamps), timestamps,
                                                 sizeof(timestamps[0]), vk::QueryResultFlags());
        assert(res == vk::Result::eSuccess || res == vk::Result::eNotReady || res == vk::Result::eErrorDeviceLost);

        if (res == vk::Result::eNotReady || res == vk::Result::eErrorDeviceLost)
        {
            return false;
        }

        const auto timestampPeriod = mContext.physicalDeviceProperties.limits.timestampPeriod; // in nanoseconds
        const float scale = 1e-9f * timestampPeriod;

        query->time = float(timestamps[1] - timestamps[0]) * scale;
        query->resolved = true;
        return true;
    }

    float ZWVKDevice::GetTimerQueryTime(ITimerQuery* _query)
    {
        ZWVKTimerQuery* query = static_cast<ZWVKTimerQuery*>(_query);

        if (!query->started)
            return 0.f;

        if (!query->resolved)
        {
            while(!PollTimerQuery(query))
                ;
        }

        query->started = false;

        assert(query->resolved);
        return query->time;
    }

    void ZWVKDevice::ResetTimerQuery(ITimerQuery* _query)
    {
        ZWVKTimerQuery* query = static_cast<ZWVKTimerQuery*>(_query);

        query->started = false;
        query->resolved = false;
        query->time = 0.f;
    }


    void ZWVKCommandList::BeginMarker(const char* name)
    {
        if (mContext.extensions.EXT_debug_utils)
        {
            assert(mCurrentCmdBuf);

            auto label = vk::DebugUtilsLabelEXT()
                            .setPLabelName(name);
            mCurrentCmdBuf->cmdBuf.beginDebugUtilsLabelEXT(&label);
        }
        else if (mContext.extensions.EXT_debug_marker)
        {
            assert(mCurrentCmdBuf);

            auto markerInfo = vk::DebugMarkerMarkerInfoEXT()
                                .setPMarkerName(name);
            mCurrentCmdBuf->cmdBuf.debugMarkerBeginEXT(&markerInfo);
        }
        
#if HRHI_WITH_AFTERMATH
        if (mDevice->IsAftermathEnabled())
        {
            const size_t aftermathMarker = mAftermathTracker.pushEvent(name);
            mCurrentCmdBuf->cmdBuf.setCheckpointNV((const void*)aftermathMarker);
        }
#endif
    }

    void ZWVKCommandList::EndMarker()
    {
        if (mContext.extensions.EXT_debug_utils)
        {
            assert(mCurrentCmdBuf);

            mCurrentCmdBuf->cmdBuf.endDebugUtilsLabelEXT();
        }
        else if (mContext.extensions.EXT_debug_marker)
        {
            assert(mCurrentCmdBuf);

            mCurrentCmdBuf->cmdBuf.debugMarkerEndEXT();
        }
        
#if HRHI_WITH_AFTERMATH
        mAftermathTracker.popEvent();
#endif
    }

} // namespace HRHI
