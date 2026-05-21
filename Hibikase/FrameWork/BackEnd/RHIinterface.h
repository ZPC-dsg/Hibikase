#pragma once
#include <BackEnd/RHIbasictypes.h>
#include <Common/refcountptr.h>

namespace HApp
{
    class ZWAftermathCrashDumpHelper;
}

namespace HRHI
{
    // Heap
    struct ZWHeapDesc
    {
        uint64_t capacity = 0;
        EHeapType type;
        std::string debugName;

        constexpr ZWHeapDesc& setCapacity(uint64_t value) { capacity = value; return *this; }
        constexpr ZWHeapDesc& setType(EHeapType value) { type = value; return *this; }
        ZWHeapDesc& setDebugName(const std::string& value) { debugName = value; return *this; }
    };

    class IHeap : public HCommon::IResource
    {
    public:
        virtual const ZWHeapDesc& GetDesc() = 0;
    };
    typedef HCommon::RefCountPtr<IHeap> ZWHeapHandle;

    // Texture
    struct ZWTextureDesc
    {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t arraySize = 1;
        uint32_t mipLevels = 1;
        uint32_t sampleCount = 1;
        uint32_t sampleQuality = 0;
        EFormat format = EFormat::UNKNOWN;
        ETextureDimension dimension = ETextureDimension::Texture2D;
        std::string debugName;

        bool isShaderResource = true; // Note: isShaderResource is initialized to 'true' for backward compatibility
        bool isRenderTarget = false;
        bool isUAV = false;
        bool isTypeless = false;
        bool isShadingRateSurface = false;

        ESharedResourceFlags sharedResourceFlags = ESharedResourceFlags::None;

        // Indicates that the texture is created with no backing memory,
        // and memory is bound to the texture later using bindTextureMemory.
        // On DX12, the texture resource is created at the time of memory binding.
        bool isVirtual = false;
        bool isTiled = false;

        ZWColor clearValue;
        bool useClearValue = false;

        EResourceStates initialState = EResourceStates::Unknown;

        // If keepInitialState is true, command lists that use the texture will automatically
        // begin tracking the texture from the initial state and transition it to the initial state 
        // on command list close.
        bool keepInitialState = false;

        constexpr ZWTextureDesc& setWidth(uint32_t value) { width = value; return *this; }
        constexpr ZWTextureDesc& setHeight(uint32_t value) { height = value; return *this; }
        constexpr ZWTextureDesc& setDepth(uint32_t value) { depth = value; return *this; }
        constexpr ZWTextureDesc& setArraySize(uint32_t value) { arraySize = value; return *this; }
        constexpr ZWTextureDesc& setMipLevels(uint32_t value) { mipLevels = value; return *this; }
        constexpr ZWTextureDesc& setSampleCount(uint32_t value) { sampleCount = value; return *this; }
        constexpr ZWTextureDesc& setSampleQuality(uint32_t value) { sampleQuality = value; return *this; }
        constexpr ZWTextureDesc& setFormat(EFormat value) { format = value; return *this; }
        constexpr ZWTextureDesc& setDimension(ETextureDimension value) { dimension = value; return *this; }
        ZWTextureDesc& setDebugName(const std::string& value) { debugName = value; return *this; }
        constexpr ZWTextureDesc& setIsRenderTarget(bool value) { isRenderTarget = value; return *this; }
        constexpr ZWTextureDesc& setIsUAV(bool value) { isUAV = value; return *this; }
        constexpr ZWTextureDesc& setIsTypeless(bool value) { isTypeless = value; return *this; }
        constexpr ZWTextureDesc& setIsVirtual(bool value) { isVirtual = value; return *this; }
        constexpr ZWTextureDesc& setClearValue(const ZWColor& value) { clearValue = value; useClearValue = true; return *this; }
        constexpr ZWTextureDesc& setUseClearValue(bool value) { useClearValue = value; return *this; }
        constexpr ZWTextureDesc& setInitialState(EResourceStates value) { initialState = value; return *this; }
        constexpr ZWTextureDesc& setKeepInitialState(bool value) { keepInitialState = value; return *this; }
        constexpr ZWTextureDesc& setSharedResourceFlags(ESharedResourceFlags value) { sharedResourceFlags = value; return *this; }

        // Equivalent to .setInitialState(_initialState).setKeepInitialState(true)
        constexpr ZWTextureDesc& enableAutomaticStateTracking(EResourceStates initialState)
        {
            initialState = initialState;
            keepInitialState = true;
            return *this;
        }
    };

    class ITexture : public HCommon::IResource
    {
    public:
        [[nodiscard]] virtual const ZWTextureDesc& GetDesc() const = 0;

        // Similar to getNativeObject, returns a native view for a specified set of subresources. Returns nullptr if unavailable.
        // TODO: on D3D12, the views might become invalid later if the view heap is grown/reallocated, we should do something about that.
        virtual HCommon::ZWObject GetNativeView(ObjectType objectType, EFormat format = EFormat::UNKNOWN, ZWTextureSubresourceSet subresources = sAllSubresources, ETextureDimension dimension = ETextureDimension::Unknown, bool isReadOnlyDSV = false) = 0;
    };
    typedef HCommon::RefCountPtr<ITexture> ZWTextureHandle;

    class IStagingTexture : public HCommon::IResource
    {
    public:
        [[nodiscard]] virtual const ZWTextureDesc& GetDesc() const = 0;
    };
    typedef HCommon::RefCountPtr<IStagingTexture> ZWStagingTextureHandle;

    struct ZWSamplerFeedbackTextureDesc
    {
        ESamplerFeedbackFormat samplerFeedbackFormat = ESamplerFeedbackFormat::MinMipOpaque;
        uint32_t samplerFeedbackMipRegionX = 0;
        uint32_t samplerFeedbackMipRegionY = 0;
        uint32_t samplerFeedbackMipRegionZ = 0;
        EResourceStates initialState = EResourceStates::Unknown;
        bool keepInitialState = false;
    };

    class ISamplerFeedbackTexture : public HCommon::IResource
    {
    public:
        [[nodiscard]] virtual const ZWSamplerFeedbackTextureDesc& GetDesc() const = 0;
        virtual ZWTextureHandle GetPairedTexture() = 0;
    };
    typedef HCommon::RefCountPtr<ISamplerFeedbackTexture> ZWSamplerFeedbackTextureHandle;

    // Input Layout
    struct ZWVertexAttributeDesc
    {
        std::string name;
        EFormat format = EFormat::UNKNOWN;
        uint32_t arraySize = 1;
        uint32_t bufferIndex = 0;
        uint32_t offset = 0;
        // note: for most APIs, all strides for a given bufferIndex must be identical
        uint32_t elementStride = 0;
        bool isInstanced = false;

        ZWVertexAttributeDesc& setName(const std::string& value) { name = value; return *this; }
        constexpr ZWVertexAttributeDesc& setFormat(EFormat value) { format = value; return *this; }
        constexpr ZWVertexAttributeDesc& setArraySize(uint32_t value) { arraySize = value; return *this; }
        constexpr ZWVertexAttributeDesc& setBufferIndex(uint32_t value) { bufferIndex = value; return *this; }
        constexpr ZWVertexAttributeDesc& setOffset(uint32_t value) { offset = value; return *this; }
        constexpr ZWVertexAttributeDesc& setElementStride(uint32_t value) { elementStride = value; return *this; }
        constexpr ZWVertexAttributeDesc& setIsInstanced(bool value) { isInstanced = value; return *this; }
    };

    class IInputLayout : public HCommon::IResource
    {
    public:
        [[nodiscard]] virtual uint32_t GetNumAttributes() const = 0;
        [[nodiscard]] virtual const ZWVertexAttributeDesc* GetAttributeDesc(uint32_t index) const = 0;
    };
    typedef HCommon::RefCountPtr<IInputLayout> ZWInputLayoutHandle;

    // Buffer
    class IBuffer : public HCommon::IResource
    {
    public:
        [[nodiscard]] virtual const ZWBufferDesc& GetDesc() const = 0;
        [[nodiscard]] virtual GpuVirtualAddress GetGpuVirtualAddress() const = 0;
    };
    typedef HCommon::RefCountPtr<IBuffer> ZWBufferHandle;

    // Shader
    struct ZWShaderDesc
    {
        EShaderType shaderType = EShaderType::None;
        std::string debugName;
        std::string entryName = "main";

        int hlslExtensionsUAV = -1;

        bool useSpecificShaderExt = false;
        uint32_t numCustomSemantics = 0;
        CustomSemantic* pCustomSemantics = nullptr;

        EFastGeometryShaderFlags fastGSFlags = EFastGeometryShaderFlags(0);
        uint32_t* pCoordinateSwizzling = nullptr;

        constexpr ZWShaderDesc& setShaderType(EShaderType value) { shaderType = value; return *this; }
        ZWShaderDesc& setDebugName(const std::string& value) { debugName = value; return *this; }
        ZWShaderDesc& setEntryName(const std::string& value) { entryName = value; return *this; }
        constexpr ZWShaderDesc& setHlslExtensionsUAV(int value) { hlslExtensionsUAV = value; return *this; }
        constexpr ZWShaderDesc& setUseSpecificShaderExt(bool value) { useSpecificShaderExt = value; return *this; }
        constexpr ZWShaderDesc& setCustomSemantics(uint32_t count, CustomSemantic* data) {
            numCustomSemantics = count;
            pCustomSemantics = data; return *this;
        }
        constexpr ZWShaderDesc& setFastGSFlags(EFastGeometryShaderFlags value) { fastGSFlags = value; return *this; }
        constexpr ZWShaderDesc& setCoordinateSwizzling(uint32_t* value) { pCoordinateSwizzling = value; return *this; }
    };

    class IShader : public HCommon::IResource
    {
    public:
        [[nodiscard]] virtual const ZWShaderDesc& GetDesc() const = 0;
        virtual void GetBytecode(const void** ppBytecode, size_t* pSize) const = 0;
    };
    typedef HCommon::RefCountPtr<IShader> ZWShaderHandle;

    // Shader Library
    class IShaderLibrary : public HCommon::IResource
    {
    public:
        virtual void GetBytecode(const void** ppBytecode, size_t* pSize) const = 0;
        virtual ZWShaderHandle GetShader(const char* entryName, EShaderType shaderType) = 0;
    };
    typedef HCommon::RefCountPtr<IShaderLibrary> ZWShaderLibraryHandle;

    // Sampler
    struct ZWSamplerDesc
    {
        ZWColor borderColor = 1.f;
        float maxAnisotropy = 1.f;
        float mipBias = 0.f;

        bool minFilter = true;
        bool magFilter = true;
        bool mipFilter = true;
        ESamplerAddressMode addressU = ESamplerAddressMode::Clamp;
        ESamplerAddressMode addressV = ESamplerAddressMode::Clamp;
        ESamplerAddressMode addressW = ESamplerAddressMode::Clamp;
        ESamplerReductionType reductionType = ESamplerReductionType::Standard;

        ZWSamplerDesc& setBorderColor(const ZWColor& color) { borderColor = color; return *this; }
        ZWSamplerDesc& setMaxAnisotropy(float value) { maxAnisotropy = value; return *this; }
        ZWSamplerDesc& setMipBias(float value) { mipBias = value; return *this; }
        ZWSamplerDesc& setMinFilter(bool enable) { minFilter = enable; return *this; }
        ZWSamplerDesc& setMagFilter(bool enable) { magFilter = enable; return *this; }
        ZWSamplerDesc& setMipFilter(bool enable) { mipFilter = enable; return *this; }
        ZWSamplerDesc& setAllFilters(bool enable) { minFilter = magFilter = mipFilter = enable; return *this; }
        ZWSamplerDesc& setAddressU(ESamplerAddressMode mode) { addressU = mode; return *this; }
        ZWSamplerDesc& setAddressV(ESamplerAddressMode mode) { addressV = mode; return *this; }
        ZWSamplerDesc& setAddressW(ESamplerAddressMode mode) { addressW = mode; return *this; }
        ZWSamplerDesc& setAllAddressModes(ESamplerAddressMode mode) { addressU = addressV = addressW = mode; return *this; }
        ZWSamplerDesc& setReductionType(ESamplerReductionType type) { reductionType = type; return *this; }
    };

    class ISampler : public HCommon::IResource
    {
    public:
        [[nodiscard]] virtual const ZWSamplerDesc& GetDesc() const = 0;
    };
    typedef HCommon::RefCountPtr<ISampler> ZWSamplerHandle;

    // Framebuffer
    struct ZWFramebufferAttachment
    {
        ITexture* texture = nullptr;
        ZWTextureSubresourceSet subresources = ZWTextureSubresourceSet(0, 1, 0, 1);
        EFormat format = EFormat::UNKNOWN;
        bool isReadOnly = false;

        constexpr ZWFramebufferAttachment& setTexture(ITexture* t) { texture = t; return *this; }
        constexpr ZWFramebufferAttachment& setSubresources(ZWTextureSubresourceSet value) { subresources = value; return *this; }
        constexpr ZWFramebufferAttachment& setArraySlice(ArraySlice index) { subresources.baseArraySlice = index; subresources.numArraySlices = 1; return *this; }
        constexpr ZWFramebufferAttachment& setArraySliceRange(ArraySlice index, ArraySlice count) { subresources.baseArraySlice = index; subresources.numArraySlices = count; return *this; }
        constexpr ZWFramebufferAttachment& setMipLevel(MipLevel level) { subresources.baseMipLevel = level; subresources.numMipLevels = 1; return *this; }
        constexpr ZWFramebufferAttachment& setFormat(EFormat f) { format = f; return *this; }
        constexpr ZWFramebufferAttachment& setReadOnly(bool ro) { isReadOnly = ro; return *this; }

        [[nodiscard]] bool valid() const { return texture != nullptr; }
    };

    struct ZWFramebufferDesc
    {
        HCommon::StaticVector<ZWFramebufferAttachment, gMaxRenderTargets> colorAttachments;
        ZWFramebufferAttachment depthAttachment;
        ZWFramebufferAttachment shadingRateAttachment;

        ZWFramebufferDesc& addColorAttachment(const ZWFramebufferAttachment& a) { colorAttachments.push_back(a); return *this; }
        ZWFramebufferDesc& addColorAttachment(ITexture* texture) { colorAttachments.push_back(ZWFramebufferAttachment().setTexture(texture)); return *this; }
        ZWFramebufferDesc& addColorAttachment(ITexture* texture, ZWTextureSubresourceSet subresources) { colorAttachments.push_back(ZWFramebufferAttachment().setTexture(texture).setSubresources(subresources)); return *this; }
        ZWFramebufferDesc& setDepthAttachment(const ZWFramebufferAttachment& d) { depthAttachment = d; return *this; }
        ZWFramebufferDesc& setDepthAttachment(ITexture* texture) { depthAttachment = ZWFramebufferAttachment().setTexture(texture); return *this; }
        ZWFramebufferDesc& setDepthAttachment(ITexture* texture, ZWTextureSubresourceSet subresources) { depthAttachment = ZWFramebufferAttachment().setTexture(texture).setSubresources(subresources); return *this; }
        ZWFramebufferDesc& setShadingRateAttachment(const ZWFramebufferAttachment& d) { shadingRateAttachment = d; return *this; }
        ZWFramebufferDesc& setShadingRateAttachment(ITexture* texture) { shadingRateAttachment = ZWFramebufferAttachment().setTexture(texture); return *this; }
        ZWFramebufferDesc& setShadingRateAttachment(ITexture* texture, ZWTextureSubresourceSet subresources) { shadingRateAttachment = ZWFramebufferAttachment().setTexture(texture).setSubresources(subresources); return *this; }
    };

    class IFramebuffer : public HCommon::IResource
    {
    public:
        [[nodiscard]] virtual const ZWFramebufferDesc& GetDesc() const = 0;
        [[nodiscard]] virtual const ZWFramebufferInfoEx& GetFramebufferInfo() const = 0;
    };
    typedef HCommon::RefCountPtr<IFramebuffer> ZWFramebufferHandle;

    // Binding Layouts
    struct ZWBindingLayoutDesc
    {
        EShaderType visibility = EShaderType::None;

        // On DX12, it controls the register space of the bindings.
        // On Vulkan, DXC maps register spaces to descriptor sets by default, so this can be used to
        // determine the descriptor set index for the binding layout.
        // In order to use this behavior, you must set `registerSpaceIsDescriptorSet` to true. See below.
        uint32_t registerSpace = 0;

        // This flag controls the behavior for pipelines that use multiple binding layouts.
        // It must be set to the same value for _all_ of the binding layouts in a pipeline.
        // - When it's set to `false`, the `registerSpace` parameter only affects the DX12 implementation,
        //   and the validation layer will report an error when non-zero `registerSpace` is used with other APIs.
        // - When it's set to `true` the parameter also affects the Vulkan implementation, allowing any
        //   layout to occupy any register space or descriptor set, regardless of their order in the pipeline.
        //   However, a consequence of DXC mapping the descriptor set index to register space is that you may
        //   not have more than one `BindingLayout` using the same `registerSpace` value in the same pipeline.
        // - When it's set to different values for the layouts in a pipeline, the validation layer will report
        //   an error.
        bool registerSpaceIsDescriptorSet = false;

        std::vector<ZWBindingLayoutItem> bindings;
        ZWVulkanBindingOffsets bindingOffsets;

        ZWBindingLayoutDesc& setVisibility(EShaderType value) { visibility = value; return *this; }
        ZWBindingLayoutDesc& setRegisterSpace(uint32_t value) { registerSpace = value; return *this; }
        ZWBindingLayoutDesc& setRegisterSpaceIsDescriptorSet(bool value) { registerSpaceIsDescriptorSet = value; return *this; }
        // Shortcut for .setRegisterSpace(value).setRegisterSpaceIsDescriptorSet(true)
        ZWBindingLayoutDesc& setRegisterSpaceAndDescriptorSet(uint32_t value) { registerSpace = value; registerSpaceIsDescriptorSet = true; return *this; }
        ZWBindingLayoutDesc& addItem(const ZWBindingLayoutItem& value) { bindings.push_back(value); return *this; }
        ZWBindingLayoutDesc& setBindingOffsets(const ZWVulkanBindingOffsets& value) { bindingOffsets = value; return *this; }
    };

    // Bindless layouts allow applications to attach a descriptor table to an unbounded
    // resource array in the shader. The size of the array is not known ahead of time.
    // The same table can be bound to multiple register spaces on DX12, in order to 
    // access different types of resources stored in the table through different arrays.
    // The `registerSpaces` vector specifies which spaces will the table be bound to,
    // with the table type (SRV or UAV) derived from the resource type assigned to each space.
    struct ZWBindlessLayoutDesc
    {

        // BindlessDescriptorType bridges the DX12 and Vulkan in supporting HLSL ResourceDescriptorHeap and SamplerDescriptorHeap
        // For DX12: 
        // - MutableSrvUavCbv, MutableCounters will enable D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED for the Root Signature
        // - MutableSampler will enable D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED for the Root Signature
        // - The BindingLayout will be ignored in terms of setting a descriptor set. DescriptorIndexing should use GetDescriptorIndexInHeap()
        // For Vulkan:
        // - The type corresponds to the SPIR-V bindings which map to ResourceDescriptorHeap and SamplerDescriptorHeap
        // - The shader needs to be compiled with the same descriptor set index as is passed into setState
        // https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#resourcedescriptorheaps-samplerdescriptorheaps
        enum class ELayoutType
        {
            Immutable = 0,      // Must use registerSpaces to define a fixed descriptor type

            MutableSrvUavCbv,   // Corresponds to SPIR-V binding -fvk-bind-resource-heap (Counter resources ResourceDescriptorHeap)
            // Valid descriptor types: Texture_SRV, Texture_UAV, TypedBuffer_SRV, TypedBuffer_UAV,
            // StructuredBuffer_SRV, StructuredBuffer_UAV, RawBuffer_SRV, RawBuffer_UAV, ConstantBuffer

            MutableCounters,    // Corresponds to SPIR-V binding -fvk-bind-counter-heap (Counter resources accessed via ResourceDescriptorHeap)
            // Valid descriptor types: StructuredBuffer_UAV

            MutableSampler,     // Corresponds to SPIR-V binding -fvk-bind-sampler-heap (SamplerDescriptorHeap)
            // Valid descriptor types: Sampler
        };

        EShaderType visibility = EShaderType::None;
        uint32_t firstSlot = 0;
        uint32_t maxCapacity = 0;
        HCommon::StaticVector<ZWBindingLayoutItem, gMaxBindlessRegisterSpaces> registerSpaces;

        ELayoutType layoutType = ELayoutType::Immutable;

        ZWBindlessLayoutDesc& setVisibility(EShaderType value) { visibility = value; return *this; }
        ZWBindlessLayoutDesc& setFirstSlot(uint32_t value) { firstSlot = value; return *this; }
        ZWBindlessLayoutDesc& setMaxCapacity(uint32_t value) { maxCapacity = value; return *this; }
        ZWBindlessLayoutDesc& addRegisterSpace(const ZWBindingLayoutItem& value) { registerSpaces.push_back(value); return *this; }
        ZWBindlessLayoutDesc& setLayoutType(ELayoutType value) { layoutType = value; return *this; }
    };

    class IBindingLayout : public HCommon::IResource
    {
    public:
        [[nodiscard]] virtual const ZWBindingLayoutDesc* GetDesc() const = 0;           // returns nullptr for bindless layouts
        [[nodiscard]] virtual const ZWBindlessLayoutDesc* GetBindlessDesc() const = 0;  // returns nullptr for regular layouts
    };
    typedef HCommon::RefCountPtr<IBindingLayout> ZWBindingLayoutHandle;
    typedef HCommon::StaticVector<ZWBindingLayoutHandle, gMaxBindingLayouts> BindingLayoutVector;

    // Ray Tracing
    namespace Hrt
    {
        // Hrt::OpacityMicromap
        struct ZWOpacityMicromapDesc
        {
            std::string debugName;
            bool trackLiveness = true;

            // OMM flags. Applies to all OMMs in array.
            EOpacityMicromapBuildFlags flags;
            // OMM counts for each subdivision level and format combination in the inputs.
            std::vector<ZWOpacityMicromapUsageCount> counts;

            // Base pointer for raw OMM input data.
            // Individual OMMs must be 1B aligned, though natural alignment is recommended.
            // It's also recommended to try to organize OMMs together that are expected to be used spatially close together.
            IBuffer* inputBuffer = nullptr;
            uint64_t inputBufferOffset = 0;

            // One NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_DESC entry per OMM.
            IBuffer* perOmmDescs = nullptr;
            uint64_t perOmmDescsOffset = 0;

            ZWOpacityMicromapDesc& setDebugName(const std::string& value) { debugName = value; return *this; }
            ZWOpacityMicromapDesc& setTrackLiveness(bool value) { trackLiveness = value; return *this; }
            ZWOpacityMicromapDesc& setFlags(EOpacityMicromapBuildFlags value) { flags = value; return *this; }
            ZWOpacityMicromapDesc& setCounts(const std::vector<ZWOpacityMicromapUsageCount>& value) { counts = value; return *this; }
            ZWOpacityMicromapDesc& setInputBuffer(IBuffer* value) { inputBuffer = value; return *this; }
            ZWOpacityMicromapDesc& setInputBufferOffset(uint64_t value) { inputBufferOffset = value; return *this; }
            ZWOpacityMicromapDesc& setPerOmmDescs(IBuffer* value) { perOmmDescs = value; return *this; }
            ZWOpacityMicromapDesc& setPerOmmDescsOffset(uint64_t value) { perOmmDescsOffset = value; return *this; }
        };

        class IOpacityMicromap : public HCommon::IResource
        {
        public:
            [[nodiscard]] virtual const ZWOpacityMicromapDesc& GetDesc() const = 0;
            [[nodiscard]] virtual bool IsCompacted() const = 0;
            [[nodiscard]] virtual uint64_t GetDeviceAddress() const = 0;
        };
        typedef HCommon::RefCountPtr<IOpacityMicromap> ZWOpacityMicromapHandle;

        // Hrt::AccelStruct
        struct ZWAccelStructDesc
        {
            size_t topLevelMaxInstances = 0; // only applies when isTopLevel = true
            std::vector<ZWGeometryDesc> bottomLevelGeometries; // only applies when isTopLevel = false
            EAccelStructBuildFlags buildFlags = EAccelStructBuildFlags::None;
            std::string debugName;
            bool trackLiveness = true;
            bool isTopLevel = false;
            bool isVirtual = false;

            ZWAccelStructDesc& setTopLevelMaxInstances(size_t value) { topLevelMaxInstances = value; isTopLevel = true; return *this; }
            ZWAccelStructDesc& addBottomLevelGeometry(const ZWGeometryDesc& value) { bottomLevelGeometries.push_back(value); isTopLevel = false; return *this; }
            ZWAccelStructDesc& setBuildFlags(EAccelStructBuildFlags value) { buildFlags = value; return *this; }
            ZWAccelStructDesc& setDebugName(const std::string& value) { debugName = value; return *this; }
            ZWAccelStructDesc& setTrackLiveness(bool value) { trackLiveness = value; return *this; }
            ZWAccelStructDesc& setIsTopLevel(bool value) { isTopLevel = value; return *this; }
            ZWAccelStructDesc& setIsVirtual(bool value) { isVirtual = value; return *this; }
        };

        class IAccelStruct : public HCommon::IResource
        {
        public:
            [[nodiscard]] virtual const ZWAccelStructDesc& GetDesc() const = 0;
            [[nodiscard]] virtual bool IsCompacted() const = 0;
            [[nodiscard]] virtual uint64_t GetDeviceAddress() const = 0;
        };
        typedef HCommon::RefCountPtr<IAccelStruct> ZWAccelStructHandle;
    }

    // Binding Sets
    struct ZWBindingSetItem
    {
        HCommon::IResource* resourceHandle;

        uint32_t slot;

        // Specifies the index in a binding array.
        // Must be less than the 'size' property of the matching BindingLayoutItem.
        // - DX11/12: Effective binding slot index is calculated as (slot + arrayElement), i.e. arrays are flattened
        // - Vulkan: Descriptor arrays are used.
        // This behavior matches the behavior of HLSL resource array declarations when compiled with DXC.
        uint32_t arrayElement;

        EResourceType type : 8;
        ETextureDimension dimension : 8; // valid for Texture_SRV, Texture_UAV
        EFormat format : 8; // valid for Texture_SRV, Texture_UAV, Buffer_SRV, Buffer_UAV
        uint8_t unused : 8;

        uint32_t unused2; // padding

        union
        {
            ZWTextureSubresourceSet subresources; // valid for Texture_SRV, Texture_UAV
            ZWBufferRange range; // valid for Buffer_SRV, Buffer_UAV, ConstantBuffer
            uint64_t rawData[2];
        };

        // verify that the `subresources` and `range` have the same size and are covered by `rawData`
        static_assert(sizeof(ZWTextureSubresourceSet) == 16, "sizeof(TextureSubresourceSet) is supposed to be 16 bytes");
        static_assert(sizeof(ZWBufferRange) == 16, "sizeof(BufferRange) is supposed to be 16 bytes");

        bool operator ==(const ZWBindingSetItem& b) const
        {
            return resourceHandle == b.resourceHandle
                && slot == b.slot
                && type == b.type
                && dimension == b.dimension
                && format == b.format
                && rawData[0] == b.rawData[0]
                && rawData[1] == b.rawData[1];
        }

        bool operator !=(const ZWBindingSetItem& b) const
        {
            return !(*this == b);
        }

        // Default constructor that doesn't initialize anything for performance:
        // BindingSetItem's are stored in large statically sized arrays.
        ZWBindingSetItem() {}  // NOLINT(cppcoreguidelines-pro-type-member-init, modernize-use-equals-default)

        // Helper functions for strongly typed initialization

        static ZWBindingSetItem None(uint32_t slot = 0)
        {
            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = EResourceType::None;
            result.resourceHandle = nullptr;
            result.format = EFormat::UNKNOWN;
            result.dimension = ETextureDimension::Unknown;
            result.rawData[0] = 0;
            result.rawData[1] = 0;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        static ZWBindingSetItem Texture_SRV(uint32_t slot, ITexture* texture, EFormat format = EFormat::UNKNOWN,
            ZWTextureSubresourceSet subresources = sAllSubresources, ETextureDimension dimension = ETextureDimension::Unknown)
        {
            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = EResourceType::Texture_SRV;
            result.resourceHandle = texture;
            result.format = format;
            result.dimension = dimension;
            result.subresources = subresources;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        static ZWBindingSetItem Texture_UAV(uint32_t slot, ITexture* texture, EFormat format = EFormat::UNKNOWN,
            ZWTextureSubresourceSet subresources = ZWTextureSubresourceSet(0, 1, 0, ZWTextureSubresourceSet::AllArraySlices),
            ETextureDimension dimension = ETextureDimension::Unknown)
        {
            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = EResourceType::Texture_UAV;
            result.resourceHandle = texture;
            result.format = format;
            result.dimension = dimension;
            result.subresources = subresources;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        static ZWBindingSetItem TypedBuffer_SRV(uint32_t slot, IBuffer* buffer, EFormat format = EFormat::UNKNOWN, ZWBufferRange range = sEntireBuffer)
        {
            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = EResourceType::TypedBuffer_SRV;
            result.resourceHandle = buffer;
            result.format = format;
            result.dimension = ETextureDimension::Unknown;
            result.range = range;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        static ZWBindingSetItem TypedBuffer_UAV(uint32_t slot, IBuffer* buffer, EFormat format = EFormat::UNKNOWN, ZWBufferRange range = sEntireBuffer)
        {
            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = EResourceType::TypedBuffer_UAV;
            result.resourceHandle = buffer;
            result.format = format;
            result.dimension = ETextureDimension::Unknown;
            result.range = range;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        static ZWBindingSetItem ConstantBuffer(uint32_t slot, IBuffer* buffer, ZWBufferRange range = sEntireBuffer)
        {
            bool isVolatile = buffer && buffer->GetDesc().isVolatile;

            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = isVolatile ? EResourceType::VolatileConstantBuffer : EResourceType::ConstantBuffer;
            result.resourceHandle = buffer;
            result.format = EFormat::UNKNOWN;
            result.dimension = ETextureDimension::Unknown;
            result.range = range;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        static ZWBindingSetItem Sampler(uint32_t slot, ISampler* sampler)
        {
            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = EResourceType::Sampler;
            result.resourceHandle = sampler;
            result.format = EFormat::UNKNOWN;
            result.dimension = ETextureDimension::Unknown;
            result.rawData[0] = 0;
            result.rawData[1] = 0;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        static ZWBindingSetItem RayTracingAccelStruct(uint32_t slot, Hrt::IAccelStruct* as)
        {
            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = EResourceType::RayTracingAccelStruct;
            result.resourceHandle = as;
            result.format = EFormat::UNKNOWN;
            result.dimension = ETextureDimension::Unknown;
            result.rawData[0] = 0;
            result.rawData[1] = 0;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        static ZWBindingSetItem StructuredBuffer_SRV(uint32_t slot, IBuffer* buffer, EFormat format = EFormat::UNKNOWN, ZWBufferRange range = sEntireBuffer)
        {
            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = EResourceType::StructuredBuffer_SRV;
            result.resourceHandle = buffer;
            result.format = format;
            result.dimension = ETextureDimension::Unknown;
            result.range = range;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        static ZWBindingSetItem StructuredBuffer_UAV(uint32_t slot, IBuffer* buffer, EFormat format = EFormat::UNKNOWN, ZWBufferRange range = sEntireBuffer)
        {
            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = EResourceType::StructuredBuffer_UAV;
            result.resourceHandle = buffer;
            result.format = format;
            result.dimension = ETextureDimension::Unknown;
            result.range = range;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        static ZWBindingSetItem RawBuffer_SRV(uint32_t slot, IBuffer* buffer, ZWBufferRange range = sEntireBuffer)
        {
            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = EResourceType::RawBuffer_SRV;
            result.resourceHandle = buffer;
            result.format = EFormat::UNKNOWN;
            result.dimension = ETextureDimension::Unknown;
            result.range = range;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        static ZWBindingSetItem RawBuffer_UAV(uint32_t slot, IBuffer* buffer, ZWBufferRange range = sEntireBuffer)
        {
            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = EResourceType::RawBuffer_UAV;
            result.resourceHandle = buffer;
            result.format = EFormat::UNKNOWN;
            result.dimension = ETextureDimension::Unknown;
            result.range = range;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        static ZWBindingSetItem PushConstants(uint32_t slot, uint32_t byteSize)
        {
            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = EResourceType::PushConstants;
            result.resourceHandle = nullptr;
            result.format = EFormat::UNKNOWN;
            result.dimension = ETextureDimension::Unknown;
            result.range.byteOffset = 0;
            result.range.byteSize = byteSize;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        static ZWBindingSetItem SamplerFeedbackTexture_UAV(uint32_t slot, ISamplerFeedbackTexture* texture)
        {
            ZWBindingSetItem result;
            result.slot = slot;
            result.arrayElement = 0;
            result.type = EResourceType::SamplerFeedbackTexture_UAV;
            result.resourceHandle = texture;
            result.format = EFormat::UNKNOWN;
            result.dimension = ETextureDimension::Unknown;
            result.subresources = sAllSubresources;
            result.unused = 0;
            result.unused2 = 0;
            return result;
        }

        ZWBindingSetItem& setArrayElement(uint32_t value) { arrayElement = value; return *this; }
        ZWBindingSetItem& setFormat(EFormat value) { format = value; return *this; }
        ZWBindingSetItem& setDimension(ETextureDimension value) { dimension = value; return *this; }
        ZWBindingSetItem& setSubresources(ZWTextureSubresourceSet value) { subresources = value; return *this; }
        ZWBindingSetItem& setRange(ZWBufferRange value) { range = value; return *this; }
    };

    // verify the packing of BindingSetItem for good alignment
    static_assert(sizeof(ZWBindingSetItem) == 40, "sizeof(BindingSetItem) is supposed to be 40 bytes");

    // Describes a set of bindings corresponding to one binding layout
    struct ZWBindingSetDesc
    {
        std::vector<ZWBindingSetItem> bindings;

        // Enables automatic liveness tracking of this binding set by hrhi command lists.
        // By setting trackLiveness to false, you take the responsibility of not releasing it 
        // until all rendering commands using the binding set are finished.
        bool trackLiveness = true;

        bool operator ==(const ZWBindingSetDesc& b) const
        {
            if (bindings.size() != b.bindings.size())
                return false;

            for (size_t i = 0; i < bindings.size(); ++i)
            {
                if (bindings[i] != b.bindings[i])
                    return false;
            }

            return true;
        }

        bool operator !=(const ZWBindingSetDesc& b) const
        {
            return !(*this == b);
        }

        ZWBindingSetDesc& addItem(const ZWBindingSetItem& value) { bindings.push_back(value); return *this; }
        ZWBindingSetDesc& setTrackLiveness(bool value) { trackLiveness = value; return *this; }
    };

    class IBindingSet : public HCommon::IResource
    {
    public:
        [[nodiscard]] virtual const ZWBindingSetDesc* GetDesc() const = 0;  // returns nullptr for descriptor tables
        [[nodiscard]] virtual IBindingLayout* GetLayout() const = 0;
    };
    typedef HCommon::RefCountPtr<IBindingSet> ZWBindingSetHandle;

    // Descriptor tables are bare, without extra mappings, state, or liveness tracking.
    // Unlike binding sets, descriptor tables are mutable - moreover, modification is the only way to populate them.
    // They can be grown or shrunk, and they are not tied to any binding layout.
    // All tracking is off, so applications should use descriptor tables with great care.
    // IDescriptorTable is derived from IBindingSet to allow mixing them in the binding arrays.
    class IDescriptorTable : public IBindingSet
    {
    public:
        [[nodiscard]] virtual uint32_t GetCapacity() const = 0;
        [[nodiscard]] virtual uint32_t GetFirstDescriptorIndexInHeap() const = 0;
    };
    typedef HCommon::RefCountPtr<IDescriptorTable> ZWDescriptorTableHandle;

    // Draw State
    struct ZWGraphicsPipelineDesc
    {
        EPrimitiveType primType = EPrimitiveType::TriangleList;
        uint32_t patchControlPoints = 0;
        ZWInputLayoutHandle inputLayout;

        ZWShaderHandle VS;
        ZWShaderHandle HS;
        ZWShaderHandle DS;
        ZWShaderHandle GS;
        ZWShaderHandle PS;

        ZWRenderState renderState;
        ZWVariableRateShadingState shadingRateState;

        BindingLayoutVector bindingLayouts;

        ZWGraphicsPipelineDesc& setPrimType(EPrimitiveType value) { primType = value; return *this; }
        ZWGraphicsPipelineDesc& setPatchControlPoints(uint32_t value) { patchControlPoints = value; return *this; }
        ZWGraphicsPipelineDesc& setInputLayout(IInputLayout* value) { inputLayout = value; return *this; }
        ZWGraphicsPipelineDesc& setVertexShader(IShader* value) { VS = value; return *this; }
        ZWGraphicsPipelineDesc& setHullShader(IShader* value) { HS = value; return *this; }
        ZWGraphicsPipelineDesc& setTessellationControlShader(IShader* value) { HS = value; return *this; }
        ZWGraphicsPipelineDesc& setDomainShader(IShader* value) { DS = value; return *this; }
        ZWGraphicsPipelineDesc& setTessellationEvaluationShader(IShader* value) { DS = value; return *this; }
        ZWGraphicsPipelineDesc& setGeometryShader(IShader* value) { GS = value; return *this; }
        ZWGraphicsPipelineDesc& setPixelShader(IShader* value) { PS = value; return *this; }
        ZWGraphicsPipelineDesc& setFragmentShader(IShader* value) { PS = value; return *this; }
        ZWGraphicsPipelineDesc& setRenderState(const ZWRenderState& value) { renderState = value; return *this; }
        ZWGraphicsPipelineDesc& setVariableRateShadingState(const ZWVariableRateShadingState& value) { shadingRateState = value; return *this; }
        ZWGraphicsPipelineDesc& addBindingLayout(IBindingLayout* layout) { bindingLayouts.push_back(layout); return *this; }
    };

    class IGraphicsPipeline : public HCommon::IResource
    {
    public:
        [[nodiscard]] virtual const ZWGraphicsPipelineDesc& GetDesc() const = 0;
        [[nodiscard]] virtual const ZWFramebufferInfo& GetFramebufferInfo() const = 0;
    };
    typedef HCommon::RefCountPtr<IGraphicsPipeline> ZWGraphicsPipelineHandle;

    struct ZWComputePipelineDesc
    {
        ZWShaderHandle CS;

        BindingLayoutVector bindingLayouts;

        ZWComputePipelineDesc& SetComputeShader(IShader* value) { CS = value; return *this; }
        ZWComputePipelineDesc& AddBindingLayout(IBindingLayout* layout) { bindingLayouts.push_back(layout); return *this; }
    };

    class IComputePipeline : public HCommon::IResource
    {
    public:
        [[nodiscard]] virtual const ZWComputePipelineDesc& GetDesc() const = 0;
    };
    typedef HCommon::RefCountPtr<IComputePipeline> ZWComputePipelineHandle;

    struct ZWMeshletPipelineDesc
    {
        EPrimitiveType primType = EPrimitiveType::TriangleList;

        ZWShaderHandle AS;
        ZWShaderHandle MS;
        ZWShaderHandle PS;

        ZWRenderState renderState;

        BindingLayoutVector bindingLayouts;

        ZWMeshletPipelineDesc& SetPrimType(EPrimitiveType value) { primType = value; return *this; }
        ZWMeshletPipelineDesc& SetTaskShader(IShader* value) { AS = value; return *this; }
        ZWMeshletPipelineDesc& SetAmplificationShader(IShader* value) { AS = value; return *this; }
        ZWMeshletPipelineDesc& SetMeshShader(IShader* value) { MS = value; return *this; }
        ZWMeshletPipelineDesc& SetPixelShader(IShader* value) { PS = value; return *this; }
        ZWMeshletPipelineDesc& SetFragmentShader(IShader* value) { PS = value; return *this; }
        ZWMeshletPipelineDesc& SetRenderState(const ZWRenderState& value) { renderState = value; return *this; }
        ZWMeshletPipelineDesc& AddBindingLayout(IBindingLayout* layout) { bindingLayouts.push_back(layout); return *this; }
    };

    class IMeshletPipeline : public HCommon::IResource
    {
    public:
        [[nodiscard]] virtual const ZWMeshletPipelineDesc& GetDesc() const = 0;
        [[nodiscard]] virtual const ZWFramebufferInfo& GetFramebufferInfo() const = 0;
    };
    typedef HCommon::RefCountPtr<IMeshletPipeline> ZWMeshletPipelineHandle;

    // Draw and Dispatch
    class IEventQuery : public HCommon::IResource {};
    typedef HCommon::RefCountPtr<IEventQuery> ZWEventQueryHandle;

    class ITimerQuery : public HCommon::IResource {};
    typedef HCommon::RefCountPtr<ITimerQuery> ZWTimerQueryHandle;

    // Ray Tracing
    namespace Hrt
    {
        struct ZWPipelineShaderDesc
        {
            std::string exportName;
            ZWShaderHandle shader;
            ZWBindingLayoutHandle bindingLayout;

            ZWPipelineShaderDesc& setExportName(const std::string& value) { exportName = value; return *this; }
            ZWPipelineShaderDesc& setShader(IShader* value) { shader = value; return *this; }
            ZWPipelineShaderDesc& setBindingLayout(IBindingLayout* value) { bindingLayout = value; return *this; }
        };

        struct ZWPipelineHitGroupDesc
        {
            std::string exportName;
            ZWShaderHandle closestHitShader;
            ZWShaderHandle anyHitShader;
            ZWShaderHandle intersectionShader;
            ZWBindingLayoutHandle bindingLayout;
            bool isProceduralPrimitive = false;

            ZWPipelineHitGroupDesc& setExportName(const std::string& value) { exportName = value; return *this; }
            ZWPipelineHitGroupDesc& setClosestHitShader(IShader* value) { closestHitShader = value; return *this; }
            ZWPipelineHitGroupDesc& setAnyHitShader(IShader* value) { anyHitShader = value; return *this; }
            ZWPipelineHitGroupDesc& setIntersectionShader(IShader* value) { intersectionShader = value; return *this; }
            ZWPipelineHitGroupDesc& setBindingLayout(IBindingLayout* value) { bindingLayout = value; return *this; }
            ZWPipelineHitGroupDesc& setIsProceduralPrimitive(bool value) { isProceduralPrimitive = value; return *this; }
        };

        struct ZWPipelineDesc
        {
            std::vector<ZWPipelineShaderDesc> shaders;
            std::vector<ZWPipelineHitGroupDesc> hitGroups;
            BindingLayoutVector globalBindingLayouts;
            uint32_t maxPayloadSize = 0;
            uint32_t maxAttributeSize = sizeof(float) * 2; // typical case: float2 uv;
            uint32_t maxRecursionDepth = 1;
            int32_t hlslExtensionsUAV = -1;
            bool allowOpacityMicromaps = false;

            ZWPipelineDesc& addShader(const ZWPipelineShaderDesc& value) { shaders.push_back(value); return *this; }
            ZWPipelineDesc& addHitGroup(const ZWPipelineHitGroupDesc& value) { hitGroups.push_back(value); return *this; }
            ZWPipelineDesc& addBindingLayout(IBindingLayout* value) { globalBindingLayouts.push_back(value); return *this; }
            ZWPipelineDesc& setMaxPayloadSize(uint32_t value) { maxPayloadSize = value; return *this; }
            ZWPipelineDesc& setMaxAttributeSize(uint32_t value) { maxAttributeSize = value; return *this; }
            ZWPipelineDesc& setMaxRecursionDepth(uint32_t value) { maxRecursionDepth = value; return *this; }
            ZWPipelineDesc& setHlslExtensionsUAV(int32_t value) { hlslExtensionsUAV = value; return *this; }
            ZWPipelineDesc& setAllowOpacityMicromaps(bool value) { allowOpacityMicromaps = value; return *this; }
        };

        class IPipeline;

        struct ZWShaderTableDesc
        {
            // Controls the memory usage and building behavior of the shader table.
            //
            // - When a shader table is cached, it creates an additional buffer that holds the built shader table.
            //   This buffer is updated in CommandList::setRayTracingState after the shader table is modified.
            // - When a shader table is uncached, this buffer is suballocated from the upload manager when the shader
            //   table is first used in CommandList::setRayTracingState after opening a command list, and reallocated
            //   and rebuilt on subsequent calls to setRayTracingState if the shader table is modified.
            //
            // The legacy and default behavior is uncached.
            // It is recommended to enable caching for large and infrequently updated shader tables.
            bool isCached = false;

            // Maximum number of entries in a cached shader table.
            // Must be nonzero when isCached == true.
            // Ignored when isCached == false.
            uint32_t maxEntries = 0;

            std::string debugName;

            ZWShaderTableDesc& setIsCached(bool value) { isCached = value; return *this; }
            ZWShaderTableDesc& setMaxEntries(uint32_t value) { maxEntries = value; return *this; }
            ZWShaderTableDesc& setDebugName(const std::string& value) { debugName = value; return *this; }
            ZWShaderTableDesc& enableCaching(uint32_t _maxEntries) { isCached = true; maxEntries = _maxEntries; return *this; }
        };

        class IShaderTable : public HCommon::IResource
        {
        public:
            virtual ZWShaderTableDesc const& GetDesc() const = 0;
            virtual uint32_t GetNumEntries() const = 0;
            virtual IPipeline* GetPipeline() const = 0;
            virtual void SetRayGenerationShader(const char* exportName, IBindingSet* bindings = nullptr) = 0;
            virtual int AddMissShader(const char* exportName, IBindingSet* bindings = nullptr) = 0;
            virtual int AddHitGroup(const char* exportName, IBindingSet* bindings = nullptr) = 0;
            virtual int AddCallableShader(const char* exportName, IBindingSet* bindings = nullptr) = 0;
            virtual void ClearMissShaders() = 0;
            virtual void ClearHitShaders() = 0;
            virtual void ClearCallableShaders() = 0;
        };
        typedef HCommon::RefCountPtr<IShaderTable> ZWShaderTableHandle;

        class IPipeline : public HCommon::IResource
        {
        public:
            [[nodiscard]] virtual const Hrt::ZWPipelineDesc& GetDesc() const = 0;
            virtual ZWShaderTableHandle CreateShaderTable(ZWShaderTableDesc const& desc = ZWShaderTableDesc()) = 0;
        };

        typedef HCommon::RefCountPtr<IPipeline> ZWPipelineHandle;
    }

    // Misc

    // IMessageCallback should be implemented by the application.
    class IMessageCallback
    {
    protected:
        IMessageCallback() = default;
        virtual ~IMessageCallback() = default;

    public:
        // HRHI will call message(...) whenever it needs to signal something.
        // The application is free to ignore the messages, show message boxes, or terminate.
        virtual void message(EMessageSeverity severity, const char* messageText) = 0;

        IMessageCallback(const IMessageCallback&) = delete;
        IMessageCallback(const IMessageCallback&&) = delete;
        IMessageCallback& operator=(const IMessageCallback&) = delete;
        IMessageCallback& operator=(const IMessageCallback&&) = delete;
    };

    // ICommandList
    // - DX12: One command list object may contain multiple instances of ID3D12GraphicsCommandList* and
    //   ID3D12CommandAllocator objects, reusing older ones as they finish executing on the GPU. A command list object
    //   also contains the upload manager (for suballocating memory from the upload heap on operations such as
    //   writeBuffer) and the DXR scratch manager (for suballocating memory for acceleration structure builds).
    //   The upload and scratch managers' memory is reused when possible, but it is only freed when the command list
    //   object is destroyed. Thus, it might be a good idea to use a dedicated HRHI command list for uploading large
    //   amounts of data and to destroy it when uploading is finished.
    // - Vulkan: The command list objects don't own the VkCommandBuffer-s but request available ones from the queue
    //   instead. The upload and scratch buffers behave the same way they do on DX12.
    class ICommandList : public HCommon::IResource
    {
    public:
        // Prepares the command list for recording a new sequence of commands.
        // All other methods of ICommandList must only be used when the command list is open.
        // - DX12, Vulkan: Creates or reuses the command list or buffer object and the command allocator (DX12),
        //   starts tracking the resources being referenced in the command list.
        virtual void Open() = 0;

        // Finalizes the command list and prepares it for execution.
        // Use IDevice::executeCommandLists(...) to execute it.
        // Re-opening the command list without execution is allowed but not well-tested.
        virtual void Close() = 0;

        // Resets the HRHI state cache associated with the command list, clears some of the underlying API state.
        // This method is mostly useful when switching from recording commands to the open command list using 
        // non-HRHI code - see getNativeObject(...) - to recording further commands using HRHI.
        virtual void ClearState() = 0;

        // Clears some or all subresources of the given color texture using the provided color.
        // - DX12: The clear operation uses either an RTV or a UAV, depending on the texture usage flags
        //   (isRenderTarget and isUAV).
        // - Vulkan: vkCmdClearColorImage is always used with the Float32 color fields set.
        // At least one of the 'isRenderTarget' and 'isUAV' flags must be set, and the format of the texture
        // must be of a color type.
        virtual void ClearTextureFloat(ITexture* t, ZWTextureSubresourceSet subresources, const ZWColor& clearColor) = 0;

        // Clears some or all subresources of the given depth-stencil texture using the provided depth and/or stencil
        // values. The texture must have the isRenderTarget flag set, and its format must be of a depth-stencil type.
        virtual void ClearDepthStencilTexture(ITexture* t, ZWTextureSubresourceSet subresources, bool clearDepth,
            float depth, bool clearStencil, uint8_t stencil) = 0;

        // Clears some or all subresources of the given color texture using the provided integer value.
        // - DX12: If the texture has the isUAV flag set, the clear is performed using ClearUnorderedAccessViewUint.
        //   Otherwise, the clear value is converted to a float, and the texture is cleared as an RTV with all 4
        //   color components using the same value.
        // - Vulkan: vkCmdClearColorImage is always used with the UInt32 and Int32 color fields set.
        virtual void ClearTextureUInt(ITexture* t, ZWTextureSubresourceSet subresources, uint32_t clearColor) = 0;

        // Copies a single 2D or 3D region of texture data from texture 'src' into texture 'dst'.
        // The region's dimensions must be compatible between the two textures, meaning that for simple color textures
        // they must be equal, and for reinterpret copies between compressed and uncompressed textures, they must differ
        // by a factor equal to the block size. The function does not resize textures, only 1:1 pixel copies are
        // supported.
        virtual void CopyTexture(ITexture* dest, const ZWTextureSlice& destSlice, ITexture* src,
            const ZWTextureSlice& srcSlice) = 0;

        // Copies a single 2D or 3D region of texture data from regular texture 'src' into staging texture 'dst'.
        virtual void CopyTexture(IStagingTexture* dest, const ZWTextureSlice& destSlice, ITexture* src,
            const ZWTextureSlice& srcSlice) = 0;

        // Copies a single 2D or 3D region of texture data from staging texture 'src' into regular texture 'dst'.
        virtual void CopyTexture(ITexture* dest, const ZWTextureSlice& destSlice, IStagingTexture* src,
            const ZWTextureSlice& srcSlice) = 0;

        // Uploads the contents of an entire 2D or 3D mip level of a single array slice of the texture from CPU memory.
        // The data in CPU memory must be in the same pixel format as the texture. Pixels in every row must be tightly
        // packed, rows are packed with a stride of 'rowPitch' which must not be 0 unless the texture has a height of 1,
        // and depth slices are packed with a stride of 'depthPitch' which also must not be 0 if the texture is 3D.
        // - DX12, Vulkan: A region of the automatic upload buffer is suballocated, data is copied there, and then
        //   copied on the GPU into the destination texture using CopyTextureRegion (DX12) or vkCmdCopyBufferToImage (VK).
        //   The upload buffer region can only be reused when this command list instance finishes executing on the GPU.
        // For more advanced uploading operations, such as updating only a region in the texture, use staging texture
        // objects and copyTexture(...).
        virtual void WriteTexture(ITexture* dest, uint32_t arraySlice, uint32_t mipLevel, const void* data,
            size_t rowPitch, size_t depthPitch = 0) = 0;

        // Performs a resolve operation to combine samples from some or all subresources of a multisample texture 'src'
        // into matching subresources of a non-multisample texture 'dest'. Both textures' formats must be of color type.
        // - DX12: Maps to a sequence of ResolveSubresource calls, one per subresource.
        // - Vulkan: Maps to a single vkCmdResolveImage call.
        virtual void ResolveTexture(ITexture* dest, const ZWTextureSubresourceSet& dstSubresources, ITexture* src,
            const ZWTextureSubresourceSet& srcSubresources) = 0;

        // Uploads 'dataSize' bytes of data from CPU memory into the GPU buffer 'b' at offset 'destOffsetBytes'.
        // - DX12: If the buffer's 'isVolatile' flag is set, a region of the automatic upload buffer is suballocated,
        //   and the data is copied there. Subsequent uses of the buffer will directly refer to that location in the
        //   upload buffer, until the next call to writeBuffer(...) or until the command list is closed. A volatile
        //   buffer can not be used until writeBuffer(...) is called on it every time after the command list is opened.
        //   If the 'isVolatile' flag is not set, a region of the automatic upload buffer is suballocated, the data
        //   is copied there, and then copied into the real GPU buffer object using CopyBufferRegion.
        // - Vulkan: Similar behavior to DX12, except that each volatile buffer actually has its own Vulkan resource.
        //   The size of such resource is determined by the 'maxVersions' field of the BufferDesc. When writeBuffer(...)
        //   is called on a volatile buffer, a region of that buffer object (a single version) is suballocated, data
        //   is copied there, and subsequent uses of the buffer in the same command list will refer to that version.
        //   For non-volatile buffers, writes of 64 kB or smaller use vkCmdUpdateBuffer. Larger writes suballocate
        //   a portion of the automatic upload buffer and copy the data to the real GPU buffer through that and 
        //   vkCmdCopyBuffer.
        virtual void WriteBuffer(IBuffer* b, const void* data, size_t dataSize, uint64_t destOffsetBytes = 0) = 0;

        // Fills the entire buffer using the provided uint32 value.
        // - DX12: Maps to ClearUnorderedAccessViewUint.
        // - Vulkan: Maps to vkCmdFillBuffer.
        virtual void ClearBufferUInt(IBuffer* b, uint32_t clearValue) = 0;

        // Copies 'dataSizeBytes' of data from buffer 'src' at offset 'srcOffsetBytes' into buffer 'dest' at offset
        // 'destOffsetBytes'. The source and destination regions must be within the sizes of the respective buffers.
        // - DX12: Maps to CopyBufferRegion.
        // - Vulkan: Maps to vkCmdCopyBuffer.
        virtual void CopyBuffer(IBuffer* dest, uint64_t destOffsetBytes, IBuffer* src, uint64_t srcOffsetBytes,
            uint64_t dataSizeBytes) = 0;

        // Clears the entire sampler feedback texture.
        // - DX12: Maps to ClearUnorderedAccessViewUint.
        // - Vulkan: Unsupported.
        virtual void ClearSamplerFeedbackTexture(ISamplerFeedbackTexture* texture) = 0;

        // Decodes the sampler feedback texture into an application-usable format, storing data into the provided buffer.
        // The 'format' parameter should be Format::R8_UINT.
        // - DX12: Maps to ResolveSubresourceRegion.
        //   See https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html
        // - Vulkan: Unsupported.
        virtual void DecodeSamplerFeedbackTexture(IBuffer* buffer, ISamplerFeedbackTexture* texture,
            EFormat format) = 0;

        // Transitions the sampler feedback texture into the requested state, placing a barrier if necessary.
        // The barrier is appended into the pending barrier list and not issued immediately,
        // instead waiting for any rendering, compute or transfer operation.
        // Use commitBarriers() to issue the barriers explicitly.
        // Like the other sampler feedback functions, only supported on DX12.
        virtual void SetSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, EResourceStates stateBits) = 0;

        // Writes the provided data into the push constants block for the currently set pipeline.
        // A graphics, compute, ray tracing or meshlet state must be set using the corresponding call
        // (setGraphicsState etc.) before using setPushConstants. Changing the state invalidates push constants.
        // - DX12: Push constants map to root constants in the PSO/root signature. This function maps to 
        //   SetGraphicsRoot32BitConstants for graphics or meshlet pipelines, and SetComputeRoot32BitConstants for
        //   compute or ray tracing pipelines.
        // - Vulkan: Push constants are just Vulkan push constants. This function maps to vkCmdPushConstants.
        // Note that HRHI only supports one push constants binding in all layouts used in a pipeline.
        virtual void SetPushConstants(const void* data, size_t byteSize) = 0;

        // Sets the specified graphics state on the command list.
        // The state includes the pipeline (or individual shaders on DX11) and all resources bound to it,
        // from input buffers to render targets. See the members of GraphicsState for more information.
        // State is cached by HRHI, so if some parts of it are not modified by the setGraphicsState(...) call,
        // the corresponding changes won't be made on the underlying graphics API. When combining command list
        // operations made through HRHI and through direct access to the command list, state caching may lead to
        // incomplete or incorrect state being set on the underlying API because of cache mismatch with the actual
        // state. To avoid these issues, call clearState() when switching from direct command list access to HRHI.
        virtual void SetGraphicsState(const ZWGraphicsState& state) = 0;

        // Draws non-indexed primitives using the current graphics state.
        // setGraphicsState(...) must be called between opening the command list or using other types of pipelines
        // and calling draw(...) or any of its siblings. If the pipeline uses push constants, those must be set
        // using setPushConstants(...) between setGraphicsState(...) and draw(...). If the pipeline uses volatile
        // constant buffers, their contents must be written using writeBuffer(...) between open(...) and draw(...),
        // which may be before or after setGraphicsState(...).
        // - DX12: Maps to DrawInstanced.
        // - Vulkan: Maps to vkCmdDraw.
        virtual void Draw(const ZWDrawArguments& args) = 0;

        // Draws indexed primitives using the current graphics state.
        // See the comment to draw(...) for state information.
        // - DX12: Maps to DrawIndexedInstanced.
        // - Vulkan: Maps to vkCmdDrawIndexed.
        virtual void DrawIndexed(const ZWDrawArguments& args) = 0;

        // Draws one or multiple sets of non-indexed primitives using the parameters provided in the indirect buffer
        // specified in the prior call to setGraphicsState(...). The memory layout in the buffer is the same for all
        // graphics APIs and is described by the DrawIndirectArguments structure. If drawCount is more than 1,
        // multiple sets of primitives are drawn, and the parameter structures for them are tightly packed in the
        // indirect parameter buffer one after another.
        // See the comment to draw(...) for state information.
        // - DX12: Maps to ExecuteIndirect with a predefined signature.
        // - Vulkan: Maps to vkCmdDrawIndirect.
        virtual void DrawIndirect(uint32_t offsetBytes, uint32_t drawCount = 1) = 0;

        // Draws one or multiple sets of indexed primitives using the parameters provided in the indirect buffer
        // specified in the prior call to setGraphicsState(...). The memory layout in the buffer is the same for all
        // graphics APIs and is described by the DrawIndexedIndirectArguments structure. If drawCount is more than 1,
        // multiple sets of primitives are drawn, and the parameter structures for them are tightly packed in the
        // indirect parameter buffer one after another.
        // See the comment to draw(...) for state information.
        // - DX12: Maps to ExecuteIndirect with a predefined signature.
        // - Vulkan: Maps to vkCmdDrawIndexedIndirect.
        virtual void DrawIndexedIndirect(uint32_t offsetBytes, uint32_t drawCount = 1) = 0;

        // Draws primitives with indexed vertices using the parameters provided in the indirect arguments buffer
        //   at offset 'paramOffsetBytes'.
        // The draw count is read from the indirectCountBuffer specified in setGraphicsState(...)
        //   at offset 'countOffsetBytes'.
        // - DX12: Maps to ExecuteIndirect with pCountBuffer parameter.
        // - Vulkan: Maps to vkCmdDrawIndexedIndirectCount.
        virtual void DrawIndexedIndirectCount(uint32_t paramOffsetBytes, uint32_t countOffsetBytes, uint32_t maxDrawCount) = 0;

        // Sets the specified compute state on the command list.
        // The state includes the pipeline and all resources bound to it.
        // See the members of ComputeState for more information.
        // See the comment to setGraphicsState(...) for information on state caching.
        virtual void SetComputeState(const ZWComputeState& state) = 0;

        // Launches a compute kernel using the current compute state.
        // See the comment to draw(...) for information on state setting, push constants, and volatile constant buffers,
        // replacing graphics with compute.
        // - DX12: Maps to Dispatch.
        // - Vulkan: Maps to vkCmdDispatch.
        virtual void Dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) = 0;

        // Launches a compute kernel using the parameters provided in the indirect buffer specified in the prior
        // call to setComputeState(...). The memory layout in the buffer is the same for all graphics APIs and is
        // described by the DispatchIndirectArguments structure.
        // See the comment to dispatch(...) for state information.
        // - DX12: Maps to ExecuteIndirect with a predefined signature.
        // - Vulkan: Maps to vkCmdDispatchIndirect.
        virtual void DispatchIndirect(uint32_t offsetBytes) = 0;

        // Sets the specified meshlet rendering state on the command list.
        // The state includes the pipeline and all resources bound to it.
        // Meshlet support on DX12 and Vulkan can be queried using IDevice::queryFeatureSupport(Feature::Meshlets).
        // See the members of MeshletState for more information.
        // See the comment to setGraphicsState(...) for information on state caching.
        virtual void SetMeshletState(const ZWMeshletState& state) = 0;

        // Draws meshlet primitives using the current meshlet state.
        // See the comment to draw(...) for information on state setting, push constants, and volatile constant buffers,
        // replacing graphics with meshlets.
        // - DX12: Maps to DispatchMesh.
        // - Vulkan: Maps to vkCmdDispatchMesh.
        virtual void DispatchMesh(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) = 0;

        // Sets the specified ray tracing state on the command list.
        // The state includes the shader table, which references the pipeline, and all bound resources.
        // See the members of rt::State for more information.
        // See the comment to setGraphicsState(...) for information on state caching.
        virtual void SetRayTracingState(const Hrt::ZWState& state) = 0;

        // Launches a grid of ray generation shader threads using the current ray tracing state.
        // The ray generation shader to use is specified by the shader table, which currently supports only one
        // ray generation shader. There may be multiple shaders of all other ray tracing types in the shader table.
        // See the comment to draw(...) for information on state setting, push constants, and volatile constant buffers,
        // replacing graphics with ray tracing.
        // - DX12: Maps to DispatchRays.
        // - Vulkan: Maps to vkCmdTraceRaysKHR.
        virtual void DispatchRays(const Hrt::ZWDispatchRaysArguments& args) = 0;

        // Launches an opacity micromap (OMM) build kernel.
        // A temporary memory region for the build is suballocated using the scratch buffer manager attached to the
        // command list. The size of this memory region is determined automatically inside this function.
        // - DX12: Maps to NvAPI_D3D12_BuildRaytracingOpacityMicromapArray and requires NVAPI.
        // - Vulkan: Maps to vkCmdBuildMicromapsEXT.
        virtual void BuildOpacityMicromap(Hrt::IOpacityMicromap* omm, const Hrt::ZWOpacityMicromapDesc& desc) = 0;

        // Builds or updates a bottom-level ray tracing acceleration structure (BLAS).
        // A temporary memory region for the build is suballocated using the scratch buffer manager attached to the
        // command list. The size of this memory region is determined automatically inside this function.
        // The type of operation to perform is specified by the buildFlags parameter.
        // When building a new BLAS, the amount of memory allocated for it must be sufficient to build the BLAS
        // for the provided geometry. Usually this is achieved by passing the same geometry descriptors to this function
        // and to IDevice::createAccelStruct(...).
        // When updating a BLAS, the geometries and primitive counts must match the BLAS that was previously built,
        // and the BLAS must have been built with the AllowUpdate flag.
        // If compaction is enabled when building the BLAS, the BLAS cannot be rebuilt or updated later, it can only
        // be compacted.
        // - DX12: Maps to BuildRaytracingAccelerationStructure, or NvAPI_D3D12_BuildRaytracingAccelerationStructureEx
        //   if Opacity Micromaps or Line-Swept Sphere geometries are supported by the device.
        // - Vulkan: Maps to vkCmdBuildAccelerationStructuresKHR.
        // If HRHI is built with RTXMU enabled, all BLAS builds, updates and compactions are handled by RTXMU.
        // Note that RTXMU currently doesn't support OMM or LSS.
        virtual void BuildBottomLevelAccelStruct(Hrt::IAccelStruct* as, const Hrt::ZWGeometryDesc* pGeometries,
            size_t numGeometries, Hrt::EAccelStructBuildFlags buildFlags = Hrt::EAccelStructBuildFlags::None) = 0;

        // Compacts all bottom-level ray tracing acceleration structures (BLASes) that are currently available
        // for compaction. This process is handled by the RTXMU library. If HRHI is built without RTXMU,
        // this function has no effect.
        virtual void CompactBottomLevelAccelStructs() = 0;

        // Builds or updates a top-level ray tracing acceleration structure (TLAS).
        // A temporary memory region for the build is suballocated using the scratch buffer manager attached to the
        // command list. The size of this memory region is determined automatically inside this function.
        // The type of operation to perform is specified by the buildFlags parameter.
        // When building a new TLAS, the amount of memory allocated for it must be sufficient to build the TLAS
        // for the provided geometry. Usually this is achieved by making sure that the instance count does not exceed
        // that provided to IDevice::createAccelStruct(...).
        // When updating a TLAS, the instance counts and types must match the TLAS that was previously built,
        // and the TLAS must have been built with the AllowUpdate flag.
        // - DX12: Maps to BuildRaytracingAccelerationStructure.
        // - Vulkan: Maps to vkCmdBuildAccelerationStructuresKHR.
        virtual void BuildTopLevelAccelStruct(Hrt::IAccelStruct* as, const Hrt::ZWInstanceDesc* pInstances,
            size_t numInstances, Hrt::EAccelStructBuildFlags buildFlags = Hrt::EAccelStructBuildFlags::None) = 0;

        // Performs one of the supported operations on clustered ray tracing acceleration structures (CLAS).
        // See the comments to rt::cluster::OperationDesc for more information.
        // - DX12: Maps to NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation and requires NVAPI.
        // - Vulkan: Not supported.
        virtual void ExecuteMultiIndirectClusterOperation(const Hrt::HCluster::ZWOperationDesc& desc) = 0;

        // Builds or updates a top-level ray tracing acceleration structure (TLAS) using instance data provided
        // through a buffer on the GPU. The buffer must be pre-filled with rt::InstanceDesc structures using a
        // copy operation or a shader. No validation on the buffer contents is performed by HRHI, and no state
        // or liveness tracking is done for the referenced BLAS'es.
        // See the comment to buildTopLevelAccelStruct(...) for more information.
        // - DX12: Maps to BuildRaytracingAccelerationStructure.
        // - Vulkan: Maps to vkCmdBuildAccelerationStructuresKHR.
        virtual void BuildTopLevelAccelStructFromBuffer(Hrt::IAccelStruct* as, IBuffer* instanceBuffer,
            uint64_t instanceBufferOffset, size_t numInstances,
            Hrt::EAccelStructBuildFlags buildFlags = Hrt::EAccelStructBuildFlags::None) = 0;

        // Converts one or several CoopVec compatible matrices between layouts in GPU memory.
        // Source and destination buffers must be different.
        // - DX12: Maps to ConvertLinearAlgebraMatrix.
        // - Vulkan: Maps to vkCmdConvertCooperativeVectorMatrixNV.
        virtual void ConvertCoopVecMatrices(HCoopVec::ZWConvertMatrixLayoutDesc const* convertDescs, size_t numDescs) = 0;

        // Starts measuring GPU execution time using the provided timer query at this point in the command list.
        // Use endTimerQuery(...) to stop measuring time, and IDevice::getTimerQueryTime(...) to get the results later.
        // The same timer query cannot be used multiple times within the same command list, or in different
        // command lists until it is resolved.
        // - DX12: Maps to EndQuery.
        // - Vulkan: Maps to vkCmdResetQueryPool and vkCmdWriteTimestamp.
        virtual void BeginTimerQuery(ITimerQuery* query) = 0;

        // Stops measuring GPU execution time using the provided timer query at this point in the command list.
        // beginTimerQuery(...) must have been used on the same timer query in this command list previously.
        // - DX12: Maps to EndQuery and ResolveQueryData.
        // - Vulkan: Maps to vkCmdWriteTimestamp.
        virtual void EndTimerQuery(ITimerQuery* query) = 0;

        // Places a debug marker denoting the beginning of a range of commands in the command list.
        // Use endMarker() to denote the end of the range. Ranges may be nested, i.e. calling beginMarker(...)
        // multiple times, followed by multiple endMarker(), is allowed.
        // - DX12: Maps to PIXBeginEvent.
        // - Vulkan: Maps to cmdBeginDebugUtilsLabelEXT or cmdDebugMarkerBeginEXT.
        // If NSight Aftermath integration is enabled, also calls GFSDK_Aftermath_SetEventMarker on DX11 and DX12.
        virtual void BeginMarker(const char* name) = 0;

        // Places a debug marker denoting the end of a range of commands in the command list.
        // - DX12: Maps to PIXEndEvent.
        // - Vulkan: Maps to cmdEndDebugUtilsLabelEXT or cmdDebugMarkerEndEXT.
        virtual void EndMarker() = 0;

        // Enables or disables the automatic barrier placement on set[...]State, copy, write, and clear operations.
        // By default, automatic barriers are enabled, but can be optionally disabled to improve CPU performance
        // and/or specific barrier placement. When automatic barriers are disabled, it is application's responsibility
        // to set correct states for all used resources.
        virtual void SetEnableAutomaticBarriers(bool enable) = 0;

        // Sets the necessary resource states for all non-permanent resources used in the binding set.
        virtual void SetResourceStatesForBindingSet(IBindingSet* bindingSet) = 0;

        // Sets the necessary resource states for all targets of the framebuffer.
        void SetResourceStatesForFramebuffer(IFramebuffer* framebuffer);

        // Enables or disables the placement of UAV barriers for the given texture (DX12/VK) or all resources (DX11)
        // between draw or dispatch calls. Disabling UAV barriers may improve performance in cases when the same
        // resource is used by multiple draws or dispatches, but they don't depend on each other's results.
        // Note that this only affects barriers between multiple uses of the same texture as a UAV, and the
        // transition barrier when the texture is first used as a UAV will still be placed.
        // - DX12, Vulkan: Does not map to any specific API calls, affects HRHI automatic barriers.
        virtual void SetEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers) = 0;

        // Enables or disables the placement of UAV barriers for the given buffer (DX12/VK)
        // between draw or dispatch calls.
        // See the comment to setEnableUavBarriersForTexture(...) for more information.
        virtual void SetEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers) = 0;

        // Informs the command list state tracker of the current state of a texture or some of its subresources.
        // This function must be called after opening the command list and before the first use of any textures 
        // that do not have the keepInitialState flag set, and that were not transitioned to a permanent state
        // previously using setPermanentTextureState(...).
        virtual void BeginTrackingTextureState(ITexture* texture, ZWTextureSubresourceSet subresources,
            EResourceStates stateBits) = 0;

        // Informs the command list state tracker of the current state of a buffer.
        // See the comment to beginTrackingTextureState(...) for more information.
        virtual void BeginTrackingBufferState(IBuffer* buffer, EResourceStates stateBits) = 0;

        // Places the necessary barriers to make sure that the texture or some of its subresources are in the given
        // state. If the texture or subresources are already in that state, no action is performed.
        // If the texture was previously transitioned to a permanent state, the new state must be compatible
        // with that permanent state, and no action is performed.
        // The barriers are not immediately submitted to the underlying graphics API, but are placed to the pending
        // list instead. Call commitBarriers() to submit them to the graphics API explicitly or set graphics
        // or other type of state.
        virtual void SetTextureState(ITexture* texture, ZWTextureSubresourceSet subresources,
            EResourceStates stateBits) = 0;

        // Places the necessary barriers to make sure that the buffer is in the given state.
        // See the comment to setTextureState(...) for more information.
        virtual void SetBufferState(IBuffer* buffer, EResourceStates stateBits) = 0;

        // Places the necessary barriers to make sure that the underlying buffer for the acceleration structure is
        // in the given state. See the comment to setTextureState(...) for more information.
        virtual void SetAccelStructState(Hrt::IAccelStruct* as, EResourceStates stateBits) = 0;

        // Places the necessary barriers to make sure that the entire texture is in the given state, and marks that
        // state as the texture's permanent state. Once a texture is transitioned into a permanent state, its state
        // can not be modified. This can improve performance by excluding the texture from automatic state tracking
        // in the future.
        // The barriers are not immediately submitted to the underlying graphics API, but are placed to the pending
        // list instead. Call commitBarriers() to submit them to the graphics API explicitly or set graphics
        // or other type of state.
        // Note that the permanent state transitions affect all command lists, and are only applied when the command
        // list that sets them is executed. If the command list is closed but not executed, the permanent states
        // will be abandoned.
        virtual void SetPermanentTextureState(ITexture* texture, EResourceStates stateBits) = 0;

        // Places the necessary barriers to make sure that the buffer is in the given state, and marks that state
        // as the buffer's permanent state. See the comment to setPermanentTextureState(...) for more information.
        virtual void SetPermanentBufferState(IBuffer* buffer, EResourceStates stateBits) = 0;

        // Flushes the barriers from the pending list into the graphics API command list.
        virtual void CommitBarriers() = 0;

        // Returns the current tracked state of a texture subresource.
        // If the state is not known to the command list, returns ResourceStates::Unknown. Using the texture in this
        // state is not allowed.
        virtual EResourceStates GetTextureSubresourceState(ITexture* texture, ArraySlice arraySlice,
            MipLevel mipLevel) = 0;

        // Returns the current tracked state of a buffer.
        // See the comment to getTextureSubresourceState(...) for more information.
        virtual EResourceStates GetBufferState(IBuffer* buffer) = 0;

        // Returns the owning device, does NOT call AddRef on it.
        virtual IDevice* GetDevice() = 0;

        // Returns the CommandListParameters structure that was used to create the command list. 
        virtual const ZWCommandListParameters& GetDesc() = 0;
    };
    typedef HCommon::RefCountPtr<ICommandList> ZWCommandListHandle;

    // IDevice
    class IDevice : public HCommon::IResource
    {
    public:
        virtual ZWHeapHandle CreateHeap(const ZWHeapDesc& d) = 0;

        virtual ZWTextureHandle CreateTexture(const ZWTextureDesc& d) = 0;
        virtual ZWMemoryRequirements GetTextureMemoryRequirements(ITexture* texture) = 0;
        virtual bool BindTextureMemory(ITexture* texture, IHeap* heap, uint64_t offset) = 0;

        virtual ZWTextureHandle CreateHandleForNativeTexture(ObjectType objectType, HCommon::ZWObject texture, const ZWTextureDesc& desc) = 0;

        virtual ZWStagingTextureHandle CreateStagingTexture(const ZWTextureDesc& d, ECpuAccessMode cpuAccess) = 0;
        virtual void* MapStagingTexture(IStagingTexture* tex, const ZWTextureSlice& slice, ECpuAccessMode cpuAccess, size_t* outRowPitch) = 0;
        virtual void UnmapStagingTexture(IStagingTexture* tex) = 0;

        virtual void GetTextureTiling(ITexture* texture, uint32_t* numTiles, ZWPackedMipDesc* desc, ZWTileShape* tileShape, uint32_t* subresourceTilingsNum, ZWSubresourceTiling* subresourceTilings) = 0;
        virtual void UpdateTextureTileMappings(ITexture* texture, const ZWTextureTilesMapping* tileMappings, uint32_t numTileMappings, ECommandQueue executionQueue = ECommandQueue::Graphics) = 0;

        virtual ZWSamplerFeedbackTextureHandle CreateSamplerFeedbackTexture(ITexture* pairedTexture, const ZWSamplerFeedbackTextureDesc& desc) = 0;
        virtual ZWSamplerFeedbackTextureHandle CreateSamplerFeedbackForNativeTexture(ObjectType objectType, HCommon::ZWObject texture, ITexture* pairedTexture) = 0;

        virtual ZWBufferHandle CreateBuffer(const ZWBufferDesc& d) = 0;
        virtual void* MapBuffer(IBuffer* buffer, ECpuAccessMode cpuAccess) = 0;
        virtual void UnmapBuffer(IBuffer* buffer) = 0;
        virtual ZWMemoryRequirements GetBufferMemoryRequirements(IBuffer* buffer) = 0;
        virtual bool BindBufferMemory(IBuffer* buffer, IHeap* heap, uint64_t offset) = 0;

        virtual ZWBufferHandle CreateHandleForNativeBuffer(ObjectType objectType, HCommon::ZWObject buffer, const ZWBufferDesc& desc) = 0;

        virtual ZWShaderHandle CreateShader(const ZWShaderDesc& d, const void* binary, size_t binarySize) = 0;
        virtual ZWShaderHandle CreateShaderSpecialization(IShader* baseShader, const ZWShaderSpecialization* constants, uint32_t numConstants) = 0;
        virtual ZWShaderLibraryHandle CreateShaderLibrary(const void* binary, size_t binarySize) = 0;

        virtual ZWSamplerHandle CreateSampler(const ZWSamplerDesc& d) = 0;

        // Note: vertexShader is only necessary on D3D11, otherwise it may be null
        virtual ZWInputLayoutHandle CreateInputLayout(const ZWVertexAttributeDesc* d, uint32_t attributeCount, IShader* vertexShader) = 0;

        // Event queries
        virtual ZWEventQueryHandle CreateEventQuery() = 0;
        virtual void SetEventQuery(IEventQuery* query, ECommandQueue queue) = 0;
        virtual bool PollEventQuery(IEventQuery* query) = 0;
        virtual void WaitEventQuery(IEventQuery* query) = 0;
        virtual void ResetEventQuery(IEventQuery* query) = 0;

        // Timer queries - see also begin/endTimerQuery in ICommandList
        virtual ZWTimerQueryHandle CreateTimerQuery() = 0;
        virtual bool PollTimerQuery(ITimerQuery* query) = 0;
        // returns time in seconds
        virtual float GetTimerQueryTime(ITimerQuery* query) = 0;
        virtual void ResetTimerQuery(ITimerQuery* query) = 0;

        // Returns the API kind that the RHI backend is running on top of.
        virtual EGraphicsAPI GetGraphicsAPI() = 0;

        virtual ZWFramebufferHandle CreateFramebuffer(const ZWFramebufferDesc& desc) = 0;

        virtual ZWGraphicsPipelineHandle CreateGraphicsPipeline(const ZWGraphicsPipelineDesc& desc, ZWFramebufferInfo const& fbinfo) = 0;

        virtual ZWComputePipelineHandle CreateComputePipeline(const ZWComputePipelineDesc& desc) = 0;

        virtual ZWMeshletPipelineHandle CreateMeshletPipeline(const ZWMeshletPipelineDesc& desc, ZWFramebufferInfo const& fbinfo) = 0;

        virtual Hrt::ZWPipelineHandle CreateRayTracingPipeline(const Hrt::ZWPipelineDesc& desc) = 0;

        virtual ZWBindingLayoutHandle CreateBindingLayout(const ZWBindingLayoutDesc& desc) = 0;
        virtual ZWBindingLayoutHandle CreateBindlessLayout(const ZWBindlessLayoutDesc& desc) = 0;

        virtual ZWBindingSetHandle CreateBindingSet(const ZWBindingSetDesc& desc, IBindingLayout* layout) = 0;
        virtual ZWDescriptorTableHandle CreateDescriptorTable(IBindingLayout* layout) = 0;

        virtual void ResizeDescriptorTable(IDescriptorTable* descriptorTable, uint32_t newSize, bool keepContents = true) = 0;
        virtual bool WriteDescriptorTable(IDescriptorTable* descriptorTable, const ZWBindingSetItem& item) = 0;

        virtual Hrt::ZWOpacityMicromapHandle CreateOpacityMicromap(const Hrt::ZWOpacityMicromapDesc& desc) = 0;
        virtual Hrt::ZWAccelStructHandle CreateAccelStruct(const Hrt::ZWAccelStructDesc& desc) = 0;
        virtual ZWMemoryRequirements GetAccelStructMemoryRequirements(Hrt::IAccelStruct* as) = 0;
        virtual Hrt::HCluster::ZWOperationSizeInfo GetClusterOperationSizeInfo(const Hrt::HCluster::ZWOperationParams& params) = 0;
        virtual bool BindAccelStructMemory(Hrt::IAccelStruct* as, IHeap* heap, uint64_t offset) = 0;

        virtual ZWCommandListHandle CreateCommandList(const ZWCommandListParameters& params = ZWCommandListParameters()) = 0;
        virtual uint64_t ExecuteCommandLists(ICommandList* const* pCommandLists, size_t numCommandLists, ECommandQueue executionQueue = ECommandQueue::Graphics) = 0;
        virtual void QueueWaitForCommandList(ECommandQueue waitQueue, ECommandQueue executionQueue, uint64_t instance) = 0;
        // returns true if the wait completes successfully, false if detecting a problem (e.g. device removal)
        virtual bool WaitForIdle() = 0;

        // Releases the resources that were referenced in the command lists that have finished executing.
        // IMPORTANT: Call this method at least once per frame.
        virtual void RunGarbageCollection() = 0;

        virtual bool QueryFeatureSupport(EFeature feature, void* pInfo = nullptr, size_t infoSize = 0) = 0;

        virtual EFormatSupport QueryFormatSupport(EFormat format) = 0;

        // Returns a list of supported CoopVec matrix multiplication formats and accumulation capabilities.
        virtual HCoopVec::ZWDeviceFeatures QueryCoopVecFeatures() = 0;

        // Calculates and returns the on-device size for a CoopVec matrix of the given dimensions, type and layout.
        virtual size_t GetCoopVecMatrixSize(HCoopVec::EDataType type, HCoopVec::EMatrixLayout layout, int rows, int columns) = 0;

        virtual HCommon::ZWObject GetNativeQueue(ObjectType objectType, ECommandQueue queue) = 0;

        virtual IMessageCallback* GetMessageCallback() = 0;

        virtual bool IsAftermathEnabled() = 0;
        virtual HApp::ZWAftermathCrashDumpHelper& GetAftermathCrashDumpHelper() = 0;

        // Front-end for executeCommandLists(..., 1) for compatibility and convenience
        uint64_t ExecuteCommandList(ICommandList* commandList, ECommandQueue executionQueue = ECommandQueue::Graphics)
        {
            return ExecuteCommandLists(&commandList, 1, executionQueue);
        }
    };
    typedef HCommon::RefCountPtr<IDevice> ZWDeviceHandle;

    template <class T>
    void hash_combine(size_t& seed, const T& v)
    {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
}

namespace std
{
    template<> struct hash<HRHI::ZWTextureSubresourceSet>
    {
        std::size_t operator()(HRHI::ZWTextureSubresourceSet const& s) const noexcept
        {
            size_t hash = 0;
            HRHI::hash_combine(hash, s.baseMipLevel);
            HRHI::hash_combine(hash, s.numMipLevels);
            HRHI::hash_combine(hash, s.baseArraySlice);
            HRHI::hash_combine(hash, s.numArraySlices);
            return hash;
        }
    };

    template<> struct hash<HRHI::ZWBufferRange>
    {
        std::size_t operator()(HRHI::ZWBufferRange const& s) const noexcept
        {
            size_t hash = 0;
            HRHI::hash_combine(hash, s.byteOffset);
            HRHI::hash_combine(hash, s.byteSize);
            return hash;
        }
    };

    template<> struct hash<HRHI::ZWBindingSetItem>
    {
        std::size_t operator()(HRHI::ZWBindingSetItem const& s) const noexcept
        {
            size_t value = 0;
            HRHI::hash_combine(value, s.resourceHandle);
            HRHI::hash_combine(value, s.slot);
            HRHI::hash_combine(value, s.type);
            HRHI::hash_combine(value, s.dimension);
            HRHI::hash_combine(value, s.format);
            HRHI::hash_combine(value, s.rawData[0]);
            HRHI::hash_combine(value, s.rawData[1]);
            return value;
        }
    };

    template<> struct hash<HRHI::ZWBindingSetDesc>
    {
        std::size_t operator()(HRHI::ZWBindingSetDesc const& s) const noexcept
        {
            size_t value = 0;
            for (const auto& item : s.bindings)
                hash_combine(value, item);
            return value;
        }
    };

    template<> struct hash<HRHI::ZWFramebufferInfo>
    {
        std::size_t operator()(HRHI::ZWFramebufferInfo const& s) const noexcept
        {
            size_t hash = 0;
            for (auto format : s.colorFormats)
                HRHI::hash_combine(hash, format);
            HRHI::hash_combine(hash, s.depthFormat);
            HRHI::hash_combine(hash, s.sampleCount);
            HRHI::hash_combine(hash, s.sampleQuality);
            return hash;
        }
    };

    template<> struct hash<HRHI::ZWBlendState::ZWRenderTarget>
    {
        std::size_t operator()(HRHI::ZWBlendState::ZWRenderTarget const& s) const noexcept
        {
            size_t hash = 0;
            HRHI::hash_combine(hash, s.blendEnable);
            HRHI::hash_combine(hash, s.logicBlendEnable);
            HRHI::hash_combine(hash, s.srcBlend);
            HRHI::hash_combine(hash, s.destBlend);
            HRHI::hash_combine(hash, s.blendOp);
            HRHI::hash_combine(hash, s.srcBlendAlpha);
            HRHI::hash_combine(hash, s.destBlendAlpha);
            HRHI::hash_combine(hash, s.blendOpAlpha);
            HRHI::hash_combine(hash, s.logicBlendOp);
            HRHI::hash_combine(hash, s.colorWriteMask);
            return hash;
        }
    };

    template<> struct hash<HRHI::ZWBlendState>
    {
        std::size_t operator()(HRHI::ZWBlendState const& s) const noexcept
        {
            size_t hash = 0;
            HRHI::hash_combine(hash, s.alphaToCoverageEnable);
            HRHI::hash_combine(hash, s.independentBlendEnabled);
            for (const auto& target : s.targets)
                HRHI::hash_combine(hash, target);
            return hash;
        }
    };

    template<> struct hash<HRHI::ZWVariableRateShadingState>
    {
        std::size_t operator()(HRHI::ZWVariableRateShadingState const& s) const noexcept
        {
            size_t hash = 0;
            HRHI::hash_combine(hash, s.enabled);
            HRHI::hash_combine(hash, s.shadingRate);
            HRHI::hash_combine(hash, s.pipelinePrimitiveCombiner);
            HRHI::hash_combine(hash, s.imageCombiner);
            return hash;
        }
    };
}