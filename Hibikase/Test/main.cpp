#include <BackEnd/RHIconstants.h>
#include <BackEnd/RHIinterface.h>
#include <BackEnd/d3d12unique.h>
#include <Utils/consolelogger.h>
#include <Window/window.h>
#include <Window/windowmanager.h>

#include <GLFW/glfw3.h>
#include <Windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{

namespace fs = std::filesystem;

constexpr std::size_t kSwapChainBufferCount = 2;
constexpr DXGI_FORMAT kSwapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

std::string HrToString(HRESULT result)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << result;
    return stream.str();
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int requiredSize = ::WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);

    if (requiredSize <= 1)
    {
        return {};
    }

    std::vector<char> buffer(static_cast<std::size_t>(requiredSize), '\0');
    ::WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        -1,
        buffer.data(),
        requiredSize,
        nullptr,
        nullptr);

    return std::string(buffer.data());
}

fs::path GetExecutableDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return fs::path(path).parent_path();
}

std::vector<std::uint8_t> ReadBinaryFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return {};
    }

    const std::streamsize size = file.tellg();
    if (size <= 0)
    {
        return {};
    }

    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(data.data()), size))
    {
        return {};
    }

    return data;
}

int ParseSelfTestDurationMs(int argc, char** argv)
{
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const std::string argument = argv[argumentIndex];
        const std::string prefix = "--self-test-ms=";
        if (argument.rfind(prefix, 0) == 0)
        {
            return std::max(0, std::atoi(argument.substr(prefix.size()).c_str()));
        }

        if (argument == "--self-test")
        {
            return 2000;
        }
    }

    return 0;
}

ComPtr<IDXGIAdapter1> SelectAdapter(IDXGIFactory4* factory, bool& isWarpAdapter)
{
    isWarpAdapter = false;

    if (factory == nullptr)
    {
        return nullptr;
    }

    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(factory6.ReleaseAndGetAddressOf()))))
    {
        for (UINT adapterIndex = 0;; ++adapterIndex)
        {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory6->EnumAdapterByGpuPreference(
                adapterIndex,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())) == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            DXGI_ADAPTER_DESC1 description = {};
            adapter->GetDesc1(&description);
            if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            {
                continue;
            }

            ComPtr<ID3D12Device> testDevice;
            if (SUCCEEDED(D3D12CreateDevice(
                adapter.Get(),
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(testDevice.ReleaseAndGetAddressOf()))))
            {
                return adapter;
            }
        }
    }

    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIndex, adapter.ReleaseAndGetAddressOf()) == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }

        DXGI_ADAPTER_DESC1 description = {};
        adapter->GetDesc1(&description);
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            continue;
        }

        ComPtr<ID3D12Device> testDevice;
        if (SUCCEEDED(D3D12CreateDevice(
            adapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(testDevice.ReleaseAndGetAddressOf()))))
        {
            return adapter;
        }
    }

    ComPtr<IDXGIAdapter1> warpAdapter;
    if (SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(warpAdapter.ReleaseAndGetAddressOf()))))
    {
        isWarpAdapter = true;
        return warpAdapter;
    }

    return nullptr;
}

class BackEndMessageCallback final : public HRHI::IMessageCallback
{
public:
    void message(HRHI::EMessageSeverity severity, const char* messageText) override
    {
        const char* text = messageText != nullptr ? messageText : "Unknown backend message.";

        switch (severity)
        {
        case HRHI::EMessageSeverity::Info:
            HApp::ZWConsoleLogger::Info("[HRHI] {}", text);
            break;

        case HRHI::EMessageSeverity::Warning:
            HApp::ZWConsoleLogger::Warning("[HRHI] {}", text);
            break;

        case HRHI::EMessageSeverity::Error:
        case HRHI::EMessageSeverity::Fatal:
            HApp::ZWConsoleLogger::Error("[HRHI] {}", text);
            break;
        }
    }
};

struct SwapChainRenderTarget
{
    ComPtr<ID3D12Resource> resource;
    HRHI::ZWTextureHandle texture;
    HRHI::ZWFramebufferHandle framebuffer;

    void Reset()
    {
        framebuffer = nullptr;
        texture = nullptr;
        resource.Reset();
    }
};

class TriangleTestApplication final
{
public:
    bool Initialize()
    {
        HApp::ZWConsoleLogger::PrintSection("Startup");

        if (!mWindowManager.Initialize())
        {
            HApp::ZWConsoleLogger::Error("Failed to initialize window manager: {}", mWindowManager.GetLastError());
            return false;
        }

        HApp::ZWWindowDesc windowDesc;
        windowDesc.width = 1280;
        windowDesc.height = 720;
        windowDesc.title = "Hibikase D3D12 Triangle Test";
        windowDesc.graphicsBackend = HApp::ZWGraphicsBackend::D3D12;
        windowDesc.role = HApp::ZWWindowRole::Main;

        mWindow = mWindowManager.CreateWindow(windowDesc);
        if (mWindow == nullptr)
        {
            HApp::ZWConsoleLogger::Error("Failed to create test window: {}", mWindowManager.GetLastError());
            return false;
        }

        if (!CreateNativeDeviceAndSwapChain())
        {
            return false;
        }

        if (!CreateBackEndDevice())
        {
            return false;
        }

        if (!CreateRenderTargets())
        {
            return false;
        }

        if (!CreatePipeline())
        {
            return false;
        }

        HApp::ZWConsoleLogger::PrintProperty("Window title", mWindow->GetTitle());
        HApp::ZWConsoleLogger::PrintProperty("Back buffer size", std::to_string(mFramebufferWidth) + " x " + std::to_string(mFramebufferHeight));
        HApp::ZWConsoleLogger::PrintProperty("Shader directory", (GetExecutableDirectory() / "Shaders" / "test").string());
        HApp::ZWConsoleLogger::PrintListItem("Press ESC to close the test window.");
        return true;
    }

    int Run(int selfTestDurationMs)
    {
        HApp::ZWConsoleLogger::PrintSection("Runtime");
        const auto startTime = std::chrono::steady_clock::now();

        while (mWindow != nullptr && !mWindow->ShouldClose())
        {
            mWindowManager.PollEvents();

            if (glfwGetKey(mWindow->GetNativeWindow(), GLFW_KEY_ESCAPE) == GLFW_PRESS)
            {
                mWindow->RequestClose();
                continue;
            }

            const HApp::ZWSize framebufferSize = mWindow->GetFramebufferSize();
            if (framebufferSize.width <= 0 || framebufferSize.height <= 0 || mWindow->IsIconified())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }

            if (!ResizeSwapChainIfNeeded())
            {
                return 1;
            }

            if (!RenderFrame())
            {
                return 1;
            }

            if (selfTestDurationMs > 0)
            {
                const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime).count();
                if (elapsedMs >= selfTestDurationMs)
                {
                    mWindow->RequestClose();
                }
            }
        }

        HApp::ZWConsoleLogger::Info("Triangle test finished cleanly.");
        return 0;
    }

    void Shutdown()
    {
        WaitForGpu();
        WaitForPresentQueue();
        DestroyRenderTargets();

        mPipeline = nullptr;
        mPixelShader = nullptr;
        mVertexShader = nullptr;
        mCommandList = nullptr;
        mDevice = nullptr;

        mWindow = nullptr;
        mWindowManager.Shutdown();

        mSwapChain.Reset();
        mGraphicsQueue.Reset();
        mNativeDevice.Reset();
        mAdapter.Reset();
        mFactory.Reset();
    }

private:
    bool CreateNativeDeviceAndSwapChain()
    {
#if defined(_DEBUG)
        UINT factoryFlags = 0;
        ComPtr<ID3D12Debug> debugLayer;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugLayer.ReleaseAndGetAddressOf()))))
        {
            debugLayer->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            HApp::ZWConsoleLogger::Info("Enabled the D3D12 debug layer.");
        }
#else
        UINT factoryFlags = 0;
#endif

        HRESULT result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(mFactory.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            HApp::ZWConsoleLogger::Error("CreateDXGIFactory2 failed: {}", HrToString(result));
            return false;
        }

        mAdapter = SelectAdapter(mFactory.Get(), mUsingWarpAdapter);
        if (mAdapter == nullptr)
        {
            HApp::ZWConsoleLogger::Error("Failed to find a DXGI adapter for D3D12.");
            return false;
        }

        DXGI_ADAPTER_DESC1 adapterDesc = {};
        mAdapter->GetDesc1(&adapterDesc);
        HApp::ZWConsoleLogger::PrintProperty("Adapter", WideToUtf8(adapterDesc.Description));
        HApp::ZWConsoleLogger::PrintProperty("Using WARP", mUsingWarpAdapter);

        result = D3D12CreateDevice(
            mAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(mNativeDevice.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            HApp::ZWConsoleLogger::Error("D3D12CreateDevice failed: {}", HrToString(result));
            return false;
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        result = mNativeDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(mGraphicsQueue.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            HApp::ZWConsoleLogger::Error("CreateCommandQueue failed: {}", HrToString(result));
            return false;
        }

        const HApp::ZWSize framebufferSize = mWindow->GetFramebufferSize();
        mFramebufferWidth = framebufferSize.width > 0 ? static_cast<UINT>(framebufferSize.width) : 1u;
        mFramebufferHeight = framebufferSize.height > 0 ? static_cast<UINT>(framebufferSize.height) : 1u;

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = mFramebufferWidth;
        swapChainDesc.Height = mFramebufferHeight;
        swapChainDesc.Format = kSwapChainFormat;
        swapChainDesc.Stereo = FALSE;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = static_cast<UINT>(kSwapChainBufferCount);
        swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        swapChainDesc.Flags = 0;

        ComPtr<IDXGISwapChain1> swapChain1;
        result = mFactory->CreateSwapChainForHwnd(
            mGraphicsQueue.Get(),
            mWindow->GetNativeHandle(),
            &swapChainDesc,
            nullptr,
            nullptr,
            swapChain1.ReleaseAndGetAddressOf());
        if (FAILED(result))
        {
            HApp::ZWConsoleLogger::Error("CreateSwapChainForHwnd failed: {}", HrToString(result));
            return false;
        }

        result = mFactory->MakeWindowAssociation(mWindow->GetNativeHandle(), DXGI_MWA_NO_ALT_ENTER);
        if (FAILED(result))
        {
            HApp::ZWConsoleLogger::Warning("MakeWindowAssociation failed: {}", HrToString(result));
        }

        result = swapChain1.As(&mSwapChain);
        if (FAILED(result))
        {
            HApp::ZWConsoleLogger::Error("Failed to query IDXGISwapChain3: {}", HrToString(result));
            return false;
        }

        return true;
    }

    bool CreateBackEndDevice()
    {
        HRHI::HD3D12::ZWDeviceDesc deviceDesc;
        deviceDesc.errorCB = &mBackEndMessageCallback;
        deviceDesc.pDevice = mNativeDevice.Get();
        deviceDesc.pGraphicsCommandQueue = mGraphicsQueue.Get();

        mDevice = HRHI::HD3D12::CreateDevice(deviceDesc);
        if (!mDevice)
        {
            HApp::ZWConsoleLogger::Error("Failed to create the HRHI D3D12 device wrapper.");
            return false;
        }

        HRHI::ZWCommandListParameters commandListParameters;
        commandListParameters.setQueueType(HRHI::ECommandQueue::Graphics);
        mCommandList = mDevice->CreateCommandList(commandListParameters);
        if (!mCommandList)
        {
            HApp::ZWConsoleLogger::Error("Failed to create the HRHI graphics command list.");
            return false;
        }

        return true;
    }

    bool CreateRenderTargets()
    {
        DestroyRenderTargets();

        for (UINT bufferIndex = 0; bufferIndex < static_cast<UINT>(kSwapChainBufferCount); ++bufferIndex)
        {
            HRESULT result = mSwapChain->GetBuffer(
                bufferIndex,
                IID_PPV_ARGS(mRenderTargets[bufferIndex].resource.ReleaseAndGetAddressOf()));
            if (FAILED(result))
            {
                HApp::ZWConsoleLogger::Error("GetBuffer({}) failed: {}", bufferIndex, HrToString(result));
                return false;
            }

            HRHI::ZWTextureDesc textureDesc;
            textureDesc.setWidth(mFramebufferWidth)
                .setHeight(mFramebufferHeight)
                .setFormat(HRHI::EFormat::RGBA8_UNORM)
                .setDimension(HRHI::ETextureDimension::Texture2D)
                .setIsRenderTarget(true)
                .setInitialState(HRHI::EResourceStates::Present)
                .setKeepInitialState(true)
                .setDebugName("Test SwapChain BackBuffer " + std::to_string(bufferIndex));
            textureDesc.isShaderResource = false;

            mRenderTargets[bufferIndex].texture = mDevice->CreateHandleForNativeTexture(
                HRHI::HRHIObjectTypes::gD3D12Resource,
                HCommon::ZWObject(mRenderTargets[bufferIndex].resource.Get()),
                textureDesc);
            if (!mRenderTargets[bufferIndex].texture)
            {
                HApp::ZWConsoleLogger::Error("Failed to wrap swap-chain buffer {} as an HRHI texture.", bufferIndex);
                return false;
            }

            HRHI::ZWFramebufferDesc framebufferDesc;
            framebufferDesc.addColorAttachment(mRenderTargets[bufferIndex].texture.Get());
            mRenderTargets[bufferIndex].framebuffer = mDevice->CreateFramebuffer(framebufferDesc);
            if (!mRenderTargets[bufferIndex].framebuffer)
            {
                HApp::ZWConsoleLogger::Error("Failed to create framebuffer for swap-chain buffer {}.", bufferIndex);
                return false;
            }
        }

        return true;
    }

    bool CreatePipeline()
    {
        const fs::path shaderDirectory = GetExecutableDirectory() / "Shaders" / "test";
        const fs::path vertexShaderPath = shaderDirectory / "triangle_vs.cso";
        const fs::path pixelShaderPath = shaderDirectory / "triangle_ps.cso";

        const std::vector<std::uint8_t> vertexShaderBinary = ReadBinaryFile(vertexShaderPath);
        if (vertexShaderBinary.empty())
        {
            HApp::ZWConsoleLogger::Error("Failed to read vertex shader binary: {}", vertexShaderPath.string());
            return false;
        }

        const std::vector<std::uint8_t> pixelShaderBinary = ReadBinaryFile(pixelShaderPath);
        if (pixelShaderBinary.empty())
        {
            HApp::ZWConsoleLogger::Error("Failed to read pixel shader binary: {}", pixelShaderPath.string());
            return false;
        }

        HRHI::ZWShaderDesc vertexShaderDesc;
        vertexShaderDesc.setShaderType(HRHI::EShaderType::Vertex).setDebugName("Test Triangle VS");
        mVertexShader = mDevice->CreateShader(
            vertexShaderDesc,
            vertexShaderBinary.data(),
            vertexShaderBinary.size());
        if (!mVertexShader)
        {
            HApp::ZWConsoleLogger::Error("Failed to create the vertex shader.");
            return false;
        }

        HRHI::ZWShaderDesc pixelShaderDesc;
        pixelShaderDesc.setShaderType(HRHI::EShaderType::Pixel).setDebugName("Test Triangle PS");
        mPixelShader = mDevice->CreateShader(
            pixelShaderDesc,
            pixelShaderBinary.data(),
            pixelShaderBinary.size());
        if (!mPixelShader)
        {
            HApp::ZWConsoleLogger::Error("Failed to create the pixel shader.");
            return false;
        }

        HRHI::ZWRenderState renderState;
        renderState.depthStencilState.disableDepthTest().disableDepthWrite();
        renderState.rasterState.setCullNone().enableDepthClip();

        HRHI::ZWGraphicsPipelineDesc pipelineDesc;
        pipelineDesc.setVertexShader(mVertexShader.Get())
            .setPixelShader(mPixelShader.Get())
            .setRenderState(renderState);

        if (!mRenderTargets[0].framebuffer)
        {
            HApp::ZWConsoleLogger::Error("No framebuffer is available to derive pipeline compatibility info.");
            return false;
        }

        mPipeline = mDevice->CreateGraphicsPipeline(
            pipelineDesc,
            mRenderTargets[0].framebuffer->GetFramebufferInfo());
        if (!mPipeline)
        {
            HApp::ZWConsoleLogger::Error("Failed to create the triangle graphics pipeline.");
            return false;
        }

        return true;
    }

    bool ResizeSwapChainIfNeeded()
    {
        const HApp::ZWSize framebufferSize = mWindow->GetFramebufferSize();
        const UINT newWidth = framebufferSize.width > 0 ? static_cast<UINT>(framebufferSize.width) : 0u;
        const UINT newHeight = framebufferSize.height > 0 ? static_cast<UINT>(framebufferSize.height) : 0u;

        if (newWidth == 0 || newHeight == 0)
        {
            return true;
        }

        if (newWidth == mFramebufferWidth && newHeight == mFramebufferHeight)
        {
            return true;
        }

        HApp::ZWConsoleLogger::Info("Resizing swap chain to {} x {}.", newWidth, newHeight);
        WaitForGpu();
        // Present is queued outside the HRHI device wrapper, so wait for it explicitly
        // before releasing swap-chain back buffers for ResizeBuffers.
        WaitForPresentQueue();
        DestroyRenderTargets();

        HRESULT result = mSwapChain->ResizeBuffers(
            static_cast<UINT>(kSwapChainBufferCount),
            newWidth,
            newHeight,
            kSwapChainFormat,
            0);
        if (FAILED(result))
        {
            HApp::ZWConsoleLogger::Error("ResizeBuffers failed: {}", HrToString(result));
            return false;
        }

        mFramebufferWidth = newWidth;
        mFramebufferHeight = newHeight;
        return CreateRenderTargets();
    }

    bool RenderFrame()
    {
        const UINT backBufferIndex = mSwapChain->GetCurrentBackBufferIndex();
        SwapChainRenderTarget& renderTarget = mRenderTargets[backBufferIndex];

        mCommandList->Open();
        mCommandList->ClearTextureFloat(
            renderTarget.texture.Get(),
            HRHI::sAllSubresources,
            HRHI::ZWColor(0.08f, 0.09f, 0.12f, 1.0f));

        HRHI::ZWGraphicsState graphicsState;
        graphicsState.setPipeline(mPipeline.Get()).setFramebuffer(renderTarget.framebuffer.Get());

        HRHI::ZWViewportState viewportState;
        viewportState.addViewportAndScissorRect(
            HRHI::ZWViewport(static_cast<float>(mFramebufferWidth), static_cast<float>(mFramebufferHeight)));
        graphicsState.setViewport(viewportState);

        mCommandList->SetGraphicsState(graphicsState);

        HRHI::ZWDrawArguments drawArguments;
        drawArguments.setVertexCount(3);
        mCommandList->Draw(drawArguments);
        mCommandList->Close();

        HRHI::ICommandList* commandLists[] = { mCommandList.Get() };
        mDevice->ExecuteCommandLists(commandLists, 1, HRHI::ECommandQueue::Graphics);

        if (!mDevice->WaitForIdle())
        {
            HApp::ZWConsoleLogger::Error("WaitForIdle failed after submitting the triangle draw.");
            return false;
        }

        mDevice->RunGarbageCollection();

        const HRESULT result = mSwapChain->Present(1, 0);
        if (FAILED(result))
        {
            HApp::ZWConsoleLogger::Error("Present failed: {}", HrToString(result));
            return false;
        }

        return true;
    }

    void WaitForGpu()
    {
        if (!mDevice)
        {
            return;
        }

        if (!mDevice->WaitForIdle())
        {
            HApp::ZWConsoleLogger::Warning("WaitForIdle reported a failure while synchronizing the GPU.");
        }

        mDevice->RunGarbageCollection();
    }

    void WaitForPresentQueue()
    {
        if (mNativeDevice == nullptr || mGraphicsQueue == nullptr)
        {
            return;
        }

        ComPtr<ID3D12Fence> fence;
        HRESULT result = mNativeDevice->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(fence.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            HApp::ZWConsoleLogger::Warning("CreateFence for present wait failed: {}", HrToString(result));
            return;
        }

        HANDLE fenceEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (fenceEvent == nullptr)
        {
            HApp::ZWConsoleLogger::Warning("CreateEvent for present wait failed.");
            return;
        }

        constexpr UINT64 fenceValue = 1;
        result = mGraphicsQueue->Signal(fence.Get(), fenceValue);
        if (FAILED(result))
        {
            HApp::ZWConsoleLogger::Warning("Queue signal for present wait failed: {}", HrToString(result));
            ::CloseHandle(fenceEvent);
            return;
        }

        if (fence->GetCompletedValue() < fenceValue)
        {
            result = fence->SetEventOnCompletion(fenceValue, fenceEvent);
            if (FAILED(result))
            {
                HApp::ZWConsoleLogger::Warning("SetEventOnCompletion for present wait failed: {}", HrToString(result));
                ::CloseHandle(fenceEvent);
                return;
            }

            ::WaitForSingleObject(fenceEvent, INFINITE);
        }

        ::CloseHandle(fenceEvent);
    }

    void DestroyRenderTargets()
    {
        for (SwapChainRenderTarget& renderTarget : mRenderTargets)
        {
            renderTarget.Reset();
        }
    }

private:
    HApp::ZWWindowManager mWindowManager;
    HApp::ZWWindow* mWindow{ nullptr };
    BackEndMessageCallback mBackEndMessageCallback;

    ComPtr<IDXGIFactory4> mFactory;
    ComPtr<IDXGIAdapter1> mAdapter;
    ComPtr<ID3D12Device> mNativeDevice;
    ComPtr<ID3D12CommandQueue> mGraphicsQueue;
    ComPtr<IDXGISwapChain3> mSwapChain;

    HRHI::ZWDeviceHandle mDevice;
    HRHI::ZWCommandListHandle mCommandList;
    HRHI::ZWShaderHandle mVertexShader;
    HRHI::ZWShaderHandle mPixelShader;
    HRHI::ZWGraphicsPipelineHandle mPipeline;

    std::array<SwapChainRenderTarget, kSwapChainBufferCount> mRenderTargets{};

    UINT mFramebufferWidth{ 0 };
    UINT mFramebufferHeight{ 0 };
    bool mUsingWarpAdapter{ false };
};

}

int main(int argc, char** argv)
{
    HApp::ZWConsoleLogger::Initialize();
    HApp::ZWConsoleLogger::PrintBanner("HIBIKASE D3D12 TRIANGLE TEST");

    const int selfTestDurationMs = ParseSelfTestDurationMs(argc, argv);
    TriangleTestApplication application;
    const bool initialized = application.Initialize();
    const int result = initialized ? application.Run(selfTestDurationMs) : 1;

    application.Shutdown();
    HApp::ZWConsoleLogger::Shutdown();
    return result;
}
