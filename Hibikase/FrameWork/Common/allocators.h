#pragma once

#include <vector>
#include <mutex>

namespace HCommon
{
    class BitSetAllocator
    {
    public:
        explicit BitSetAllocator(size_t capacity, bool multithreaded);

        int allocate();
        void release(int index);
        [[nodiscard]] size_t getCapacity() const { return mAllocated.size(); }

    private:
        int mNextAvailable = 0;
        std::vector<bool> mAllocated;
        bool mMultiThreaded;
        std::mutex mMutex;
    };
}
