#include <Window\inputsystem.h>

#include <Window\window.h>

#include <algorithm>

namespace
{

bool IsValidKeyCode(int keyCode)
{
    return keyCode >= 0 && keyCode <= GLFW_KEY_LAST;
}

bool IsValidMouseButton(int mouseButton)
{
    return mouseButton >= 0 && mouseButton <= GLFW_MOUSE_BUTTON_LAST;
}

}

namespace HApp
{

enum : int
{
    ActionDown = 0,
    ActionPressed = 1,
    ActionReleased = 2
};

ZWInputSystem::ZWInputSystem() = default;

void ZWInputSystem::AttachWindow(ZWWindow& window)
{
    if (mWindow == &window)
    {
        return;
    }

    DetachWindow();
    mWindow = &window;
    mWindow->SetInputSystem(this);
    ApplyCursorMode();
    ResetAllState();
}

void ZWInputSystem::DetachWindow()
{
    if (mWindow != nullptr && mWindow->GetInputSystem() == this)
    {
        mWindow->SetInputSystem(nullptr);
    }

    mWindow = nullptr;
    mCursorLocked = false;
    mRightDragging = false;
    mHasCursorSample = false;
    ResetAllState();
}

ZWWindow* ZWInputSystem::GetWindow() const
{
    return mWindow;
}

void ZWInputSystem::BeginFrame()
{
    std::fill(mKeyPressed.begin(), mKeyPressed.end(), 0);
    std::fill(mKeyReleased.begin(), mKeyReleased.end(), 0);
    std::fill(mMousePressed.begin(), mMousePressed.end(), 0);
    std::fill(mMouseReleased.begin(), mMouseReleased.end(), 0);
    mMouseDeltaX = 0.0;
    mMouseDeltaY = 0.0;
    mScrollDeltaX = 0.0;
    mScrollDeltaY = 0.0;
}

bool ZWInputSystem::IsKeyDown(int keyCode) const
{
    return IsValidKeyCode(keyCode) ? mKeyDown[static_cast<std::size_t>(keyCode)] != 0 : false;
}

bool ZWInputSystem::WasKeyPressed(int keyCode) const
{
    return IsValidKeyCode(keyCode) ? mKeyPressed[static_cast<std::size_t>(keyCode)] != 0 : false;
}

bool ZWInputSystem::WasKeyReleased(int keyCode) const
{
    return IsValidKeyCode(keyCode) ? mKeyReleased[static_cast<std::size_t>(keyCode)] != 0 : false;
}

bool ZWInputSystem::IsMouseButtonDown(int mouseButton) const
{
    return IsValidMouseButton(mouseButton) ? mMouseDown[static_cast<std::size_t>(mouseButton)] != 0 : false;
}

bool ZWInputSystem::WasMouseButtonPressed(int mouseButton) const
{
    return IsValidMouseButton(mouseButton) ? mMousePressed[static_cast<std::size_t>(mouseButton)] != 0 : false;
}

bool ZWInputSystem::WasMouseButtonReleased(int mouseButton) const
{
    return IsValidMouseButton(mouseButton) ? mMouseReleased[static_cast<std::size_t>(mouseButton)] != 0 : false;
}

bool ZWInputSystem::IsActionDown(const std::string& actionName, const ZWInputMapping& inputMapping) const
{
    return EvaluateAction(actionName, inputMapping, ActionDown);
}

bool ZWInputSystem::WasActionPressed(const std::string& actionName, const ZWInputMapping& inputMapping) const
{
    return EvaluateAction(actionName, inputMapping, ActionPressed);
}

bool ZWInputSystem::WasActionReleased(const std::string& actionName, const ZWInputMapping& inputMapping) const
{
    return EvaluateAction(actionName, inputMapping, ActionReleased);
}

double ZWInputSystem::GetMouseX() const
{
    return mMouseX;
}

double ZWInputSystem::GetMouseY() const
{
    return mMouseY;
}

double ZWInputSystem::GetMouseDeltaX() const
{
    return mMouseDeltaX;
}

double ZWInputSystem::GetMouseDeltaY() const
{
    return mMouseDeltaY;
}

double ZWInputSystem::GetMousePixelX() const
{
    if (mWindow == nullptr)
    {
        return mMouseX;
    }

    const ZWDpiScale dpiScale = mWindow->GetDpiScale();
    return mMouseX * static_cast<double>(dpiScale.x);
}

double ZWInputSystem::GetMousePixelY() const
{
    if (mWindow == nullptr)
    {
        return mMouseY;
    }

    const ZWDpiScale dpiScale = mWindow->GetDpiScale();
    return mMouseY * static_cast<double>(dpiScale.y);
}

double ZWInputSystem::GetMousePixelDeltaX() const
{
    if (mWindow == nullptr)
    {
        return mMouseDeltaX;
    }

    const ZWDpiScale dpiScale = mWindow->GetDpiScale();
    return mMouseDeltaX * static_cast<double>(dpiScale.x);
}

double ZWInputSystem::GetMousePixelDeltaY() const
{
    if (mWindow == nullptr)
    {
        return mMouseDeltaY;
    }

    const ZWDpiScale dpiScale = mWindow->GetDpiScale();
    return mMouseDeltaY * static_cast<double>(dpiScale.y);
}

double ZWInputSystem::GetScrollDeltaX() const
{
    return mScrollDeltaX;
}

double ZWInputSystem::GetScrollDeltaY() const
{
    return mScrollDeltaY;
}

bool ZWInputSystem::IsCursorLocked() const
{
    return mCursorLocked;
}

void ZWInputSystem::SetCursorLocked(bool cursorLocked)
{
    mCursorLocked = cursorLocked;
    ApplyCursorMode();
}

bool ZWInputSystem::IsRightDragging() const
{
    return mRightDragging;
}

void ZWInputSystem::HandleKeyEvent(const ZWKeyEvent& keyEvent)
{
    if (!IsValidKeyCode(keyEvent.key))
    {
        return;
    }

    const std::size_t keyIndex = static_cast<std::size_t>(keyEvent.key);
    if (keyEvent.action == GLFW_PRESS)
    {
        if (mKeyDown[keyIndex] == 0)
        {
            mKeyPressed[keyIndex] = 1;
        }

        mKeyDown[keyIndex] = 1;
    }
    else if (keyEvent.action == GLFW_RELEASE)
    {
        if (mKeyDown[keyIndex] != 0)
        {
            mKeyReleased[keyIndex] = 1;
        }

        mKeyDown[keyIndex] = 0;
    }
}

void ZWInputSystem::HandleCursorEvent(const ZWCursorEvent& cursorEvent)
{
    mMouseX = cursorEvent.x;
    mMouseY = cursorEvent.y;

    if (!mHasCursorSample)
    {
        mHasCursorSample = true;
        return;
    }

    mMouseDeltaX += cursorEvent.deltaX;
    mMouseDeltaY += cursorEvent.deltaY;
}

void ZWInputSystem::HandleMouseButtonEvent(const ZWMouseButtonEvent& mouseButtonEvent)
{
    if (!IsValidMouseButton(mouseButtonEvent.button))
    {
        return;
    }

    const std::size_t buttonIndex = static_cast<std::size_t>(mouseButtonEvent.button);
    if (mouseButtonEvent.action == GLFW_PRESS)
    {
        if (mMouseDown[buttonIndex] == 0)
        {
            mMousePressed[buttonIndex] = 1;
        }

        mMouseDown[buttonIndex] = 1;
    }
    else if (mouseButtonEvent.action == GLFW_RELEASE)
    {
        if (mMouseDown[buttonIndex] != 0)
        {
            mMouseReleased[buttonIndex] = 1;
        }

        mMouseDown[buttonIndex] = 0;
    }

    mRightDragging = IsMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT);
}

void ZWInputSystem::HandleScrollEvent(const ZWScrollEvent& scrollEvent)
{
    mScrollDeltaX += scrollEvent.offsetX;
    mScrollDeltaY += scrollEvent.offsetY;
}

void ZWInputSystem::HandleFocusEvent(const ZWFocusEvent& focusEvent)
{
    if (!focusEvent.focused)
    {
        ResetAllState();
        mRightDragging = false;
        mHasCursorSample = false;
    }
}

void ZWInputSystem::ResetAllState()
{
    std::fill(mKeyDown.begin(), mKeyDown.end(), 0);
    std::fill(mKeyPressed.begin(), mKeyPressed.end(), 0);
    std::fill(mKeyReleased.begin(), mKeyReleased.end(), 0);
    std::fill(mMouseDown.begin(), mMouseDown.end(), 0);
    std::fill(mMousePressed.begin(), mMousePressed.end(), 0);
    std::fill(mMouseReleased.begin(), mMouseReleased.end(), 0);
    mMouseDeltaX = 0.0;
    mMouseDeltaY = 0.0;
    mScrollDeltaX = 0.0;
    mScrollDeltaY = 0.0;
}

void ZWInputSystem::ApplyCursorMode()
{
    if (mWindow == nullptr || mWindow->GetNativeWindow() == nullptr)
    {
        return;
    }

    glfwSetInputMode(mWindow->GetNativeWindow(), GLFW_CURSOR, mCursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

bool ZWInputSystem::EvaluateAction(const std::string& actionName, const ZWInputMapping& inputMapping, int evaluationMode) const
{
    const std::vector<ZWInputBinding>& bindings = inputMapping.GetBindings(actionName);
    for (const ZWInputBinding& binding : bindings)
    {
        if (binding.bindingType == ZWInputBindingType::Key)
        {
            if ((evaluationMode == ActionDown && IsKeyDown(binding.code)) ||
                (evaluationMode == ActionPressed && WasKeyPressed(binding.code)) ||
                (evaluationMode == ActionReleased && WasKeyReleased(binding.code)))
            {
                return true;
            }
        }
        else if (binding.bindingType == ZWInputBindingType::MouseButton)
        {
            if ((evaluationMode == ActionDown && IsMouseButtonDown(binding.code)) ||
                (evaluationMode == ActionPressed && WasMouseButtonPressed(binding.code)) ||
                (evaluationMode == ActionReleased && WasMouseButtonReleased(binding.code)))
            {
                return true;
            }
        }
    }

    return false;
}

}
