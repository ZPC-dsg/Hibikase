#include <BackEnd\d3d12backend.h>
#include <BackEnd\vulkanbackend.h>
#include <Window\cameracontroller.h>
#include <Window\imguilayer.h>
#include <Window\inputsystem.h>
#include <Window\windowmanager.h>

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace
{

const char* ToBackendName(HApp::ZWGraphicsBackend graphicsBackend)
{
    switch (graphicsBackend)
    {
    case HApp::ZWGraphicsBackend::D3D12:
        return "D3D12";
    case HApp::ZWGraphicsBackend::Vulkan:
        return "Vulkan";
    default:
        return "None";
    }
}

void PrintControls()
{
    std::cout << "Controls\n";
    std::cout << "  Right Mouse Drag: rotate camera\n";
    std::cout << "  Mouse Wheel: zoom FOV\n";
    std::cout << "  WASD / Arrow Keys: move camera\n";
    std::cout << "  F1: toggle cursor lock\n";
    std::cout << "  F11: toggle fullscreen\n";
    std::cout << "  ESC: close active demo window\n";
}

int ParseSelfTestDurationMs(int argc, char** argv)
{
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const std::string argument = argv[argumentIndex];
        const std::string prefix = "--self-test-ms=";
        if (argument.rfind(prefix, 0) == 0)
        {
            return std::max(0, std::atoi(argument.substr(prefix.size()).c_str()));
        }

        if (argument == "--self-test")
        {
            return 2000;
        }
    }

    return 0;
}

}

int main(int argc, char** argv)
{
    const int selfTestDurationMs = ParseSelfTestDurationMs(argc, argv);

    HApp::ZWWindowManager windowManager;
    if (!windowManager.Initialize())
    {
        std::cerr << "Failed to initialize window manager: " << windowManager.GetLastError() << '\n';
        return 1;
    }

    HApp::ZWWindowDesc mainWindowDesc;
    mainWindowDesc.width = 1280;
    mainWindowDesc.height = 720;
    mainWindowDesc.title = "Hibikase Main Window";
    mainWindowDesc.graphicsBackend = HApp::ZWGraphicsBackend::D3D12;
    mainWindowDesc.role = HApp::ZWWindowRole::Main;

    HApp::ZWWindow* mainWindow = windowManager.CreateWindow(mainWindowDesc);
    if (mainWindow == nullptr)
    {
        std::cerr << "Failed to create main window: " << windowManager.GetLastError() << '\n';
        return 1;
    }

    HApp::ZWWindowDesc auxiliaryWindowDesc;
    auxiliaryWindowDesc.width = 640;
    auxiliaryWindowDesc.height = 360;
    auxiliaryWindowDesc.title = "Hibikase Auxiliary Window";
    auxiliaryWindowDesc.graphicsBackend = windowManager.IsVulkanSupported() ? HApp::ZWGraphicsBackend::Vulkan : HApp::ZWGraphicsBackend::D3D12;
    auxiliaryWindowDesc.role = HApp::ZWWindowRole::Auxiliary;

    HApp::ZWWindow* auxiliaryWindow = windowManager.CreateWindow(auxiliaryWindowDesc);
    if (auxiliaryWindow == nullptr)
    {
        std::cerr << "Auxiliary window creation failed, continuing with the main window only.\n";
    }

    HApp::ZWInputSystem mainInputSystem;
    mainInputSystem.AttachWindow(*mainWindow);

    HApp::ZWInputSystem auxiliaryInputSystem;
    if (auxiliaryWindow != nullptr)
    {
        auxiliaryInputSystem.AttachWindow(*auxiliaryWindow);
    }

    HApp::ZWInputMapping inputMapping = HApp::ZWCameraController::CreateDefaultInputMapping();
    HApp::ZWCameraController cameraController;

    HApp::ZWImGuiLayer imguiLayer;
    imguiLayer.InitializeForD3D12(*mainWindow, HApp::ZWImGuiD3D12InitInfo{});

    std::cout << imguiLayer.GetStatusMessage() << '\n';
    std::cout << "ImGui platform backend active: " << (imguiLayer.IsNativeIntegrationAvailable() ? "yes" : "no") << '\n';
    std::cout << "ImGui renderer backend active: " << (imguiLayer.IsRendererBackendInitialized() ? "yes" : "no") << '\n';
    std::cout << "Vulkan surface support: " << (windowManager.IsVulkanSupported() ? "available" : "unavailable") << '\n';
    std::cout << "Main HWND ready: " << (HRHI::D3D12Backend::GetWindowHandle(*mainWindow) != nullptr ? "yes" : "no") << '\n';
    std::cout << "GLFW Vulkan extension count: " << HRHI::VulkanBackend::GetRequiredInstanceExtensions(windowManager).size() << '\n';
    if (selfTestDurationMs > 0)
    {
        std::cout << "Self-test mode: the demo will close automatically after " << selfTestDurationMs << " ms.\n";
    }
    PrintControls();

    auto previousTime = std::chrono::steady_clock::now();
    const auto selfTestStartTime = previousTime;

    while (!mainWindow->ShouldClose())
    {
        const auto currentTime = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - previousTime).count();
        previousTime = currentTime;
        if (deltaTime > 0.1f)
        {
            deltaTime = 0.1f;
        }

        mainInputSystem.BeginFrame();
        if (auxiliaryWindow != nullptr)
        {
            auxiliaryInputSystem.BeginFrame();
        }

        windowManager.PollEvents();

        if (mainInputSystem.WasKeyPressed(GLFW_KEY_ESCAPE))
        {
            mainWindow->RequestClose();
        }
        if (mainInputSystem.WasKeyPressed(GLFW_KEY_F11))
        {
            mainWindow->ToggleFullscreen();
        }
        if (mainInputSystem.WasKeyPressed(GLFW_KEY_F1))
        {
            mainInputSystem.SetCursorLocked(!mainInputSystem.IsCursorLocked());
        }

        if (auxiliaryWindow != nullptr && auxiliaryInputSystem.WasKeyPressed(GLFW_KEY_ESCAPE))
        {
            auxiliaryWindow->RequestClose();
        }

        cameraController.Update(mainInputSystem, inputMapping, deltaTime);

        const HApp::ZWDpiScale dpiScale = mainWindow->GetDpiScale();
        HApp::ZWImGuiDebugState debugState;
        debugState.frameTimeMs = deltaTime * 1000.0f;
        debugState.framesPerSecond = deltaTime > 0.0f ? (1.0f / deltaTime) : 0.0f;
        debugState.fieldOfView = cameraController.GetFieldOfView();
        debugState.yaw = cameraController.GetYaw();
        debugState.pitch = cameraController.GetPitch();
        debugState.dpiScaleX = dpiScale.x;
        debugState.dpiScaleY = dpiScale.y;
        debugState.cameraPositionX = cameraController.GetPosition().x;
        debugState.cameraPositionY = cameraController.GetPosition().y;
        debugState.cameraPositionZ = cameraController.GetPosition().z;
        debugState.activeBackend = ToBackendName(mainWindow->GetGraphicsBackend());

        imguiLayer.BeginFrame();
        imguiLayer.SetDebugState(debugState);
        imguiLayer.EndFrame();

        if (selfTestDurationMs > 0)
        {
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - selfTestStartTime).count();
            if (elapsedMs >= selfTestDurationMs)
            {
                mainWindow->RequestClose();
            }
        }

        if (auxiliaryWindow != nullptr && auxiliaryWindow->ShouldClose())
        {
            const std::uint32_t auxiliaryWindowId = auxiliaryWindow->GetWindowId();
            auxiliaryInputSystem.DetachWindow();
            windowManager.DestroyWindow(auxiliaryWindowId);
            auxiliaryWindow = nullptr;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    imguiLayer.Shutdown();
    auxiliaryInputSystem.DetachWindow();
    mainInputSystem.DetachWindow();
    windowManager.Shutdown();
    return 0;
}
