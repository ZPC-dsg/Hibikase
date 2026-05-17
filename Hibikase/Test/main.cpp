#include <Utils/consolelogger.h>

#include "testcommon.h"

namespace
{

bool ShouldUseVulkanTest(int argc, char** argv)
{
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const std::string argument = argv[argumentIndex] != nullptr ? argv[argumentIndex] : "";
        if (argument == "--backend=d3d12")
        {
            return false;
        }

        if (argument == "--backend=vulkan")
        {
            return true;
        }
    }

    return true;
}

}

namespace HTest
{

int RunD3D12TriangleTest(int selfTestDurationMs);
int RunVulkanTriangleTest(int selfTestDurationMs);

}

int main(int argc, char** argv)
{
    const bool useVulkanTest = ShouldUseVulkanTest(argc, argv);

    HApp::ZWConsoleLogger::Initialize();
    HApp::ZWConsoleLogger::PrintBanner(
        useVulkanTest ? "HIBIKASE VULKAN TRIANGLE TEST" : "HIBIKASE D3D12 TRIANGLE TEST");

    const int selfTestDurationMs = HTest::ParseSelfTestDurationMs(argc, argv);
    const int result = useVulkanTest
        ? HTest::RunVulkanTriangleTest(selfTestDurationMs)
        : HTest::RunD3D12TriangleTest(selfTestDurationMs);

    HApp::ZWConsoleLogger::Shutdown();
    return result;
}
