#include <Utils/aftermathtraker.h>

#include <Windows.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <thread>

#if HRHI_WITH_AFTERMATH
#include <d3d12.h>
#endif

namespace
{
    void DispatchAftermathMessage(HRHI::IMessageCallback* messageCallback, HRHI::EMessageSeverity severity, const std::string& message)
    {
        if (messageCallback != nullptr)
        {
            messageCallback->message(severity, message.c_str());
            return;
        }

        const std::string messageWithNewLine = message + "\n";
        ::OutputDebugStringA(messageWithNewLine.c_str());
    }

#if HRHI_WITH_AFTERMATH
    struct ZWShaderDebugInfoIdentifierLess
    {
        bool operator()(const GFSDK_Aftermath_ShaderDebugInfoIdentifier& lhs, const GFSDK_Aftermath_ShaderDebugInfoIdentifier& rhs) const
        {
            if (lhs.id[0] != rhs.id[0])
            {
                return lhs.id[0] < rhs.id[0];
            }

            return lhs.id[1] < rhs.id[1];
        }
    };

    struct ZWShaderBinaryHashLess
    {
        bool operator()(const GFSDK_Aftermath_ShaderBinaryHash& lhs, const GFSDK_Aftermath_ShaderBinaryHash& rhs) const
        {
            return lhs.hash < rhs.hash;
        }
    };

    std::string ToHexString(uint32_t value)
    {
        std::ostringstream stream;
        stream << "0x" << std::hex << std::uppercase << value;
        return stream.str();
    }

    std::string ToString(const GFSDK_Aftermath_ShaderDebugInfoIdentifier& identifier)
    {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0')
            << std::setw(16) << identifier.id[0]
            << "-"
            << std::setw(16) << identifier.id[1];
        return stream.str();
    }

    std::string WideToUtf8(const wchar_t* value)
    {
        if (value == nullptr || value[0] == L'\0')
        {
            return {};
        }

        const int requiredChars = ::WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
        if (requiredChars <= 1)
        {
            return {};
        }

        std::string result(static_cast<size_t>(requiredChars) - 1, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), requiredChars, nullptr, nullptr);
        return result;
    }

    template <typename T>
    bool LoadAftermathFunction(HMODULE module, const char* name, T& functionPointer)
    {
        functionPointer = reinterpret_cast<T>(::GetProcAddress(module, name));
        return functionPointer != nullptr;
    }

    class ZWAftermathApi
    {
    public:
        bool EnsureLoaded(HRHI::IMessageCallback* messageCallback)
        {
            if (mLoadAttempted)
            {
                return mModule != nullptr;
            }

            mLoadAttempted = true;
            mModule = ::LoadLibraryW(L"GFSDK_AFTERMATH_Lib.x64.dll");
            if (mModule == nullptr)
            {
                DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Info, "Nsight Aftermath runtime DLL was not found. Aftermath integration is disabled.");
                return false;
            }

            wchar_t loadedModulePath[MAX_PATH] = {};
            if (::GetModuleFileNameW(mModule, loadedModulePath, MAX_PATH) != 0)
            {
                DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Info, "Loaded Nsight Aftermath runtime DLL from: " + WideToUtf8(loadedModulePath));
            }

            bool loaded = true;
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_EnableGpuCrashDumps", EnableGpuCrashDumps);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_DisableGpuCrashDumps", DisableGpuCrashDumps);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_DX12_Initialize", DX12Initialize);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_DX12_CreateContextHandle", DX12CreateContextHandle);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_SetEventMarker", SetEventMarker);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_GetCrashDumpStatus", GetCrashDumpStatus);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_GetShaderDebugInfoIdentifier", GetShaderDebugInfoIdentifier);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_GetShaderHash", GetShaderHash);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_GetShaderHashSpirv", GetShaderHashSpirv);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_GpuCrashDump_CreateDecoder", CreateDecoder);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_GpuCrashDump_DestroyDecoder", DestroyDecoder);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_GpuCrashDump_GetBaseInfo", GetBaseInfo);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_GpuCrashDump_GetDescriptionSize", GetDescriptionSize);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_GpuCrashDump_GetDescription", GetDescription);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_GpuCrashDump_GenerateJSON", GenerateJson);
            loaded &= LoadAftermathFunction(mModule, "GFSDK_Aftermath_GpuCrashDump_GetJSON", GetJson);

            if (!loaded)
            {
                DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Warning, "Nsight Aftermath runtime DLL is missing one or more required exports. Aftermath integration is disabled.");
                ::FreeLibrary(mModule);
                mModule = nullptr;
                return false;
            }

            return true;
        }

        HMODULE mModule = nullptr;
        bool mLoadAttempted = false;

        decltype(&GFSDK_Aftermath_EnableGpuCrashDumps) EnableGpuCrashDumps = nullptr;
        decltype(&GFSDK_Aftermath_DisableGpuCrashDumps) DisableGpuCrashDumps = nullptr;
        decltype(&GFSDK_Aftermath_DX12_Initialize) DX12Initialize = nullptr;
        decltype(&GFSDK_Aftermath_DX12_CreateContextHandle) DX12CreateContextHandle = nullptr;
        decltype(&GFSDK_Aftermath_SetEventMarker) SetEventMarker = nullptr;
        decltype(&GFSDK_Aftermath_GetCrashDumpStatus) GetCrashDumpStatus = nullptr;
        decltype(&GFSDK_Aftermath_GetShaderDebugInfoIdentifier) GetShaderDebugInfoIdentifier = nullptr;
        decltype(&GFSDK_Aftermath_GetShaderHash) GetShaderHash = nullptr;
        decltype(&GFSDK_Aftermath_GetShaderHashSpirv) GetShaderHashSpirv = nullptr;
        decltype(&GFSDK_Aftermath_GpuCrashDump_CreateDecoder) CreateDecoder = nullptr;
        decltype(&GFSDK_Aftermath_GpuCrashDump_DestroyDecoder) DestroyDecoder = nullptr;
        decltype(&GFSDK_Aftermath_GpuCrashDump_GetBaseInfo) GetBaseInfo = nullptr;
        decltype(&GFSDK_Aftermath_GpuCrashDump_GetDescriptionSize) GetDescriptionSize = nullptr;
        decltype(&GFSDK_Aftermath_GpuCrashDump_GetDescription) GetDescription = nullptr;
        decltype(&GFSDK_Aftermath_GpuCrashDump_GenerateJSON) GenerateJson = nullptr;
        decltype(&GFSDK_Aftermath_GpuCrashDump_GetJSON) GetJson = nullptr;
    };

    struct ZWAftermathState
    {
        std::recursive_mutex mutex;
        ZWAftermathApi api;
        bool crashDumpsEnabled = false;
        bool d3d12Initialized = false;
        bool vulkanInitialized = false;
        std::string applicationName = "Hibikase";
        std::string applicationVersion = "initial";
        std::string commandLine;
        HApp::ZWAftermathCrashDumpHelper* activeHelper = nullptr;
        std::map<GFSDK_Aftermath_ShaderDebugInfoIdentifier, std::vector<uint8_t>, ZWShaderDebugInfoIdentifierLess> shaderDebugInfos;
        std::map<GFSDK_Aftermath_ShaderBinaryHash, std::vector<uint8_t>, ZWShaderBinaryHashLess> shaderBinaries;
        uint32_t crashDumpCount = 0;
    };

    ZWAftermathState& GetAftermathState()
    {
        static ZWAftermathState sState;
        return sState;
    }

    void AftermathCrashDumpCallback(const void* gpuCrashDump, const uint32_t gpuCrashDumpSize, void* userData)
    {
        static_cast<HApp::ZWAftermathRuntime*>(userData)->OnCrashDump(gpuCrashDump, gpuCrashDumpSize);
    }

    void AftermathShaderDebugInfoCallback(const void* shaderDebugInfo, const uint32_t shaderDebugInfoSize, void* userData)
    {
        static_cast<HApp::ZWAftermathRuntime*>(userData)->OnShaderDebugInfo(shaderDebugInfo, shaderDebugInfoSize);
    }

    void AftermathCrashDumpDescriptionCallback(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription, void* userData)
    {
        static_cast<HApp::ZWAftermathRuntime*>(userData)->OnDescription(addDescription);
    }

    void AftermathResolveMarkerCallback(const void* markerData, const uint32_t markerDataSize, void* userData, void** resolvedMarkerData, uint32_t* resolvedMarkerDataSize)
    {
        static_cast<HApp::ZWAftermathRuntime*>(userData)->OnResolveMarker(markerData, markerDataSize, resolvedMarkerData, resolvedMarkerDataSize);
    }

    void AftermathShaderDebugInfoLookupCallback(const GFSDK_Aftermath_ShaderDebugInfoIdentifier* identifier, PFN_GFSDK_Aftermath_SetData setShaderDebugInfo, void* userData)
    {
        static_cast<const HApp::ZWAftermathRuntime*>(userData)->OnShaderDebugInfoLookup(*identifier, setShaderDebugInfo);
    }

    void AftermathShaderLookupCallback(const GFSDK_Aftermath_ShaderBinaryHash* shaderHash, PFN_GFSDK_Aftermath_SetData setShaderBinary, void* userData)
    {
        static_cast<const HApp::ZWAftermathRuntime*>(userData)->OnShaderLookup(*shaderHash, setShaderBinary);
    }
#endif
}

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
        std::lock_guard<std::mutex> lock(mMutex);

        mEventStack.append(name != nullptr ? name : "");
        std::string eventString = mEventStack.generic_string();
        size_t hash = std::hash<std::string>{}(eventString);
        if (hash == 0)
        {
            hash = 1;
        }

        if (mEventStrings.find(hash) == mEventStrings.end())
        {
            const size_t evictedHash = mEventHashes[mOldestHashIndex];
            if (evictedHash != 0)
            {
                mEventStrings.erase(evictedHash);
            }

            mEventStrings[hash] = eventString;
            mEventHashes[mOldestHashIndex] = hash;
            mOldestHashIndex = (mOldestHashIndex + 1) % sMaxEventStrings;
        }

        return hash;
    }

    void ZWAftermathMarkerTracker::PopEvent()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mEventStack = mEventStack.parent_path();
    }

    const static std::string sNotFoundMarkerString = "ERROR: could not resolve marker";

    ZWResolvedMarker ZWAftermathMarkerTracker::GetEventString(size_t hash)
    {
        std::lock_guard<std::mutex> lock(mMutex);

        auto const& found = mEventStrings.find(hash);
        if (found != mEventStrings.end())
        {
            return std::make_pair<bool, std::reference_wrapper<const std::string>>(true, found->second);
        }

        return std::make_pair(false, sNotFoundMarkerString);
    }

    tsl::robin_map<size_t, std::string> ZWAftermathMarkerTracker::GetEventStringsSnapshot() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mEventStrings;
    }

    ZWAftermathCrashDumpHelper::ZWAftermathCrashDumpHelper() :
        mMarkerTrackers{},
        mShaderBinaryLookupCallbacks{}
    {
    }

    void ZWAftermathCrashDumpHelper::RegisterAftermathMarkerTracker(ZWAftermathMarkerTracker* tracker)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mMarkerTrackers.insert(tracker);
    }

    void ZWAftermathCrashDumpHelper::UnRegisterAftermathMarkerTracker(ZWAftermathMarkerTracker* tracker)
    {
        std::lock_guard<std::mutex> lock(mMutex);

        const static size_t NumDestroyedMarkerTrackers = 2;
        if (mDestroyedMarkerTrackers.size() >= NumDestroyedMarkerTrackers)
        {
            mDestroyedMarkerTrackers.pop_front();
        }

        mDestroyedMarkerTrackers.push_back(tracker->GetEventStringsSnapshot());
        mMarkerTrackers.erase(tracker);
    }

    void ZWAftermathCrashDumpHelper::RegisterShaderBinaryLookupCallback(void* client, ZWShaderBinaryLookupCallback lookupCallback)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mShaderBinaryLookupCallbacks[client] = lookupCallback;
    }

    void ZWAftermathCrashDumpHelper::UnRegisterShaderBinaryLookupCallback(void* client)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mShaderBinaryLookupCallbacks.erase(client);
    }

    ZWResolvedMarker ZWAftermathCrashDumpHelper::ResolveMarker(size_t markerHash)
    {
        std::lock_guard<std::mutex> lock(mMutex);

        for (auto markerTracker : mMarkerTrackers)
        {
            auto [found, markerString] = markerTracker->GetEventString(markerHash);
            if (found)
            {
                return std::make_pair(found, markerString);
            }
        }

        for (auto& markerTracker : mDestroyedMarkerTrackers)
        {
            const auto found = markerTracker.find(markerHash);
            if (found != markerTracker.end())
            {
                return std::make_pair(true, std::cref(found.value()));
            }
        }

        return std::make_pair(false, sNotFoundMarkerString);
    }

    ZWBinaryBlob ZWAftermathCrashDumpHelper::FindShaderBinary(uint64_t shaderHash, ZWShaderHashGeneratorFunction hashGenerator)
    {
        std::lock_guard<std::mutex> lock(mMutex);

        for (auto& shaderLookupClientCallback : mShaderBinaryLookupCallbacks)
        {
            auto [ptr, size] = shaderLookupClientCallback.second(shaderHash, hashGenerator);
            if (size > 0)
            {
                return std::make_pair(ptr, size);
            }
        }

        return std::make_pair(nullptr, 0);
    }

    ZWAftermathRuntime& ZWAftermathRuntime::Get()
    {
        static ZWAftermathRuntime sRuntime;
        return sRuntime;
    }

    bool ZWAftermathRuntime::EnableD3D12CrashDumps(
        const char* applicationName,
        const char* applicationVersion,
        const char* commandLine,
        HRHI::IMessageCallback* messageCallback)
    {
#if HRHI_WITH_AFTERMATH
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        if (!state.api.EnsureLoaded(messageCallback))
        {
            return false;
        }

        if (applicationName != nullptr && applicationName[0] != '\0')
        {
            state.applicationName = applicationName;
        }

        if (applicationVersion != nullptr && applicationVersion[0] != '\0')
        {
            state.applicationVersion = applicationVersion;
        }

        state.commandLine = commandLine != nullptr ? commandLine : "";

        if (state.crashDumpsEnabled)
        {
            return true;
        }

        const GFSDK_Aftermath_Result result = state.api.EnableGpuCrashDumps(
            GFSDK_Aftermath_Version_API,
            GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX,
            GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks,
            AftermathCrashDumpCallback,
            AftermathShaderDebugInfoCallback,
            AftermathCrashDumpDescriptionCallback,
            AftermathResolveMarkerCallback,
            this);

        if (!GFSDK_Aftermath_SUCCEED(result))
        {
            DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Info, "Failed to enable Nsight Aftermath GPU crash dumps for D3D12: " + ToHexString(static_cast<uint32_t>(result)));
            return false;
        }

        state.crashDumpsEnabled = true;
        DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Info, "Nsight Aftermath GPU crash dump collection is enabled for D3D12.");
        return true;
#else
        (void)applicationName;
        (void)applicationVersion;
        (void)commandLine;
        (void)messageCallback;
        return false;
#endif
    }

    bool ZWAftermathRuntime::EnableVulkanCrashDumps(
        const char* applicationName,
        const char* applicationVersion,
        const char* commandLine,
        HRHI::IMessageCallback* messageCallback)
    {
#if HRHI_WITH_AFTERMATH
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        if (!state.api.EnsureLoaded(messageCallback))
        {
            return false;
        }

        if (applicationName != nullptr && applicationName[0] != '\0')
        {
            state.applicationName = applicationName;
        }

        if (applicationVersion != nullptr && applicationVersion[0] != '\0')
        {
            state.applicationVersion = applicationVersion;
        }

        state.commandLine = commandLine != nullptr ? commandLine : "";

        if (state.crashDumpsEnabled)
        {
            return true;
        }

        const GFSDK_Aftermath_Result result = state.api.EnableGpuCrashDumps(
            GFSDK_Aftermath_Version_API,
            GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan,
            GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks,
            AftermathCrashDumpCallback,
            AftermathShaderDebugInfoCallback,
            AftermathCrashDumpDescriptionCallback,
            AftermathResolveMarkerCallback,
            this);

        if (!GFSDK_Aftermath_SUCCEED(result))
        {
            DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Info, "Failed to enable Nsight Aftermath GPU crash dumps for Vulkan: " + ToHexString(static_cast<uint32_t>(result)));
            return false;
        }

        state.crashDumpsEnabled = true;
        DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Info, "Nsight Aftermath GPU crash dump collection is enabled for Vulkan.");
        return true;
#else
        (void)applicationName;
        (void)applicationVersion;
        (void)commandLine;
        (void)messageCallback;
        return false;
#endif
    }

    bool ZWAftermathRuntime::InitializeD3D12Device(
        ID3D12Device* device,
        ZWAftermathCrashDumpHelper* helper,
        HRHI::IMessageCallback* messageCallback)
    {
#if HRHI_WITH_AFTERMATH
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        if (device == nullptr || !state.api.EnsureLoaded(messageCallback))
        {
            return false;
        }

        if (!state.crashDumpsEnabled)
        {
            DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Info, "Nsight Aftermath was requested for D3D12, but GPU crash dumps were not enabled before creating the native D3D12 device.");
            return false;
        }

        const uint32_t aftermathFlags =
            GFSDK_Aftermath_FeatureFlags_EnableMarkers |
            GFSDK_Aftermath_FeatureFlags_CallStackCapturing |
            GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |
            GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo |
            GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting;

        const GFSDK_Aftermath_Result result = state.api.DX12Initialize(
            GFSDK_Aftermath_Version_API,
            aftermathFlags,
            device);

        if (!GFSDK_Aftermath_SUCCEED(result))
        {
            DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Info, "Failed to initialize Nsight Aftermath for the D3D12 device: " + ToHexString(static_cast<uint32_t>(result)));
            return false;
        }

        state.d3d12Initialized = true;
        state.activeHelper = helper;
        DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Info, "Nsight Aftermath is active for the D3D12 device.");
        return true;
#else
        (void)device;
        (void)helper;
        (void)messageCallback;
        return false;
#endif
    }

    bool ZWAftermathRuntime::InitializeVulkanDevice(
        ZWAftermathCrashDumpHelper* helper,
        HRHI::IMessageCallback* messageCallback)
    {
#if HRHI_WITH_AFTERMATH
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        if (!state.api.EnsureLoaded(messageCallback))
        {
            return false;
        }

        if (!state.crashDumpsEnabled)
        {
            DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Info, "Nsight Aftermath was requested for Vulkan, but GPU crash dumps were not enabled before creating the native Vulkan device.");
            return false;
        }

        state.vulkanInitialized = true;
        state.activeHelper = helper;
        DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Info, "Nsight Aftermath is active for the Vulkan device.");
        return true;
#else
        (void)helper;
        (void)messageCallback;
        return false;
#endif
    }

    bool ZWAftermathRuntime::CreateD3D12ContextHandle(
        ID3D12GraphicsCommandList* commandList,
        ZWAftermathContextHandle& contextHandle,
        HRHI::IMessageCallback* messageCallback)
    {
#if HRHI_WITH_AFTERMATH
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        contextHandle = nullptr;
        if (commandList == nullptr || !state.api.EnsureLoaded(messageCallback) || !state.d3d12Initialized)
        {
            return false;
        }

        GFSDK_Aftermath_ContextHandle aftermathContextHandle = nullptr;
        const GFSDK_Aftermath_Result result = state.api.DX12CreateContextHandle(commandList, &aftermathContextHandle);
        if (!GFSDK_Aftermath_SUCCEED(result))
        {
            DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Warning, "Failed to create an Nsight Aftermath command list context handle: " + ToHexString(static_cast<uint32_t>(result)));
            return false;
        }

        contextHandle = aftermathContextHandle;
        return true;
#else
        (void)commandList;
        (void)contextHandle;
        (void)messageCallback;
        return false;
#endif
    }

    void ZWAftermathRuntime::SetEventMarker(
        ZWAftermathContextHandle contextHandle,
        const void* markerData,
        uint32_t markerDataSize,
        HRHI::IMessageCallback* messageCallback)
    {
#if HRHI_WITH_AFTERMATH
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        if (contextHandle == nullptr || !state.api.EnsureLoaded(messageCallback) || !state.d3d12Initialized)
        {
            return;
        }

        const GFSDK_Aftermath_Result result = state.api.SetEventMarker(
            static_cast<GFSDK_Aftermath_ContextHandle>(contextHandle),
            const_cast<void*>(markerData),
            markerDataSize);
        if (!GFSDK_Aftermath_SUCCEED(result))
        {
            DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Warning, "Failed to insert an Nsight Aftermath event marker: " + ToHexString(static_cast<uint32_t>(result)));
        }
#else
        (void)contextHandle;
        (void)markerData;
        (void)markerDataSize;
        (void)messageCallback;
#endif
    }

    bool ZWAftermathRuntime::RegisterShaderBinary(
        ZWBinaryBlob shaderBinary,
        HRHI::EGraphicsAPI graphicsApi,
        HRHI::IMessageCallback* messageCallback)
    {
#if HRHI_WITH_AFTERMATH
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        if (!state.api.EnsureLoaded(messageCallback)
            || (!state.d3d12Initialized && !state.vulkanInitialized)
            || shaderBinary.first == nullptr
            || shaderBinary.second == 0
            || (graphicsApi != HRHI::EGraphicsAPI::D3D12 && graphicsApi != HRHI::EGraphicsAPI::VULKAN))
        {
            return false;
        }

        GFSDK_Aftermath_ShaderBinaryHash shaderHash = {};
        GFSDK_Aftermath_Result result = GFSDK_Aftermath_Result_Fail;

        if (graphicsApi == HRHI::EGraphicsAPI::D3D12)
        {
            const D3D12_SHADER_BYTECODE bytecode = { shaderBinary.first, shaderBinary.second };
            result = state.api.GetShaderHash(
                GFSDK_Aftermath_Version_API,
                &bytecode,
                &shaderHash);
        }
        else
        {
            const GFSDK_Aftermath_SpirvCode spirvCode = {
                shaderBinary.first,
                static_cast<uint32_t>(shaderBinary.second)
            };
            result = state.api.GetShaderHashSpirv(
                GFSDK_Aftermath_Version_API,
                &spirvCode,
                &shaderHash);
        }

        if (!GFSDK_Aftermath_SUCCEED(result))
        {
            const char* graphicsApiName = graphicsApi == HRHI::EGraphicsAPI::D3D12 ? "D3D12" : "Vulkan";
            DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Warning, std::string("Failed to generate an Nsight Aftermath shader hash for a ") + graphicsApiName + " shader: " + ToHexString(static_cast<uint32_t>(result)));
            return false;
        }

        std::vector<uint8_t>& storedBinary = state.shaderBinaries[shaderHash];
        storedBinary.resize(shaderBinary.second);
        std::memcpy(storedBinary.data(), shaderBinary.first, shaderBinary.second);
        return true;
#else
        (void)shaderBinary;
        (void)graphicsApi;
        (void)messageCallback;
        return false;
#endif
    }

    void ZWAftermathRuntime::ClearActiveHelper(ZWAftermathCrashDumpHelper* helper)
    {
#if HRHI_WITH_AFTERMATH
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        if (state.activeHelper == helper)
        {
            state.activeHelper = nullptr;
        }
#else
        (void)helper;
#endif
    }

    void ZWAftermathRuntime::WaitForCrashDump(uint32_t timeoutMs, HRHI::IMessageCallback* messageCallback)
    {
#if HRHI_WITH_AFTERMATH
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        if (!state.api.EnsureLoaded(messageCallback) || !state.crashDumpsEnabled)
        {
            return;
        }

        GFSDK_Aftermath_CrashDump_Status status = GFSDK_Aftermath_CrashDump_Status_Unknown;
        GFSDK_Aftermath_Result result = state.api.GetCrashDumpStatus(&status);
        if (!GFSDK_Aftermath_SUCCEED(result))
        {
            return;
        }

        const auto startTime = std::chrono::steady_clock::now();
        auto elapsedTime = std::chrono::milliseconds::zero();

        while (status != GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed
            && status != GFSDK_Aftermath_CrashDump_Status_Finished
            && elapsedTime.count() < timeoutMs)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            result = state.api.GetCrashDumpStatus(&status);
            if (!GFSDK_Aftermath_SUCCEED(result))
            {
                return;
            }

            elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime);
        }

        if (status == GFSDK_Aftermath_CrashDump_Status_Finished)
        {
            DispatchAftermathMessage(messageCallback, HRHI::EMessageSeverity::Info, "Nsight Aftermath finished processing the GPU crash dump.");
        }
#else
        (void)timeoutMs;
        (void)messageCallback;
#endif
    }

    bool ZWAftermathRuntime::IsAvailable() const
    {
        return HRHI_WITH_AFTERMATH != 0;
    }

    bool ZWAftermathRuntime::IsEnabled() const
    {
#if HRHI_WITH_AFTERMATH
        ZWAftermathState& state = GetAftermathState();
        return state.crashDumpsEnabled;
#else
        return false;
#endif
    }

#if HRHI_WITH_AFTERMATH
    void ZWAftermathRuntime::OnCrashDump(const void* gpuCrashDump, uint32_t gpuCrashDumpSize)
    {
        WriteGpuCrashDumpToFile(gpuCrashDump, gpuCrashDumpSize);
    }

    void ZWAftermathRuntime::OnShaderDebugInfo(const void* shaderDebugInfo, uint32_t shaderDebugInfoSize)
    {
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier = {};
        const GFSDK_Aftermath_Result result = state.api.GetShaderDebugInfoIdentifier(
            GFSDK_Aftermath_Version_API,
            shaderDebugInfo,
            shaderDebugInfoSize,
            &identifier);
        if (!GFSDK_Aftermath_SUCCEED(result))
        {
            return;
        }

        std::vector<uint8_t> data(shaderDebugInfoSize);
        std::memcpy(data.data(), shaderDebugInfo, shaderDebugInfoSize);
        state.shaderDebugInfos[identifier].swap(data);

        WriteShaderDebugInformationToFile(identifier, shaderDebugInfo, shaderDebugInfoSize);
    }

    void ZWAftermathRuntime::OnDescription(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription)
    {
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, state.applicationName.c_str());
        addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationVersion, state.applicationVersion.c_str());
        if (!state.commandLine.empty())
        {
            addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_UserDefined, state.commandLine.c_str());
        }
    }

    void ZWAftermathRuntime::OnResolveMarker(const void* markerData, uint32_t markerDataSize, void** resolvedMarkerData, uint32_t* resolvedMarkerDataSize)
    {
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        if (resolvedMarkerData != nullptr)
        {
            *resolvedMarkerData = nullptr;
        }

        if (resolvedMarkerDataSize != nullptr)
        {
            *resolvedMarkerDataSize = 0;
        }

        if (markerDataSize != 0
            || markerData == nullptr
            || state.activeHelper == nullptr
            || resolvedMarkerData == nullptr
            || resolvedMarkerDataSize == nullptr)
        {
            return;
        }

        const auto [found, resolvedMarker] = state.activeHelper->ResolveMarker(reinterpret_cast<size_t>(markerData));
        if (!found)
        {
            return;
        }

        const std::string& markerString = resolvedMarker.get();
        *resolvedMarkerData = const_cast<char*>(markerString.data());
        *resolvedMarkerDataSize = static_cast<uint32_t>(markerString.size());
    }

    void ZWAftermathRuntime::OnShaderDebugInfoLookup(const GFSDK_Aftermath_ShaderDebugInfoIdentifier& identifier, PFN_GFSDK_Aftermath_SetData setShaderDebugInfo) const
    {
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        const auto found = state.shaderDebugInfos.find(identifier);
        if (found == state.shaderDebugInfos.end())
        {
            return;
        }

        setShaderDebugInfo(found->second.data(), static_cast<uint32_t>(found->second.size()));
    }

    void ZWAftermathRuntime::OnShaderLookup(const GFSDK_Aftermath_ShaderBinaryHash& shaderHash, PFN_GFSDK_Aftermath_SetData setShaderBinary) const
    {
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        const auto found = state.shaderBinaries.find(shaderHash);
        if (found == state.shaderBinaries.end())
        {
            return;
        }

        setShaderBinary(found->second.data(), static_cast<uint32_t>(found->second.size()));
    }

    void ZWAftermathRuntime::WriteGpuCrashDumpToFile(const void* gpuCrashDump, uint32_t gpuCrashDumpSize)
    {
        ZWAftermathState& state = GetAftermathState();
        std::lock_guard<std::recursive_mutex> lock(state.mutex);

        GFSDK_Aftermath_GpuCrashDump_Decoder decoder = {};
        GFSDK_Aftermath_Result result = state.api.CreateDecoder(
            GFSDK_Aftermath_Version_API,
            gpuCrashDump,
            gpuCrashDumpSize,
            &decoder);
        if (!GFSDK_Aftermath_SUCCEED(result))
        {
            return;
        }

        GFSDK_Aftermath_GpuCrashDump_BaseInfo baseInfo = {};
        result = state.api.GetBaseInfo(decoder, &baseInfo);
        if (!GFSDK_Aftermath_SUCCEED(result))
        {
            state.api.DestroyDecoder(decoder);
            return;
        }

        uint32_t applicationNameLength = 0;
        result = state.api.GetDescriptionSize(decoder, GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, &applicationNameLength);

        std::string applicationName = state.applicationName;
        if (GFSDK_Aftermath_SUCCEED(result) && applicationNameLength > 1)
        {
            std::vector<char> applicationNameBuffer(applicationNameLength, '\0');
            if (GFSDK_Aftermath_SUCCEED(state.api.GetDescription(
                decoder,
                GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName,
                applicationNameLength,
                applicationNameBuffer.data())))
            {
                applicationName = applicationNameBuffer.data();
            }
        }

        const std::filesystem::path dumpDirectory = std::filesystem::current_path() / "Aftermath";
        std::filesystem::create_directories(dumpDirectory);

        const std::string baseFileName = applicationName + "-" + std::to_string(baseInfo.pid) + "-" + std::to_string(++state.crashDumpCount);

        const std::filesystem::path dumpFilePath = dumpDirectory / (baseFileName + ".nv-gpudmp");
        std::ofstream dumpFile(dumpFilePath, std::ios::binary);
        if (dumpFile)
        {
            dumpFile.write(reinterpret_cast<const char*>(gpuCrashDump), gpuCrashDumpSize);
        }

        uint32_t jsonSize = 0;
        result = state.api.GenerateJson(
            decoder,
            GFSDK_Aftermath_GpuCrashDumpDecoderFlags_ALL_INFO,
            GFSDK_Aftermath_GpuCrashDumpFormatterFlags_NONE,
            AftermathShaderDebugInfoLookupCallback,
            AftermathShaderLookupCallback,
            nullptr,
            this,
            &jsonSize);

        if (GFSDK_Aftermath_SUCCEED(result) && result != GFSDK_Aftermath_Result_NotAvailable && jsonSize > 0)
        {
            std::vector<char> json(jsonSize, '\0');
            if (GFSDK_Aftermath_SUCCEED(state.api.GetJson(decoder, jsonSize, json.data())))
            {
                const std::filesystem::path jsonFilePath = dumpDirectory / (baseFileName + ".nv-gpudmp.json");
                std::ofstream jsonFile(jsonFilePath, std::ios::binary);
                if (jsonFile)
                {
                    jsonFile.write(json.data(), static_cast<std::streamsize>(json.size() - 1));
                }
            }
        }

        state.api.DestroyDecoder(decoder);
    }

    void ZWAftermathRuntime::WriteShaderDebugInformationToFile(
        GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier,
        const void* shaderDebugInfo,
        uint32_t shaderDebugInfoSize)
    {
        const std::filesystem::path dumpDirectory = std::filesystem::current_path() / "Aftermath";
        std::filesystem::create_directories(dumpDirectory);

        const std::filesystem::path shaderDebugInfoPath = dumpDirectory / ("shader-" + ToString(identifier) + ".nvdbg");
        std::ofstream shaderDebugInfoFile(shaderDebugInfoPath, std::ios::binary);
        if (shaderDebugInfoFile)
        {
            shaderDebugInfoFile.write(reinterpret_cast<const char*>(shaderDebugInfo), shaderDebugInfoSize);
        }
    }
#endif
}
