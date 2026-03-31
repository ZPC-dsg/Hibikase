#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#ifdef CreateWindow
#undef CreateWindow
#endif

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace HApp
{

class ZWWindow;

enum class ZWGraphicsBackend
{
    None,
    D3D12,
    Vulkan
};

enum class ZWWindowRole
{
    Main,
    Auxiliary
};

enum class ZWInputBindingType
{
    Key,
    MouseButton
};

struct ZWWindowDesc
{
    int width{ 1280 };
    int height{ 720 };
    std::string title{ "Hibikase" };
    bool visible{ true };
    bool resizable{ true };
    bool maximized{ false };
    bool decorated{ true };
    bool focused{ true };
    bool startFullscreen{ false };
    ZWGraphicsBackend graphicsBackend{ ZWGraphicsBackend::None };
    ZWWindowRole role{ ZWWindowRole::Main };
};

struct ZWSize
{
    int width{ 0 };
    int height{ 0 };
};

struct ZWDpiScale
{
    float x{ 1.0f };
    float y{ 1.0f };
};

struct ZWWindowFrameState
{
    ZWSize windowSize;
    ZWSize framebufferSize;
    ZWDpiScale dpiScale;
    bool focused{ false };
    bool iconified{ false };
    bool maximized{ false };
    bool fullscreen{ false };
};

struct ZWKeyEvent
{
    int key{ -1 };
    int scanCode{ 0 };
    int action{ GLFW_RELEASE };
    int mods{ 0 };
};

struct ZWCursorEvent
{
    double x{ 0.0 };
    double y{ 0.0 };
    double deltaX{ 0.0 };
    double deltaY{ 0.0 };
};

struct ZWMouseButtonEvent
{
    int button{ -1 };
    int action{ GLFW_RELEASE };
    int mods{ 0 };
};

struct ZWScrollEvent
{
    double offsetX{ 0.0 };
    double offsetY{ 0.0 };
};

struct ZWResizeEvent
{
    int width{ 0 };
    int height{ 0 };
    int framebufferWidth{ 0 };
    int framebufferHeight{ 0 };
};

struct ZWFocusEvent
{
    bool focused{ false };
};

struct ZWDpiEvent
{
    float scaleX{ 1.0f };
    float scaleY{ 1.0f };
};

struct ZWInputBinding
{
    ZWInputBindingType bindingType{ ZWInputBindingType::Key };
    int code{ -1 };

    bool operator==(const ZWInputBinding& other) const
    {
        return bindingType == other.bindingType && code == other.code;
    }
};

struct ZWImGuiD3D12InitInfo
{
    ID3D12Device* device{ nullptr };
    ID3D12CommandQueue* commandQueue{ nullptr };
    ID3D12DescriptorHeap* descriptorHeap{ nullptr };
    std::uint32_t frameCount{ 2 };
    DXGI_FORMAT renderTargetFormat{ DXGI_FORMAT_R8G8B8A8_UNORM };
    DXGI_FORMAT depthStencilFormat{ DXGI_FORMAT_UNKNOWN };
    void* userData{ nullptr };
    void (*srvDescriptorAllocFn)(ZWImGuiD3D12InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuDescriptorHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuDescriptorHandle){ nullptr };
    void (*srvDescriptorFreeFn)(ZWImGuiD3D12InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle){ nullptr };
    D3D12_CPU_DESCRIPTOR_HANDLE legacySingleSrvCpuDescriptor{};
    D3D12_GPU_DESCRIPTOR_HANDLE legacySingleSrvGpuDescriptor{};
};

struct ZWImGuiVulkanInitInfo
{
    VkInstance instance{ VK_NULL_HANDLE };
    VkPhysicalDevice physicalDevice{ VK_NULL_HANDLE };
    VkDevice device{ VK_NULL_HANDLE };
    VkQueue queue{ VK_NULL_HANDLE };
    VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
    VkRenderPass renderPass{ VK_NULL_HANDLE };
    std::uint32_t queueFamilyIndex{ 0 };
    std::uint32_t minImageCount{ 2 };
    std::uint32_t imageCount{ 2 };
    const VkAllocationCallbacks* allocator{ nullptr };
    void (*checkVkResultFn)(VkResult result){ nullptr };
};

struct ZWImGuiDebugState
{
    float frameTimeMs{ 0.0f };
    float framesPerSecond{ 0.0f };
    float fieldOfView{ 60.0f };
    float yaw{ 0.0f };
    float pitch{ 0.0f };
    float dpiScaleX{ 1.0f };
    float dpiScaleY{ 1.0f };
    float cameraPositionX{ 0.0f };
    float cameraPositionY{ 0.0f };
    float cameraPositionZ{ 0.0f };
    std::string activeBackend{ "None" };
};

using ZWKeyEventHandler = std::function<void(ZWWindow&, const ZWKeyEvent&)>;
using ZWCursorEventHandler = std::function<void(ZWWindow&, const ZWCursorEvent&)>;
using ZWMouseButtonEventHandler = std::function<void(ZWWindow&, const ZWMouseButtonEvent&)>;
using ZWScrollEventHandler = std::function<void(ZWWindow&, const ZWScrollEvent&)>;
using ZWResizeEventHandler = std::function<void(ZWWindow&, const ZWResizeEvent&)>;
using ZWFocusEventHandler = std::function<void(ZWWindow&, const ZWFocusEvent&)>;
using ZWDpiEventHandler = std::function<void(ZWWindow&, const ZWDpiEvent&)>;
using ZWImGuiDrawCallback = std::function<void()>;

}
