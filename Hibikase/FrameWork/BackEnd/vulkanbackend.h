#pragma once

// 暂时写死，之后该宏会设置为cmake的一个选项，由用户决定（根据自己的显卡是否为N卡）是否开启
#define HRHI_VULKAN_NV 1

#include <BackEnd/vulkanunique.h>
#include <Common/allocators.h>
#include <Utils/aftermathtraker.h>
#include <BackEnd/statetracking.h>
#include <BackEnd/versioning.h>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#if HRHI_WITH_RTXMU
#include <rtxmu/VkAccelStructManager.h>
#endif

#include <mutex>
#include <list>
#include <unordered_map>
#include <vector>

#if (VK_HEADER_VERSION < 318)
#error "Vulkan SDK version 1.4.318 or later is required to compile HRHI"
#endif

#define CHECK_VK_RETURN(res) if ((res) != vk::Result::eSuccess) { return res; }
#define CHECK_VK_FAIL(res) if ((res) != vk::Result::eSuccess) { return nullptr; }
#if _DEBUG
#define ASSERT_VK_OK(res) assert((res) == vk::Result::eSuccess)
#else // _DEBUG
#define ASSERT_VK_OK(res) do {(void)(res);} while(0)
#endif // _DEBUG


namespace HApp
{

class ZWWindow;
class ZWWindowManager;

}

namespace HRHI
{

    class ZWVKTexture;
    class ZWVKStagingTexture;
    class ZWVKInputLayout;
    class ZWVKBuffer;
    class ZWVKShader;
    class ZWVKSampler;
    class ZWVKFramebuffer;
    class ZWVKGraphicsPipeline;
    class ZWVKComputePipeline;
    class ZWVKBindingSet;
    class ZWVKEventQuery;
    class ZWVKTimerQuery;
    class ZWVKMarker;
    class ZWVKDevice;

    VkFormat ConvertFormat(EFormat format);
    struct ZWVKResourceStateMapping
    {
        EResourceStates hrhiState;
        vk::PipelineStageFlags2 stageFlags;
        vk::AccessFlags2 accessMask;
        vk::ImageLayout imageLayout;
    };

    vk::SamplerAddressMode ConvertSamplerAddressMode(ESamplerAddressMode mode);
    vk::PipelineStageFlagBits2 ConvertShaderTypeToPipelineStageFlagBits(EShaderType shaderType);
    vk::ShaderStageFlagBits ConvertShaderTypeToShaderStageFlagBits(EShaderType shaderType);
    ZWVKResourceStateMapping ConvertResourceState(EResourceStates state, bool isImage);
    vk::PrimitiveTopology ConvertPrimitiveTopology(EPrimitiveType topology);
    vk::PolygonMode ConvertFillMode(ERasterFillMode mode);
    vk::CullModeFlagBits ConvertCullMode(ERasterCullMode mode);
    vk::CompareOp ConvertCompareOp(EComparisonFunc op);
    vk::StencilOp ConvertStencilOp(EStencilOp op);
    vk::StencilOpState ConvertStencilState(const ZWDepthStencilState& depthStencilState, const ZWDepthStencilState::ZWStencilOpDesc& desc);
    vk::BlendFactor ConvertBlendValue(EBlendFactor value);
    vk::BlendOp ConvertBlendOp(EBlendOp op);
    vk::ColorComponentFlags ConvertColorMask(EColorMask mask);
    vk::PipelineColorBlendAttachmentState ConvertBlendState(const ZWBlendState::ZWRenderTarget& state);
    vk::BuildAccelerationStructureFlagsKHR ConvertAccelStructBuildFlags(Hrt::EAccelStructBuildFlags buildFlags);
    vk::GeometryInstanceFlagsKHR ConvertInstanceFlags(Hrt::EInstanceFlags instanceFlags);
    vk::Extent2D ConvertFragmentShadingRate(EVariableShadingRate shadingRate);
    vk::FragmentShadingRateCombinerOpKHR ConvertShadingRateCombiner(EShadingRateCombiner combiner);
    vk::DescriptorType ConvertResourceType(EResourceType type);
    vk::ComponentTypeKHR ConvertCoopVecDataType(HCoopVec::EDataType type);
    HCoopVec::EDataType ConvertCoopVecDataType(vk::ComponentTypeKHR type);
#if HRHI_VULKAN_NV
    vk::CooperativeVectorMatrixLayoutNV ConvertCoopVecMatrixLayout(HCoopVec::EMatrixLayout layout);
#endif

    void CountSpecializationConstants(
        ZWVKShader* shader,
        size_t& numShaders,
        size_t& numShadersWithSpecializations,
        size_t& numSpecializationConstants);

    vk::PipelineShaderStageCreateInfo MakeShaderStageCreateInfo(
        ZWVKShader* shader,
        std::vector<vk::SpecializationInfo>& specInfos,
        std::vector<vk::SpecializationMapEntry>& specMapEntries,
        std::vector<uint32_t>& specData);

#if HRHI_WITH_RTXMU
    struct ZWVKRtxMuResources
    {
        std::vector<uint64_t> asBuildsCompleted;
        std::mutex asListMutex;
    };
#endif

    // underlying vulkan context
    struct ZWVKContext
    {
        ZWVKContext(vk::Instance instance,
            vk::PhysicalDevice physicalDevice,
            vk::Device device,
            vk::AllocationCallbacks* allocationCallbacks = nullptr)
            : instance(instance)
            , physicalDevice(physicalDevice)
            , device(device)
            , allocationCallbacks(allocationCallbacks)
            , pipelineCache(nullptr)
        {
        }

        vk::Instance instance;
        vk::PhysicalDevice physicalDevice;
        vk::Device device;
        vk::AllocationCallbacks* allocationCallbacks;
        vk::PipelineCache pipelineCache;

        struct {
            bool EXT_debug_report = false;
            bool EXT_debug_marker = false;
            bool KHR_acceleration_structure = false;
            bool buffer_device_address = false; // either KHR_ or Vulkan 1.2 versions
            bool KHR_ray_query = false;
            bool KHR_ray_tracing_pipeline = false;
            bool EXT_mesh_shader = false;
            bool KHR_fragment_shading_rate = false;
            bool EXT_conservative_rasterization = false;
            bool EXT_opacity_micromap = false;
            bool EXT_mutable_descriptor_type = false;
            bool EXT_debug_utils = false;

#if HRHI_VULKAN_NV
            bool NV_ray_tracing_invocation_reorder = false;
            bool NV_cluster_acceleration_structure = false;
            bool NV_cooperative_vector = false;
            bool NV_ray_tracing_linear_swept_spheres = false;

#if HRHI_WITH_AFTERMATH
            bool NV_device_diagnostic_checkpoints = false;
            bool NV_device_diagnostics_config = false;
#endif

#endif
        } extensions;

        vk::PhysicalDeviceProperties physicalDeviceProperties;
        vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties;
        vk::PhysicalDeviceAccelerationStructurePropertiesKHR accelStructProperties;
        vk::PhysicalDeviceConservativeRasterizationPropertiesEXT conservativeRasterizationProperties;
        vk::PhysicalDeviceFragmentShadingRatePropertiesKHR shadingRateProperties;
        vk::PhysicalDeviceOpacityMicromapPropertiesEXT opacityMicromapProperties;
        vk::PhysicalDeviceRayTracingInvocationReorderPropertiesNV nvRayTracingInvocationReorderProperties;
        vk::PhysicalDeviceClusterAccelerationStructurePropertiesNV nvClusterAccelerationStructureProperties;
        vk::PhysicalDeviceFragmentShadingRateFeaturesKHR shadingRateFeatures;
        vk::PhysicalDeviceCooperativeVectorFeaturesNV coopVecFeatures;
        vk::PhysicalDeviceCooperativeVectorPropertiesNV coopVecProperties;
        vk::PhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV linearSweptSpheresFeatures;
        vk::PhysicalDeviceSubgroupProperties subgroupProperties;
        IMessageCallback* messageCallback = nullptr;
        bool logBufferLifetime = false;
#if HRHI_WITH_RTXMU
        std::unique_ptr<rtxmu::VkAccelStructManager> rtxMemUtil;
        std::unique_ptr<ZWVKRtxMuResources> rtxMuResources;
#endif
        vk::DescriptorSetLayout emptyDescriptorSetLayout;

        void NameVKObject(const void* handle, const vk::ObjectType objtype,
            const vk::DebugReportObjectTypeEXT objtypeEXT, const char* name) const;
        void Error(const std::string& message) const;
        void Warning(const std::string& message) const;
        void Info(const std::string& message) const;
    };

    // command buffer with resource tracking
    class ZWVKTrackedCommandBuffer
    {
    public:

        // the command buffer itself
        vk::CommandBuffer cmdBuf = vk::CommandBuffer();
        vk::CommandPool cmdPool = vk::CommandPool();

        std::vector<HCommon::RefCountPtr<HCommon::IResource>> referencedResources; // to keep them alive
        std::vector<ZWBufferHandle> referencedStagingBuffers; // to allow synchronous mapBuffer

        uint64_t recordingID = 0;
        uint64_t submissionID = 0;

#if HRHI_WITH_RTXMU
        std::vector<uint64_t> rtxmuBuildIds;
        std::vector<uint64_t> rtxmuCompactionIds;
#endif

        explicit ZWVKTrackedCommandBuffer(const ZWVKContext& context)
            : mContext(context)
        {
        }

        ~ZWVKTrackedCommandBuffer();

    private:
        const ZWVKContext& mContext;
    };

    typedef std::shared_ptr<ZWVKTrackedCommandBuffer> ZWVKTrackedCommandBufferPtr;

    // represents a hardware queue
    class ZWVKQueue
    {
    public:
        vk::Semaphore trackingSemaphore;

        ZWVKQueue(const ZWVKContext& context, ECommandQueue queueID, vk::Queue queue, uint32_t queueFamilyIndex);
        ~ZWVKQueue();

        // creates a command buffer and its synchronization resources
        ZWVKTrackedCommandBufferPtr CreateCommandBuffer();

        ZWVKTrackedCommandBufferPtr GetOrCreateCommandBuffer();

        void AddWaitSemaphore(vk::Semaphore semaphore, uint64_t value);
        void AddSignalSemaphore(vk::Semaphore semaphore, uint64_t value);

        // submits a command buffer to this queue, returns submissionID
        uint64_t Submit(ICommandList* const* ppCmd, size_t numCmd);

        void UpdateTextureTileMappings(ITexture* texture, const ZWTextureTilesMapping* tileMappings, uint32_t numTileMappings);

        // retire any command buffers that have finished execution from the pending execution list
        void RetireCommandBuffers();

        ZWVKTrackedCommandBufferPtr GetCommandBufferInFlight(uint64_t submissionID);

        uint64_t UpdateLastFinishedID();
        uint64_t GetLastSubmittedID() const { return mLastSubmittedID; }
        uint64_t GetLastFinishedID() const { return mLastFinishedID; }
        ECommandQueue GetQueueID() const { return mQueueID; }
        vk::Queue GetVkQueue() const { return mQueue; }

        bool PollCommandList(uint64_t commandListID);
        bool WaitCommandList(uint64_t commandListID, uint64_t timeout);

    private:
        const ZWVKContext& mContext;

        vk::Queue mQueue;
        ECommandQueue mQueueID;
        uint32_t mQueueFamilyIndex = uint32_t(-1);

        std::mutex mMutex;
        std::vector<vk::Semaphore> mWaitSemaphores;
        std::vector<uint64_t> mWaitSemaphoreValues;
        std::vector<vk::Semaphore> mSignalSemaphores;
        std::vector<uint64_t> mSignalSemaphoreValues;

        uint64_t mLastRecordingID = 0;
        uint64_t mLastSubmittedID = 0;
        uint64_t mLastFinishedID = 0;

        // tracks the list of command buffers in flight on this queue
        std::list<ZWVKTrackedCommandBufferPtr> mCommandBuffersInFlight;
        std::list<ZWVKTrackedCommandBufferPtr> mCommandBuffersPool;
    };

    class ZWVKMemoryResource
    {
    public:
        bool managed = true;
        vk::DeviceMemory memory;
    };

    class ZWVKAllocator
    {
    public:
        explicit ZWVKAllocator(const ZWVKContext& context)
            : mContext(context)
        {
        }

        vk::Result AllocateBufferMemory(ZWVKBuffer* buffer, bool enableBufferAddress = false) const;
        void FreeBufferMemory(ZWVKBuffer* buffer) const;

        vk::Result AllocateTextureMemory(ZWVKTexture* texture) const;
        void FreeTextureMemory(ZWVKTexture* texture) const;

        vk::Result AllocateMemory(ZWVKMemoryResource* res,
            vk::MemoryRequirements memRequirements,
            vk::MemoryPropertyFlags memPropertyFlags,
            bool enableDeviceAddress = false,
            bool enableExportMemory = false,
            VkImage dedicatedImage = nullptr,
            VkBuffer dedicatedBuffer = nullptr) const;
        void FreeMemory(ZWVKMemoryResource* res) const;

    private:
        const ZWVKContext& mContext;
    };

    class ZWVKHeap : public ZWVKMemoryResource, public HCommon::RefCounter<IHeap>
    {
    public:
        explicit ZWVKHeap(ZWVKAllocator& allocator)
            : mAllocator(allocator)
        {
        }

        ~ZWVKHeap() override;

        ZWHeapDesc desc;

        const ZWHeapDesc& GetDesc() override { return desc; }

    private:
        ZWVKAllocator& mAllocator;
    };

    struct ZWVKTextureSubresourceView
    {
        ZWVKTexture& texture;
        ZWTextureSubresourceSet subresource;

        vk::ImageView view = nullptr;
        vk::ImageSubresourceRange subresourceRange;

        ZWVKTextureSubresourceView(ZWVKTexture& texture)
            : texture(texture)
        {
        }

        ZWVKTextureSubresourceView(const ZWVKTextureSubresourceView&) = delete;

        bool operator==(const ZWVKTextureSubresourceView& other) const
        {
            return &texture == &other.texture &&
                subresource == other.subresource &&
                view == other.view &&
                subresourceRange == other.subresourceRange;
        }
    };

    class ZWVKTexture : public ZWVKMemoryResource, public HCommon::RefCounter<ITexture>, public ZWTextureStateExtension
    {
    public:

        enum class ETextureSubresourceViewType // see GetSubresourceView()
        {
            AllAspects,
            DepthOnly,
            StencilOnly
        };

        typedef std::tuple<ZWTextureSubresourceSet, ETextureSubresourceViewType, ETextureDimension, EFormat, vk::ImageUsageFlags> ZWVKSubresourceViewKey;

        struct ZWVKHash
        {
            std::size_t operator()(ZWVKSubresourceViewKey const& s) const noexcept
            {
                const auto& [subresources, viewType, dimension, format, usage] = s;

                size_t hash = 0;

                hash_combine(hash, subresources.baseMipLevel);
                hash_combine(hash, subresources.numMipLevels);
                hash_combine(hash, subresources.baseArraySlice);
                hash_combine(hash, subresources.numArraySlices);
                hash_combine(hash, viewType);
                hash_combine(hash, dimension);
                hash_combine(hash, format);
                hash_combine(hash, uint32_t(usage));

                return hash;
            }
        };


        ZWTextureDesc desc;

        vk::ImageCreateInfo imageInfo;
        vk::ExternalMemoryImageCreateInfo externalMemoryImageInfo;
        vk::Image image;
        static constexpr uint32_t tileByteSize = 65536;

        ZWHeapHandle heap;

        void* sharedHandle = nullptr;

        // contains subresource views for this texture
        // note that we only create the views that the app uses, and that multiple views may map to the same subresources
        std::unordered_map<ZWVKSubresourceViewKey, ZWVKTextureSubresourceView, ZWVKTexture::ZWVKHash> subresourceViews;

        ZWVKTexture(const ZWVKContext& context, ZWVKAllocator& allocator)
            : ZWTextureStateExtension(desc)
            , mContext(context)
            , mAllocator(allocator)
        {
        }

        // returns a subresource view for an arbitrary range of mip levels and array layers.
        // 'viewtype' only matters when asking for a depth-stencil view; in situations where only depth or stencil can be bound
        // (such as an SRV with ImageLayout::eShaderReadOnlyOptimal), but not both, then this specifies which of the two aspect bits is to be set.
        ZWVKTextureSubresourceView& GetSubresourceView(const ZWTextureSubresourceSet& subresources, ETextureDimension dimension,
            EFormat format, vk::ImageUsageFlags usage, ETextureSubresourceViewType viewtype = ETextureSubresourceViewType::AllAspects);

        uint32_t GetNumSubresources() const;
        uint32_t GetSubresourceIndex(uint32_t mipLevel, uint32_t arrayLayer) const;

        ~ZWVKTexture() override;
        const ZWTextureDesc& GetDesc() const override { return desc; }
        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;
        HCommon::ZWObject GetNativeView(ObjectType objectType, EFormat format, ZWTextureSubresourceSet subresources, ETextureDimension dimension, bool isReadOnlyDSV = false) override;

    private:
        const ZWVKContext& mContext;
        ZWVKAllocator& mAllocator;
        std::mutex mMutex;
    };

    /* ----------------------------------------------------------------------------

    The volatile buffer implementation needs some explanation, might as well be here.

    The implementation is designed around a few constraints and assumptions:

    1.  Need to efficiently represent them with core Vulkan API with minimal overhead.
        This rules out a few options:

        - Can't use regular descriptors and update the references to each volatile CB
          in every descriptor set. That would require versioning of the descriptor
          sets and tracking of every use of volatile CBs.
        - Can't use push descriptors (vkCmdPushDescriptorSetKHR) because they are not
          in core Vulkan and are not supported by e.g. AMD drivers at this time. This
          rules out the DX12 style approach where an upload manager is assigned to a
          command list and creates buffers as needed - because then one volatile CB
          might be using different buffer objects for different versions.
        - Any other options that I missed?...

        The only option left is dynamic descriptors. You create a UBO descriptor that
        points to a buffer and then bind it with different offsets within that buffer.
        So all the versions of a volatile CB must live in the same buffer because the
        descriptor may be baked into multiple descriptor sets.

    2.  A volatile buffer may be written into from different command lists, potentially
        those which are recorded concurrently or out of order, and then executed on
        different queues.

        This requirement makes it impossible to put different versions of a CB into a
        single buffer in a round-robin fashion and track their completion with chunks.
        Tracking must be more fine-grained.

    3.  The version tracking implementation should be efficient, which means we shouldn't
        do things like allocating tracking objects for each version or pooling them
        for reuse, and keep iterating over many buffers or versions to a minimum.

    The system designed with these characteristics in mind is following.

    Every volatile buffer has a fixed maximum number of versions specified at creation,
    see BufferDesc::maxVersions. For a typical once-per-frame render pass, something
    like 3-4 versions should be sufficient. Iterative passes may need more, or should
    avoid using volatile CBs in that fashion and switch to push constants or maybe
    structured buffers.

    For each version of a buffer, a tracking object is stored in the Buffer::versionTracking
    array. The object is just a 64-bit word, which contains a bitfield:

        - c_VersionSubmittedFlag means that the version is used in a submitted
            command list;

        - (queue & c_VersionQueueMask << c_VersionQueueShift) is the queue index,
            see HRHI::ECommandQueue for values;

        - (id & c_VersionIDMask) is the instance ID of the command list, either
            pending or submitted. If pending, it matches the recordingID field of
            TrackedCommandBuffer, otherwise the submissionID.

    When a buffer version is allocated, it is transitioned into the pending state.
    When the command list containing such pending versions is submitted, all the
    pending versions are transitioned to the submitted state. In the submitted
    state, they may be reused later if that submitted instance of the command list
    has finished executing, which is determined based on the queue's semaphore.
    Pending versions cannot be reused. Also, pending versions might be transitioned
    to the available state (tracking word == 0) if their command list is abandoned,
    but that is currently not implemented.

    See also:
        - CommandList::writeVolatileBuffer
        - CommandList::flushVolatileBufferWrites
        - CommandList::submitVolatileBuffers

    -----------------------------------------------------------------------------*/

    struct ZWVKVolatileBufferState
    {
        int32_t latestVersion = 0;
        int32_t minVersion = 0;
        int32_t maxVersion = 0;
        bool initialized = false;
    };

    // A copyable version of std::atomic to be used in an std::vector
    class ZWVKBufferVersionItem : public std::atomic<uint64_t>  // NOLINT(cppcoreguidelines-special-member-functions)
    {
    public:
        ZWVKBufferVersionItem()
            : std::atomic<uint64_t>()
        {
        }

        ZWVKBufferVersionItem(const ZWVKBufferVersionItem& other)
        {
            store(other);
        }

        ZWVKBufferVersionItem& operator=(const uint64_t a)
        {
            store(a);
            return *this;
        }
    };

    class ZWVKBuffer : public ZWVKMemoryResource, public HCommon::RefCounter<IBuffer>, public ZWBufferStateExtension
    {
    public:
        ZWBufferDesc desc;

        vk::Buffer buffer;
        vk::DeviceAddress deviceAddress = 0;

        ZWHeapHandle heap;

        tsl::robin_map<uint64_t, vk::BufferView> viewCache;

        std::vector<ZWVKBufferVersionItem> versionTracking;
        void* mappedMemory = nullptr;
        void* sharedHandle = nullptr;
        uint32_t versionSearchStart = 0;

        // For staging buffers only
        ECommandQueue lastUseQueue = ECommandQueue::Graphics;
        uint64_t lastUseCommandListID = 0;

        ZWVKBuffer(const ZWVKContext& context, ZWVKAllocator& allocator)
            : ZWBufferStateExtension(desc)
            , mContext(context)
            , mAllocator(allocator)
        {
        }

        ~ZWVKBuffer() override;
        const ZWBufferDesc& GetDesc() const override { return desc; }
        GpuVirtualAddress GetGpuVirtualAddress() const override { return deviceAddress; }
        HCommon::ZWObject GetNativeObject(ObjectType type) override;

    private:
        const ZWVKContext& mContext;
        ZWVKAllocator& mAllocator;
    };

    struct PlacedSubresourceFootprint
    {
        // offset, size in bytes
        size_t offset;
        size_t totalBytes;
        uint32_t rowSizeInBytes;
        uint32_t numRows;
        EFormat format;
        uint32_t width;
        uint32_t height;
        uint32_t depth;
        uint32_t rowPitch;
    };

    class StagingTexture : public HCommon::RefCounter<IStagingTexture>
    {
    public:
        ZWTextureDesc desc;
        // backing store for staging texture is a buffer
        HCommon::RefCountPtr<ZWVKBuffer> buffer;
        // Per-mip, per-slice regions: index = mipLevel * arraySize + arraySlice
        std::vector<PlacedSubresourceFootprint> placedFootprints;

        size_t ComputeCopyableFootprints();
        const PlacedSubresourceFootprint* GetCopyableFootprint(MipLevel mipLevel, ArraySlice arraySlice);

        const ZWTextureDesc& GetDesc() const override { return desc; }
    };

    class ZWVKSampler : public HCommon::RefCounter<ISampler>
    {
    public:
        ZWSamplerDesc desc;

        vk::SamplerCreateInfo samplerInfo;
        vk::Sampler sampler;

        explicit ZWVKSampler(const ZWVKContext& context)
            : mContext(context)
        {
        }

        ~ZWVKSampler() override;
        const ZWSamplerDesc& GetDesc() const override { return desc; }
        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

    private:
        const ZWVKContext& mContext;
    };

    class ZWVKShader : public HCommon::RefCounter<IShader>
    {
    public:
        ZWShaderDesc desc;
        std::vector<uint8_t> bytecode;

        vk::ShaderModule shaderModule;
        vk::ShaderStageFlagBits stageFlagBits{};

        // Shader specializations are just references to the original shader module
        // plus the specialization constant array.
        HCommon::ZWResourceHandle baseShader; // Could be a Shader or ShaderLibrary
        std::vector<ZWShaderSpecialization> specializationConstants;

        explicit ZWVKShader(const ZWVKContext& context)
            : mContext(context)
        {
        }

        ~ZWVKShader() override;
        const ZWShaderDesc& GetDesc() const override { return desc; }
        void GetBytecode(const void** ppBytecode, size_t* pSize) const override;
        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

    private:
        const ZWVKContext& mContext;
    };

    class ZWVKShaderLibrary : public HCommon::RefCounter<IShaderLibrary>
    {
    public:
        std::vector<uint8_t> bytecode;
        vk::ShaderModule shaderModule;

        explicit ZWVKShaderLibrary(const ZWVKContext& context)
            : mContext(context)
        {
        }

        ~ZWVKShaderLibrary() override;
        void GetBytecode(const void** ppBytecode, size_t* pSize) const override;
        ZWShaderHandle GetShader(const char* entryName, EShaderType shaderType) override;
    private:
        const ZWVKContext& mContext;
    };

    class ZWVKInputLayout : public HCommon::RefCounter<IInputLayout>
    {
    public:
        std::vector<ZWVertexAttributeDesc> inputDesc;

        std::vector<vk::VertexInputBindingDescription> bindingDesc;
        std::vector<vk::VertexInputAttributeDescription> attributeDesc;

        uint32_t GetNumAttributes() const override;
        const ZWVertexAttributeDesc* GetAttributeDesc(uint32_t index) const override;
    };

    class ZWVKEventQuery : public HCommon::RefCounter<IEventQuery>
    {
    public:
        ECommandQueue queue = ECommandQueue::Graphics;
        uint64_t commandListID = 0;
    };

    class ZWVKTimerQuery : public HCommon::RefCounter<ITimerQuery>
    {
    public:
        int beginQueryIndex = -1;
        int endQueryIndex = -1;

        bool started = false;
        bool resolved = false;
        float time = 0.f;

        explicit ZWVKTimerQuery(HCommon::BitSetAllocator& allocator)
            : mQueryAllocator(allocator)
        {
        }

        ~ZWVKTimerQuery() override;

    private:
        HCommon::BitSetAllocator& mQueryAllocator;
    };

    class ZWVKFramebuffer : public HCommon::RefCounter<IFramebuffer>
    {
    public:
        ZWFramebufferDesc desc;
        ZWFramebufferInfoEx framebufferInfo;

        HCommon::StaticVector<vk::RenderingAttachmentInfo, gMaxRenderTargets> colorAttachments;
        vk::RenderingAttachmentInfo depthAttachment{};
        vk::RenderingAttachmentInfo stencilAttachment{};
        vk::RenderingFragmentShadingRateAttachmentInfoKHR shadingRateAttachment{};

        std::vector<HCommon::ZWResourceHandle> resources;

        bool managed = true;

        const ZWFramebufferDesc& GetDesc() const override { return desc; }
        const ZWFramebufferInfoEx& GetFramebufferInfo() const override { return framebufferInfo; }
    };

    class ZWVKBindingLayout : public HCommon::RefCounter<IBindingLayout>
    {
    public:
        ZWBindingLayoutDesc desc;
        ZWBindlessLayoutDesc bindlessDesc;
        bool isBindless;

        std::vector<vk::DescriptorSetLayoutBinding> vulkanLayoutBindings;

        vk::DescriptorSetLayout descriptorSetLayout;

        // descriptor pool size information per binding set
        std::vector<vk::DescriptorPoolSize> descriptorPoolSizeInfo;

        ZWVKBindingLayout(const ZWVKContext& context, const ZWBindingLayoutDesc& desc);
        ZWVKBindingLayout(const ZWVKContext& context, const ZWBindlessLayoutDesc& desc);
        ~ZWVKBindingLayout() override;
        const ZWBindingLayoutDesc* GetDesc() const override { return isBindless ? nullptr : &desc; }
        const ZWBindlessLayoutDesc* GetBindlessDesc() const override { return isBindless ? &bindlessDesc : nullptr; }
        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

        // generate the descriptor set layout
        vk::Result Bake();

    private:
        const ZWVKContext& mContext;
    };

    // contains a vk::DescriptorSet
    class ZWVKBindingSet : public HCommon::RefCounter<IBindingSet>
    {
    public:
        ZWBindingSetDesc desc;
        ZWBindingLayoutHandle layout;

        // TODO: move pool to the context instead
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        std::vector<HCommon::ZWResourceHandle> resources;
        HCommon::StaticVector<ZWVKBuffer*, gMaxVolatileConstantBuffersPerLayout> volatileConstantBuffers;

        std::vector<uint16_t> bindingsThatNeedTransitions;
        bool hasUavBindings = false;

        explicit ZWVKBindingSet(const ZWVKContext& context)
            : mContext(context)
        {
        }

        ~ZWVKBindingSet() override;
        const ZWBindingSetDesc* GetDesc() const override { return &desc; }
        IBindingLayout* GetLayout() const override { return layout; }
        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

    private:
        const ZWVKContext& mContext;
    };

    class ZWVKDescriptorTable : public HCommon::RefCounter<IDescriptorTable>
    {
    public:
        ZWBindingLayoutHandle layout;
        uint32_t capacity = 0;

        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        explicit ZWVKDescriptorTable(const ZWVKContext& context)
            : mContext(context)
        {
        }

        ~ZWVKDescriptorTable() override;
        const ZWBindingSetDesc* GetDesc() const override { return nullptr; }
        IBindingLayout* GetLayout() const override { return layout; }
        uint32_t GetCapacity() const override { return capacity; }

        // Vulkan doesn't have a concept of the first descriptor in the heap
        uint32_t GetFirstDescriptorIndexInHeap() const override { return 0; }
        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

    private:
        const ZWVKContext& mContext;
    };

    template <typename T>
    using ZWVKBindingVector = HCommon::StaticVector<T, gMaxBindingLayouts>;

    // common code when creating shader pipelines to build binding set layouts
    vk::Result CreatePipelineLayout(
        vk::PipelineLayout& outPipelineLayout,
        ZWVKBindingVector<HCommon::RefCountPtr<ZWVKBindingLayout>>& outBindingLayouts,
        vk::ShaderStageFlags& outPushConstantVisibility,
        ZWVKBindingVector<uint32_t>& outStateBindingIdxToPipelineBindingIdx,
        ZWVKContext const& context,
        BindingLayoutVector const& inBindingLayouts);

    class ZWVKGraphicsPipeline : public HCommon::RefCounter<IGraphicsPipeline>
    {
    public:
        ZWGraphicsPipelineDesc desc;
        ZWFramebufferInfo framebufferInfo;
        EShaderType shaderMask = EShaderType::None;
        ZWVKBindingVector<HCommon::RefCountPtr<ZWVKBindingLayout>> pipelineBindingLayouts;
        ZWVKBindingVector<uint32_t> descriptorSetIdxToBindingIdx;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;
        vk::ShaderStageFlags pushConstantVisibility;
        bool usesBlendConstants = false;

        explicit ZWVKGraphicsPipeline(const ZWVKContext& context)
            : mContext(context)
        {
        }

        ~ZWVKGraphicsPipeline() override;
        const ZWGraphicsPipelineDesc& GetDesc() const override { return desc; }
        const ZWFramebufferInfo& GetFramebufferInfo() const override { return framebufferInfo; }
        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

    private:
        const ZWVKContext& mContext;
    };

    class ZWVKComputePipeline : public HCommon::RefCounter<IComputePipeline>
    {
    public:
        ZWComputePipelineDesc desc;

        ZWVKBindingVector<HCommon::RefCountPtr<ZWVKBindingLayout>> pipelineBindingLayouts;
        ZWVKBindingVector<uint32_t> descriptorSetIdxToBindingIdx;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;
        vk::ShaderStageFlags pushConstantVisibility;

        explicit ZWVKComputePipeline(const ZWVKContext& context)
            : mContext(context)
        {
        }

        ~ZWVKComputePipeline() override;
        const ZWComputePipelineDesc& GetDesc() const override { return desc; }
        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

    private:
        const ZWVKContext& mContext;
    };

    class ZWVKMeshletPipeline : public HCommon::RefCounter<IMeshletPipeline>
    {
    public:
        ZWMeshletPipelineDesc desc;
        ZWFramebufferInfo framebufferInfo;
        EShaderType shaderMask = EShaderType::None;
        ZWVKBindingVector<HCommon::RefCountPtr<ZWVKBindingLayout>> pipelineBindingLayouts;
        ZWVKBindingVector<uint32_t> descriptorSetIdxToBindingIdx;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;
        vk::ShaderStageFlags pushConstantVisibility;
        bool usesBlendConstants = false;

        explicit ZWVKMeshletPipeline(const ZWVKContext& context)
            : mContext(context)
        {
        }

        ~ZWVKMeshletPipeline() override;
        const ZWMeshletPipelineDesc& GetDesc() const override { return desc; }
        const ZWFramebufferInfo& GetFramebufferInfo() const override { return framebufferInfo; }
        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

    private:
        const ZWVKContext& mContext;
    };

    class ZWVKRayTracingPipeline : public HCommon::RefCounter<Hrt::IPipeline>
    {
    public:
        Hrt::ZWPipelineDesc desc;
        ZWVKBindingVector<HCommon::RefCountPtr<ZWVKBindingLayout>> pipelineBindingLayouts;
        ZWVKBindingVector<uint32_t> descriptorSetIdxToBindingIdx;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;
        vk::ShaderStageFlags pushConstantVisibility;

        tsl::robin_map<std::string, uint32_t> shaderGroups; // name -> index
        std::vector<uint8_t> shaderGroupHandles;

        explicit ZWVKRayTracingPipeline(const ZWVKContext& context, ZWVKDevice* device)
            : mContext(context)
            , mDevice(device)
        {
        }

        ~ZWVKRayTracingPipeline() override;
        const Hrt::ZWPipelineDesc& GetDesc() const override { return desc; }
        Hrt::ZWShaderTableHandle CreateShaderTable(Hrt::ZWShaderTableDesc const& stDesc) override;
        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

        int32_t FindShaderGroup(const std::string& name); // returns -1 if not found
        uint32_t GetShaderTableEntrySize() const { return mContext.rayTracingPipelineProperties.shaderGroupBaseAlignment; }

    private:
        const ZWVKContext& mContext;
        ZWVKDevice* mDevice;
    };

    struct ZWVKShaderTableState
    {
        uint32_t version = 0;
        vk::StridedDeviceAddressRegionKHR rayGen;
        vk::StridedDeviceAddressRegionKHR miss;
        vk::StridedDeviceAddressRegionKHR hitGroups;
        vk::StridedDeviceAddressRegionKHR callable;
    };

    class ZWVKShaderTable : public HCommon::RefCounter<Hrt::IShaderTable>
    {
    public:
        HCommon::RefCountPtr<ZWVKRayTracingPipeline> pipeline;

        int32_t rayGenerationShader = -1;
        std::vector<uint32_t> missShaders;
        std::vector<uint32_t> callableShaders;
        std::vector<uint32_t> hitGroups;

        uint32_t version = 0;

        ZWBufferHandle cache;
        ZWVKShaderTableState cacheState;

        ZWVKShaderTable(const ZWVKContext& context, ZWVKRayTracingPipeline* _pipeline, Hrt::ZWShaderTableDesc const& desc)
            : pipeline(_pipeline)
            , mContext(context)
            , mDesc(desc)
        {
        }

        size_t GetUploadSize() const { return pipeline->GetShaderTableEntrySize() * size_t(GetNumEntries()); }
        void Bake(uint8_t* cpuVA, vk::DeviceAddress gpuVA, ZWVKShaderTableState& state);

        Hrt::ZWShaderTableDesc const& GetDesc() const override { return mDesc; }
        uint32_t GetNumEntries() const override;
        Hrt::IPipeline* GetPipeline() const override { return pipeline; }
        void SetRayGenerationShader(const char* exportName, IBindingSet* bindings = nullptr) override;
        int32_t AddMissShader(const char* exportName, IBindingSet* bindings = nullptr) override;
        int32_t AddHitGroup(const char* exportName, IBindingSet* bindings = nullptr) override;
        int32_t AddCallableShader(const char* exportName, IBindingSet* bindings = nullptr) override;
        void ClearMissShaders() override;
        void ClearHitShaders() override;
        void ClearCallableShaders() override;

    private:
        const ZWVKContext& mContext;
        Hrt::ZWShaderTableDesc const mDesc;

        bool VerifyShaderGroupExists(const char* exportName, int32_t shaderGroupIndex) const;
    };

    struct ZWVKBufferChunk
    {
        ZWBufferHandle buffer;
        uint64_t version = 0;
        uint64_t bufferSize = 0;
        uint64_t writePointer = 0;
        void* mappedMemory = nullptr;

        static constexpr uint64_t cSizeAlignment = 4096; // GPU page size
    };

    class ZWVKUploadManager
    {
    public:
        ZWVKUploadManager(ZWVKDevice* pParent, uint64_t defaultChunkSize, uint64_t memoryLimit, bool isScratchBuffer)
            : mDevice(pParent)
            , mDefaultChunkSize(defaultChunkSize)
            , mMemoryLimit(memoryLimit)
            , mIsScratchBuffer(isScratchBuffer)
        {
        }

        std::shared_ptr<ZWVKBufferChunk> CreateChunk(uint64_t size);

        bool SuballocateBuffer(uint64_t size, ZWVKBuffer** pBuffer, uint64_t* pOffset, void** pCpuVA, uint64_t currentVersion, uint32_t alignment = 256);
        void SubmitChunks(uint64_t currentVersion, uint64_t submittedVersion);

    private:
        ZWVKDevice* mDevice;
        uint64_t mDefaultChunkSize = 0;
        uint64_t mMemoryLimit = 0;
        uint64_t mAllocatedMemory = 0;
        bool mIsScratchBuffer = false;

        std::list<std::shared_ptr<ZWVKBufferChunk>> mChunkPool;
        std::shared_ptr<ZWVKBufferChunk> mCurrentChunk;
    };

    class ZWVKAccelStruct : public HCommon::RefCounter<Hrt::IAccelStruct>
    {
    public:
        ZWBufferHandle dataBuffer;
        std::vector<vk::AccelerationStructureInstanceKHR> instances;
        vk::AccelerationStructureKHR accelStruct;
        vk::DeviceAddress accelStructDeviceAddress = 0;
        Hrt::ZWAccelStructDesc desc;
        bool allowUpdate = false;
        bool compacted = false;
        size_t rtxmuId = ~0ull;
        vk::Buffer rtxmuBuffer;


        explicit ZWVKAccelStruct(const ZWVKContext& context)
            : mContext(context)
        {
        }

        ~ZWVKAccelStruct() override;

        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;
        const Hrt::ZWAccelStructDesc& GetDesc() const override { return desc; }
        bool IsCompacted() const override { return compacted; }
        uint64_t GetDeviceAddress() const override;

    private:
        const ZWVKContext& mContext;
    };

    class ZWVKOpacityMicromap : public HCommon::RefCounter<Hrt::IOpacityMicromap>
    {
    public:
        ZWBufferHandle dataBuffer;
        vk::UniqueMicromapEXT opacityMicromap;
        Hrt::ZWOpacityMicromapDesc desc;
        bool allowUpdate = false;
        bool compacted = false;

        explicit ZWVKOpacityMicromap()
        {
        }

        ~ZWVKOpacityMicromap() override;

        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;
        const Hrt::ZWOpacityMicromapDesc& GetDesc() const override { return desc; }
        bool IsCompacted() const override { return compacted; }
        uint64_t GetDeviceAddress() const override;
    };

    class ZWVKDevice : public HCommon::RefCounter<HRHI::HVulkan::IDevice>
    {
    public:
        // Internal backend methods

        ZWVKDevice(const HRHI::HVulkan::ZWDeviceDesc& desc);
        ~ZWVKDevice() override;

        ZWVKQueue* GetQueue(ECommandQueue queue) const { return mQueues[int(queue)].get(); }
        vk::QueryPool GetTimerQueryPool() const { return mTimerQueryPool; }

        // IResource implementation

        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;


        // IDevice implementation

        ZWHeapHandle CreateHeap(const ZWHeapDesc& d) override;

        ZWTextureHandle CreateTexture(const ZWTextureDesc& d) override;
        ZWMemoryRequirements GetTextureMemoryRequirements(ITexture* texture) override;
        bool BindTextureMemory(ITexture* texture, IHeap* heap, uint64_t offset) override;

        ZWTextureHandle CreateHandleForNativeTexture(ObjectType objectType, HCommon::ZWObject texture, const ZWTextureDesc& desc) override;

        ZWStagingTextureHandle CreateStagingTexture(const ZWTextureDesc& d, ECpuAccessMode cpuAccess) override;
        void* MapStagingTexture(IStagingTexture* tex, const ZWTextureSlice& slice, ECpuAccessMode cpuAccess, size_t* outRowPitch) override;
        void UnmapStagingTexture(IStagingTexture* tex) override;

        void GetTextureTiling(ITexture* texture, uint32_t* numTiles, ZWPackedMipDesc* desc, ZWTileShape* tileShape, uint32_t* subresourceTilingsNum, ZWSubresourceTiling* subresourceTilings) override;
        void UpdateTextureTileMappings(ITexture* texture, const ZWTextureTilesMapping* tileMappings, uint32_t numTileMappings, ECommandQueue executionQueue = ECommandQueue::Graphics) override;

        ZWSamplerFeedbackTextureHandle CreateSamplerFeedbackTexture(ITexture* pairedTexture, const ZWSamplerFeedbackTextureDesc& desc) override;
        ZWSamplerFeedbackTextureHandle CreateSamplerFeedbackForNativeTexture(ObjectType objectType, HCommon::ZWObject texture, ITexture* pairedTexture) override;

        ZWBufferHandle CreateBuffer(const ZWBufferDesc& d) override;
        void* MapBuffer(IBuffer* b, ECpuAccessMode mapFlags) override;
        void UnmapBuffer(IBuffer* b) override;
        ZWMemoryRequirements GetBufferMemoryRequirements(IBuffer* buffer) override;
        bool BindBufferMemory(IBuffer* buffer, IHeap* heap, uint64_t offset) override;

        ZWBufferHandle CreateHandleForNativeBuffer(ObjectType objectType, HCommon::ZWObject buffer, const ZWBufferDesc& desc) override;

        ZWShaderHandle CreateShader(const ZWShaderDesc& d, const void* binary, size_t binarySize) override;
        ZWShaderHandle CreateShaderSpecialization(IShader* baseShader, const ZWShaderSpecialization* constants, uint32_t numConstants) override;
        ZWShaderLibraryHandle CreateShaderLibrary(const void* binary, size_t binarySize) override;

        ZWSamplerHandle CreateSampler(const ZWSamplerDesc& d) override;

        ZWInputLayoutHandle CreateInputLayout(const ZWVertexAttributeDesc* d, uint32_t attributeCount, IShader* vertexShader) override;

        // event queries
        ZWEventQueryHandle CreateEventQuery() override;
        void SetEventQuery(IEventQuery* query, ECommandQueue queue) override;
        bool PollEventQuery(IEventQuery* query) override;
        void WaitEventQuery(IEventQuery* query) override;
        void ResetEventQuery(IEventQuery* query) override;

        // timer queries
        ZWTimerQueryHandle CreateTimerQuery() override;
        bool PollTimerQuery(ITimerQuery* query) override;
        float GetTimerQueryTime(ITimerQuery* query) override;
        void ResetTimerQuery(ITimerQuery* query) override;

        EGraphicsAPI GetGraphicsAPI() override;

        ZWFramebufferHandle CreateFramebuffer(const ZWFramebufferDesc& desc) override;

        ZWGraphicsPipelineHandle CreateGraphicsPipeline(const ZWGraphicsPipelineDesc& desc, ZWFramebufferInfo const& fbinfo) override;

        ZWComputePipelineHandle CreateComputePipeline(const ZWComputePipelineDesc& desc) override;

        ZWMeshletPipelineHandle CreateMeshletPipeline(const ZWMeshletPipelineDesc& desc, ZWFramebufferInfo const& fbinfo) override;

        Hrt::ZWPipelineHandle CreateRayTracingPipeline(const Hrt::ZWPipelineDesc& desc) override;

        ZWBindingLayoutHandle CreateBindingLayout(const ZWBindingLayoutDesc& desc) override;
        ZWBindingLayoutHandle CreateBindlessLayout(const ZWBindlessLayoutDesc& desc) override;

        ZWBindingSetHandle CreateBindingSet(const ZWBindingSetDesc& desc, IBindingLayout* layout) override;
        ZWDescriptorTableHandle CreateDescriptorTable(IBindingLayout* layout) override;

        void ResizeDescriptorTable(IDescriptorTable* descriptorTable, uint32_t newSize, bool keepContents = true) override;
        bool WriteDescriptorTable(IDescriptorTable* descriptorTable, const ZWBindingSetItem& item) override;

        Hrt::ZWOpacityMicromapHandle CreateOpacityMicromap(const Hrt::ZWOpacityMicromapDesc& desc) override;
        Hrt::ZWAccelStructHandle CreateAccelStruct(const Hrt::ZWAccelStructDesc& desc) override;
        ZWMemoryRequirements GetAccelStructMemoryRequirements(Hrt::IAccelStruct* as) override;
        Hrt::HCluster::ZWOperationSizeInfo GetClusterOperationSizeInfo(const Hrt::HCluster::ZWOperationParams& params) override;
        bool BindAccelStructMemory(Hrt::IAccelStruct* as, IHeap* heap, uint64_t offset) override;

        ZWCommandListHandle CreateCommandList(const ZWCommandListParameters& params = ZWCommandListParameters()) override;
        uint64_t ExecuteCommandLists(ICommandList* const* pCommandLists, size_t numCommandLists, ECommandQueue executionQueue = ECommandQueue::Graphics) override;
        void QueueWaitForCommandList(ECommandQueue waitQueue, ECommandQueue executionQueue, uint64_t instance) override;
        bool WaitForIdle() override;
        void RunGarbageCollection() override;
        bool QueryFeatureSupport(EFeature feature, void* pInfo = nullptr, size_t infoSize = 0) override;
        EFormatSupport QueryFormatSupport(EFormat format) override;
        HCoopVec::ZWDeviceFeatures QueryCoopVecFeatures() override;
        size_t GetCoopVecMatrixSize(HCoopVec::EDataType type, HCoopVec::EMatrixLayout layout, int rows, int columns) override;
        HCommon::ZWObject GetNativeQueue(ObjectType objectType, ECommandQueue queue) override;
        IMessageCallback* GetMessageCallback() override { return mContext.messageCallback; }
        bool IsAftermathEnabled() override { return mAftermathEnabled; }
        HApp::ZWAftermathCrashDumpHelper& GetAftermathCrashDumpHelper() override { return mAftermathCrashDumpHelper; }

        // vulkan::IDevice implementation
        VkSemaphore GetQueueSemaphore(ECommandQueue queue) override;
        void QueueWaitForSemaphore(ECommandQueue waitQueue, VkSemaphore semaphore, uint64_t value) override;
        void QueueSignalSemaphore(ECommandQueue executionQueue, VkSemaphore semaphore, uint64_t value) override;
        uint64_t QueueGetCompletedInstance(ECommandQueue queue) override;

    private:
        // Warning m_AftermathCrashDump helper must be first due to reverse destruction order
        // Queues will destroy CommandLists which will unregister from m_AftermathCrashDumpHelper in their destructors
        bool mAftermathEnabled = false;
        HApp::ZWAftermathCrashDumpHelper mAftermathCrashDumpHelper;

        ZWVKContext mContext;
        ZWVKAllocator mAllocator;

        vk::QueryPool mTimerQueryPool = nullptr;
        HCommon::BitSetAllocator mTimerQueryAllocator;

        std::mutex mMutex;

        // array of submission queues
        std::array<std::unique_ptr<ZWVKQueue>, uint32_t(ECommandQueue::Count)> mQueues;

        void* MapBuffer(IBuffer* b, ECpuAccessMode flags, uint64_t offset, size_t size) const;
    };

    class ZWVKCommandList : public HCommon::RefCounter<ICommandList>
    {
    public:
        // Internal backend methods

        ZWVKCommandList(ZWVKDevice* device, const ZWVKContext& context, const ZWCommandListParameters& parameters);
        ~ZWVKCommandList() override;

        void Executed(ZWVKQueue& queue, uint64_t submissionID);

        // IResource implementation

        HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

        // ICommandList implementation

        void Open() override;
        void Close() override;
        void ClearState() override;

        void ClearTextureFloat(ITexture* texture, ZWTextureSubresourceSet subresources, const ZWColor& clearColor) override;
        void ClearDepthStencilTexture(ITexture* texture, ZWTextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil) override;
        void ClearTextureUInt(ITexture* texture, ZWTextureSubresourceSet subresources, uint32_t clearColor) override;
        void ClearSamplerFeedbackTexture(ISamplerFeedbackTexture* texture) override;
        void DecodeSamplerFeedbackTexture(IBuffer* buffer, ISamplerFeedbackTexture* texture, EFormat format) override;
        void SetSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, EResourceStates stateBits) override;

        void CopyTexture(ITexture* dest, const ZWTextureSlice& destSlice, ITexture* src, const ZWTextureSlice& srcSlice) override;
        void CopyTexture(IStagingTexture* dest, const ZWTextureSlice& dstSlice, ITexture* src, const ZWTextureSlice& srcSlice) override;
        void CopyTexture(ITexture* dest, const ZWTextureSlice& dstSlice, IStagingTexture* src, const ZWTextureSlice& srcSlice) override;
        void WriteTexture(ITexture* dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch) override;
        void ResolveTexture(ITexture* dest, const ZWTextureSubresourceSet& dstSubresources, ITexture* src, const ZWTextureSubresourceSet& srcSubresources) override;

        void WriteBuffer(IBuffer* b, const void* data, size_t dataSize, uint64_t destOffsetBytes = 0) override;
        void ClearBufferUInt(IBuffer* b, uint32_t clearValue) override;
        void CopyBuffer(IBuffer* dest, uint64_t destOffsetBytes, IBuffer* src, uint64_t srcOffsetBytes, uint64_t dataSizeBytes) override;

        void SetPushConstants(const void* data, size_t byteSize) override;

        void SetGraphicsState(const ZWGraphicsState& state) override;
        void Draw(const ZWDrawArguments& args) override;
        void DrawIndexed(const ZWDrawArguments& args) override;
        void DrawIndirect(uint32_t offsetBytes, uint32_t drawCount) override;
        void DrawIndexedIndirect(uint32_t offsetBytes, uint32_t drawCount) override;
        void DrawIndexedIndirectCount(uint32_t paramOffsetBytes, uint32_t countOffsetBytes, uint32_t maxDrawCount) override;

        void SetComputeState(const ZWComputeState& state) override;
        void Dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override;
        void DispatchIndirect(uint32_t offsetBytes)  override;

        void SetMeshletState(const ZWMeshletState& state) override;
        void DispatchMesh(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override;

        void SetRayTracingState(const Hrt::ZWState& state) override;
        void DispatchRays(const Hrt::ZWDispatchRaysArguments& args) override;

        void BuildOpacityMicromap(Hrt::IOpacityMicromap* omm, const Hrt::ZWOpacityMicromapDesc& desc) override;
        void BuildBottomLevelAccelStruct(Hrt::IAccelStruct* as, const Hrt::ZWGeometryDesc* pGeometries, size_t numGeometries, Hrt::EAccelStructBuildFlags buildFlags) override;
        void CompactBottomLevelAccelStructs() override;
        void BuildTopLevelAccelStruct(Hrt::IAccelStruct* as, const Hrt::ZWInstanceDesc* pInstances, size_t numInstances, Hrt::EAccelStructBuildFlags buildFlags) override;
        void BuildTopLevelAccelStructFromBuffer(Hrt::IAccelStruct* as, HRHI::IBuffer* instanceBuffer, uint64_t instanceBufferOffset, size_t numInstances,
            Hrt::EAccelStructBuildFlags buildFlags = Hrt::EAccelStructBuildFlags::None) override;
        void ExecuteMultiIndirectClusterOperation(const Hrt::HCluster::ZWOperationDesc& desc) override;

        void ConvertCoopVecMatrices(HCoopVec::ZWConvertMatrixLayoutDesc const* convertDescs, size_t numDescs) override;

        void BeginTimerQuery(ITimerQuery* query) override;
        void EndTimerQuery(ITimerQuery* query) override;

        void BeginMarker(const char* name) override;
        void EndMarker() override;

        void SetEnableAutomaticBarriers(bool enable) override;
        void SetResourceStatesForBindingSet(IBindingSet* bindingSet) override;

        void SetEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers) override;
        void SetEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers) override;

        void BeginTrackingTextureState(ITexture* texture, ZWTextureSubresourceSet subresources, EResourceStates stateBits) override;
        void BeginTrackingBufferState(IBuffer* buffer, EResourceStates stateBits) override;

        void SetTextureState(ITexture* texture, ZWTextureSubresourceSet subresources, EResourceStates stateBits) override;
        void SetBufferState(IBuffer* buffer, EResourceStates stateBits) override;
        void SetAccelStructState(Hrt::IAccelStruct* _as, EResourceStates stateBits) override;

        void SetPermanentTextureState(ITexture* texture, EResourceStates stateBits) override;
        void SetPermanentBufferState(IBuffer* buffer, EResourceStates stateBits) override;

        void CommitBarriers() override;

        EResourceStates GetTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel) override;
        EResourceStates GetBufferState(IBuffer* buffer) override;

        IDevice* GetDevice() override { return mDevice; }
        const ZWCommandListParameters& GetDesc() override { return mCommandListParameters; }

        ZWVKTrackedCommandBufferPtr GetCurrentCmdBuf() const { return mCurrentCmdBuf; }

    private:
        ZWVKDevice* mDevice;
        const ZWVKContext& mContext;

        ZWCommandListParameters mCommandListParameters;

        ZWCommandListResourceStateTracker mStateTracker;
        bool m_EnableAutomaticBarriers = true;

        // current internal command buffer
        ZWVKTrackedCommandBufferPtr mCurrentCmdBuf = nullptr;

#if HRHI_WITH_AFTERMATH
        HApp::ZWAftermathMarkerTracker mAftermathTracker;
#endif

        vk::PipelineLayout mCurrentPipelineLayout;
        vk::ShaderStageFlags mCurrentPushConstantsVisibility;
        ZWGraphicsState mCurrentGraphicsState{};
        ZWComputeState mCurrentComputeState{};
        ZWMeshletState mCurrentMeshletState{};
        Hrt::ZWState mCurrentRayTracingState;
        bool mAnyVolatileBufferWrites = false;
        bool mBindingStatesDirty = false;

        tsl::robin_map<Hrt::IShaderTable*, std::unique_ptr<ZWVKShaderTableState>> m_UncachedShaderTableStates;
        ZWVKShaderTableState& GetShaderTableState(Hrt::IShaderTable* shaderTable);

        tsl::robin_map<ZWVKBuffer*, ZWVKVolatileBufferState> mVolatileBufferStates;

        std::unique_ptr<ZWVKUploadManager> m_UploadManager;
        std::unique_ptr<ZWVKUploadManager> m_ScratchManager;

        void ClearTexture(ITexture* texture, ZWTextureSubresourceSet subresources, const vk::ClearColorValue& clearValue);

        void BindBindingSets(vk::PipelineBindPoint bindPoint, vk::PipelineLayout pipelineLayout, const BindingSetVector& bindings, ZWVKBindingVector<uint32_t> const& descriptorSetIdxToBindingIdx);

        void BeginRenderPass(HRHI::IFramebuffer* framebuffer);
        void EndRenderPass();

        void InsertGraphicsResourceBarriers(const ZWGraphicsState& state);
        void InsertComputeResourceBarriers(const ZWComputeState& state);
        void InsertMeshletResourceBarriers(const ZWMeshletState& state);
        void InsertRayTracingResourceBarriers(const Hrt::ZWState& state);
        void InsertResourceBarriersForBindingSets(const BindingSetVector& newBindings, const BindingSetVector& oldBindings);

        void WriteVolatileBuffer(ZWVKBuffer* buffer, const void* data, size_t dataSize);
        void FlushVolatileBufferWrites();
        void SubmitVolatileBuffers(uint64_t recordingID, uint64_t submittedID);

        void UpdateGraphicsVolatileBuffers();
        void UpdateComputeVolatileBuffers();
        void UpdateMeshletVolatileBuffers();
        void UpdateRayTracingVolatileBuffers();

        void RequireTextureState(ITexture* texture, ZWTextureSubresourceSet subresources, EResourceStates state);
        void RequireBufferState(IBuffer* buffer, EResourceStates state);
        bool AnyBarriers() const;

        void BuildTopLevelAccelStructInternal(ZWVKAccelStruct* as, VkDeviceAddress instanceData, size_t numInstances, Hrt::EAccelStructBuildFlags buildFlags, uint64_t currentVersion);

        void CommitBarriersInternal();
    };

    class VulkanBackend final
    {
    public:
        static std::vector<const char*> GetRequiredInstanceExtensions(const HApp::ZWWindowManager& windowManager);
        static bool CreateWindowSurface(const HApp::ZWWindow& window, VkInstance instance, VkSurfaceKHR* outSurface, const VkAllocationCallbacks* allocator = nullptr);
    };
}
