#pragma once

#include <BackEnd/RHIinterface.h>

namespace HRHI
{
    /*
    Version words are used to track the usage of upload buffers, scratch buffers,
    and volatile constant buffers across multiple command lists and their instances.

    Versioned objects are initially allocated in the "pending" state, meaing they have
    the submitted flag set to zero, but the instance is nonzero. When the command list
    instance using the object is executed, the objects with a matching version are
    transitioned into the "submitted" state. Later, when the command list instance has
    finished executing, the objects are transitioned into the "available" state, i.e. 0.
     */

    constexpr uint64_t cVersionSubmittedFlag = 0x8000000000000000;
    constexpr uint32_t cVersionQueueShift = 60;
    constexpr uint32_t cVersionQueueMask = 0x7;
    constexpr uint64_t cVersionIDMask = 0x0FFFFFFFFFFFFFFF;

    constexpr uint64_t MakeVersion(uint64_t id, ECommandQueue queue, bool submitted)
    {
        uint64_t result = (id & cVersionIDMask) | (uint64_t(queue) << cVersionQueueShift);
        if (submitted) result |= cVersionSubmittedFlag;
        return result;
    }

    constexpr uint64_t VersionGetInstance(uint64_t version)
    {
        return version & cVersionIDMask;
    }

    constexpr ECommandQueue VersionGetQueue(uint64_t version)
    {
        return ECommandQueue((version >> cVersionQueueShift) & cVersionQueueMask);
    }

    constexpr bool VersionGetSubmitted(uint64_t version)
    {
        return (version & cVersionSubmittedFlag) != 0;
    }
}