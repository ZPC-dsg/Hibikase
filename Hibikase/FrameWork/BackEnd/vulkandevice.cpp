#include <BackEnd/vulkanbackend.h>

#include <Utils/consolelogger.h>
#include <Utils/stringtranslatehelper.h>

#include <algorithm>
#include <cassert>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace
{

void DispatchMessage(HRHI::IMessageCallback* messageCallback, HRHI::EMessageSeverity severity, const std::string& message)
{
    if (messageCallback != nullptr)
    {
        messageCallback->message(severity, message.c_str());
        return;
    }

    switch (severity)
    {
    case HRHI::EMessageSeverity::Info:
        HApp::ZWConsoleLogger::Info("{}", message);
        break;
    case HRHI::EMessageSeverity::Warning:
        HApp::ZWConsoleLogger::Warning("{}", message);
        break;
    case HRHI::EMessageSeverity::Error:
    case HRHI::EMessageSeverity::Fatal:
        HApp::ZWConsoleLogger::Error("{}", message);
        break;
    }
}

std::string FormatVkResult(vk::Result result)
{
    std::ostringstream stream;
    stream << "VkResult = " << static_cast<int>(result);
    return stream.str();
}

}

namespace HRHI::HVulkan
{

bool TryEnableAftermath(
    const char* applicationName,
    const char* applicationVersion,
    const char* commandLine,
    IMessageCallback* messageCallback)
{
    return HApp::ZWAftermathRuntime::Get().EnableVulkanCrashDumps(
        applicationName,
        applicationVersion,
        commandLine,
        messageCallback);
}

void ConfigureAftermathDeviceExtensions(
    std::vector<const char*>& deviceExtensions,
    ZWAftermathDeviceConfiguration& configuration,
    const void* deviceCreateInfoNext,
    IMessageCallback* messageCallback)
{
    configuration = {};

#if HRHI_WITH_AFTERMATH
    if (!HApp::ZWAftermathRuntime::Get().IsEnabled())
    {
        DispatchMessage(messageCallback, EMessageSeverity::Info, "Vulkan Aftermath device extensions were not injected because crash dump collection is inactive.");
        return;
    }

    const auto appendExtension = [&deviceExtensions](const char* extensionName)
    {
        if (std::find(deviceExtensions.begin(), deviceExtensions.end(), extensionName) == deviceExtensions.end())
        {
            deviceExtensions.push_back(extensionName);
        }
    };

    appendExtension(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
    appendExtension(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);

    configuration.enabled = true;
    configuration.diagnosticsConfig = {};
    configuration.diagnosticsConfig.sType = VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV;
    configuration.diagnosticsConfig.pNext = const_cast<void*>(deviceCreateInfoNext);
    configuration.diagnosticsConfig.flags =
        VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV |
        VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV |
        VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV |
        VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV;

    DispatchMessage(
        messageCallback,
        EMessageSeverity::Info,
        "Injected Vulkan Aftermath device extensions: VK_NV_device_diagnostic_checkpoints, VK_NV_device_diagnostics_config.");
#else
    (void)deviceExtensions;
    (void)deviceCreateInfoNext;

    DispatchMessage(messageCallback, EMessageSeverity::Info, "Vulkan Aftermath support was compiled out because Nsight Aftermath SDK headers are unavailable.");
#endif

    (void)messageCallback;
}

void WaitForAftermathCrashDump(uint32_t timeoutMs, IMessageCallback* messageCallback)
{
    HApp::ZWAftermathRuntime::Get().WaitForCrashDump(timeoutMs, messageCallback);
}

ZWDeviceHandle CreateDevice(const ZWDeviceDesc& desc)
{
    if (desc.instance == nullptr || desc.physicalDevice == nullptr || desc.device == nullptr)
    {
        return nullptr;
    }

    VULKAN_HPP_DEFAULT_DISPATCHER.init(::vkGetInstanceProcAddr);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Instance(desc.instance));
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Device(desc.device));

    return ZWDeviceHandle::Create(new ZWVKDevice(desc));
}

}

namespace HRHI
{

ZWVKDevice::ZWVKDevice(const HRHI::HVulkan::ZWDeviceDesc& desc)
    : mContext(
        vk::Instance(desc.instance),
        vk::PhysicalDevice(desc.physicalDevice),
        vk::Device(desc.device),
        reinterpret_cast<vk::AllocationCallbacks*>(desc.allocationCallbacks))
    , mAllocator(mContext)
    , mTimerQueryAllocator(desc.maxTimerQueries, true)
{
    assert(desc.device != nullptr);

    if (desc.graphicsQueue != nullptr)
    {
        mQueues[uint32_t(ECommandQueue::Graphics)] = std::make_unique<ZWVKQueue>(
            mContext,
            ECommandQueue::Graphics,
            vk::Queue(desc.graphicsQueue),
            desc.graphicsQueueIndex);
    }

    if (desc.computeQueue != nullptr)
    {
        mQueues[uint32_t(ECommandQueue::Compute)] = std::make_unique<ZWVKQueue>(
            mContext,
            ECommandQueue::Compute,
            vk::Queue(desc.computeQueue),
            desc.computeQueueIndex);
    }

    if (desc.transferQueue != nullptr)
    {
        mQueues[uint32_t(ECommandQueue::Copy)] = std::make_unique<ZWVKQueue>(
            mContext,
            ECommandQueue::Copy,
            vk::Queue(desc.transferQueue),
            desc.transferQueueIndex);
    }

    const std::unordered_map<std::string, bool*> extensionStringMap = {
        { VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME, &mContext.extensions.EXT_conservative_rasterization },
        { VK_EXT_DEBUG_MARKER_EXTENSION_NAME, &mContext.extensions.EXT_debug_marker },
        { VK_EXT_DEBUG_REPORT_EXTENSION_NAME, &mContext.extensions.EXT_debug_report },
        { VK_EXT_DEBUG_UTILS_EXTENSION_NAME, &mContext.extensions.EXT_debug_utils },
        { VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME, &mContext.extensions.EXT_opacity_micromap },
        { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &mContext.extensions.KHR_acceleration_structure },
        { VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, &mContext.extensions.buffer_device_address },
        { VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, &mContext.extensions.KHR_fragment_shading_rate },
        { VK_KHR_RAY_QUERY_EXTENSION_NAME, &mContext.extensions.KHR_ray_query },
        { VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &mContext.extensions.KHR_ray_tracing_pipeline },
        { VK_EXT_MESH_SHADER_EXTENSION_NAME, &mContext.extensions.EXT_mesh_shader },
        { VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME, &mContext.extensions.EXT_mutable_descriptor_type },
#if HRHI_VULKAN_NV
        { VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME, &mContext.extensions.NV_ray_tracing_invocation_reorder },
        { VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME, &mContext.extensions.NV_cluster_acceleration_structure },
        { VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME, &mContext.extensions.NV_cooperative_vector },
        { VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME, &mContext.extensions.NV_ray_tracing_linear_swept_spheres },

#if HRHI_WITH_AFTERMATH
    { VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME, &mContext.extensions.NV_device_diagnostic_checkpoints },
    { VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME, &mContext.extensions.NV_device_diagnostics_config },
#endif
#endif
    };

    if (desc.instanceExtensions != nullptr)
    {
        for (size_t extensionIndex = 0; extensionIndex < desc.numInstanceExtensions; ++extensionIndex)
        {
            const auto extension = extensionStringMap.find(desc.instanceExtensions[extensionIndex]);
            if (extension != extensionStringMap.end())
            {
                *extension->second = true;
            }
        }
    }

    if (desc.deviceExtensions != nullptr)
    {
        for (size_t extensionIndex = 0; extensionIndex < desc.numDeviceExtensions; ++extensionIndex)
        {
            const auto extension = extensionStringMap.find(desc.deviceExtensions[extensionIndex]);
            if (extension != extensionStringMap.end())
            {
                *extension->second = true;
            }
        }
    }

    if (desc.bufferDeviceAddressSupported)
    {
        mContext.extensions.buffer_device_address = true;
    }

    void* next = nullptr;
    vk::PhysicalDeviceAccelerationStructurePropertiesKHR accelStructProperties;
    vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties;
    vk::PhysicalDeviceConservativeRasterizationPropertiesEXT conservativeRasterizationProperties;
    vk::PhysicalDeviceFragmentShadingRatePropertiesKHR shadingRateProperties;
    vk::PhysicalDeviceOpacityMicromapPropertiesEXT opacityMicromapProperties;
#if HRHI_VULKAN_NV
    vk::PhysicalDeviceRayTracingInvocationReorderPropertiesNV nvRayTracingInvocationReorderProperties;
    vk::PhysicalDeviceClusterAccelerationStructurePropertiesNV nvClusterAccelerationStructureProperties;
    vk::PhysicalDeviceCooperativeVectorPropertiesNV coopVecProperties;
#endif
    vk::PhysicalDeviceSubgroupProperties subgroupProperties;

    vk::PhysicalDeviceProperties2 deviceProperties2;

    subgroupProperties.pNext = next;
    next = &subgroupProperties;

    if (mContext.extensions.KHR_acceleration_structure)
    {
        accelStructProperties.pNext = next;
        next = &accelStructProperties;
    }

    if (mContext.extensions.KHR_ray_tracing_pipeline)
    {
        rayTracingPipelineProperties.pNext = next;
        next = &rayTracingPipelineProperties;
    }

    if (mContext.extensions.KHR_fragment_shading_rate)
    {
        shadingRateProperties.pNext = next;
        next = &shadingRateProperties;
    }

    if (mContext.extensions.EXT_conservative_rasterization)
    {
        conservativeRasterizationProperties.pNext = next;
        next = &conservativeRasterizationProperties;
    }

    if (mContext.extensions.EXT_opacity_micromap)
    {
        opacityMicromapProperties.pNext = next;
        next = &opacityMicromapProperties;
    }

#if HRHI_VULKAN_NV
    if (mContext.extensions.NV_ray_tracing_invocation_reorder)
    {
        nvRayTracingInvocationReorderProperties.pNext = next;
        next = &nvRayTracingInvocationReorderProperties;
    }

    if (mContext.extensions.NV_cluster_acceleration_structure)
    {
        nvClusterAccelerationStructureProperties.pNext = next;
        next = &nvClusterAccelerationStructureProperties;
    }

    if (mContext.extensions.NV_cooperative_vector)
    {
        coopVecProperties.pNext = next;
        next = &coopVecProperties;
    }
#endif

    deviceProperties2.pNext = next;

    mContext.physicalDevice.getProperties2(&deviceProperties2);

    mContext.physicalDeviceProperties = deviceProperties2.properties;
    mContext.accelStructProperties = accelStructProperties;
    mContext.rayTracingPipelineProperties = rayTracingPipelineProperties;
    mContext.conservativeRasterizationProperties = conservativeRasterizationProperties;
    mContext.shadingRateProperties = shadingRateProperties;
    mContext.opacityMicromapProperties = opacityMicromapProperties;
#if HRHI_VULKAN_NV
    mContext.nvRayTracingInvocationReorderProperties = nvRayTracingInvocationReorderProperties;
    mContext.nvClusterAccelerationStructureProperties = nvClusterAccelerationStructureProperties;
    mContext.coopVecProperties = coopVecProperties;
#endif
    mContext.subgroupProperties = subgroupProperties;
    mContext.messageCallback = desc.errorCB;
    mContext.logBufferLifetime = desc.logBufferLifetime;

    if (mContext.extensions.KHR_fragment_shading_rate)
    {
        vk::PhysicalDeviceFeatures2 deviceFeatures2;
        deviceFeatures2.setPNext(&mContext.shadingRateFeatures);
        mContext.physicalDevice.getFeatures2(&deviceFeatures2);
    }

#if HRHI_VULKAN_NV
    if (mContext.extensions.NV_cooperative_vector)
    {
        vk::PhysicalDeviceFeatures2 deviceFeatures2;
        deviceFeatures2.setPNext(&mContext.coopVecFeatures);
        mContext.physicalDevice.getFeatures2(&deviceFeatures2);
    }

    if (mContext.extensions.NV_ray_tracing_linear_swept_spheres)
    {
        vk::PhysicalDeviceFeatures2 deviceFeatures2;
        deviceFeatures2.setPNext(&mContext.linearSweptSpheresFeatures);
        mContext.physicalDevice.getFeatures2(&deviceFeatures2);
    }
#endif

#if HRHI_WITH_RTXMU
    if (mContext.extensions.KHR_acceleration_structure)
    {
        mContext.rtxMemUtil = std::make_unique<rtxmu::VkAccelStructManager>(
            desc.instance,
            desc.device,
            desc.physicalDevice);
        mContext.rtxMemUtil->Initialize(8 * 1024 * 1024);
        mContext.rtxMuResources = std::make_unique<ZWVKRtxMuResources>();
    }

    if (mContext.extensions.EXT_opacity_micromap)
    {
        mContext.Warning("Opacity micromaps are not currently supported with RTXMU enabled.");
    }
#endif

    const vk::PipelineCacheCreateInfo pipelineCacheInfo;
    vk::Result result = mContext.device.createPipelineCache(
        &pipelineCacheInfo,
        mContext.allocationCallbacks,
        &mContext.pipelineCache);
    if (result != vk::Result::eSuccess)
    {
        mContext.Error("Failed to create the Vulkan pipeline cache.");
    }

    const vk::DescriptorSetLayoutCreateInfo emptyDescriptorSetLayoutInfo =
        vk::DescriptorSetLayoutCreateInfo()
            .setBindingCount(0)
            .setPBindings(nullptr);
    result = mContext.device.createDescriptorSetLayout(
        &emptyDescriptorSetLayoutInfo,
        mContext.allocationCallbacks,
        &mContext.emptyDescriptorSetLayout);
    if (result != vk::Result::eSuccess)
    {
        mContext.Error("Failed to create the empty Vulkan descriptor set layout.");
    }

#if HRHI_WITH_AFTERMATH
    mAftermathEnabled =
        desc.aftermathEnabled
        && mContext.extensions.NV_device_diagnostic_checkpoints
        && mContext.extensions.NV_device_diagnostics_config
        && HApp::ZWAftermathRuntime::Get().InitializeVulkanDevice(&mAftermathCrashDumpHelper, mContext.messageCallback);

    if (desc.aftermathEnabled)
    {
        if (mAftermathEnabled)
        {
            mContext.Info("Vulkan Aftermath is active for this device.");
        }
        else if (!mContext.extensions.NV_device_diagnostic_checkpoints || !mContext.extensions.NV_device_diagnostics_config)
        {
            mContext.Warning("Vulkan Aftermath was requested, but the required Vulkan diagnostics extensions were not enabled on the native device.");
        }
        else
        {
            mContext.Warning("Vulkan Aftermath was requested, but runtime initialization did not complete.");
        }
    }
#else
    mAftermathEnabled = false;

    if (desc.aftermathEnabled)
    {
        mContext.Info("Vulkan Aftermath was requested, but this build was compiled without Nsight Aftermath SDK headers.");
    }
#endif
}

ZWVKDevice::~ZWVKDevice()
{
    if (mAftermathEnabled)
    {
        HApp::ZWAftermathRuntime::Get().ClearActiveHelper(&mAftermathCrashDumpHelper);
    }

    if (mTimerQueryPool)
    {
        mContext.device.destroyQueryPool(mTimerQueryPool);
        mTimerQueryPool = vk::QueryPool();
    }

    if (mContext.pipelineCache)
    {
        mContext.device.destroyPipelineCache(mContext.pipelineCache);
        mContext.pipelineCache = vk::PipelineCache();
    }

    if (mContext.emptyDescriptorSetLayout)
    {
        mContext.device.destroyDescriptorSetLayout(mContext.emptyDescriptorSetLayout);
        mContext.emptyDescriptorSetLayout = vk::DescriptorSetLayout();
    }
}

HCommon::ZWObject ZWVKDevice::GetNativeObject(ObjectType objectType)
{
    switch (objectType)
    {
    case HRHIObjectTypes::gVKDevice:
        return HCommon::ZWObject(VkDevice(mContext.device));

    case HRHIObjectTypes::gVKPhysicalDevice:
        return HCommon::ZWObject(VkPhysicalDevice(mContext.physicalDevice));

    case HRHIObjectTypes::gVKInstance:
        return HCommon::ZWObject(VkInstance(mContext.instance));

    case HVulkanObjectTypes::Nvrhi_VK_Device:
        return HCommon::ZWObject(this);

    default:
        return nullptr;
    }
}

EGraphicsAPI ZWVKDevice::GetGraphicsAPI()
{
    return EGraphicsAPI::VULKAN;
}

bool ZWVKDevice::WaitForIdle()
{
    try
    {
        mContext.device.waitIdle();
    }
    catch (const vk::DeviceLostError&)
    {
        if (mAftermathEnabled)
        {
            HApp::ZWAftermathRuntime::Get().WaitForCrashDump(3000, mContext.messageCallback);
        }

        mContext.Error("Vulkan device lost while waiting for the device to become idle.");
        return false;
    }

    return true;
}

void ZWVKDevice::RunGarbageCollection()
{
    for (const std::unique_ptr<ZWVKQueue>& queue : mQueues)
    {
        if (queue != nullptr)
        {
            queue->RetireCommandBuffers();
        }
    }
}

bool ZWVKDevice::QueryFeatureSupport(EFeature feature, void* pInfo, size_t infoSize)
{
    switch (feature)
    {
    case EFeature::DeferredCommandLists:
        return true;

    case EFeature::RayTracingAccelStruct:
        return mContext.extensions.KHR_acceleration_structure;

    case EFeature::RayTracingPipeline:
        return mContext.extensions.KHR_ray_tracing_pipeline;

    case EFeature::RayTracingOpacityMicromap:
#if HRHI_WITH_RTXMU
        return false;
#else
        return mContext.extensions.EXT_opacity_micromap;
#endif

    case EFeature::RayQuery:
        return mContext.extensions.KHR_ray_query;

    case EFeature::ShaderExecutionReordering:
#if HRHI_VULKAN_NV
        if (mContext.extensions.NV_ray_tracing_invocation_reorder)
        {
            return vk::RayTracingInvocationReorderModeNV::eReorder ==
                mContext.nvRayTracingInvocationReorderProperties.rayTracingInvocationReorderReorderingHint;
        }
#endif
        return false;

    case EFeature::RayTracingClusters:
#if HRHI_VULKAN_NV
        return mContext.extensions.NV_cluster_acceleration_structure;
#else
        return false;
#endif

    case EFeature::ShaderSpecializations:
        return true;

    case EFeature::Meshlets:
        return mContext.extensions.EXT_mesh_shader;

    case EFeature::VariableRateShading:
        if (pInfo != nullptr && infoSize == sizeof(ZWVariableRateShadingFeatureInfo))
        {
            auto* variableRateShadingInfo = static_cast<ZWVariableRateShadingFeatureInfo*>(pInfo);
            const vk::Extent2D& tileExtent = mContext.shadingRateProperties.minFragmentShadingRateAttachmentTexelSize;
            variableRateShadingInfo->shadingRateImageTileSize = std::max(tileExtent.width, tileExtent.height);
        }
        else if (pInfo != nullptr && infoSize != 0)
        {
            mContext.Warning("Unexpected payload size passed to Vulkan VariableRateShading feature query.");
        }
        return mContext.extensions.KHR_fragment_shading_rate &&
            mContext.shadingRateFeatures.attachmentFragmentShadingRate;

    case EFeature::ConservativeRasterization:
        return mContext.extensions.EXT_conservative_rasterization;

    case EFeature::VirtualResources:
        return true;

    case EFeature::ComputeQueue:
        return mQueues[uint32_t(ECommandQueue::Compute)] != nullptr;

    case EFeature::CopyQueue:
        return mQueues[uint32_t(ECommandQueue::Copy)] != nullptr;

    case EFeature::ConstantBufferRanges:
        return true;

    case EFeature::WaveLaneCountMinMax:
        if (mContext.subgroupProperties.subgroupSize == 0)
        {
            return false;
        }

        if (pInfo != nullptr && infoSize == sizeof(ZWWaveLaneCountMinMaxFeatureInfo))
        {
            auto* waveLaneInfo = static_cast<ZWWaveLaneCountMinMaxFeatureInfo*>(pInfo);
            waveLaneInfo->minWaveLaneCount = mContext.subgroupProperties.subgroupSize;
            waveLaneInfo->maxWaveLaneCount = mContext.subgroupProperties.subgroupSize;
        }
        else if (pInfo != nullptr && infoSize != 0)
        {
            mContext.Warning("Unexpected payload size passed to Vulkan WaveLaneCountMinMax feature query.");
        }
        return true;

    case EFeature::HeapDirectlyIndexed:
        return mContext.extensions.EXT_mutable_descriptor_type;

    case EFeature::CooperativeVectorInferencing:
#if HRHI_VULKAN_NV
        return mContext.extensions.NV_cooperative_vector && mContext.coopVecFeatures.cooperativeVector;
#else
        return false;
#endif

    case EFeature::CooperativeVectorTraining:
#if HRHI_VULKAN_NV
        return mContext.extensions.NV_cooperative_vector && mContext.coopVecFeatures.cooperativeVectorTraining;
#else
        return false;
#endif

    case EFeature::Spheres:
#if HRHI_VULKAN_NV
        return mContext.extensions.NV_ray_tracing_linear_swept_spheres && mContext.linearSweptSpheresFeatures.spheres;
#else
        return false;
#endif

    case EFeature::LinearSweptSpheres:
#if HRHI_VULKAN_NV
        return mContext.extensions.NV_ray_tracing_linear_swept_spheres &&
            mContext.linearSweptSpheresFeatures.linearSweptSpheres;
#else
        return false;
#endif

    case EFeature::FastGeometryShader:
    case EFeature::HlslExtensionUAV:
    case EFeature::SamplerFeedback:
    case EFeature::SinglePassStereo:
        return false;

    default:
        return false;
    }
}

EFormatSupport ZWVKDevice::QueryFormatSupport(EFormat format)
{
    const VkFormat vulkanFormat = HVulkan::ConvertFormat(format);
    if (vulkanFormat == VK_FORMAT_UNDEFINED)
    {
        return EFormatSupport::None;
    }

    vk::FormatProperties formatProperties;
    mContext.physicalDevice.getFormatProperties(vk::Format(vulkanFormat), &formatProperties);

    EFormatSupport result = EFormatSupport::None;

    if (formatProperties.bufferFeatures)
    {
        result = result | EFormatSupport::Buffer;
    }

    if (format == EFormat::R32_UINT || format == EFormat::R16_UINT)
    {
        result = result | EFormatSupport::IndexBuffer;
    }

    if ((formatProperties.bufferFeatures & vk::FormatFeatureFlagBits::eVertexBuffer) != vk::FormatFeatureFlags())
    {
        result = result | EFormatSupport::VertexBuffer;
    }

    if (formatProperties.optimalTilingFeatures)
    {
        result = result | EFormatSupport::Texture;
    }

    if ((formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) != vk::FormatFeatureFlags())
    {
        result = result | EFormatSupport::DepthStencil;
    }

    if ((formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eColorAttachment) != vk::FormatFeatureFlags())
    {
        result = result | EFormatSupport::RenderTarget;
    }

    if ((formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eColorAttachmentBlend) != vk::FormatFeatureFlags())
    {
        result = result | EFormatSupport::Blendable;
    }

    if ((formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage) != vk::FormatFeatureFlags() ||
        (formatProperties.bufferFeatures & vk::FormatFeatureFlagBits::eUniformTexelBuffer) != vk::FormatFeatureFlags())
    {
        result = result | EFormatSupport::ShaderLoad;
    }

    if ((formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear) != vk::FormatFeatureFlags())
    {
        result = result | EFormatSupport::ShaderSample;
    }

    if ((formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage) != vk::FormatFeatureFlags() ||
        (formatProperties.bufferFeatures & vk::FormatFeatureFlagBits::eStorageTexelBuffer) != vk::FormatFeatureFlags())
    {
        result = result | EFormatSupport::ShaderUavLoad;
        result = result | EFormatSupport::ShaderUavStore;
    }

    if ((formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImageAtomic) != vk::FormatFeatureFlags() ||
        (formatProperties.bufferFeatures & vk::FormatFeatureFlagBits::eStorageTexelBufferAtomic) != vk::FormatFeatureFlags())
    {
        result = result | EFormatSupport::ShaderAtomic;
    }

    return result;
}

HCoopVec::ZWDeviceFeatures ZWVKDevice::QueryCoopVecFeatures()
{
    HCoopVec::ZWDeviceFeatures result;

#if HRHI_VULKAN_NV
    if (!mContext.extensions.NV_cooperative_vector)
    {
        return result;
    }

    uint32_t propertyCount = 0;
    if (mContext.physicalDevice.getCooperativeVectorPropertiesNV(&propertyCount, nullptr) != vk::Result::eSuccess ||
        propertyCount == 0)
    {
        return result;
    }

    std::vector<vk::CooperativeVectorPropertiesNV> properties(propertyCount);
    if (mContext.physicalDevice.getCooperativeVectorPropertiesNV(&propertyCount, properties.data()) != vk::Result::eSuccess)
    {
        return result;
    }

    result.matMulFormats.reserve(propertyCount);
    for (const vk::CooperativeVectorPropertiesNV& property : properties)
    {
        HCoopVec::ZWMatMulFormatCombo& combo = result.matMulFormats.emplace_back();
        combo.inputType = ConvertCoopVecDataType(property.inputType);
        combo.inputInterpretation = ConvertCoopVecDataType(property.inputInterpretation);
        combo.matrixInterpretation = ConvertCoopVecDataType(property.matrixInterpretation);
        combo.biasInterpretation = ConvertCoopVecDataType(property.biasInterpretation);
        combo.outputType = ConvertCoopVecDataType(property.resultType);
        combo.transposeSupported = property.transpose != 0;
    }

    result.trainingFloat16 = mContext.coopVecProperties.cooperativeVectorTrainingFloat16Accumulation;
    result.trainingFloat32 = mContext.coopVecProperties.cooperativeVectorTrainingFloat32Accumulation;
#endif

    return result;
}

size_t ZWVKDevice::GetCoopVecMatrixSize(HCoopVec::EDataType type, HCoopVec::EMatrixLayout layout, int rows, int columns)
{
#if HRHI_VULKAN_NV
    if (!mContext.extensions.NV_cooperative_vector)
    {
        return 0;
    }

    size_t destinationSize = 0;
    const size_t elementSize = HCoopVec::getDataTypeSize(type);

    vk::ConvertCooperativeVectorMatrixInfoNV convertInfo = {};
    convertInfo.sType = vk::StructureType::eConvertCooperativeVectorMatrixInfoNV;
    convertInfo.srcSize = elementSize * static_cast<size_t>(rows) * static_cast<size_t>(columns);
    convertInfo.srcData.hostAddress = nullptr;
    convertInfo.pDstSize = &destinationSize;
    convertInfo.dstData.hostAddress = nullptr;
    convertInfo.srcComponentType = vk::ComponentTypeKHR(ConvertCoopVecDataType(type));
    convertInfo.dstComponentType = convertInfo.srcComponentType;
    convertInfo.numRows = rows;
    convertInfo.numColumns = columns;
    convertInfo.srcLayout = vk::CooperativeVectorMatrixLayoutNV::eRowMajor;
    convertInfo.srcStride = elementSize * static_cast<size_t>(columns);
    convertInfo.dstLayout = ConvertCoopVecMatrixLayout(layout);
    convertInfo.dstStride = HCoopVec::getOptimalMatrixStride(type, layout, rows, columns);

    if (mContext.device.convertCooperativeVectorMatrixNV(&convertInfo) == vk::Result::eSuccess)
    {
        return destinationSize;
    }
#else
    (void)type;
    (void)layout;
    (void)rows;
    (void)columns;
#endif

    return 0;
}

HCommon::ZWObject ZWVKDevice::GetNativeQueue(ObjectType objectType, ECommandQueue queue)
{
    if (objectType != HRHIObjectTypes::gVKQueue || queue >= ECommandQueue::Count)
    {
        return nullptr;
    }

    ZWVKQueue* vkQueue = GetQueue(queue);
    return vkQueue != nullptr ? HCommon::ZWObject(VkQueue(vkQueue->GetVkQueue())) : HCommon::ZWObject(nullptr);
}

ZWCommandListHandle ZWVKDevice::CreateCommandList(const ZWCommandListParameters& params)
{
    if (GetQueue(params.queueType) == nullptr)
    {
        return nullptr;
    }

    return ZWCommandListHandle::Create(new ZWVKCommandList(this, mContext, params));
}

uint64_t ZWVKDevice::ExecuteCommandLists(ICommandList* const* pCommandLists, size_t numCommandLists, ECommandQueue executionQueue)
{
    if (pCommandLists == nullptr || numCommandLists == 0)
    {
        return 0;
    }

    ZWVKQueue* queue = GetQueue(executionQueue);
    if (queue == nullptr)
    {
        mContext.Error("Failed to execute Vulkan command lists because the queue is unavailable.");
        return 0;
    }

    const uint64_t submissionID = queue->Submit(pCommandLists, numCommandLists);
    for (size_t commandListIndex = 0; commandListIndex < numCommandLists; ++commandListIndex)
    {
        ZWVKCommandList* commandList = static_cast<ZWVKCommandList*>(pCommandLists[commandListIndex]);
        if (commandList != nullptr)
        {
            commandList->Executed(*queue, submissionID);
        }
    }

    return submissionID;
}

void ZWVKDevice::GetTextureTiling(
    ITexture* texture,
    uint32_t* numTiles,
    ZWPackedMipDesc* packedMipDesc,
    ZWTileShape* tileShape,
    uint32_t* subresourceTilingsNum,
    ZWSubresourceTiling* subresourceTilings)
{
    ZWVKTexture* vkTexture = static_cast<ZWVKTexture*>(texture);
    if (vkTexture == nullptr)
    {
        return;
    }

    uint32_t numStandardMips = 0;
    uint32_t tileWidth = 1;
    uint32_t tileHeight = 1;
    uint32_t tileDepth = 1;

    const std::vector<vk::SparseImageMemoryRequirements> sparseMemoryRequirements =
        mContext.device.getImageSparseMemoryRequirements(vkTexture->image);
    if (!sparseMemoryRequirements.empty())
    {
        numStandardMips = sparseMemoryRequirements[0].imageMipTailFirstLod;

        if (packedMipDesc != nullptr)
        {
            packedMipDesc->numStandardMips = numStandardMips;
            packedMipDesc->numPackedMips = vkTexture->imageInfo.mipLevels - sparseMemoryRequirements[0].imageMipTailFirstLod;
            packedMipDesc->startTileIndexInOverallResource =
                static_cast<uint32_t>(sparseMemoryRequirements[0].imageMipTailOffset / ZWVKTexture::tileByteSize);
            packedMipDesc->numTilesForPackedMips =
                static_cast<uint32_t>(sparseMemoryRequirements[0].imageMipTailSize / ZWVKTexture::tileByteSize);
        }
    }

    const std::vector<vk::SparseImageFormatProperties> formatProperties =
        mContext.physicalDevice.getSparseImageFormatProperties(
            vkTexture->imageInfo.format,
            vkTexture->imageInfo.imageType,
            vkTexture->imageInfo.samples,
            vkTexture->imageInfo.usage,
            vkTexture->imageInfo.tiling);
    if (!formatProperties.empty())
    {
        tileWidth = formatProperties[0].imageGranularity.width;
        tileHeight = formatProperties[0].imageGranularity.height;
        tileDepth = formatProperties[0].imageGranularity.depth;
    }

    if (tileShape != nullptr)
    {
        tileShape->widthInTexels = tileWidth;
        tileShape->heightInTexels = tileHeight;
        tileShape->depthInTexels = tileDepth;
    }

    if (subresourceTilingsNum != nullptr && subresourceTilings != nullptr)
    {
        *subresourceTilingsNum = std::min(*subresourceTilingsNum, vkTexture->desc.mipLevels);

        uint32_t startTileIndexInOverallResource = 0;
        uint32_t width = vkTexture->desc.width;
        uint32_t height = vkTexture->desc.height;
        uint32_t depth = vkTexture->desc.depth;

        for (uint32_t mipLevel = 0; mipLevel < *subresourceTilingsNum; ++mipLevel)
        {
            if (mipLevel < numStandardMips)
            {
                subresourceTilings[mipLevel].widthInTiles = (width + tileWidth - 1) / tileWidth;
                subresourceTilings[mipLevel].heightInTiles = (height + tileHeight - 1) / tileHeight;
                subresourceTilings[mipLevel].depthInTiles = (depth + tileDepth - 1) / tileDepth;
                subresourceTilings[mipLevel].startTileIndexInOverallResource = startTileIndexInOverallResource;
            }
            else
            {
                subresourceTilings[mipLevel].widthInTiles = 0;
                subresourceTilings[mipLevel].heightInTiles = 0;
                subresourceTilings[mipLevel].depthInTiles = 0;
                subresourceTilings[mipLevel].startTileIndexInOverallResource = UINT32_MAX;
            }

            width = std::max(width / 2, tileWidth);
            height = std::max(height / 2, tileHeight);
            depth = std::max(depth / 2, tileDepth);

            startTileIndexInOverallResource +=
                subresourceTilings[mipLevel].widthInTiles *
                subresourceTilings[mipLevel].heightInTiles *
                subresourceTilings[mipLevel].depthInTiles;
        }
    }

    if (numTiles != nullptr)
    {
        const vk::MemoryRequirements imageMemoryRequirements = mContext.device.getImageMemoryRequirements(vkTexture->image);
        *numTiles = static_cast<uint32_t>(imageMemoryRequirements.size / ZWVKTexture::tileByteSize);
    }
}

ZWSamplerFeedbackTextureHandle ZWVKDevice::CreateSamplerFeedbackTexture(ITexture* pairedTexture, const ZWSamplerFeedbackTextureDesc& desc)
{
    (void)pairedTexture;
    (void)desc;

    mContext.Warning("Sampler feedback textures are not currently supported by the Vulkan backend.");
    return nullptr;
}

ZWSamplerFeedbackTextureHandle ZWVKDevice::CreateSamplerFeedbackForNativeTexture(ObjectType objectType, HCommon::ZWObject texture, ITexture* pairedTexture)
{
    (void)objectType;
    (void)texture;
    (void)pairedTexture;

    mContext.Warning("Wrapping native sampler feedback textures is not currently supported by the Vulkan backend.");
    return nullptr;
}

ZWHeapHandle ZWVKDevice::CreateHeap(const ZWHeapDesc& desc)
{
    vk::MemoryRequirements memoryRequirements = {};
    memoryRequirements.alignment = 0;
    memoryRequirements.memoryTypeBits = ~0u;
    memoryRequirements.size = desc.capacity;

    vk::MemoryPropertyFlags memoryPropertyFlags;
    switch (desc.type)
    {
    case EHeapType::DeviceLocal:
        memoryPropertyFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
        break;

    case EHeapType::Upload:
        memoryPropertyFlags = vk::MemoryPropertyFlagBits::eHostVisible;
        break;

    case EHeapType::Readback:
        memoryPropertyFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached;
        break;

    default:
        mContext.Error("Invalid Vulkan heap type.");
        return nullptr;
    }

    ZWVKHeap* heap = new ZWVKHeap(mAllocator);
    heap->desc = desc;
    heap->managed = true;

    const bool enableDeviceAddress = mContext.extensions.buffer_device_address;
    const vk::Result result = mAllocator.AllocateMemory(heap, memoryRequirements, memoryPropertyFlags, enableDeviceAddress);
    if (result != vk::Result::eSuccess)
    {
        std::ostringstream messageBuilder;
        messageBuilder << "Failed to allocate memory for Vulkan heap "
            << HApp::DebugNameToString(desc.debugName)
            << ", " << FormatVkResult(result) << ".";
        mContext.Error(messageBuilder.str());

        delete heap;
        return nullptr;
    }

    if (!desc.debugName.empty())
    {
        mContext.NameVKObject(
            heap->memory,
            vk::ObjectType::eDeviceMemory,
            vk::DebugReportObjectTypeEXT::eDeviceMemory,
            desc.debugName.c_str());
    }

    return ZWHeapHandle::Create(heap);
}

ZWVKHeap::~ZWVKHeap()
{
    if (memory && managed)
    {
        mAllocator.FreeMemory(this);
        memory = vk::DeviceMemory();
    }
}

void ZWVKContext::NameVKObject(
    const void* handle,
    const vk::ObjectType objectType,
    const vk::DebugReportObjectTypeEXT debugReportObjectType,
    const char* name) const
{
    if (handle == nullptr || name == nullptr || *name == '\0')
    {
        return;
    }

    if (extensions.EXT_debug_utils)
    {
        const vk::DebugUtilsObjectNameInfoEXT info =
            vk::DebugUtilsObjectNameInfoEXT()
                .setObjectType(objectType)
                .setObjectHandle(reinterpret_cast<uint64_t>(handle))
                .setPObjectName(name);
        device.setDebugUtilsObjectNameEXT(info);
    }
    else if (extensions.EXT_debug_marker)
    {
        const vk::DebugMarkerObjectNameInfoEXT info =
            vk::DebugMarkerObjectNameInfoEXT()
                .setObjectType(debugReportObjectType)
                .setObject(reinterpret_cast<uint64_t>(handle))
                .setPObjectName(name);
        (void)device.debugMarkerSetObjectNameEXT(&info);
    }
}

void ZWVKContext::Error(const std::string& message) const
{
    DispatchMessage(messageCallback, EMessageSeverity::Error, message);
}

void ZWVKContext::Warning(const std::string& message) const
{
    DispatchMessage(messageCallback, EMessageSeverity::Warning, message);
}

void ZWVKContext::Info(const std::string& message) const
{
    DispatchMessage(messageCallback, EMessageSeverity::Info, message);
}

}
