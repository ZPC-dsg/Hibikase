#include <Utils/aftermathtraker.h>

namespace HApp
{
    ZWAftermathMarkerTracker::ZWAftermathMarkerTracker() :
        mEventStack{},
        mEventHashes{},
        mOldestHashIndex(0),
        mEventStrings{}
    {
    }

    size_t ZWAftermathMarkerTracker::PushEvent(const char* name)
    {
        mEventStack.append(name);
        std::string eventString = mEventStack.generic_string();
        size_t hash = std::hash<std::string>{}(eventString);
        if (mEventStrings.find(hash) == mEventStrings.end())
        {
            mEventStrings.erase(mEventHashes[mOldestHashIndex]);
            mEventStrings[hash] = eventString;
            mEventHashes[mOldestHashIndex] = hash;
            mOldestHashIndex = (mOldestHashIndex + 1) % sMaxEventStrings;
        }
        return hash;
    }

    void ZWAftermathMarkerTracker::PopEvent()
    {
        mEventStack = mEventStack.parent_path();
    }

    const static std::string sNotFoundMarkerString = "ERROR: could not resolve marker";

    std::pair<bool, std::reference_wrapper<const std::string>> ZWAftermathMarkerTracker::GetEventString(size_t hash)
    {
        auto const& found = mEventStrings.find(hash);
        if (found != mEventStrings.end())
        {
            return std::make_pair<bool, std::reference_wrapper<const std::string>>(true, found->second);
        }
        else
        {
            // could technically return a string literal according to the spec, but compiler complains, so using static
            return std::make_pair(false, sNotFoundMarkerString);
        }
    }

    ZWAftermathCrashDumpHelper::ZWAftermathCrashDumpHelper() :
        mMarkerTrackers{},
        mShaderBinaryLookupCallbacks{}
    {
    }

    void ZWAftermathCrashDumpHelper::RegisterAftermathMarkerTracker(ZWAftermathMarkerTracker* tracker)
    {
        mMarkerTrackers.insert(tracker);
    }

    void ZWAftermathCrashDumpHelper::UnRegisterAftermathMarkerTracker(ZWAftermathMarkerTracker* tracker)
    {
        // it's possible that a destroyed command list's markers might still be executing on the GPU,
        // so will keep the last few of them around to search in case of a crash
        const static size_t NumDestroyedMarkerTrackers = 2;
        if (mDestroyedMarkerTrackers.size() >= NumDestroyedMarkerTrackers)
        {
            mDestroyedMarkerTrackers.pop_front();
        }
        // copying by value to keep the tracker contents after command list is destroyed
        mDestroyedMarkerTrackers.push_back(*tracker);
        mMarkerTrackers.erase(tracker);
    }

    void ZWAftermathCrashDumpHelper::RegisterShaderBinaryLookupCallback(void* client, ZWShaderBinaryLookupCallback lookupCallback)
    {
        mShaderBinaryLookupCallbacks[client] = lookupCallback;
    }

    void ZWAftermathCrashDumpHelper::UnRegisterShaderBinaryLookupCallback(void* client)
    {
        mShaderBinaryLookupCallbacks.erase(client);
    }

    ZWResolvedMarker ZWAftermathCrashDumpHelper::ResolveMarker(size_t markerHash)
    {
        for (auto markerTracker : mMarkerTrackers)
        {
            auto [found, markerString] = markerTracker->GetEventString(markerHash);
            if (found)
                return std::make_pair(found, markerString);
        }
        for (auto markerTracker : mDestroyedMarkerTrackers)
        {
            auto [found, markerString] = markerTracker.GetEventString(markerHash);
            if (found)
                return std::make_pair(found, markerString);
        }
        return std::make_pair(false, sNotFoundMarkerString);
    }

    ZWBinaryBlob ZWAftermathCrashDumpHelper::FindShaderBinary(uint64_t shaderHash, ZWShaderHashGeneratorFunction hashGenerator)
    {
        for (auto shaderLookupClientCallback : mShaderBinaryLookupCallbacks)
        {
            auto [ptr, size] = shaderLookupClientCallback.second(shaderHash, hashGenerator);
            if (size > 0)
            {
                return std::make_pair(ptr, size);
            }
        }
        return std::make_pair(nullptr, 0);
    }
}