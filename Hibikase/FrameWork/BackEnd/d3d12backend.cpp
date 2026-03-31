#include <BackEnd/d3d12backend.h>
#include <Window/window.h>

namespace HRHI::HD3D12
{
    HWND ZWD3D12Backend::GetWindowHandle(const HApp::ZWWindow& window)
    {
        return window.GetNativeHandle();
    }
}
