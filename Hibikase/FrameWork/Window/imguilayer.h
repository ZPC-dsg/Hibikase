#pragma once

#include <Window\windowtypes.h>

#include <string>

struct ImDrawData;
struct ImGuiContext;

namespace HApp
{

class ZWWindow;

class ZWImGuiLayer final
{
public:
    bool InitializeForD3D12(ZWWindow& window, const ZWImGuiD3D12InitInfo& initInfo);
    bool InitializeForVulkan(ZWWindow& window, const ZWImGuiVulkanInitInfo& initInfo);
    void Shutdown();

    void BeginFrame();
    void EndFrame();
    void RenderForD3D12(ID3D12GraphicsCommandList& graphicsCommandList);
    void RenderForVulkan(VkCommandBuffer commandBuffer, VkPipeline pipeline = VK_NULL_HANDLE);

    bool IsInitialized() const;
    bool IsNativeIntegrationAvailable() const;
    bool IsRendererBackendInitialized() const;
    ZWGraphicsBackend GetBackendType() const;
    const std::string& GetStatusMessage() const;

    void SetDebugState(const ZWImGuiDebugState& debugState);
    const ZWImGuiDebugState& GetDebugState() const;
    void SetDrawCallback(ZWImGuiDrawCallback drawCallback);

private:
    bool InitializePlatformBackend(ZWWindow& window, ZWGraphicsBackend backendType);
    void BuildDefaultDebugWindow();
    void SetContext() const;
    void SetStatusMessage(const std::string& statusMessage);

private:
    ZWWindow* mWindow{ nullptr };
    ImGuiContext* mContext{ nullptr };
    ZWGraphicsBackend mBackendType{ ZWGraphicsBackend::None };
    bool mInitialized{ false };
    bool mNativeIntegrationAvailable{ false };
    bool mRendererBackendInitialized{ false };
    bool mFrameActive{ false };
    std::string mStatusMessage;
    ZWImGuiDebugState mDebugState;
    ZWImGuiDrawCallback mDrawCallback;
};

}
