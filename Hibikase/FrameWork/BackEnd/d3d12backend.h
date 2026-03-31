#pragma once

typedef struct HWND__* HWND;

namespace HApp
{

class ZWWindow;

}

namespace HRHI
{

class D3D12Backend final
{
public:
    static HWND GetWindowHandle(const HApp::ZWWindow& window);
};

}
