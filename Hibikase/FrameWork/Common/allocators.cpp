#include <Common/allocators.h>

namespace HCommon
{
    BitSetAllocator::BitSetAllocator(const size_t capacity, bool multithreaded)
        : mMultiThreaded(multithreaded)
    {
        mAllocated.resize(capacity);
    }

    int BitSetAllocator::allocate()
    {
        if (mMultiThreaded)
            mMutex.lock();

        int result = -1;

        int capacity = static_cast<int>(mAllocated.size());
        for (int i = 0; i < capacity; i++)
        {
            int ii = (mNextAvailable + i) % capacity;

            if (!mAllocated[ii])
            {
                result = ii;
                mNextAvailable = (ii + 1) % capacity;
                mAllocated[ii] = true;
                break;
            }
        }

        if (mMultiThreaded)
            mMutex.unlock();

        return result;
    }

    void BitSetAllocator::release(const int index)
    {
        if (index >= 0 && index < static_cast<int>(mAllocated.size()))
        {
            if (mMultiThreaded)
                mMutex.lock();

            mAllocated[index] = false;
            mNextAvailable = std::min(mNextAvailable, index);

            if (mMultiThreaded)
                mMutex.unlock();
        }
    }
}