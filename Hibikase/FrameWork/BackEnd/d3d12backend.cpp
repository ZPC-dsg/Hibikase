#include <BackEnd/d3d12backend.h>
#include <Window/window.h>

namespace HRHI
{

HWND D3D12Backend::GetWindowHandle(const HApp::ZWWindow& window)
{
    return window.GetNativeHandle();
}

}
