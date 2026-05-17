#include <BackEnd/vulkanbackend.h>

#include <cassert>

namespace HRHI
{

static vk::MemoryPropertyFlags PickBufferMemoryProperties(const ZWBufferDesc& desc)
{
    vk::MemoryPropertyFlags flags{};

    switch (desc.cpuAccess)
    {
    case ECpuAccessMode::None:
        flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
        break;

    case ECpuAccessMode::Read:
        flags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached;
        break;

    case ECpuAccessMode::Write:
        flags = vk::MemoryPropertyFlagBits::eHostVisible;
        break;
    }

    return flags;
}

vk::Result ZWVKAllocator::AllocateBufferMemory(ZWVKBuffer* buffer, bool enableBufferAddress) const
{
    vk::MemoryRequirements memRequirements;
    mContext.device.getBufferMemoryRequirements(buffer->buffer, &memRequirements);

    const bool enableMemoryExport = (buffer->desc.sharedResourceFlags & ESharedResourceFlags::Shared) != ESharedResourceFlags::None;
    const vk::Result result = AllocateMemory(
        buffer,
        memRequirements,
        PickBufferMemoryProperties(buffer->desc),
        enableBufferAddress,
        enableMemoryExport,
        nullptr,
        buffer->buffer);
    CHECK_VK_RETURN(result)

    mContext.device.bindBufferMemory(buffer->buffer, buffer->memory, 0);
    return vk::Result::eSuccess;
}

void ZWVKAllocator::FreeBufferMemory(ZWVKBuffer* buffer) const
{
    FreeMemory(buffer);
}

vk::Result ZWVKAllocator::AllocateTextureMemory(ZWVKTexture* texture) const
{
    vk::MemoryRequirements memRequirements;
    mContext.device.getImageMemoryRequirements(texture->image, &memRequirements);

    const vk::MemoryPropertyFlags memProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
    const bool enableDeviceAddress = false;
    const bool enableMemoryExport = (texture->desc.sharedResourceFlags & ESharedResourceFlags::Shared) != ESharedResourceFlags::None;
    const vk::Result result = AllocateMemory(
        texture,
        memRequirements,
        memProperties,
        enableDeviceAddress,
        enableMemoryExport,
        texture->image,
        nullptr);
    CHECK_VK_RETURN(result)

    mContext.device.bindImageMemory(texture->image, texture->memory, 0);
    return vk::Result::eSuccess;
}

void ZWVKAllocator::FreeTextureMemory(ZWVKTexture* texture) const
{
    FreeMemory(texture);
}

vk::Result ZWVKAllocator::AllocateMemory(
    ZWVKMemoryResource* resource,
    vk::MemoryRequirements memRequirements,
    vk::MemoryPropertyFlags memPropertyFlags,
    bool enableDeviceAddress,
    bool enableExportMemory,
    VkImage dedicatedImage,
    VkBuffer dedicatedBuffer) const
{
    resource->managed = true;

    vk::PhysicalDeviceMemoryProperties memProperties;
    mContext.physicalDevice.getMemoryProperties(&memProperties);

    uint32_t memTypeIndex = memProperties.memoryTypeCount;
    for (uint32_t index = 0; index < memProperties.memoryTypeCount; ++index)
    {
        if ((memRequirements.memoryTypeBits & (1 << index)) &&
            ((memProperties.memoryTypes[index].propertyFlags & memPropertyFlags) == memPropertyFlags))
        {
            memTypeIndex = index;
            break;
        }
    }

    if (memTypeIndex == memProperties.memoryTypeCount)
    {
        return vk::Result::eErrorOutOfDeviceMemory;
    }

    vk::MemoryAllocateFlagsInfo allocFlagsInfo;
    if (enableDeviceAddress)
    {
        allocFlagsInfo.flags |= vk::MemoryAllocateFlagBits::eDeviceAddress;
    }
    const void* next = &allocFlagsInfo;

    const vk::MemoryDedicatedAllocateInfo dedicatedAllocation =
        vk::MemoryDedicatedAllocateInfo()
            .setImage(dedicatedImage)
            .setBuffer(dedicatedBuffer)
            .setPNext(next);

    if (dedicatedImage || dedicatedBuffer)
    {
        next = &dedicatedAllocation;
    }

#ifdef _WIN32
    const auto handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
#else
    const auto handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
#endif

    const vk::ExportMemoryAllocateInfo exportInfo =
        vk::ExportMemoryAllocateInfo()
            .setHandleTypes(handleType)
            .setPNext(next);

    if (enableExportMemory)
    {
        next = &exportInfo;
    }

    const vk::MemoryAllocateInfo allocInfo =
        vk::MemoryAllocateInfo()
            .setAllocationSize(memRequirements.size)
            .setMemoryTypeIndex(memTypeIndex)
            .setPNext(next);

    return mContext.device.allocateMemory(&allocInfo, mContext.allocationCallbacks, &resource->memory);
}

void ZWVKAllocator::FreeMemory(ZWVKMemoryResource* resource) const
{
    assert(resource->managed);
    mContext.device.freeMemory(resource->memory, mContext.allocationCallbacks);
    resource->memory = vk::DeviceMemory(nullptr);
}

}
