#include <windows.h>
#include <dxgi.h>
#include <d3d12.h>

#include <atomic>
#include <mutex>
#include <string>
#include <utility>

#pragma warning(push)
#pragma warning(disable: 4828)
#include "nvapi.h"
#pragma warning(pop)

namespace
{
    using ZWNvApiQueryInterfaceFn = void* (__cdecl*)(NvU32);

    std::once_flag gNvApiLoadOnce;
    HMODULE gNvApiModule = nullptr;
    ZWNvApiQueryInterfaceFn gNvApiQueryInterface = nullptr;
    NvAPI_Status gNvApiLoadStatus = NVAPI_LIBRARY_NOT_FOUND;

    bool BuildSystemLibraryPath(const wchar_t* libraryName, std::wstring& outPath)
    {
        wchar_t systemDirectory[MAX_PATH] = {};
        const UINT length = ::GetSystemDirectoryW(systemDirectory, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            return false;
        }

        outPath.assign(systemDirectory, length);
        if (!outPath.empty() && outPath.back() != L'\\')
        {
            outPath.push_back(L'\\');
        }

        outPath += libraryName;
        return true;
    }

    void LoadNvApiModule()
    {
#if defined(_WIN64)
        constexpr const wchar_t* cNvApiLibraryName = L"nvapi64.dll";
#else
        constexpr const wchar_t* cNvApiLibraryName = L"nvapi.dll";
#endif

        std::wstring systemLibraryPath;
        if (BuildSystemLibraryPath(cNvApiLibraryName, systemLibraryPath))
        {
            gNvApiModule = ::LoadLibraryW(systemLibraryPath.c_str());
        }

        if (gNvApiModule == nullptr)
        {
            gNvApiModule = ::LoadLibraryW(cNvApiLibraryName);
        }

        if (gNvApiModule == nullptr)
        {
            gNvApiLoadStatus = NVAPI_LIBRARY_NOT_FOUND;
            return;
        }

        gNvApiQueryInterface = reinterpret_cast<ZWNvApiQueryInterfaceFn>(::GetProcAddress(gNvApiModule, "nvapi_QueryInterface"));
        if (gNvApiQueryInterface == nullptr)
        {
            ::FreeLibrary(gNvApiModule);
            gNvApiModule = nullptr;
            gNvApiLoadStatus = NVAPI_LIBRARY_NOT_FOUND;
            return;
        }

        gNvApiLoadStatus = NVAPI_OK;
    }

    NvAPI_Status EnsureNvApiLoaded()
    {
        std::call_once(gNvApiLoadOnce, LoadNvApiModule);
        return gNvApiLoadStatus;
    }

    template<typename Fn>
    Fn ResolveNvApiFunction(NvU32 interfaceId, std::atomic<void*>& cache)
    {
        if (EnsureNvApiLoaded() != NVAPI_OK || gNvApiQueryInterface == nullptr)
        {
            return nullptr;
        }

        void* functionPointer = cache.load(std::memory_order_acquire);
        if (functionPointer == nullptr)
        {
            functionPointer = gNvApiQueryInterface(interfaceId);
            if (functionPointer == nullptr)
            {
                return nullptr;
            }

            cache.store(functionPointer, std::memory_order_release);
        }

        return reinterpret_cast<Fn>(functionPointer);
    }

    template<typename Fn, typename... Args>
    NvAPI_Status CallNvApiFunction(NvU32 interfaceId, std::atomic<void*>& cache, Args&&... args)
    {
        Fn functionPointer = ResolveNvApiFunction<Fn>(interfaceId, cache);
        if (functionPointer == nullptr)
        {
            const NvAPI_Status loadStatus = EnsureNvApiLoaded();
            return loadStatus == NVAPI_OK ? NVAPI_NO_IMPLEMENTATION : loadStatus;
        }

        return functionPointer(std::forward<Args>(args)...);
    }
}

extern "C"
{
    NvAPI_Status __cdecl NvAPI_Initialize()
    {
        using Fn = decltype(&NvAPI_Initialize);
        static std::atomic<void*> sFunctionPointer = nullptr;
        return CallNvApiFunction<Fn>(0x0150E828u, sFunctionPointer);
    }

    NvAPI_Status __cdecl NvAPI_D3D12_QuerySinglePassStereoSupport(
        ID3D12Device* pDevice,
        NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS* pQuerySinglePassStereoSupportedParams)
    {
        using Fn = decltype(&NvAPI_D3D12_QuerySinglePassStereoSupport);
        static std::atomic<void*> sFunctionPointer = nullptr;
        return CallNvApiFunction<Fn>(0x3B03791Bu, sFunctionPointer, pDevice, pQuerySinglePassStereoSupportedParams);
    }

    NvAPI_Status __cdecl NvAPI_D3D12_SetSinglePassStereoMode(
        ID3D12GraphicsCommandList* pCommandList,
        NvU32 numViews,
        NvU32 renderTargetIndexOffset,
        NvU8 independentViewportMaskEnable)
    {
        using Fn = decltype(&NvAPI_D3D12_SetSinglePassStereoMode);
        static std::atomic<void*> sFunctionPointer = nullptr;
        return CallNvApiFunction<Fn>(
            0x83556D87u,
            sFunctionPointer,
            pCommandList,
            numViews,
            renderTargetIndexOffset,
            independentViewportMaskEnable);
    }

    NvAPI_Status __cdecl NvAPI_D3D12_IsNvShaderExtnOpCodeSupported(
        ID3D12Device* pDevice,
        NvU32 opCode,
        bool* pSupported)
    {
        using Fn = decltype(&NvAPI_D3D12_IsNvShaderExtnOpCodeSupported);
        static std::atomic<void*> sFunctionPointer = nullptr;
        return CallNvApiFunction<Fn>(0x3DFACEC8u, sFunctionPointer, pDevice, opCode, pSupported);
    }

    NvAPI_Status __cdecl NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread(
        IUnknown* pDev,
        NvU32 uavSlot,
        NvU32 uavSpace)
    {
        using Fn = decltype(&NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread);
        static std::atomic<void*> sFunctionPointer = nullptr;
        return CallNvApiFunction<Fn>(0x43D867C0u, sFunctionPointer, pDev, uavSlot, uavSpace);
    }

    NvAPI_Status __cdecl NvAPI_D3D12_CreateGraphicsPipelineState(
        ID3D12Device* pDevice,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pPSODesc,
        NvU32 numExtensions,
        const NVAPI_D3D12_PSO_EXTENSION_DESC** ppExtensions,
        ID3D12PipelineState** ppPSO)
    {
        using Fn = decltype(&NvAPI_D3D12_CreateGraphicsPipelineState);
        static std::atomic<void*> sFunctionPointer = nullptr;
        return CallNvApiFunction<Fn>(0x2FC28856u, sFunctionPointer, pDevice, pPSODesc, numExtensions, ppExtensions, ppPSO);
    }

    NvAPI_Status __cdecl NvAPI_D3D12_CreateComputePipelineState(
        ID3D12Device* pDevice,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC* pPSODesc,
        NvU32 numExtensions,
        const NVAPI_D3D12_PSO_EXTENSION_DESC** ppExtensions,
        ID3D12PipelineState** ppPSO)
    {
        using Fn = decltype(&NvAPI_D3D12_CreateComputePipelineState);
        static std::atomic<void*> sFunctionPointer = nullptr;
        return CallNvApiFunction<Fn>(0x2762DEACu, sFunctionPointer, pDevice, pPSODesc, numExtensions, ppExtensions, ppPSO);
    }

    NvAPI_Status __cdecl NvAPI_D3D12_GetRaytracingCaps(
        ID3D12Device* pDevice,
        NVAPI_D3D12_RAYTRACING_CAPS_TYPE type,
        void* pData,
        size_t dataSize)
    {
        using Fn = decltype(&NvAPI_D3D12_GetRaytracingCaps);
        static std::atomic<void*> sFunctionPointer = nullptr;
        return CallNvApiFunction<Fn>(0x85A6C2A0u, sFunctionPointer, pDevice, type, pData, dataSize);
    }

    NvAPI_Status __cdecl NvAPI_D3D12_GetRaytracingOpacityMicromapArrayPrebuildInfo(
        ID3D12Device5* pDevice,
        NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS* pParams)
    {
        using Fn = decltype(&NvAPI_D3D12_GetRaytracingOpacityMicromapArrayPrebuildInfo);
        static std::atomic<void*> sFunctionPointer = nullptr;
        return CallNvApiFunction<Fn>(0x4726D180u, sFunctionPointer, pDevice, pParams);
    }

    NvAPI_Status __cdecl NvAPI_D3D12_SetCreatePipelineStateOptions(
        ID3D12Device5* pDevice,
        const NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS* pState)
    {
        using Fn = decltype(&NvAPI_D3D12_SetCreatePipelineStateOptions);
        static std::atomic<void*> sFunctionPointer = nullptr;
        return CallNvApiFunction<Fn>(0x5C607A27u, sFunctionPointer, pDevice, pState);
    }

    NvAPI_Status __cdecl NvAPI_D3D12_BuildRaytracingOpacityMicromapArray(
        ID3D12GraphicsCommandList4* pCommandList,
        NVAPI_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_PARAMS* pParams)
    {
        using Fn = decltype(&NvAPI_D3D12_BuildRaytracingOpacityMicromapArray);
        static std::atomic<void*> sFunctionPointer = nullptr;
        return CallNvApiFunction<Fn>(0x814F8D11u, sFunctionPointer, pCommandList, pParams);
    }

    NvAPI_Status __cdecl NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx(
        ID3D12Device5* pDevice,
        NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS* pParams)
    {
        using Fn = decltype(&NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx);
        static std::atomic<void*> sFunctionPointer = nullptr;
        return CallNvApiFunction<Fn>(0x8D025B77u, sFunctionPointer, pDevice, pParams);
    }

    NvAPI_Status __cdecl NvAPI_D3D12_BuildRaytracingAccelerationStructureEx(
        ID3D12GraphicsCommandList4* pCommandList,
        const NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS* pParams)
    {
        using Fn = decltype(&NvAPI_D3D12_BuildRaytracingAccelerationStructureEx);
        static std::atomic<void*> sFunctionPointer = nullptr;
        return CallNvApiFunction<Fn>(0xE24EAD45u, sFunctionPointer, pCommandList, pParams);
    }
}
