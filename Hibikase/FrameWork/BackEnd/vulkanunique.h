#pragma once

#include <BackEnd/RHIinterface.h>

#include <vulkan/vulkan.h>

namespace HRHI
{
	namespace HVulkanObjectTypes
	{
        typedef uint32_t ObjectType;

        constexpr ObjectType Nvrhi_VK_Device = 0x00030101;
	}

	namespace HVulkan
	{
        class IDevice : public HRHI::IDevice
        {
        public:
            // Additional Vulkan-specific public methods
            virtual VkSemaphore GetQueueSemaphore(ECommandQueue queue) = 0;
            virtual void QueueWaitForSemaphore(ECommandQueue waitQueue, VkSemaphore semaphore, uint64_t value) = 0;
            virtual void QueueSignalSemaphore(ECommandQueue executionQueue, VkSemaphore semaphore, uint64_t value) = 0;
            virtual uint64_t QueueGetCompletedInstance(ECommandQueue queue) = 0;
        };
        typedef HCommon::RefCountPtr<IDevice> ZWDeviceHandle;

        struct ZWDeviceDesc
        {
            IMessageCallback* errorCB = nullptr;

            VkInstance instance;
            VkPhysicalDevice physicalDevice;
            VkDevice device;

            // any of the queues can be null if this context doesn't intend to use them
            VkQueue graphicsQueue;
            int graphicsQueueIndex = -1;
            VkQueue transferQueue;
            int transferQueueIndex = -1;
            VkQueue computeQueue;
            int computeQueueIndex = -1;

            VkAllocationCallbacks* allocationCallbacks = nullptr;

            const char** instanceExtensions = nullptr;
            size_t numInstanceExtensions = 0;

            const char** deviceExtensions = nullptr;
            size_t numDeviceExtensions = 0;

            uint32_t maxTimerQueries = 256;

            // Indicates if VkPhysicalDeviceVulkan12Features::bufferDeviceAddress was set to 'true' at device creation time
            bool bufferDeviceAddressSupported = false;
            bool aftermathEnabled = false;
            bool logBufferLifetime = false;

            std::string vulkanLibraryName; // if empty, use default
        };

        ZWDeviceHandle CreateDevice(const ZWDeviceDesc& desc);

        VkFormat ConvertFormat(EFormat format);

        const char* ResultToString(VkResult result);
	}
}