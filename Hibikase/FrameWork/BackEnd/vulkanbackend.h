#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace HApp
{

class ZWWindow;
class ZWWindowManager;

}

namespace HRHI
{

class VulkanBackend final
{
public:
    static std::vector<const char*> GetRequiredInstanceExtensions(const HApp::ZWWindowManager& windowManager);
    static bool CreateWindowSurface(const HApp::ZWWindow& window, VkInstance instance, VkSurfaceKHR* outSurface, const VkAllocationCallbacks* allocator = nullptr);
};

}
