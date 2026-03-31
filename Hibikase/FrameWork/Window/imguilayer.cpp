#include <Window\imguilayer.h>

#include <Window\window.h>

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <sstream>
#include <utility>

namespace
{

void Dx12SrvDescriptorAllocate(ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuDescriptorHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuDescriptorHandle)
{
    auto* appInitInfo = static_cast<HApp::ZWImGuiD3D12InitInfo*>(initInfo->UserData);
    if (appInitInfo != nullptr && appInitInfo->srvDescriptorAllocFn != nullptr)
    {
        appInitInfo->srvDescriptorAllocFn(appInitInfo, outCpuDescriptorHandle, outGpuDescriptorHandle);
    }
}

void Dx12SrvDescriptorFree(ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle)
{
    auto* appInitInfo = static_cast<HApp::ZWImGuiD3D12InitInfo*>(initInfo->UserData);
    if (appInitInfo != nullptr && appInitInfo->srvDescriptorFreeFn != nullptr)
    {
        appInitInfo->srvDescriptorFreeFn(appInitInfo, cpuDescriptorHandle, gpuDescriptorHandle);
    }
}

std::string BuildBackendStatus(const char* platformStatus, const char* rendererStatus)
{
    std::ostringstream statusStream;
    statusStream << platformStatus;
    if (rendererStatus != nullptr && rendererStatus[0] != '\0')
    {
        statusStream << ' ' << rendererStatus;
    }

    return statusStream.str();
}

}

namespace HApp
{

bool ZWImGuiLayer::InitializeForD3D12(ZWWindow& window, const ZWImGuiD3D12InitInfo& initInfo)
{
    if (!InitializePlatformBackend(window, ZWGraphicsBackend::D3D12))
    {
        return false;
    }

    const bool hasDeviceInfo = initInfo.device != nullptr && initInfo.commandQueue != nullptr && initInfo.descriptorHeap != nullptr;
    const bool hasAllocatorCallbacks = initInfo.srvDescriptorAllocFn != nullptr && initInfo.srvDescriptorFreeFn != nullptr;
    const bool hasLegacyDescriptor = initInfo.legacySingleSrvCpuDescriptor.ptr != 0 && initInfo.legacySingleSrvGpuDescriptor.ptr != 0;

    if (!hasDeviceInfo)
    {
        SetStatusMessage(BuildBackendStatus(
            "Dear ImGui context + GLFW platform backend initialized.",
            "D3D12 renderer backend is deferred because no D3D12 device info was supplied."));
        return true;
    }

    SetContext();

    bool rendererInitialized = false;
    if (hasAllocatorCallbacks)
    {
        ImGui_ImplDX12_InitInfo dx12InitInfo;
        dx12InitInfo.Device = initInfo.device;
        dx12InitInfo.CommandQueue = initInfo.commandQueue;
        dx12InitInfo.NumFramesInFlight = static_cast<int>(initInfo.frameCount);
        dx12InitInfo.RTVFormat = initInfo.renderTargetFormat;
        dx12InitInfo.DSVFormat = initInfo.depthStencilFormat;
        dx12InitInfo.UserData = const_cast<ZWImGuiD3D12InitInfo*>(&initInfo);
        dx12InitInfo.SrvDescriptorHeap = initInfo.descriptorHeap;
        dx12InitInfo.SrvDescriptorAllocFn = Dx12SrvDescriptorAllocate;
        dx12InitInfo.SrvDescriptorFreeFn = Dx12SrvDescriptorFree;
        rendererInitialized = ImGui_ImplDX12_Init(&dx12InitInfo);
    }
    else if (hasLegacyDescriptor)
    {
        rendererInitialized = ImGui_ImplDX12_Init(
            initInfo.device,
            static_cast<int>(initInfo.frameCount),
            initInfo.renderTargetFormat,
            initInfo.descriptorHeap,
            initInfo.legacySingleSrvCpuDescriptor,
            initInfo.legacySingleSrvGpuDescriptor);
    }

    if (!rendererInitialized)
    {
        SetStatusMessage(BuildBackendStatus(
            "Dear ImGui context + GLFW platform backend initialized.",
            "D3D12 renderer backend is deferred because descriptor allocation data was not fully supplied."));
        return true;
    }

    mRendererBackendInitialized = true;
    SetStatusMessage(BuildBackendStatus(
        "Dear ImGui context + GLFW platform backend initialized.",
        "D3D12 renderer backend initialized."));
    return true;
}

bool ZWImGuiLayer::InitializeForVulkan(ZWWindow& window, const ZWImGuiVulkanInitInfo& initInfo)
{
    if (!InitializePlatformBackend(window, ZWGraphicsBackend::Vulkan))
    {
        return false;
    }

    const bool hasRendererInfo =
        initInfo.instance != VK_NULL_HANDLE &&
        initInfo.physicalDevice != VK_NULL_HANDLE &&
        initInfo.device != VK_NULL_HANDLE &&
        initInfo.queue != VK_NULL_HANDLE &&
        initInfo.descriptorPool != VK_NULL_HANDLE &&
        initInfo.renderPass != VK_NULL_HANDLE;

    if (!hasRendererInfo)
    {
        SetStatusMessage(BuildBackendStatus(
            "Dear ImGui context + GLFW platform backend initialized.",
            "Vulkan renderer backend is deferred because the Vulkan init info is incomplete."));
        return true;
    }

    SetContext();

    ImGui_ImplVulkan_InitInfo vulkanInitInfo{};
    vulkanInitInfo.Instance = initInfo.instance;
    vulkanInitInfo.PhysicalDevice = initInfo.physicalDevice;
    vulkanInitInfo.Device = initInfo.device;
    vulkanInitInfo.QueueFamily = initInfo.queueFamilyIndex;
    vulkanInitInfo.Queue = initInfo.queue;
    vulkanInitInfo.DescriptorPool = initInfo.descriptorPool;
    vulkanInitInfo.RenderPass = initInfo.renderPass;
    vulkanInitInfo.MinImageCount = initInfo.minImageCount;
    vulkanInitInfo.ImageCount = initInfo.imageCount;
    vulkanInitInfo.Allocator = initInfo.allocator;
    vulkanInitInfo.CheckVkResultFn = initInfo.checkVkResultFn;

    if (!ImGui_ImplVulkan_Init(&vulkanInitInfo))
    {
        Shutdown();
        return false;
    }

    mRendererBackendInitialized = true;
    SetStatusMessage(BuildBackendStatus(
        "Dear ImGui context + GLFW platform backend initialized.",
        "Vulkan renderer backend initialized."));
    return true;
}

void ZWImGuiLayer::Shutdown()
{
    if (mContext != nullptr)
    {
        SetContext();

        if (mRendererBackendInitialized)
        {
            if (mBackendType == ZWGraphicsBackend::D3D12)
            {
                ImGui_ImplDX12_Shutdown();
            }
            else if (mBackendType == ZWGraphicsBackend::Vulkan)
            {
                ImGui_ImplVulkan_Shutdown();
            }
        }

        if (mNativeIntegrationAvailable)
        {
            ImGui_ImplGlfw_Shutdown();
        }

        ImGui::DestroyContext(mContext);
    }

    mWindow = nullptr;
    mContext = nullptr;
    mBackendType = ZWGraphicsBackend::None;
    mInitialized = false;
    mNativeIntegrationAvailable = false;
    mRendererBackendInitialized = false;
    mFrameActive = false;
    mDrawCallback = nullptr;
    mStatusMessage.clear();
}

void ZWImGuiLayer::BeginFrame()
{
    if (!mInitialized || mContext == nullptr)
    {
        return;
    }

    SetContext();

    ImGui_ImplGlfw_NewFrame();
    if (mRendererBackendInitialized)
    {
        if (mBackendType == ZWGraphicsBackend::D3D12)
        {
            ImGui_ImplDX12_NewFrame();
        }
        else if (mBackendType == ZWGraphicsBackend::Vulkan)
        {
            ImGui_ImplVulkan_NewFrame();
        }
    }

    ImGui::NewFrame();
    mFrameActive = true;
}

void ZWImGuiLayer::EndFrame()
{
    if (!mInitialized || !mFrameActive || mContext == nullptr)
    {
        return;
    }

    SetContext();
    BuildDefaultDebugWindow();

    if (mDrawCallback)
    {
        mDrawCallback();
    }

    ImGui::Render();
    mFrameActive = false;
}

void ZWImGuiLayer::RenderForD3D12(ID3D12GraphicsCommandList& graphicsCommandList)
{
    if (mBackendType != ZWGraphicsBackend::D3D12 || !mRendererBackendInitialized)
    {
        return;
    }

    if (mFrameActive)
    {
        EndFrame();
    }

    SetContext();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData != nullptr)
    {
        ImGui_ImplDX12_RenderDrawData(drawData, &graphicsCommandList);
    }
}

void ZWImGuiLayer::RenderForVulkan(VkCommandBuffer commandBuffer, VkPipeline pipeline)
{
    if (mBackendType != ZWGraphicsBackend::Vulkan || !mRendererBackendInitialized || commandBuffer == VK_NULL_HANDLE)
    {
        return;
    }

    if (mFrameActive)
    {
        EndFrame();
    }

    SetContext();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData != nullptr)
    {
        ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer, pipeline);
    }
}

bool ZWImGuiLayer::IsInitialized() const
{
    return mInitialized;
}

bool ZWImGuiLayer::IsNativeIntegrationAvailable() const
{
    return mNativeIntegrationAvailable;
}

bool ZWImGuiLayer::IsRendererBackendInitialized() const
{
    return mRendererBackendInitialized;
}

ZWGraphicsBackend ZWImGuiLayer::GetBackendType() const
{
    return mBackendType;
}

const std::string& ZWImGuiLayer::GetStatusMessage() const
{
    return mStatusMessage;
}

void ZWImGuiLayer::SetDebugState(const ZWImGuiDebugState& debugState)
{
    mDebugState = debugState;
}

const ZWImGuiDebugState& ZWImGuiLayer::GetDebugState() const
{
    return mDebugState;
}

void ZWImGuiLayer::SetDrawCallback(ZWImGuiDrawCallback drawCallback)
{
    mDrawCallback = std::move(drawCallback);
}

bool ZWImGuiLayer::InitializePlatformBackend(ZWWindow& window, ZWGraphicsBackend backendType)
{
    Shutdown();

    if (window.GetNativeWindow() == nullptr)
    {
        return false;
    }

    IMGUI_CHECKVERSION();
    mContext = ImGui::CreateContext();
    if (mContext == nullptr)
    {
        return false;
    }

    SetContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const ZWDpiScale dpiScale = window.GetDpiScale();
    io.DisplayFramebufferScale = ImVec2(dpiScale.x, dpiScale.y);
    // Build the default font atlas up front so debug-only mode can render
    // even before a concrete D3D12/Vulkan renderer backend is attached.
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);

    bool platformBackendInitialized = false;
    if (backendType == ZWGraphicsBackend::Vulkan)
    {
        platformBackendInitialized = ImGui_ImplGlfw_InitForVulkan(window.GetNativeWindow(), true);
    }
    else
    {
        platformBackendInitialized = ImGui_ImplGlfw_InitForOther(window.GetNativeWindow(), true);
    }

    if (!platformBackendInitialized)
    {
        Shutdown();
        return false;
    }

    ImGui_ImplGlfw_SetCallbacksChainForAllWindows(true);

    mWindow = &window;
    mBackendType = backendType;
    mInitialized = true;
    mNativeIntegrationAvailable = true;
    mRendererBackendInitialized = false;
    mFrameActive = false;
    return true;
}

void ZWImGuiLayer::BuildDefaultDebugWindow()
{
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Hibikase Debug"))
    {
        ImGui::Text("Backend: %s", mDebugState.activeBackend.c_str());
        ImGui::Text("Renderer backend: %s", mRendererBackendInitialized ? "active" : "deferred");
        ImGui::Separator();
        ImGui::Text("FPS: %.1f", mDebugState.framesPerSecond);
        ImGui::Text("Frame Time: %.2f ms", mDebugState.frameTimeMs);
        ImGui::Text("FOV: %.1f", mDebugState.fieldOfView);
        ImGui::Text("Yaw / Pitch: %.2f / %.2f", mDebugState.yaw, mDebugState.pitch);
        ImGui::Text("DPI Scale: %.2f x %.2f", mDebugState.dpiScaleX, mDebugState.dpiScaleY);
        ImGui::Text("Camera: %.2f, %.2f, %.2f",
            mDebugState.cameraPositionX,
            mDebugState.cameraPositionY,
            mDebugState.cameraPositionZ);
    }
    ImGui::End();
}

void ZWImGuiLayer::SetContext() const
{
    if (mContext != nullptr)
    {
        ImGui::SetCurrentContext(mContext);
    }
}

void ZWImGuiLayer::SetStatusMessage(const std::string& statusMessage)
{
    mStatusMessage = statusMessage;
}

}
