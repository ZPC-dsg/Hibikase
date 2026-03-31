#include <vulkan/vulkan.h>

#include <Window\window.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <Window\inputsystem.h>

#include <algorithm>
#include <utility>

namespace
{

GLFWmonitor* SelectMonitorForWindow(GLFWwindow* nativeWindow)
{
    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    if (monitors == nullptr || monitorCount <= 0)
    {
        return glfwGetPrimaryMonitor();
    }

    int windowPosX = 0;
    int windowPosY = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowPos(nativeWindow, &windowPosX, &windowPosY);
    glfwGetWindowSize(nativeWindow, &windowWidth, &windowHeight);

    const int windowCenterX = windowPosX + (windowWidth / 2);
    const int windowCenterY = windowPosY + (windowHeight / 2);

    for (int monitorIndex = 0; monitorIndex < monitorCount; ++monitorIndex)
    {
        int monitorPosX = 0;
        int monitorPosY = 0;
        glfwGetMonitorPos(monitors[monitorIndex], &monitorPosX, &monitorPosY);

        const GLFWvidmode* videoMode = glfwGetVideoMode(monitors[monitorIndex]);
        if (videoMode == nullptr)
        {
            continue;
        }

        const bool insideHorizontalBounds = windowCenterX >= monitorPosX && windowCenterX < (monitorPosX + videoMode->width);
        const bool insideVerticalBounds = windowCenterY >= monitorPosY && windowCenterY < (monitorPosY + videoMode->height);
        if (insideHorizontalBounds && insideVerticalBounds)
        {
            return monitors[monitorIndex];
        }
    }

    return glfwGetPrimaryMonitor();
}

}

namespace HApp
{

ZWWindow::ZWWindow(std::uint32_t windowId, const ZWWindowDesc& windowDesc)
    : mWindowId(windowId)
    , mWindowDesc(windowDesc)
    , mTitle(windowDesc.title)
    , mWindowedWidth(std::max(1, windowDesc.width))
    , mWindowedHeight(std::max(1, windowDesc.height))
{
}

ZWWindow::~ZWWindow()
{
    Shutdown();
}

bool ZWWindow::Initialize()
{
    if (mWindow != nullptr)
    {
        return true;
    }

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, mWindowDesc.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, mWindowDesc.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, mWindowDesc.maximized ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, mWindowDesc.decorated ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, mWindowDesc.focused ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    const int initialWidth = std::max(1, mWindowDesc.width);
    const int initialHeight = std::max(1, mWindowDesc.height);
    mWindow = glfwCreateWindow(initialWidth, initialHeight, mTitle.c_str(), nullptr, nullptr);
    if (mWindow == nullptr)
    {
        return false;
    }

    glfwSetWindowUserPointer(mWindow, this);
    RegisterCallbacks();
    RefreshFrameState();
    CacheWindowedPlacement();

    if (mWindowDesc.startFullscreen)
    {
        ToggleFullscreen();
    }

    return true;
}

void ZWWindow::Shutdown()
{
    if (mWindow == nullptr)
    {
        return;
    }

    glfwDestroyWindow(mWindow);
    mWindow = nullptr;
    mRenderContext = nullptr;
    mInputSystem = nullptr;
}

std::uint32_t ZWWindow::GetWindowId() const
{
    return mWindowId;
}

ZWWindowRole ZWWindow::GetRole() const
{
    return mWindowDesc.role;
}

bool ZWWindow::IsPrimaryWindow() const
{
    return mWindowDesc.role == ZWWindowRole::Main;
}

ZWGraphicsBackend ZWWindow::GetGraphicsBackend() const
{
    return mWindowDesc.graphicsBackend;
}

const std::string& ZWWindow::GetTitle() const
{
    return mTitle;
}

bool ZWWindow::ShouldClose() const
{
    return mWindow != nullptr ? glfwWindowShouldClose(mWindow) == GLFW_TRUE : true;
}

void ZWWindow::RequestClose()
{
    if (mWindow != nullptr)
    {
        glfwSetWindowShouldClose(mWindow, GLFW_TRUE);
    }
}

void ZWWindow::Focus()
{
    if (mWindow != nullptr)
    {
        glfwFocusWindow(mWindow);
    }
}

void ZWWindow::Maximize()
{
    if (mWindow != nullptr)
    {
        glfwMaximizeWindow(mWindow);
    }
}

void ZWWindow::Minimize()
{
    if (mWindow != nullptr)
    {
        glfwIconifyWindow(mWindow);
    }
}

void ZWWindow::Restore()
{
    if (mWindow != nullptr)
    {
        glfwRestoreWindow(mWindow);
    }
}

void ZWWindow::ToggleFullscreen()
{
    if (mWindow == nullptr)
    {
        return;
    }

    if (IsFullscreen())
    {
        glfwSetWindowMonitor(mWindow, nullptr, mWindowedPosX, mWindowedPosY, mWindowedWidth, mWindowedHeight, 0);
        RefreshFrameState();
        return;
    }

    CacheWindowedPlacement();
    GLFWmonitor* monitor = SelectMonitorForWindow(mWindow);
    if (monitor == nullptr)
    {
        monitor = glfwGetPrimaryMonitor();
    }

    const GLFWvidmode* videoMode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
    if (monitor == nullptr || videoMode == nullptr)
    {
        return;
    }

    glfwSetWindowMonitor(mWindow, monitor, 0, 0, videoMode->width, videoMode->height, videoMode->refreshRate);
    RefreshFrameState();
}

void ZWWindow::SetTitle(const std::string& title)
{
    mTitle = title;
    if (mWindow != nullptr)
    {
        glfwSetWindowTitle(mWindow, mTitle.c_str());
    }
}

void ZWWindow::SetSize(int width, int height)
{
    if (mWindow == nullptr)
    {
        return;
    }

    glfwSetWindowSize(mWindow, std::max(1, width), std::max(1, height));
}

ZWSize ZWWindow::GetWindowSize() const
{
    return mFrameState.windowSize;
}

ZWSize ZWWindow::GetFramebufferSize() const
{
    return mFrameState.framebufferSize;
}

ZWDpiScale ZWWindow::GetDpiScale() const
{
    return mFrameState.dpiScale;
}

ZWWindowFrameState ZWWindow::GetFrameState() const
{
    return mFrameState;
}

bool ZWWindow::IsFocused() const
{
    return mFrameState.focused;
}

bool ZWWindow::IsIconified() const
{
    return mFrameState.iconified;
}

bool ZWWindow::IsMaximized() const
{
    return mFrameState.maximized;
}

bool ZWWindow::IsFullscreen() const
{
    return mFrameState.fullscreen;
}

GLFWwindow* ZWWindow::GetNativeWindow() const
{
    return mWindow;
}

HWND ZWWindow::GetNativeHandle() const
{
    return mWindow != nullptr ? glfwGetWin32Window(mWindow) : nullptr;
}

bool ZWWindow::CreateVulkanSurface(VkInstance instance, VkSurfaceKHR* outSurface, const VkAllocationCallbacks* allocator) const
{
    if (mWindow == nullptr || instance == nullptr || outSurface == nullptr)
    {
        return false;
    }

    return glfwCreateWindowSurface(instance, mWindow, allocator, outSurface) == VK_SUCCESS;
}

void ZWWindow::SetRenderContext(void* renderContext)
{
    mRenderContext = renderContext;
}

void* ZWWindow::GetRenderContext() const
{
    return mRenderContext;
}

void ZWWindow::SetInputSystem(ZWInputSystem* inputSystem)
{
    mInputSystem = inputSystem;
}

ZWInputSystem* ZWWindow::GetInputSystem() const
{
    return mInputSystem;
}

void ZWWindow::AddKeyEventHandler(ZWKeyEventHandler handler)
{
    mKeyEventHandlers.push_back(std::move(handler));
}

void ZWWindow::AddCursorEventHandler(ZWCursorEventHandler handler)
{
    mCursorEventHandlers.push_back(std::move(handler));
}

void ZWWindow::AddMouseButtonEventHandler(ZWMouseButtonEventHandler handler)
{
    mMouseButtonEventHandlers.push_back(std::move(handler));
}

void ZWWindow::AddScrollEventHandler(ZWScrollEventHandler handler)
{
    mScrollEventHandlers.push_back(std::move(handler));
}

void ZWWindow::AddResizeEventHandler(ZWResizeEventHandler handler)
{
    mResizeEventHandlers.push_back(std::move(handler));
}

void ZWWindow::AddFocusEventHandler(ZWFocusEventHandler handler)
{
    mFocusEventHandlers.push_back(std::move(handler));
}

void ZWWindow::AddDpiEventHandler(ZWDpiEventHandler handler)
{
    mDpiEventHandlers.push_back(std::move(handler));
}

void ZWWindow::RegisterCallbacks()
{
    glfwSetKeyCallback(mWindow, KeyCallback);
    glfwSetCursorPosCallback(mWindow, CursorPositionCallback);
    glfwSetMouseButtonCallback(mWindow, MouseButtonCallback);
    glfwSetScrollCallback(mWindow, ScrollCallback);
    glfwSetWindowSizeCallback(mWindow, WindowSizeCallback);
    glfwSetFramebufferSizeCallback(mWindow, FramebufferSizeCallback);
    glfwSetWindowFocusCallback(mWindow, FocusCallback);
    glfwSetWindowContentScaleCallback(mWindow, ContentScaleCallback);
    glfwSetWindowIconifyCallback(mWindow, IconifyCallback);
    glfwSetWindowMaximizeCallback(mWindow, MaximizeCallback);
}

void ZWWindow::RefreshFrameState()
{
    if (mWindow == nullptr)
    {
        return;
    }

    glfwGetWindowSize(mWindow, &mFrameState.windowSize.width, &mFrameState.windowSize.height);
    glfwGetFramebufferSize(mWindow, &mFrameState.framebufferSize.width, &mFrameState.framebufferSize.height);
    glfwGetWindowContentScale(mWindow, &mFrameState.dpiScale.x, &mFrameState.dpiScale.y);
    mFrameState.focused = glfwGetWindowAttrib(mWindow, GLFW_FOCUSED) == GLFW_TRUE;
    mFrameState.iconified = glfwGetWindowAttrib(mWindow, GLFW_ICONIFIED) == GLFW_TRUE;
    mFrameState.maximized = glfwGetWindowAttrib(mWindow, GLFW_MAXIMIZED) == GLFW_TRUE;
    mFrameState.fullscreen = glfwGetWindowMonitor(mWindow) != nullptr;
}

void ZWWindow::CacheWindowedPlacement()
{
    if (mWindow == nullptr || IsFullscreen())
    {
        return;
    }

    glfwGetWindowPos(mWindow, &mWindowedPosX, &mWindowedPosY);
    glfwGetWindowSize(mWindow, &mWindowedWidth, &mWindowedHeight);
}

void ZWWindow::HandleKeyEvent(const ZWKeyEvent& keyEvent)
{
    if (mInputSystem != nullptr)
    {
        mInputSystem->HandleKeyEvent(keyEvent);
    }

    for (const ZWKeyEventHandler& handler : mKeyEventHandlers)
    {
        handler(*this, keyEvent);
    }
}

void ZWWindow::HandleCursorEvent(double x, double y)
{
    ZWCursorEvent cursorEvent;
    cursorEvent.x = x;
    cursorEvent.y = y;
    cursorEvent.deltaX = mHasCursorSample ? (x - mCursorX) : 0.0;
    cursorEvent.deltaY = mHasCursorSample ? (y - mCursorY) : 0.0;

    mCursorX = x;
    mCursorY = y;
    mHasCursorSample = true;

    if (mInputSystem != nullptr)
    {
        mInputSystem->HandleCursorEvent(cursorEvent);
    }

    for (const ZWCursorEventHandler& handler : mCursorEventHandlers)
    {
        handler(*this, cursorEvent);
    }
}

void ZWWindow::HandleMouseButtonEvent(const ZWMouseButtonEvent& mouseButtonEvent)
{
    if (mInputSystem != nullptr)
    {
        mInputSystem->HandleMouseButtonEvent(mouseButtonEvent);
    }

    for (const ZWMouseButtonEventHandler& handler : mMouseButtonEventHandlers)
    {
        handler(*this, mouseButtonEvent);
    }
}

void ZWWindow::HandleScrollEvent(const ZWScrollEvent& scrollEvent)
{
    if (mInputSystem != nullptr)
    {
        mInputSystem->HandleScrollEvent(scrollEvent);
    }

    for (const ZWScrollEventHandler& handler : mScrollEventHandlers)
    {
        handler(*this, scrollEvent);
    }
}

void ZWWindow::HandleResizeEvent()
{
    RefreshFrameState();

    ZWResizeEvent resizeEvent;
    resizeEvent.width = mFrameState.windowSize.width;
    resizeEvent.height = mFrameState.windowSize.height;
    resizeEvent.framebufferWidth = mFrameState.framebufferSize.width;
    resizeEvent.framebufferHeight = mFrameState.framebufferSize.height;

    for (const ZWResizeEventHandler& handler : mResizeEventHandlers)
    {
        handler(*this, resizeEvent);
    }
}

void ZWWindow::HandleFocusEvent(bool focused)
{
    mFrameState.focused = focused;

    if (mInputSystem != nullptr)
    {
        mInputSystem->HandleFocusEvent(ZWFocusEvent{ focused });
    }

    for (const ZWFocusEventHandler& handler : mFocusEventHandlers)
    {
        handler(*this, ZWFocusEvent{ focused });
    }
}

void ZWWindow::HandleDpiEvent(float scaleX, float scaleY)
{
    mFrameState.dpiScale = ZWDpiScale{ scaleX, scaleY };

    for (const ZWDpiEventHandler& handler : mDpiEventHandlers)
    {
        handler(*this, ZWDpiEvent{ scaleX, scaleY });
    }
}

void ZWWindow::HandleIconifyEvent(bool iconified)
{
    mFrameState.iconified = iconified;
}

void ZWWindow::HandleMaximizeEvent(bool maximized)
{
    mFrameState.maximized = maximized;
    if (!maximized)
    {
        CacheWindowedPlacement();
    }
}

ZWWindow* ZWWindow::FromNativeWindow(GLFWwindow* nativeWindow)
{
    return nativeWindow != nullptr ? static_cast<ZWWindow*>(glfwGetWindowUserPointer(nativeWindow)) : nullptr;
}

void ZWWindow::KeyCallback(GLFWwindow* nativeWindow, int key, int scanCode, int action, int mods)
{
    ZWWindow* window = FromNativeWindow(nativeWindow);
    if (window != nullptr)
    {
        window->HandleKeyEvent(ZWKeyEvent{ key, scanCode, action, mods });
    }
}

void ZWWindow::CursorPositionCallback(GLFWwindow* nativeWindow, double x, double y)
{
    ZWWindow* window = FromNativeWindow(nativeWindow);
    if (window != nullptr)
    {
        window->HandleCursorEvent(x, y);
    }
}

void ZWWindow::MouseButtonCallback(GLFWwindow* nativeWindow, int button, int action, int mods)
{
    ZWWindow* window = FromNativeWindow(nativeWindow);
    if (window != nullptr)
    {
        window->HandleMouseButtonEvent(ZWMouseButtonEvent{ button, action, mods });
    }
}

void ZWWindow::ScrollCallback(GLFWwindow* nativeWindow, double offsetX, double offsetY)
{
    ZWWindow* window = FromNativeWindow(nativeWindow);
    if (window != nullptr)
    {
        window->HandleScrollEvent(ZWScrollEvent{ offsetX, offsetY });
    }
}

void ZWWindow::WindowSizeCallback(GLFWwindow* nativeWindow, int, int)
{
    ZWWindow* window = FromNativeWindow(nativeWindow);
    if (window != nullptr)
    {
        window->HandleResizeEvent();
    }
}

void ZWWindow::FramebufferSizeCallback(GLFWwindow* nativeWindow, int, int)
{
    ZWWindow* window = FromNativeWindow(nativeWindow);
    if (window != nullptr)
    {
        window->HandleResizeEvent();
    }
}

void ZWWindow::FocusCallback(GLFWwindow* nativeWindow, int focused)
{
    ZWWindow* window = FromNativeWindow(nativeWindow);
    if (window != nullptr)
    {
        window->HandleFocusEvent(focused == GLFW_TRUE);
    }
}

void ZWWindow::ContentScaleCallback(GLFWwindow* nativeWindow, float scaleX, float scaleY)
{
    ZWWindow* window = FromNativeWindow(nativeWindow);
    if (window != nullptr)
    {
        window->HandleDpiEvent(scaleX, scaleY);
    }
}

void ZWWindow::IconifyCallback(GLFWwindow* nativeWindow, int iconified)
{
    ZWWindow* window = FromNativeWindow(nativeWindow);
    if (window != nullptr)
    {
        window->HandleIconifyEvent(iconified == GLFW_TRUE);
    }
}

void ZWWindow::MaximizeCallback(GLFWwindow* nativeWindow, int maximized)
{
    ZWWindow* window = FromNativeWindow(nativeWindow);
    if (window != nullptr)
    {
        window->HandleMaximizeEvent(maximized == GLFW_TRUE);
    }
}

}
