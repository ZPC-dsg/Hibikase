#pragma once

#include <Window\inputmapping.h>
#include <Window\windowtypes.h>

#include <array>
#include <cstdint>

namespace HApp
{

class ZWWindow;

class ZWInputSystem final
{
public:
    ZWInputSystem();

    void AttachWindow(ZWWindow& window);
    void DetachWindow();
    ZWWindow* GetWindow() const;

    void BeginFrame();

    bool IsKeyDown(int keyCode) const;
    bool WasKeyPressed(int keyCode) const;
    bool WasKeyReleased(int keyCode) const;
    bool IsMouseButtonDown(int mouseButton) const;
    bool WasMouseButtonPressed(int mouseButton) const;
    bool WasMouseButtonReleased(int mouseButton) const;

    bool IsActionDown(const std::string& actionName, const ZWInputMapping& inputMapping) const;
    bool WasActionPressed(const std::string& actionName, const ZWInputMapping& inputMapping) const;
    bool WasActionReleased(const std::string& actionName, const ZWInputMapping& inputMapping) const;

    double GetMouseX() const;
    double GetMouseY() const;
    double GetMouseDeltaX() const;
    double GetMouseDeltaY() const;
    double GetMousePixelX() const;
    double GetMousePixelY() const;
    double GetMousePixelDeltaX() const;
    double GetMousePixelDeltaY() const;
    double GetScrollDeltaX() const;
    double GetScrollDeltaY() const;

    bool IsCursorLocked() const;
    void SetCursorLocked(bool cursorLocked);
    bool IsRightDragging() const;

private:
    friend class ZWWindow;

    void HandleKeyEvent(const ZWKeyEvent& keyEvent);
    void HandleCursorEvent(const ZWCursorEvent& cursorEvent);
    void HandleMouseButtonEvent(const ZWMouseButtonEvent& mouseButtonEvent);
    void HandleScrollEvent(const ZWScrollEvent& scrollEvent);
    void HandleFocusEvent(const ZWFocusEvent& focusEvent);

    void ResetAllState();
    void ApplyCursorMode();
    bool EvaluateAction(const std::string& actionName, const ZWInputMapping& inputMapping, int evaluationMode) const;

private:
    static constexpr std::size_t KeyStateCount = GLFW_KEY_LAST + 1;
    static constexpr std::size_t MouseStateCount = GLFW_MOUSE_BUTTON_LAST + 1;

    ZWWindow* mWindow{ nullptr };
    std::array<std::uint8_t, KeyStateCount> mKeyDown{};
    std::array<std::uint8_t, KeyStateCount> mKeyPressed{};
    std::array<std::uint8_t, KeyStateCount> mKeyReleased{};
    std::array<std::uint8_t, MouseStateCount> mMouseDown{};
    std::array<std::uint8_t, MouseStateCount> mMousePressed{};
    std::array<std::uint8_t, MouseStateCount> mMouseReleased{};
    bool mCursorLocked{ false };
    bool mRightDragging{ false };
    bool mHasCursorSample{ false };
    double mMouseX{ 0.0 };
    double mMouseY{ 0.0 };
    double mMouseDeltaX{ 0.0 };
    double mMouseDeltaY{ 0.0 };
    double mScrollDeltaX{ 0.0 };
    double mScrollDeltaY{ 0.0 };
};

}
