#include <BackEnd/vulkanbackend.h>
#include <Window/window.h>
#include <Window/windowmanager.h>

namespace HRHI
{

std::vector<const char*> VulkanBackend::GetRequiredInstanceExtensions(const HApp::ZWWindowManager& windowManager)
{
    return windowManager.GetRequiredVulkanExtensions();
}

bool VulkanBackend::CreateWindowSurface(const HApp::ZWWindow& window, VkInstance instance, VkSurfaceKHR* outSurface, const VkAllocationCallbacks* allocator)
{
    return window.CreateVulkanSurface(instance, outSurface, allocator);
}

}
