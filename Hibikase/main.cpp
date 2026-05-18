#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <Utils\consolelogger.h>
#include <Window\cameracontroller.h>
#include <Window\imguilayer.h>
#include <Window\inputsystem.h>
#include <Window\windowmanager.h>

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdlib>
#include <cctype>
#include <string>
#include <thread>
#include <vector>

typedef struct HWND__* HWND;

namespace HApp
{
class ZWWindow;
class ZWWindowManager;
}

namespace HRHI::HD3D12
{
    class ZWD3D12Backend final
    {
    public:
        static HWND GetWindowHandle(const HApp::ZWWindow& window);
    };
}

namespace HRHI
{
    class VulkanBackend final
    {
    public:
        static std::vector<const char*> GetRequiredInstanceExtensions(const HApp::ZWWindowManager& windowManager);
    };
}

namespace
{

enum class ECommandLineNvApiMode
{
    Auto,
    ForceEnable,
    ForceDisable
};

constexpr const char* cRuntimeNvApiEnvironmentVariable = "HIBIKASE_RUNTIME_NVAPI";

const char* ToNvApiModeName(const ECommandLineNvApiMode mode)
{
    switch (mode)
    {
    case ECommandLineNvApiMode::ForceEnable:
        return "force-enable";
    case ECommandLineNvApiMode::ForceDisable:
        return "force-disable";
    case ECommandLineNvApiMode::Auto:
    default:
        return "auto";
    }
}

std::string GetRuntimeNvApiOverrideValue()
{
    char* value = nullptr;
    size_t valueLength = 0;
    if (_dupenv_s(&value, &valueLength, cRuntimeNvApiEnvironmentVariable) != 0 || value == nullptr)
    {
        return "auto";
    }

    std::string result(value);
    free(value);
    return result;
}

bool TryParseNvApiMode(const std::string& value, ECommandLineNvApiMode& outMode)
{
    std::string normalizedValue = value;
    std::transform(
        normalizedValue.begin(),
        normalizedValue.end(),
        normalizedValue.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

    if (normalizedValue == "auto")
    {
        outMode = ECommandLineNvApiMode::Auto;
        return true;
    }

    if (normalizedValue == "on"
        || normalizedValue == "enable"
        || normalizedValue == "enabled"
        || normalizedValue == "force-on"
        || normalizedValue == "force-enable"
        || normalizedValue == "1"
        || normalizedValue == "true")
    {
        outMode = ECommandLineNvApiMode::ForceEnable;
        return true;
    }

    if (normalizedValue == "off"
        || normalizedValue == "disable"
        || normalizedValue == "disabled"
        || normalizedValue == "force-off"
        || normalizedValue == "force-disable"
        || normalizedValue == "0"
        || normalizedValue == "false")
    {
        outMode = ECommandLineNvApiMode::ForceDisable;
        return true;
    }

    return false;
}

void ApplyNvApiMode(const ECommandLineNvApiMode mode)
{
    const char* value = nullptr;
    switch (mode)
    {
    case ECommandLineNvApiMode::ForceEnable:
        value = "1";
        break;
    case ECommandLineNvApiMode::ForceDisable:
        value = "0";
        break;
    case ECommandLineNvApiMode::Auto:
    default:
        value = "auto";
        break;
    }

    _putenv_s(cRuntimeNvApiEnvironmentVariable, value);
}

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

HApp::ZWGraphicsBackend ToBackEndAPI(const std::string& name)
{
    if (name == "D3D12" || name == "d3d12" || name == "dx12")
    {
        return HApp::ZWGraphicsBackend::D3D12;
    }
    else if (name == "Vulkan" || name == "vulkan")
    {
        return HApp::ZWGraphicsBackend::Vulkan;
    }
    else
    {
        return HApp::ZWGraphicsBackend::None;
    }
}

void PrintControls()
{
    HApp::ZWConsoleLogger::PrintSection("Controls");
    HApp::ZWConsoleLogger::PrintListItem("Right Mouse Drag  rotate camera");
    HApp::ZWConsoleLogger::PrintListItem("Mouse Wheel       zoom field of view");
    HApp::ZWConsoleLogger::PrintListItem("WASD / Arrow Keys move camera");
    HApp::ZWConsoleLogger::PrintListItem("F1                toggle cursor lock");
    HApp::ZWConsoleLogger::PrintListItem("F11               toggle fullscreen");
    HApp::ZWConsoleLogger::PrintListItem("ESC               close the active demo window");
}

int ParseCommandLineArgs(int argc, char** argv, HApp::ZWWindowDesc& windowDesc)
{
    ECommandLineNvApiMode nvapiMode = ECommandLineNvApiMode::Auto;
    int selfTestDurationMs = 0;

    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const std::string argument = argv[argumentIndex];
        std::string prefix = "--self-test-ms=";
        if (argument.rfind(prefix, 0) == 0)
        {
            selfTestDurationMs = std::max(0, std::atoi(argument.substr(prefix.size()).c_str()));
            continue;
        }
        prefix = "--backend=";
        if (argument.rfind(prefix, 0) == 0)
        {
            const std::string backendName = argument.substr(prefix.size());
            HApp::ZWGraphicsBackend backend = ToBackEndAPI(backendName);
            if (backend != HApp::ZWGraphicsBackend::None)
            {
                windowDesc.graphicsBackend = backend;
            }
            else
            {
                HApp::ZWConsoleLogger::Warning("No backend named {} supported!Fallback to D3D12 instead.", backendName);
            }
        }

        prefix = "--nvapi=";
        if (argument.rfind(prefix, 0) == 0)
        {
            const std::string nvapiModeName = argument.substr(prefix.size());
            if (!TryParseNvApiMode(nvapiModeName, nvapiMode))
            {
                HApp::ZWConsoleLogger::Warning("Unknown NVAPI mode '{}', falling back to auto.", nvapiModeName);
                nvapiMode = ECommandLineNvApiMode::Auto;
            }
        }

        if (argument == "--enable-nvapi")
        {
            nvapiMode = ECommandLineNvApiMode::ForceEnable;
        }

        if (argument == "--disable-nvapi")
        {
            nvapiMode = ECommandLineNvApiMode::ForceDisable;
        }

        if (argument == "--self-test")
        {
            selfTestDurationMs = 2000;
        }
    }

    ApplyNvApiMode(nvapiMode);

    return selfTestDurationMs;
}

}

int main(int argc, char** argv)
{
    // ����CRT�ڴ�й¶
#if defined(_DEBUG) || defined(DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    // TODO ��Create Scene

    HApp::ZWWindowDesc mainWindowDesc;
    mainWindowDesc.width = 1280;
    mainWindowDesc.height = 720;
    mainWindowDesc.title = "Hibikase Main Window";
    mainWindowDesc.graphicsBackend = HApp::ZWGraphicsBackend::D3D12;
    mainWindowDesc.role = HApp::ZWWindowRole::Main;

    const int selfTestDurationMs = ParseCommandLineArgs(argc, argv, mainWindowDesc);
    HApp::ZWConsoleLogger::Initialize();
    HApp::ZWConsoleLogger::PrintBanner("HIBIKASE RUNTIME");
    HApp::ZWConsoleLogger::PrintSection("Startup");
    HApp::ZWConsoleLogger::PrintProperty("NVAPI override", GetRuntimeNvApiOverrideValue());

    HApp::ZWWindowManager windowManager;
    if (!windowManager.Initialize())
    {
        HApp::ZWConsoleLogger::Error("Failed to initialize window manager: {}", windowManager.GetLastError());
        HApp::ZWConsoleLogger::Shutdown();
        return 1;
    }

    HApp::ZWConsoleLogger::PrintProperty("Window manager", "ready");
    HApp::ZWConsoleLogger::PrintProperty("Vulkan surface support", windowManager.IsVulkanSupported() ? "available" : "unavailable");


    HApp::ZWWindow* mainWindow = windowManager.CreateWindow(mainWindowDesc);
    if (mainWindow == nullptr)
    {
        HApp::ZWConsoleLogger::Error("Failed to create main window: {}", windowManager.GetLastError());
        HApp::ZWConsoleLogger::Shutdown();
        return 1;
    }

    HApp::ZWConsoleLogger::PrintProperty("Main window", mainWindowDesc.title);
    HApp::ZWConsoleLogger::PrintProperty("Main backend", ToBackendName(mainWindowDesc.graphicsBackend));

    HApp::ZWWindowDesc auxiliaryWindowDesc;
    auxiliaryWindowDesc.width = 640;
    auxiliaryWindowDesc.height = 360;
    auxiliaryWindowDesc.title = "Hibikase Auxiliary Window";
    auxiliaryWindowDesc.graphicsBackend = windowManager.IsVulkanSupported() ? HApp::ZWGraphicsBackend::Vulkan : HApp::ZWGraphicsBackend::D3D12;
    auxiliaryWindowDesc.role = HApp::ZWWindowRole::Auxiliary;

    HApp::ZWWindow* auxiliaryWindow = windowManager.CreateWindow(auxiliaryWindowDesc);
    if (auxiliaryWindow == nullptr)
    {
        HApp::ZWConsoleLogger::Warning("Auxiliary window creation failed, continuing with the main window only.");
    }
    else
    {
        HApp::ZWConsoleLogger::PrintProperty("Auxiliary window", auxiliaryWindowDesc.title);
        HApp::ZWConsoleLogger::PrintProperty("Auxiliary backend", ToBackendName(auxiliaryWindowDesc.graphicsBackend));
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

    HApp::ZWConsoleLogger::PrintSection("Runtime Overview");
    HApp::ZWConsoleLogger::Info("{}", imguiLayer.GetStatusMessage());
    HApp::ZWConsoleLogger::PrintProperty("ImGui platform backend", imguiLayer.IsNativeIntegrationAvailable());
    HApp::ZWConsoleLogger::PrintProperty("ImGui renderer backend", imguiLayer.IsRendererBackendInitialized());
    HApp::ZWConsoleLogger::PrintProperty("Main HWND ready", HRHI::HD3D12::ZWD3D12Backend::GetWindowHandle(*mainWindow) != nullptr);
    HApp::ZWConsoleLogger::PrintProperty("GLFW Vulkan extension count", HRHI::VulkanBackend::GetRequiredInstanceExtensions(windowManager).size());
    if (selfTestDurationMs > 0)
    {
        HApp::ZWConsoleLogger::Warning("Self-test mode is active. The demo will close automatically after {} ms.", selfTestDurationMs);
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
    HApp::ZWConsoleLogger::PrintSection("Shutdown");
    HApp::ZWConsoleLogger::Info("Demo finished cleanly.");
    HApp::ZWConsoleLogger::Shutdown();
    return 0;
}
