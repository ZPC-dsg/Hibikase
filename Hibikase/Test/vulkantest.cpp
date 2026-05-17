#include <BackEnd/RHIconstants.h>
#include <BackEnd/RHIinterface.h>
#include <BackEnd/vulkanbackend.h>
#include <BackEnd/vulkanunique.h>
#include <Window/window.h>
#include <Window/windowmanager.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "testcommon.h"

namespace
{
constexpr uint32_t kPreferredSwapChainBufferCount = 2;

struct QueueFamilySelection
{
    uint32_t graphicsAndPresentFamily = std::numeric_limits<uint32_t>::max();
    [[nodiscard]] bool IsComplete() const { return graphicsAndPresentFamily != std::numeric_limits<uint32_t>::max(); }
};

struct PhysicalDeviceSelection
{
    VkPhysicalDevice physicalDevice{ VK_NULL_HANDLE };
    QueueFamilySelection queueFamilies;
    std::string deviceName;
    int score{ -1 };
};

struct SwapChainRenderTarget
{
    VkImage image{ VK_NULL_HANDLE };
    HRHI::ZWTextureHandle texture;
    HRHI::ZWFramebufferHandle framebuffer;
    void Reset()
    {
        framebuffer = nullptr;
        texture = nullptr;
        image = VK_NULL_HANDLE;
    }
};

std::string VkResultToString(VkResult result)
{
    std::ostringstream stream;
    stream << HRHI::HVulkan::ResultToString(result) << " (0x"
           << std::hex << std::uppercase << static_cast<uint32_t>(result) << ")";
    return stream.str();
}

bool CheckVkResult(VkResult result, const char* operation)
{
    if (result == VK_SUCCESS)
    {
        return true;
    }

    HApp::ZWConsoleLogger::Error("{} failed: {}", operation, VkResultToString(result));
    return false;
}

bool SupportsSwapchainExtension(VkPhysicalDevice physicalDevice);
bool SupportsRequiredFeatures(VkPhysicalDevice physicalDevice);
QueueFamilySelection FindQueueFamilies(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
bool QuerySwapChainSupport(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR& capabilities,
    std::vector<VkSurfaceFormatKHR>& formats,
    std::vector<VkPresentModeKHR>& presentModes);
PhysicalDeviceSelection SelectPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
HRHI::EFormat ConvertToHrhiFormat(VkFormat format);
VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, const HApp::ZWSize framebufferSize);
VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(const VkSurfaceCapabilitiesKHR& capabilities);

class VulkanTriangleTestApplication final
{
public:
    bool Initialize();
    int Run(int selfTestDurationMs);
    void Shutdown();

private:
    bool CreateNativeDeviceAndSwapChain();
    bool CreateBackEndDevice();
    bool CreateSwapChain();
    void DestroySwapChain();
    bool CreateRenderTargets();
    bool CreatePipeline();
    bool ResizeSwapChainIfNeeded();
    bool RenderFrame();
    void WaitForGpu();
    void DestroyRenderTargets();

private:
    HApp::ZWWindowManager mWindowManager;
    HApp::ZWWindow* mWindow{ nullptr };
    HTest::BackEndMessageCallback mBackEndMessageCallback;

    VkInstance mInstance{ VK_NULL_HANDLE };
    VkSurfaceKHR mSurface{ VK_NULL_HANDLE };
    VkPhysicalDevice mPhysicalDevice{ VK_NULL_HANDLE };
    VkDevice mNativeDevice{ VK_NULL_HANDLE };
    VkQueue mGraphicsQueue{ VK_NULL_HANDLE };
    VkFence mAcquireFence{ VK_NULL_HANDLE };
    VkSwapchainKHR mSwapChain{ VK_NULL_HANDLE };

    std::vector<const char*> mEnabledInstanceExtensions;
    std::vector<const char*> mEnabledDeviceExtensions;

    HRHI::HVulkan::ZWDeviceHandle mDevice;
    HRHI::ZWCommandListHandle mCommandList;
    HRHI::ZWShaderHandle mVertexShader;
    HRHI::ZWShaderHandle mPixelShader;
    HRHI::ZWGraphicsPipelineHandle mPipeline;

    std::vector<SwapChainRenderTarget> mRenderTargets;

    std::string mPhysicalDeviceName;
    VkFormat mSwapChainFormat{ VK_FORMAT_UNDEFINED };
    HRHI::EFormat mSwapChainHrhiFormat{ HRHI::EFormat::UNKNOWN };
    uint32_t mGraphicsQueueFamilyIndex{ 0 };
    uint32_t mFramebufferWidth{ 0 };
    uint32_t mFramebufferHeight{ 0 };
};

bool SupportsSwapchainExtension(VkPhysicalDevice physicalDevice)
{
    uint32_t extensionCount = 0;
    VkResult result = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    if (result != VK_SUCCESS)
    {
        return false;
    }

    std::vector<VkExtensionProperties> extensions(extensionCount);
    result = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data());
    if (result != VK_SUCCESS)
    {
        return false;
    }

    for (const VkExtensionProperties& extension : extensions)
    {
        if (std::string(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        {
            return true;
        }
    }

    return false;
}

bool SupportsRequiredFeatures(VkPhysicalDevice physicalDevice)
{
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.pNext = &features13;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features12;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
    return features12.timelineSemaphore == VK_TRUE &&
        features13.dynamicRendering == VK_TRUE &&
        features13.synchronization2 == VK_TRUE;
}

QueueFamilySelection FindQueueFamilies(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    QueueFamilySelection selection;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    if (queueFamilyCount == 0)
    {
        return selection;
    }

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount; ++queueFamilyIndex)
    {
        const VkQueueFamilyProperties& queueFamily = queueFamilies[queueFamilyIndex];
        if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 || queueFamily.queueCount == 0)
        {
            continue;
        }

        VkBool32 presentSupported = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, &presentSupported) == VK_SUCCESS &&
            presentSupported == VK_TRUE)
        {
            selection.graphicsAndPresentFamily = queueFamilyIndex;
            break;
        }
    }

    return selection;
}

bool QuerySwapChainSupport(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR& capabilities,
    std::vector<VkSurfaceFormatKHR>& formats,
    std::vector<VkPresentModeKHR>& presentModes)
{
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
    if (result != VK_SUCCESS)
    {
        return false;
    }

    uint32_t formatCount = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    if (result != VK_SUCCESS || formatCount == 0)
    {
        return false;
    }

    formats.resize(formatCount);
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    if (result != VK_SUCCESS)
    {
        return false;
    }

    uint32_t presentModeCount = 0;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    if (result != VK_SUCCESS || presentModeCount == 0)
    {
        return false;
    }

    presentModes.resize(presentModeCount);
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());
    return result == VK_SUCCESS;
}

PhysicalDeviceSelection SelectPhysicalDevice(VkInstance instance, VkSurfaceKHR surface)
{
    PhysicalDeviceSelection bestSelection;

    uint32_t deviceCount = 0;
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr) != VK_SUCCESS || deviceCount == 0)
    {
        return bestSelection;
    }

    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data()) != VK_SUCCESS)
    {
        return bestSelection;
    }

    for (VkPhysicalDevice physicalDevice : physicalDevices)
    {
        if (!SupportsSwapchainExtension(physicalDevice) || !SupportsRequiredFeatures(physicalDevice))
        {
            continue;
        }

        const QueueFamilySelection queueFamilies = FindQueueFamilies(physicalDevice, surface);
        if (!queueFamilies.IsComplete())
        {
            continue;
        }

        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
        if (!QuerySwapChainSupport(physicalDevice, surface, capabilities, formats, presentModes))
        {
            continue;
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);

        int score = 50;
        switch (properties.deviceType)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            score = 300;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            score = 200;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            score = 100;
            break;
        default:
            break;
        }

        if (score > bestSelection.score)
        {
            bestSelection.physicalDevice = physicalDevice;
            bestSelection.queueFamilies = queueFamilies;
            bestSelection.deviceName = properties.deviceName;
            bestSelection.score = score;
        }
    }

    return bestSelection;
}

VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
    for (const VkSurfaceFormatKHR& format : availableFormats)
    {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    for (const VkSurfaceFormatKHR& format : availableFormats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    for (const VkSurfaceFormatKHR& format : availableFormats)
    {
        if (format.format == VK_FORMAT_R8G8B8A8_SRGB ||
            format.format == VK_FORMAT_B8G8R8A8_SRGB)
        {
            return format;
        }
    }

    return availableFormats.front();
}

HRHI::EFormat ConvertToHrhiFormat(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R8G8B8A8_UNORM:
        return HRHI::EFormat::RGBA8_UNORM;
    case VK_FORMAT_B8G8R8A8_UNORM:
        return HRHI::EFormat::BGRA8_UNORM;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return HRHI::EFormat::SRGBA8_UNORM;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return HRHI::EFormat::SBGRA8_UNORM;
    default:
        return HRHI::EFormat::UNKNOWN;
    }
}

VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, const HApp::ZWSize framebufferSize)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }

    VkExtent2D extent{};
    extent.width = std::clamp(
        static_cast<uint32_t>(std::max(1, framebufferSize.width)),
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width);
    extent.height = std::clamp(
        static_cast<uint32_t>(std::max(1, framebufferSize.height)),
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height);
    return extent;
}

VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(const VkSurfaceCapabilitiesKHR& capabilities)
{
    constexpr VkCompositeAlphaFlagBitsKHR candidates[] =
    {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
    };

    for (VkCompositeAlphaFlagBitsKHR candidate : candidates)
    {
        if ((capabilities.supportedCompositeAlpha & candidate) != 0)
        {
            return candidate;
        }
    }

    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

bool VulkanTriangleTestApplication::Initialize()
{
    HApp::ZWConsoleLogger::PrintSection("Startup");

    if (!mWindowManager.Initialize())
    {
        HApp::ZWConsoleLogger::Error("Failed to initialize window manager: {}", mWindowManager.GetLastError());
        return false;
    }

    if (!mWindowManager.IsVulkanSupported())
    {
        HApp::ZWConsoleLogger::Error("GLFW reports that Vulkan surface creation is unavailable on this machine.");
        return false;
    }

    HApp::ZWWindowDesc windowDesc;
    windowDesc.width = 1280;
    windowDesc.height = 720;
    windowDesc.title = "Hibikase Vulkan Triangle Test";
    windowDesc.graphicsBackend = HApp::ZWGraphicsBackend::Vulkan;
    windowDesc.role = HApp::ZWWindowRole::Main;

    mWindow = mWindowManager.CreateWindow(windowDesc);
    if (mWindow == nullptr)
    {
        HApp::ZWConsoleLogger::Error("Failed to create test window: {}", mWindowManager.GetLastError());
        return false;
    }

    if (!CreateNativeDeviceAndSwapChain() || !CreateBackEndDevice() || !CreateRenderTargets() || !CreatePipeline())
    {
        return false;
    }

    HApp::ZWConsoleLogger::PrintProperty("Physical device", mPhysicalDeviceName);
    HApp::ZWConsoleLogger::PrintProperty("Window title", mWindow->GetTitle());
    HApp::ZWConsoleLogger::PrintProperty(
        "Back buffer size",
        std::to_string(mFramebufferWidth) + " x " + std::to_string(mFramebufferHeight));
    HApp::ZWConsoleLogger::PrintProperty("Shader directory", (HTest::GetExecutableDirectory() / "Shaders" / "test").string());
    HApp::ZWConsoleLogger::PrintListItem("Press ESC to close the test window.");
    return true;
}

int VulkanTriangleTestApplication::Run(int selfTestDurationMs)
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

        if (!ResizeSwapChainIfNeeded() || !RenderFrame())
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

void VulkanTriangleTestApplication::Shutdown()
{
    WaitForGpu();
    DestroyRenderTargets();

    mPipeline = nullptr;
    mPixelShader = nullptr;
    mVertexShader = nullptr;
    mCommandList = nullptr;
    mDevice = nullptr;

    DestroySwapChain();

    if (mAcquireFence != VK_NULL_HANDLE && mNativeDevice != VK_NULL_HANDLE)
    {
        vkDestroyFence(mNativeDevice, mAcquireFence, nullptr);
        mAcquireFence = VK_NULL_HANDLE;
    }

    if (mNativeDevice != VK_NULL_HANDLE)
    {
        vkDestroyDevice(mNativeDevice, nullptr);
        mNativeDevice = VK_NULL_HANDLE;
    }

    if (mSurface != VK_NULL_HANDLE && mInstance != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
        mSurface = VK_NULL_HANDLE;
    }

    if (mInstance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(mInstance, nullptr);
        mInstance = VK_NULL_HANDLE;
    }

    mWindow = nullptr;
    mWindowManager.Shutdown();
}

bool VulkanTriangleTestApplication::CreateNativeDeviceAndSwapChain()
{
    mEnabledInstanceExtensions = HRHI::VulkanBackend::GetRequiredInstanceExtensions(mWindowManager);
    if (mEnabledInstanceExtensions.empty())
    {
        HApp::ZWConsoleLogger::Error("Failed to query the required Vulkan instance extensions from GLFW.");
        return false;
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "Hibikase Vulkan Triangle Test";
    applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    applicationInfo.pEngineName = "Hibikase";
    applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceCreateInfo{};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &applicationInfo;
    instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(mEnabledInstanceExtensions.size());
    instanceCreateInfo.ppEnabledExtensionNames = mEnabledInstanceExtensions.data();

    if (!CheckVkResult(vkCreateInstance(&instanceCreateInfo, nullptr, &mInstance), "vkCreateInstance"))
    {
        return false;
    }

    if (!HRHI::VulkanBackend::CreateWindowSurface(*mWindow, mInstance, &mSurface))
    {
        HApp::ZWConsoleLogger::Error("Failed to create the Vulkan window surface.");
        return false;
    }

    const PhysicalDeviceSelection selection = SelectPhysicalDevice(mInstance, mSurface);
    if (selection.physicalDevice == VK_NULL_HANDLE)
    {
        HApp::ZWConsoleLogger::Error("Failed to find a Vulkan physical device that supports graphics, presentation, and dynamic rendering.");
        return false;
    }

    mPhysicalDevice = selection.physicalDevice;
    mGraphicsQueueFamilyIndex = selection.queueFamilies.graphicsAndPresentFamily;
    mPhysicalDeviceName = selection.deviceName;

    mEnabledDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = mGraphicsQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.timelineSemaphore = VK_TRUE;
    features12.pNext = &features13;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features12;

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pNext = &features2;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(mEnabledDeviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = mEnabledDeviceExtensions.data();

    if (!CheckVkResult(vkCreateDevice(mPhysicalDevice, &deviceCreateInfo, nullptr, &mNativeDevice), "vkCreateDevice"))
    {
        return false;
    }

    vkGetDeviceQueue(mNativeDevice, mGraphicsQueueFamilyIndex, 0, &mGraphicsQueue);
    if (mGraphicsQueue == VK_NULL_HANDLE)
    {
        HApp::ZWConsoleLogger::Error("Failed to retrieve the Vulkan graphics queue.");
        return false;
    }

    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (!CheckVkResult(vkCreateFence(mNativeDevice, &fenceCreateInfo, nullptr, &mAcquireFence), "vkCreateFence"))
    {
        return false;
    }

    return CreateSwapChain();
}

bool VulkanTriangleTestApplication::CreateBackEndDevice()
{
    HRHI::HVulkan::ZWDeviceDesc deviceDesc;
    deviceDesc.errorCB = &mBackEndMessageCallback;
    deviceDesc.instance = mInstance;
    deviceDesc.physicalDevice = mPhysicalDevice;
    deviceDesc.device = mNativeDevice;
    deviceDesc.graphicsQueue = mGraphicsQueue;
    deviceDesc.graphicsQueueIndex = static_cast<int>(mGraphicsQueueFamilyIndex);
    deviceDesc.instanceExtensions = mEnabledInstanceExtensions.data();
    deviceDesc.numInstanceExtensions = mEnabledInstanceExtensions.size();
    deviceDesc.deviceExtensions = mEnabledDeviceExtensions.data();
    deviceDesc.numDeviceExtensions = mEnabledDeviceExtensions.size();

    mDevice = HRHI::HVulkan::CreateDevice(deviceDesc);
    if (!mDevice)
    {
        HApp::ZWConsoleLogger::Error("Failed to create the HRHI Vulkan device wrapper.");
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

bool VulkanTriangleTestApplication::CreateSwapChain()
{
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
    if (!QuerySwapChainSupport(mPhysicalDevice, mSurface, capabilities, formats, presentModes))
    {
        HApp::ZWConsoleLogger::Error("Failed to query Vulkan swap-chain support.");
        return false;
    }

    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
    mSwapChainHrhiFormat = ConvertToHrhiFormat(surfaceFormat.format);
    if (mSwapChainHrhiFormat == HRHI::EFormat::UNKNOWN)
    {
        HApp::ZWConsoleLogger::Error("The selected Vulkan surface format is not supported by the HRHI test path.");
        return false;
    }

    const HApp::ZWSize framebufferSize = mWindow->GetFramebufferSize();
    const VkExtent2D extent = ChooseSwapExtent(capabilities, framebufferSize);

    uint32_t imageCount = std::max(kPreferredSwapChainBufferCount, capabilities.minImageCount);
    if (capabilities.maxImageCount > 0)
    {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR swapChainCreateInfo{};
    swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapChainCreateInfo.surface = mSurface;
    swapChainCreateInfo.minImageCount = imageCount;
    swapChainCreateInfo.imageFormat = surfaceFormat.format;
    swapChainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapChainCreateInfo.imageExtent = extent;
    swapChainCreateInfo.imageArrayLayers = 1;
    swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapChainCreateInfo.preTransform = capabilities.currentTransform;
    swapChainCreateInfo.compositeAlpha = ChooseCompositeAlpha(capabilities);
    swapChainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapChainCreateInfo.clipped = VK_TRUE;
    swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

    if (!CheckVkResult(vkCreateSwapchainKHR(mNativeDevice, &swapChainCreateInfo, nullptr, &mSwapChain), "vkCreateSwapchainKHR"))
    {
        return false;
    }

    mSwapChainFormat = surfaceFormat.format;
    mFramebufferWidth = extent.width;
    mFramebufferHeight = extent.height;
    return true;
}

void VulkanTriangleTestApplication::DestroySwapChain()
{
    if (mSwapChain != VK_NULL_HANDLE && mNativeDevice != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(mNativeDevice, mSwapChain, nullptr);
        mSwapChain = VK_NULL_HANDLE;
    }
}

bool VulkanTriangleTestApplication::CreateRenderTargets()
{
    DestroyRenderTargets();

    uint32_t imageCount = 0;
    if (!CheckVkResult(vkGetSwapchainImagesKHR(mNativeDevice, mSwapChain, &imageCount, nullptr), "vkGetSwapchainImagesKHR"))
    {
        return false;
    }

    std::vector<VkImage> swapChainImages(imageCount, VK_NULL_HANDLE);
    if (!CheckVkResult(vkGetSwapchainImagesKHR(mNativeDevice, mSwapChain, &imageCount, swapChainImages.data()), "vkGetSwapchainImagesKHR"))
    {
        return false;
    }

    mRenderTargets.resize(imageCount);
    for (uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
    {
        SwapChainRenderTarget& renderTarget = mRenderTargets[imageIndex];
        renderTarget.image = swapChainImages[imageIndex];

        HRHI::ZWTextureDesc textureDesc;
        textureDesc.setWidth(mFramebufferWidth)
            .setHeight(mFramebufferHeight)
            .setFormat(mSwapChainHrhiFormat)
            .setDimension(HRHI::ETextureDimension::Texture2D)
            .setIsRenderTarget(true)
            .setInitialState(HRHI::EResourceStates::Present)
            .setKeepInitialState(true)
            .setDebugName("Vulkan Test SwapChain BackBuffer " + std::to_string(imageIndex));
        textureDesc.isShaderResource = false;

        renderTarget.texture = mDevice->CreateHandleForNativeTexture(
            HRHI::HRHIObjectTypes::gVKImage,
            HCommon::ZWObject(renderTarget.image),
            textureDesc);
        if (!renderTarget.texture)
        {
            HApp::ZWConsoleLogger::Error("Failed to wrap Vulkan swap-chain image {} as an HRHI texture.", imageIndex);
            return false;
        }

        HRHI::ZWFramebufferDesc framebufferDesc;
        framebufferDesc.addColorAttachment(renderTarget.texture.Get());
        renderTarget.framebuffer = mDevice->CreateFramebuffer(framebufferDesc);
        if (!renderTarget.framebuffer)
        {
            HApp::ZWConsoleLogger::Error("Failed to create framebuffer for Vulkan swap-chain image {}.", imageIndex);
            return false;
        }
    }

    return true;
}

bool VulkanTriangleTestApplication::CreatePipeline()
{
    const HTest::fs::path shaderDirectory = HTest::GetExecutableDirectory() / "Shaders" / "test";
    const HTest::fs::path vertexShaderPath = shaderDirectory / "triangle_vs.spv";
    const HTest::fs::path pixelShaderPath = shaderDirectory / "triangle_ps.spv";

    const std::vector<std::uint8_t> vertexShaderBinary = HTest::ReadBinaryFile(vertexShaderPath);
    if (vertexShaderBinary.empty())
    {
        HApp::ZWConsoleLogger::Error("Failed to read Vulkan vertex shader binary: {}", vertexShaderPath.string());
        return false;
    }

    const std::vector<std::uint8_t> pixelShaderBinary = HTest::ReadBinaryFile(pixelShaderPath);
    if (pixelShaderBinary.empty())
    {
        HApp::ZWConsoleLogger::Error("Failed to read Vulkan pixel shader binary: {}", pixelShaderPath.string());
        return false;
    }

    HRHI::ZWShaderDesc vertexShaderDesc;
    vertexShaderDesc.setShaderType(HRHI::EShaderType::Vertex).setDebugName("Test Triangle VS");
    mVertexShader = mDevice->CreateShader(vertexShaderDesc, vertexShaderBinary.data(), vertexShaderBinary.size());
    if (!mVertexShader)
    {
        HApp::ZWConsoleLogger::Error("Failed to create the Vulkan vertex shader.");
        return false;
    }

    HRHI::ZWShaderDesc pixelShaderDesc;
    pixelShaderDesc.setShaderType(HRHI::EShaderType::Pixel).setDebugName("Test Triangle PS");
    mPixelShader = mDevice->CreateShader(pixelShaderDesc, pixelShaderBinary.data(), pixelShaderBinary.size());
    if (!mPixelShader)
    {
        HApp::ZWConsoleLogger::Error("Failed to create the Vulkan pixel shader.");
        return false;
    }

    HRHI::ZWRenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.setCullNone().enableDepthClip();

    HRHI::ZWGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.setVertexShader(mVertexShader.Get())
        .setPixelShader(mPixelShader.Get())
        .setRenderState(renderState);

    if (mRenderTargets.empty() || !mRenderTargets.front().framebuffer)
    {
        HApp::ZWConsoleLogger::Error("No Vulkan framebuffer is available to derive pipeline compatibility info.");
        return false;
    }

    mPipeline = mDevice->CreateGraphicsPipeline(pipelineDesc, mRenderTargets.front().framebuffer->GetFramebufferInfo());
    if (!mPipeline)
    {
        HApp::ZWConsoleLogger::Error("Failed to create the Vulkan triangle graphics pipeline.");
        return false;
    }

    return true;
}

bool VulkanTriangleTestApplication::ResizeSwapChainIfNeeded()
{
    const HApp::ZWSize framebufferSize = mWindow->GetFramebufferSize();
    const uint32_t newWidth = framebufferSize.width > 0 ? static_cast<uint32_t>(framebufferSize.width) : 0u;
    const uint32_t newHeight = framebufferSize.height > 0 ? static_cast<uint32_t>(framebufferSize.height) : 0u;

    if (newWidth == 0 || newHeight == 0)
    {
        return true;
    }

    if (newWidth == mFramebufferWidth && newHeight == mFramebufferHeight)
    {
        return true;
    }

    HApp::ZWConsoleLogger::Info("Resizing Vulkan swap chain to {} x {}.", newWidth, newHeight);
    WaitForGpu();
    DestroyRenderTargets();
    DestroySwapChain();

    if (!CreateSwapChain())
    {
        return false;
    }

    return CreateRenderTargets();
}

bool VulkanTriangleTestApplication::RenderFrame()
{
    if (!CheckVkResult(vkResetFences(mNativeDevice, 1, &mAcquireFence), "vkResetFences"))
    {
        return false;
    }

    uint32_t backBufferIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        mNativeDevice,
        mSwapChain,
        std::numeric_limits<uint64_t>::max(),
        VK_NULL_HANDLE,
        mAcquireFence,
        &backBufferIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return ResizeSwapChainIfNeeded();
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        HApp::ZWConsoleLogger::Error("vkAcquireNextImageKHR failed: {}", VkResultToString(acquireResult));
        return false;
    }

    if (!CheckVkResult(
        vkWaitForFences(mNativeDevice, 1, &mAcquireFence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
        "vkWaitForFences"))
    {
        return false;
    }

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
        HApp::ZWConsoleLogger::Error("WaitForIdle failed after submitting the Vulkan triangle draw.");
        return false;
    }

    mDevice->RunGarbageCollection();

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &mSwapChain;
    presentInfo.pImageIndices = &backBufferIndex;

    const VkResult presentResult = vkQueuePresentKHR(mGraphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
    {
        return ResizeSwapChainIfNeeded();
    }

    if (presentResult != VK_SUCCESS)
    {
        HApp::ZWConsoleLogger::Error("vkQueuePresentKHR failed: {}", VkResultToString(presentResult));
        return false;
    }

    return true;
}

void VulkanTriangleTestApplication::WaitForGpu()
{
    if (!mDevice)
    {
        return;
    }

    if (!mDevice->WaitForIdle())
    {
        HApp::ZWConsoleLogger::Warning("WaitForIdle reported a failure while synchronizing the Vulkan device.");
    }

    mDevice->RunGarbageCollection();
}

void VulkanTriangleTestApplication::DestroyRenderTargets()
{
    for (SwapChainRenderTarget& renderTarget : mRenderTargets)
    {
        renderTarget.Reset();
    }

    mRenderTargets.clear();
}
}

namespace HTest
{
int RunVulkanTriangleTest(int selfTestDurationMs)
{
    VulkanTriangleTestApplication application;
    const bool initialized = application.Initialize();
    const int result = initialized ? application.Run(selfTestDurationMs) : 1;
    application.Shutdown();
    return result;
}
}
