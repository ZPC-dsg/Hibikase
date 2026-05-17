#include <Utils/consolelogger.h>

#include "testcommon.h"

namespace
{

// Toggle this to switch the Test project between the D3D12 and Vulkan triangle paths.
bool gUseVulkanTest = true;

}

namespace HTest
{

int RunD3D12TriangleTest(int selfTestDurationMs);
int RunVulkanTriangleTest(int selfTestDurationMs);

}

int main(int argc, char** argv)
{
    HApp::ZWConsoleLogger::Initialize();
    HApp::ZWConsoleLogger::PrintBanner(
        gUseVulkanTest ? "HIBIKASE VULKAN TRIANGLE TEST" : "HIBIKASE D3D12 TRIANGLE TEST");

    const int selfTestDurationMs = HTest::ParseSelfTestDurationMs(argc, argv);
    const int result = gUseVulkanTest
        ? HTest::RunVulkanTriangleTest(selfTestDurationMs)
        : HTest::RunD3D12TriangleTest(selfTestDurationMs);

    HApp::ZWConsoleLogger::Shutdown();
    return result;
}
