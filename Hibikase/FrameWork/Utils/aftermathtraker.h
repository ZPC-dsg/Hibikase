#pragma once

#include <BackEnd/RHIinterface.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <tsl/robin_map.h>
#include <tsl/robin_set.h>
#include <functional>
#include <deque>
#include <filesystem>

namespace HApp
{
    typedef std::pair<bool, std::reference_wrapper<const std::string>> ZWResolvedMarker;
    typedef std::pair<const void*, size_t> ZWBinaryBlob;
    typedef std::function<uint64_t(ZWBinaryBlob, HRHI::EGraphicsAPI)> ZWShaderHashGeneratorFunction;
    typedef std::function<ZWBinaryBlob(uint64_t, ZWShaderHashGeneratorFunction)> ZWShaderBinaryLookupCallback;

    // Aftermath will return the payload of the last marker the GPU executed, so in cases of nested regimes,
    // we want the marker payloads to represent the whole "stack" of regimes, not just the last one
    // AftermathMarkerTracker pushes/pops regimes to this stack
    // The payload itself is a 64bit value, so AftermathMarkerTracker stores the mappings of strings<->hashes
    // There should be one AftermathMarkerTracker per graphics API-level command list
    class ZWAftermathMarkerTracker
    {
    public:
        ZWAftermathMarkerTracker();

        size_t PushEvent(const char* name);
        void PopEvent();

        ZWResolvedMarker GetEventString(size_t hash);
    private:
        // using a filesystem path to track the event stack since that automatically inserts "/" separators
        // and is easy to push/pop entries
        std::filesystem::path mEventStack;

        // Some apps have unique marker text on every frame (for example, by appending the frame number to the marker)
        // In these cases, we want to cap the max number of strings stored to prevent memory usage from growing
        const static size_t sMaxEventStrings = 128;
        std::array<size_t, sMaxEventStrings> mEventHashes{};
        size_t mOldestHashIndex;
        tsl::robin_map<size_t, std::string> mEventStrings{};
    };

    // AftermathCrashDumpHelper tracks all nvrhi::IDevice-level constructs that we need when generating a crash dump
    // It provides two services: resolving a marker hash to the original string, and getting the specific shader bytecode
    // of a requested shader hash
    // There should be one AftermathCrashDumpHelper per HRHI::IDevice
    // All command lists will register their AftermathMarkerTrackers with the AftermathCrashDumpHelper
    // Any shader bytecode loading and management code (e.g. donut's ShaderFactory) should register a shader binary lookup callback
    class ZWAftermathCrashDumpHelper
    {
    public:
        ZWAftermathCrashDumpHelper();

        void RegisterAftermathMarkerTracker(ZWAftermathMarkerTracker* tracker);
        void UnRegisterAftermathMarkerTracker(ZWAftermathMarkerTracker* tracker);
        void RegisterShaderBinaryLookupCallback(void* client, ZWShaderBinaryLookupCallback lookupCallback);
        void UnRegisterShaderBinaryLookupCallback(void* client);

        ZWResolvedMarker ResolveMarker(size_t markerHash);
        ZWBinaryBlob FindShaderBinary(uint64_t shaderHash, ZWShaderHashGeneratorFunction hashGenerator);
    private:
        tsl::robin_set<ZWAftermathMarkerTracker*> mMarkerTrackers{};
        // Command lists that are deleted on the CPU-side could still be executing (and crashing) GPU side,
        // so we keep around a small number of recently destroyed marker trackers just in case
        std::deque<ZWAftermathMarkerTracker> mDestroyedMarkerTrackers{};
        tsl::robin_map<void*, ZWShaderBinaryLookupCallback> mShaderBinaryLookupCallbacks{};
    };
} // namespace nvrhi
