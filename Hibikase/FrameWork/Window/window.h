#pragma once

#include <Window\windowtypes.h>

#include <memory>
#include <string>
#include <vector>

namespace HApp
{

class ZWInputSystem;

class ZWWindow final
{
public:
    ZWWindow(std::uint32_t windowId, const ZWWindowDesc& windowDesc);
    ~ZWWindow();

    ZWWindow(const ZWWindow&) = delete;
    ZWWindow& operator=(const ZWWindow&) = delete;
    ZWWindow(ZWWindow&&) = delete;
    ZWWindow& operator=(ZWWindow&&) = delete;

    bool Initialize();
    void Shutdown();

    std::uint32_t GetWindowId() const;
    ZWWindowRole GetRole() const;
    bool IsPrimaryWindow() const;
    ZWGraphicsBackend GetGraphicsBackend() const;
    const std::string& GetTitle() const;

    bool ShouldClose() const;
    void RequestClose();
    void Focus();
    void Maximize();
    void Minimize();
    void Restore();
    void ToggleFullscreen();

    void SetTitle(const std::string& title);
    void SetSize(int width, int height);

    ZWSize GetWindowSize() const;
    ZWSize GetFramebufferSize() const;
    ZWDpiScale GetDpiScale() const;
    ZWWindowFrameState GetFrameState() const;

    bool IsFocused() const;
    bool IsIconified() const;
    bool IsMaximized() const;
    bool IsFullscreen() const;

    GLFWwindow* GetNativeWindow() const;
    HWND GetNativeHandle() const;
    bool CreateVulkanSurface(VkInstance instance, VkSurfaceKHR* outSurface, const VkAllocationCallbacks* allocator = nullptr) const;

    void SetRenderContext(void* renderContext);
    void* GetRenderContext() const;

    void SetInputSystem(ZWInputSystem* inputSystem);
    ZWInputSystem* GetInputSystem() const;

    void AddKeyEventHandler(ZWKeyEventHandler handler);
    void AddCursorEventHandler(ZWCursorEventHandler handler);
    void AddMouseButtonEventHandler(ZWMouseButtonEventHandler handler);
    void AddScrollEventHandler(ZWScrollEventHandler handler);
    void AddResizeEventHandler(ZWResizeEventHandler handler);
    void AddFocusEventHandler(ZWFocusEventHandler handler);
    void AddDpiEventHandler(ZWDpiEventHandler handler);

private:
    void RegisterCallbacks();
    void RefreshFrameState();
    void CacheWindowedPlacement();

    void HandleKeyEvent(const ZWKeyEvent& keyEvent);
    void HandleCursorEvent(double x, double y);
    void HandleMouseButtonEvent(const ZWMouseButtonEvent& mouseButtonEvent);
    void HandleScrollEvent(const ZWScrollEvent& scrollEvent);
    void HandleResizeEvent();
    void HandleFocusEvent(bool focused);
    void HandleDpiEvent(float scaleX, float scaleY);
    void HandleIconifyEvent(bool iconified);
    void HandleMaximizeEvent(bool maximized);

    static ZWWindow* FromNativeWindow(GLFWwindow* nativeWindow);
    static void KeyCallback(GLFWwindow* nativeWindow, int key, int scanCode, int action, int mods);
    static void CursorPositionCallback(GLFWwindow* nativeWindow, double x, double y);
    static void MouseButtonCallback(GLFWwindow* nativeWindow, int button, int action, int mods);
    static void ScrollCallback(GLFWwindow* nativeWindow, double offsetX, double offsetY);
    static void WindowSizeCallback(GLFWwindow* nativeWindow, int width, int height);
    static void FramebufferSizeCallback(GLFWwindow* nativeWindow, int width, int height);
    static void FocusCallback(GLFWwindow* nativeWindow, int focused);
    static void ContentScaleCallback(GLFWwindow* nativeWindow, float scaleX, float scaleY);
    static void IconifyCallback(GLFWwindow* nativeWindow, int iconified);
    static void MaximizeCallback(GLFWwindow* nativeWindow, int maximized);

private:
    std::uint32_t mWindowId{ 0 };
    ZWWindowDesc mWindowDesc;
    std::string mTitle;
    GLFWwindow* mWindow{ nullptr };
    void* mRenderContext{ nullptr };
    ZWInputSystem* mInputSystem{ nullptr };
    ZWWindowFrameState mFrameState;
    bool mHasCursorSample{ false };
    double mCursorX{ 0.0 };
    double mCursorY{ 0.0 };
    int mWindowedPosX{ 100 };
    int mWindowedPosY{ 100 };
    int mWindowedWidth{ 1280 };
    int mWindowedHeight{ 720 };
    std::vector<ZWKeyEventHandler> mKeyEventHandlers;
    std::vector<ZWCursorEventHandler> mCursorEventHandlers;
    std::vector<ZWMouseButtonEventHandler> mMouseButtonEventHandlers;
    std::vector<ZWScrollEventHandler> mScrollEventHandlers;
    std::vector<ZWResizeEventHandler> mResizeEventHandlers;
    std::vector<ZWFocusEventHandler> mFocusEventHandlers;
    std::vector<ZWDpiEventHandler> mDpiEventHandlers;
};

}
