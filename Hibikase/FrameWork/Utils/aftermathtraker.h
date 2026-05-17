#pragma once

#include <BackEnd/RHIinterface.h>

#ifndef HRHI_WITH_AFTERMATH
#if defined(_WIN64) && defined(__has_include)
#if __has_include(<GFSDK_Aftermath.h>) && __has_include(<GFSDK_Aftermath_GpuCrashDump.h>) && __has_include(<GFSDK_Aftermath_GpuCrashDumpDecoding.h>)
#define HRHI_WITH_AFTERMATH 1
#else
#define HRHI_WITH_AFTERMATH 0
#endif
#else
#define HRHI_WITH_AFTERMATH 0
#endif
#endif

#if defined(__has_include)
#if __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#endif
#endif

#if defined(_WIN32)
#include <d3d12.h>
#endif

#if HRHI_WITH_AFTERMATH
#include <GFSDK_Aftermath.h>
#include <GFSDK_Aftermath_GpuCrashDump.h>
#include <GFSDK_Aftermath_GpuCrashDumpDecoding.h>
#endif

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
#include <mutex>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

namespace HApp
{
    typedef void* ZWAftermathContextHandle;
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
        tsl::robin_map<size_t, std::string> GetEventStringsSnapshot() const;
    private:
        mutable std::mutex mMutex;

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
        mutable std::mutex mMutex;
        tsl::robin_set<ZWAftermathMarkerTracker*> mMarkerTrackers{};
        // Command lists that are deleted on the CPU-side could still be executing (and crashing) GPU side,
        // so we keep around a small number of recently destroyed marker trackers just in case
        std::deque<tsl::robin_map<size_t, std::string>> mDestroyedMarkerTrackers{};
        tsl::robin_map<void*, ZWShaderBinaryLookupCallback> mShaderBinaryLookupCallbacks{};
    };

    class ZWAftermathRuntime
    {
    public:
        static ZWAftermathRuntime& Get();

        bool EnableD3D12CrashDumps(
            const char* applicationName,
            const char* applicationVersion,
            const char* commandLine,
            HRHI::IMessageCallback* messageCallback);

        bool EnableVulkanCrashDumps(
            const char* applicationName,
            const char* applicationVersion,
            const char* commandLine,
            HRHI::IMessageCallback* messageCallback);

        bool InitializeD3D12Device(
            ID3D12Device* device,
            ZWAftermathCrashDumpHelper* helper,
            HRHI::IMessageCallback* messageCallback);

        bool InitializeVulkanDevice(
            ZWAftermathCrashDumpHelper* helper,
            HRHI::IMessageCallback* messageCallback);

        bool CreateD3D12ContextHandle(
            ID3D12GraphicsCommandList* commandList,
            ZWAftermathContextHandle& contextHandle,
            HRHI::IMessageCallback* messageCallback);

        void SetEventMarker(
            ZWAftermathContextHandle contextHandle,
            const void* markerData,
            uint32_t markerDataSize,
            HRHI::IMessageCallback* messageCallback);

        bool RegisterShaderBinary(
            ZWBinaryBlob shaderBinary,
            HRHI::EGraphicsAPI graphicsApi,
            HRHI::IMessageCallback* messageCallback);

        void ClearActiveHelper(ZWAftermathCrashDumpHelper* helper);
        void WaitForCrashDump(uint32_t timeoutMs, HRHI::IMessageCallback* messageCallback);

        bool IsAvailable() const;
        bool IsEnabled() const;

#if HRHI_WITH_AFTERMATH
        void OnCrashDump(const void* gpuCrashDump, uint32_t gpuCrashDumpSize);
        void OnShaderDebugInfo(const void* shaderDebugInfo, uint32_t shaderDebugInfoSize);
        void OnDescription(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription);
    void OnResolveMarker(const void* markerData, uint32_t markerDataSize, void** resolvedMarkerData, uint32_t* resolvedMarkerDataSize);
        void OnShaderDebugInfoLookup(const GFSDK_Aftermath_ShaderDebugInfoIdentifier& identifier, PFN_GFSDK_Aftermath_SetData setShaderDebugInfo) const;
        void OnShaderLookup(const GFSDK_Aftermath_ShaderBinaryHash& shaderHash, PFN_GFSDK_Aftermath_SetData setShaderBinary) const;

        void WriteGpuCrashDumpToFile(const void* gpuCrashDump, uint32_t gpuCrashDumpSize);
        void WriteShaderDebugInformationToFile(
            GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier,
            const void* shaderDebugInfo,
            uint32_t shaderDebugInfoSize);
#endif

    private:
        ZWAftermathRuntime() = default;
        ZWAftermathRuntime(const ZWAftermathRuntime&) = delete;
        ZWAftermathRuntime& operator=(const ZWAftermathRuntime&) = delete;
    };
} // namespace nvrhi
