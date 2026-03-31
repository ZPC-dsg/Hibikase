#include <Window\windowmanager.h>

#include <algorithm>

namespace
{

std::string gLastGlfwError;

}

namespace HApp
{

ZWWindowManager::ZWWindowManager() = default;

ZWWindowManager::~ZWWindowManager()
{
    Shutdown();
}

bool ZWWindowManager::Initialize()
{
    if (mInitialized)
    {
        return true;
    }

    glfwSetErrorCallback(ErrorCallback);
    if (glfwInit() != GLFW_TRUE)
    {
        mLastError = gLastGlfwError.empty() ? "GLFW initialization failed." : gLastGlfwError;
        return false;
    }

    mInitialized = true;
    mVulkanSupported = glfwVulkanSupported() == GLFW_TRUE;
    return true;
}

void ZWWindowManager::Shutdown()
{
    if (!mInitialized)
    {
        return;
    }

    mWindows.clear();
    glfwTerminate();
    mInitialized = false;
}

ZWWindow* ZWWindowManager::CreateWindow(const ZWWindowDesc& windowDesc)
{
    if (!Initialize())
    {
        return nullptr;
    }

    std::unique_ptr<ZWWindow> window = std::make_unique<ZWWindow>(mNextWindowId++, windowDesc);
    if (!window->Initialize())
    {
        mLastError = gLastGlfwError.empty() ? "Window creation failed." : gLastGlfwError;
        return nullptr;
    }

    ZWWindow* windowPtr = window.get();
    mWindows.push_back(std::move(window));
    return windowPtr;
}

void ZWWindowManager::DestroyWindow(std::uint32_t windowId)
{
    std::erase_if(mWindows, [windowId](const std::unique_ptr<ZWWindow>& window)
    {
        return window != nullptr && window->GetWindowId() == windowId;
    });
}

void ZWWindowManager::DestroyClosedWindows()
{
    std::erase_if(mWindows, [](const std::unique_ptr<ZWWindow>& window)
    {
        return window != nullptr && window->ShouldClose();
    });
}

void ZWWindowManager::PollEvents() const
{
    if (mInitialized)
    {
        glfwPollEvents();
    }
}

bool ZWWindowManager::HasOpenWindows() const
{
    return std::any_of(mWindows.begin(), mWindows.end(), [](const std::unique_ptr<ZWWindow>& window)
    {
        return window != nullptr && !window->ShouldClose();
    });
}

bool ZWWindowManager::IsVulkanSupported() const
{
    return mVulkanSupported;
}

std::size_t ZWWindowManager::GetWindowCount() const
{
    return mWindows.size();
}

const std::vector<std::unique_ptr<ZWWindow>>& ZWWindowManager::GetWindows() const
{
    return mWindows;
}

ZWWindow* ZWWindowManager::GetPrimaryWindow() const
{
    for (const std::unique_ptr<ZWWindow>& window : mWindows)
    {
        if (window != nullptr && window->IsPrimaryWindow())
        {
            return window.get();
        }
    }

    return nullptr;
}

ZWWindow* ZWWindowManager::FindWindowById(std::uint32_t windowId) const
{
    for (const std::unique_ptr<ZWWindow>& window : mWindows)
    {
        if (window != nullptr && window->GetWindowId() == windowId)
        {
            return window.get();
        }
    }

    return nullptr;
}

std::vector<const char*> ZWWindowManager::GetRequiredVulkanExtensions() const
{
    if (!mInitialized)
    {
        return {};
    }

    std::uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (extensions == nullptr || extensionCount == 0)
    {
        return {};
    }

    return std::vector<const char*>(extensions, extensions + extensionCount);
}

const std::string& ZWWindowManager::GetLastError() const
{
    return mLastError;
}

void ZWWindowManager::ErrorCallback(int errorCode, const char* description)
{
    gLastGlfwError = "GLFW error " + std::to_string(errorCode) + ": " + (description != nullptr ? description : "Unknown error");
}

}
