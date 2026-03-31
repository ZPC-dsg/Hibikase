#pragma once

#include <BackEnd/d3d12unique.h>
#include <BackEnd/statetracking.h>
#include <BackEnd/resourcebindingmap.h>
#include <Utils/aftermathtraker.h>
#include <Common/allocators.h>

#include <bitset>
#include <memory>
#include <queue>
#include <list>
#include <mutex>
#include <unordered_map>
#include <utility>

#ifndef HRHI_D3D12_WITH_NVAPI
#if defined(__has_include)
#if __has_include(<nvapi.h>)
#define HRHI_D3D12_WITH_NVAPI 1
#else
#define HRHI_D3D12_WITH_NVAPI 0
#endif
#else
#define HRHI_D3D12_WITH_NVAPI 0
#endif
#endif

#ifndef HRHI_WITH_RTXMU
#if defined(__has_include)
#if __has_include(<rtxmu/D3D12AccelStructManager.h>)
#define HRHI_WITH_RTXMU 1
#else
#define HRHI_WITH_RTXMU 0
#endif
#else
#define HRHI_WITH_RTXMU 0
#endif
#endif

#ifndef HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
#define HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP 0
#endif

#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
#define HRHI_WITH_NVAPI_OPACITY_MICROMAP (0)
#else
#if HRHI_D3D12_WITH_NVAPI && defined(NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS_VER)
#define HRHI_WITH_NVAPI_OPACITY_MICROMAP (1)
#else
#define HRHI_WITH_NVAPI_OPACITY_MICROMAP (0)
#endif
#endif

#if HRHI_D3D12_WITH_NVAPI && defined(NVAPI_GET_RAYTRACING_DISPLACEMENT_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS_VER)
#define HRHI_WITH_NVAPI_DISPLACEMENT_MICROMAP (1)
#else
#define HRHI_WITH_NVAPI_DISPLACEMENT_MICROMAP (0)
#endif

#if HRHI_D3D12_WITH_NVAPI && defined(NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS_VER)
#define HRHI_WITH_NVAPI_CLUSTERS (1)
#else
#define HRHI_WITH_NVAPI_CLUSTERS (0)
#endif

#if HRHI_D3D12_WITH_NVAPI && !HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP && (NVAPI_SDK_VERSION >= 57218)
#define HRHI_WITH_NVAPI_LSS (1)
#else
#define HRHI_WITH_NVAPI_LSS (0)
#endif

#if D3D12_PREVIEW_SDK_VERSION == 717
#define HRHI_D3D12_WITH_COOPVEC (1)
#else
#define HRHI_D3D12_WITH_COOPVEC (0)
#endif

#if HRHI_D3D12_WITH_NVAPI
#include <dxgi.h>
#pragma warning(push)
#pragma warning(disable: 4828)
#include <nvapi.h>
#pragma warning(pop)
#endif

#if HRHI_WITH_RTXMU
#include <rtxmu/D3D12AccelStructManager.h>
#endif

typedef struct HWND__* HWND;

namespace HApp
{

class ZWWindow;

}

namespace HRHI
{
    namespace HD3D12
    {
        class ZWD3D12RootSignature;
        class ZWD3D12Buffer;
        class ZWD3D12CommandList;
        class ZWD3D12Device;
        struct ZWD3D12Context;

        D3D12_SHADER_VISIBILITY ConvertShaderStage(EShaderType s);
        D3D12_BLEND ConvertBlendValue(EBlendFactor value);
        D3D12_BLEND_OP ConvertBlendOp(EBlendOp value);
        D3D12_STENCIL_OP ConvertStencilOp(EStencilOp value);
        D3D12_COMPARISON_FUNC ConvertComparisonFunc(EComparisonFunc value);
        D3D_PRIMITIVE_TOPOLOGY ConvertPrimitiveType(EPrimitiveType pt, uint32_t controlPoints);
        D3D12_TEXTURE_ADDRESS_MODE ConvertSamplerAddressMode(ESamplerAddressMode mode);
        UINT ConvertSamplerReductionType(ESamplerReductionType reductionType);
        D3D12_SHADING_RATE ConvertPixelShadingRate(EVariableShadingRate shadingRate);
        D3D12_SHADING_RATE_COMBINER ConvertShadingRateCombiner(EShadingRateCombiner combiner);
#if HRHI_D3D12_WITH_COOPVEC
        D3D12_LINEAR_ALGEBRA_DATATYPE ConvertCoopVecDataType(HCoopVec::EDataType type);
        HCoopVec::EDataType ConvertCoopVecDataType(D3D12_LINEAR_ALGEBRA_DATATYPE type);
        D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT ConvertCoopVecMatrixLayout(HCoopVec::EMatrixLayout layout);
#endif

        void WaitForFence(ID3D12Fence* fence, uint64_t value, HANDLE event);
        uint32_t CalcSubresource(uint32_t MipSlice, uint32_t ArraySlice, uint32_t PlaneSlice, uint32_t MipLevels, uint32_t ArraySize);
        void TranslateBlendState(const ZWBlendState& inState, D3D12_BLEND_DESC& outState);
        void TranslateDepthStencilState(const ZWDepthStencilState& inState, D3D12_DEPTH_STENCIL_DESC& outState);
        void TranslateRasterizerState(const ZWRasterState& inState, D3D12_RASTERIZER_DESC& outState);

        struct ZWD3D12Context
        {
            HCommon::RefCountPtr<ID3D12Device> device;
            HCommon::RefCountPtr<ID3D12Device2> device2;
            HCommon::RefCountPtr<ID3D12Device5> device5;
            HCommon::RefCountPtr<ID3D12Device8> device8;
#if HRHI_D3D12_WITH_COOPVEC
            HCommon::RefCountPtr<ID3D12DevicePreview> devicePreview;
#endif
#if HRHI_WITH_RTXMU
            std::unique_ptr<rtxmu::DxAccelStructManager> rtxMemUtil;
#endif

            HCommon::RefCountPtr<ID3D12CommandSignature> drawIndirectSignature;
            HCommon::RefCountPtr<ID3D12CommandSignature> drawIndexedIndirectSignature;
            HCommon::RefCountPtr<ID3D12CommandSignature> dispatchIndirectSignature;
            HCommon::RefCountPtr<ID3D12QueryHeap> timerQueryHeap;
            HCommon::RefCountPtr<IBuffer> timerQueryResolveBuffer;

            bool logBufferLifetime = false;
            IMessageCallback* messageCallback = nullptr;
            void Error(const std::string& message) const;
            void Info(const std::string& message) const;
        };

        class ZWD3D12StaticDescriptorHeap : public IDescriptorHeap
        {
        private:
            const ZWD3D12Context& m_Context;
            HCommon::RefCountPtr<ID3D12DescriptorHeap> m_Heap;
            HCommon::RefCountPtr<ID3D12DescriptorHeap> m_ShaderVisibleHeap;
            D3D12_DESCRIPTOR_HEAP_TYPE m_HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            D3D12_CPU_DESCRIPTOR_HANDLE m_StartCpuHandle = { 0 };
            D3D12_CPU_DESCRIPTOR_HANDLE m_StartCpuHandleShaderVisible = { 0 };
            D3D12_GPU_DESCRIPTOR_HANDLE m_StartGpuHandleShaderVisible = { 0 };
            uint32_t m_Stride = 0;
            uint32_t m_NumDescriptors = 0;
            std::vector<bool> m_AllocatedDescriptors;
            DescriptorIndex m_SearchStart = 0;
            uint32_t m_NumAllocatedDescriptors = 0;
            std::mutex m_Mutex;

            HRESULT Grow(uint32_t minRequiredSize);
        public:
            explicit ZWD3D12StaticDescriptorHeap(const ZWD3D12Context& context);

            HRESULT AllocateResources(D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t numDescriptors, bool shaderVisible);
            void CopyToShaderVisibleHeap(DescriptorIndex index, uint32_t count = 1);

            DescriptorIndex AllocateDescriptors(uint32_t count) override;
            DescriptorIndex AllocateDescriptor() override;
            void ReleaseDescriptors(DescriptorIndex baseIndex, uint32_t count) override;
            void ReleaseDescriptor(DescriptorIndex index) override;
            D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(DescriptorIndex index) override;
            D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandleShaderVisible(DescriptorIndex index) override;
            D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(DescriptorIndex index) override;
            [[nodiscard]] ID3D12DescriptorHeap* GetHeap() const override;
            [[nodiscard]] ID3D12DescriptorHeap* GetShaderVisibleHeap() const override;
        };

        class ZWD3D12DeviceResources
        {
        public:
            ZWD3D12StaticDescriptorHeap renderTargetViewHeap;
            ZWD3D12StaticDescriptorHeap depthStencilViewHeap;
            ZWD3D12StaticDescriptorHeap shaderResourceViewHeap;
            ZWD3D12StaticDescriptorHeap samplerHeap;
            HCommon::BitSetAllocator timerQueries;
#if HRHI_WITH_RTXMU
            std::mutex asListMutex;
            std::vector<uint64_t> asBuildsCompleted;
#endif

            // The cache does not own the RS objects, so store weak references
            tsl::robin_map<size_t, ZWD3D12RootSignature*> rootsigCache;

            explicit ZWD3D12DeviceResources(const ZWD3D12Context& context, const ZWDeviceDesc& desc);

            uint8_t GetFormatPlaneCount(DXGI_FORMAT format);

        private:
            const ZWD3D12Context& mContext;
            tsl::robin_map<DXGI_FORMAT, uint8_t> mDxgiFormatPlaneCounts;
        };

        class ZWD3D12Shader : public HCommon::RefCounter<IShader>
        {
        public:
            ZWShaderDesc desc;
            std::vector<char> bytecode;
#if HRHI_D3D12_WITH_NVAPI
            std::vector<NVAPI_D3D12_PSO_EXTENSION_DESC*> extensions;
            std::vector<NV_CUSTOM_SEMANTIC> customSemantics;
            std::vector<uint32_t> coordinateSwizzling;
#endif

            const ZWShaderDesc& GetDesc() const override { return desc; }
            void GetBytecode(const void** ppBytecode, size_t* pSize) const override;
        };

        class ZWD3D12ShaderLibrary;

        class ZWD3D12ShaderLibraryEntry : public HCommon::RefCounter<IShader>
        {
        public:
            ZWShaderDesc desc;
            HCommon::RefCountPtr<IShaderLibrary> library;

            ZWD3D12ShaderLibraryEntry(IShaderLibrary* pLibrary, const char* entryName, EShaderType shaderType)
                : library(pLibrary)
            {
                desc.shaderType = shaderType;
                desc.entryName = entryName;
            }

            const ZWShaderDesc& GetDesc() const override { return desc; }
            void GetBytecode(const void** ppBytecode, size_t* pSize) const override;
        };

        class ZWD3D12ShaderLibrary : public HCommon::RefCounter<IShaderLibrary>
        {
        public:
            std::vector<char> bytecode;

            void GetBytecode(const void** ppBytecode, size_t* pSize) const override;
            ZWShaderHandle GetShader(const char* entryName, EShaderType shaderType) override;
        };

        class ZWD3D12Heap : public HCommon::RefCounter<IHeap>
        {
        public:
            ZWHeapDesc desc;
            HCommon::RefCountPtr<ID3D12Heap> heap;

            const ZWHeapDesc& GetDesc() override { return desc; }
        };

        class ZWD3D12Texture : public HCommon::RefCounter<ITexture>, public ZWTextureStateExtension
        {
        public:
            const ZWTextureDesc desc;
            const D3D12_RESOURCE_DESC resourceDesc;
            HCommon::RefCountPtr<ID3D12Resource> resource;
            uint8_t planeCount = 1;
            HANDLE sharedHandle = nullptr;
            ZWHeapHandle heap;


            ZWD3D12Texture(const ZWD3D12Context& context, ZWD3D12DeviceResources& resources, ZWTextureDesc desc, const D3D12_RESOURCE_DESC& resourceDesc)
                : ZWTextureStateExtension(this->desc)
                , desc(std::move(desc))
                , resourceDesc(resourceDesc)
                , mContext(context)
                , mResources(resources)
            {
                ZWTextureStateExtension::stateInitialized = true;
            }

            ~ZWD3D12Texture() override;

            const ZWTextureDesc& GetDesc() const override { return desc; }

            HCommon::ZWObject GetNativeObject(ObjectType objectType) override;
            HCommon::ZWObject GetNativeView(ObjectType objectType, EFormat format, ZWTextureSubresourceSet subresources, ETextureDimension dimension, bool isReadOnlyDSV = false) override;

            void PostCreate();
            void CreateSRV(size_t descriptor, EFormat format, ETextureDimension dimension, ZWTextureSubresourceSet subresources) const;
            void CreateUAV(size_t descriptor, EFormat format, ETextureDimension dimension, ZWTextureSubresourceSet subresources) const;
            void CreateRTV(size_t descriptor, EFormat format, ZWTextureSubresourceSet subresources) const;
            void CreateDSV(size_t descriptor, ZWTextureSubresourceSet subresources, bool isReadOnly = false) const;
            DescriptorIndex GetClearMipLevelUAV(uint32_t mipLevel);

        private:
            const ZWD3D12Context& mContext;
            ZWD3D12DeviceResources& mResources;

            ZWTextureBindingKey_HashMap<DescriptorIndex> mRenderTargetViews;
            ZWTextureBindingKey_HashMap<DescriptorIndex> mDepthStencilViews;
            ZWTextureBindingKey_HashMap<DescriptorIndex> mCustomSRVs;
            ZWTextureBindingKey_HashMap<DescriptorIndex> mCustomUAVs;
            std::vector<DescriptorIndex> mClearMipLevelUAVs;
        };

        class ZWD3D12Buffer : public HCommon::RefCounter<IBuffer>, public ZWBufferStateExtension
        {
        public:
            const ZWBufferDesc desc;
            HCommon::RefCountPtr<ID3D12Resource> resource;
            D3D12_GPU_VIRTUAL_ADDRESS gpuVA{};
            D3D12_RESOURCE_DESC resourceDesc{};

            ZWHeapHandle heap;

            HCommon::RefCountPtr<ID3D12Fence> lastUseFence;
            uint64_t lastUseFenceValue = 0;
            HANDLE sharedHandle = nullptr;

            ZWD3D12Buffer(const ZWD3D12Context& context, ZWD3D12DeviceResources& resources, ZWBufferDesc desc)
                : ZWBufferStateExtension(this->desc)
                , desc(std::move(desc))
                , mContext(context)
                , mResources(resources)
            {
            }

            ~ZWD3D12Buffer() override;

            const ZWBufferDesc& GetDesc() const override { return desc; }
            GpuVirtualAddress GetGpuVirtualAddress() const override { return gpuVA; }

            HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

            void PostCreate();
            DescriptorIndex GetClearUAV();
            void CreateCBV(size_t descriptor, ZWBufferRange range) const;
            void CreateSRV(size_t descriptor, EFormat format, ZWBufferRange range, EResourceType type) const;
            void CreateUAV(size_t descriptor, EFormat format, ZWBufferRange range, EResourceType type) const;
            static void CreateNullSRV(size_t descriptor, EFormat format, const ZWD3D12Context& context);
            static void CreateNullUAV(size_t descriptor, EFormat format, const ZWD3D12Context& context);

        private:
            const ZWD3D12Context& mContext;
            ZWD3D12DeviceResources& mResources;
            DescriptorIndex mClearUAV = cInvalidDescriptorIndex;
        };

        class ZWD3D12StagingTexture : public HCommon::RefCounter<IStagingTexture>
        {
        public:
            ZWTextureDesc desc;
            D3D12_RESOURCE_DESC resourceDesc{};
            HCommon::RefCountPtr<ZWD3D12Buffer> buffer;
            ECpuAccessMode cpuAccess = ECpuAccessMode::None;
            std::vector<UINT64> subresourceOffsets;

            HCommon::RefCountPtr<ID3D12Fence> lastUseFence;
            uint64_t lastUseFenceValue = 0;

            struct ZWSliceRegion
            {
                // offset and size in bytes of this region inside the buffer
                off_t offset = 0;
                size_t size = 0;

                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
            };

            ZWSliceRegion mappedRegion;
            ECpuAccessMode mappedAccess = ECpuAccessMode::None;

            // returns a SliceRegion struct corresponding to the subresource that slice points at
            // note that this always returns the entire subresource
            ZWSliceRegion GetSliceRegion(ID3D12Device* device, const ZWTextureSlice& slice);

            // returns the total size in bytes required for this staging texture
            size_t GetSizeInBytes(ID3D12Device* device);

            void ComputeSubresourceOffsets(ID3D12Device* device);

            const ZWTextureDesc& GetDesc() const override { return desc; }
            HCommon::ZWObject GetNativeObject(ObjectType objectType) override;
        };

        class ZWD3D12SamplerFeedbackTexture : public HCommon::RefCounter<ISamplerFeedbackTexture>, public ZWTextureStateExtension
        {
        public:
            const ZWSamplerFeedbackTextureDesc desc;
            const ZWTextureDesc textureDesc; // used with state tracking
            HCommon::RefCountPtr<ID3D12Resource> resource;
            ZWTextureHandle pairedTexture;
            DescriptorIndex clearDescriptorIndex = cInvalidDescriptorIndex;

            ZWD3D12SamplerFeedbackTexture(const ZWD3D12Context& context, ZWSamplerFeedbackTextureDesc desc, ZWTextureDesc textureDesc, ITexture* pairedTexture)
                : ZWTextureStateExtension(ZWD3D12SamplerFeedbackTexture::textureDesc)
                , desc(std::move(desc))
                , textureDesc(std::move(textureDesc))
                , pairedTexture(pairedTexture)
                , mContext(context)
            {
                ZWTextureStateExtension::stateInitialized = true;
                ZWTextureStateExtension::isSamplerFeedback = true;
            }

            const ZWSamplerFeedbackTextureDesc& GetDesc() const override { return desc; }
            ZWTextureHandle GetPairedTexture() override { return pairedTexture; }

            void CreateUAV(size_t descriptor) const;

            HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

        private:
            const ZWD3D12Context& mContext;
        };

        class ZWD3D12Sampler : public HCommon::RefCounter<ISampler>
        {
        public:
            ZWD3D12Sampler(const ZWD3D12Context& context, const ZWSamplerDesc& desc);

            void CreateDescriptor(size_t descriptor) const;

            const ZWSamplerDesc& GetDesc() const override { return mDesc; }

        private:
            const ZWD3D12Context& mContext;
            const ZWSamplerDesc mDesc;
            D3D12_SAMPLER_DESC md3d12desc;
        };

        class ZWD3D12InputLayout : public HCommon::RefCounter<IInputLayout>
        {
        public:
            std::vector<ZWVertexAttributeDesc> attributes;
            std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;

            // maps a binding slot to an element stride
            tsl::robin_map<uint32_t, uint32_t> elementStrides;

            uint32_t GetNumAttributes() const override;
            const ZWVertexAttributeDesc* GetAttributeDesc(uint32_t index) const override;
        };

        class ZWD3D12EventQuery : public HCommon::RefCounter<IEventQuery>
        {
        public:
            HCommon::RefCountPtr<ID3D12Fence> fence;
            uint64_t fenceCounter = 0;
            bool started = false;
            bool resolved = false;
        };

        class ZWD3D12TimerQuery : public HCommon::RefCounter<ITimerQuery>
        {
        public:
            uint32_t beginQueryIndex = 0;
            uint32_t endQueryIndex = 0;

            HCommon::RefCountPtr<ID3D12Fence> fence;
            uint64_t fenceCounter = 0;

            bool started = false;
            bool resolved = false;
            float time = 0.f;

            ZWD3D12TimerQuery(ZWD3D12DeviceResources& resources)
                : mResources(resources)
            {
            }

            ~ZWD3D12TimerQuery() override;

        private:
            ZWD3D12DeviceResources& mResources;
        };

        class ZWD3D12BindingLayout : public HCommon::RefCounter<IBindingLayout>
        {
        public:
            ZWBindingLayoutDesc desc;
            uint32_t pushConstantByteSize = 0;
            RootParameterIndex rootParameterPushConstants = ~0u;
            RootParameterIndex rootParameterSRVetc = ~0u;
            RootParameterIndex rootParameterSamplers = ~0u;
            int32_t descriptorTableSizeSRVetc = 0;
            int32_t descriptorTableSizeSamplers = 0;
            std::vector<D3D12_DESCRIPTOR_RANGE1> descriptorRangesSRVetc;
            std::vector<D3D12_DESCRIPTOR_RANGE1> descriptorRangesSamplers;
            std::vector<ZWBindingLayoutItem> bindingLayoutsSRVetc;
            HCommon::StaticVector<std::pair<RootParameterIndex, D3D12_ROOT_DESCRIPTOR1>, gMaxVolatileConstantBuffersPerLayout> rootParametersVolatileCB;
            HCommon::StaticVector<D3D12_ROOT_PARAMETER1, 32> rootParameters;

            ZWD3D12BindingLayout(const ZWBindingLayoutDesc& desc);

            const ZWBindingLayoutDesc* GetDesc() const override { return &desc; }
            const ZWBindlessLayoutDesc* GetBindlessDesc() const override { return nullptr; }
        };

        class ZWD3D12BindlessLayout : public HCommon::RefCounter<IBindingLayout>
        {
        public:
            ZWBindlessLayoutDesc desc;
            HCommon::StaticVector<D3D12_DESCRIPTOR_RANGE1, 32> descriptorRanges;
            D3D12_ROOT_PARAMETER1 rootParameter{};

            ZWD3D12BindlessLayout(const ZWBindlessLayoutDesc& desc);

            const ZWBindingLayoutDesc* GetDesc() const override { return nullptr; }
            const ZWBindlessLayoutDesc* GetBindlessDesc() const override { return &desc; }
        };

        class ZWD3D12RootSignature : public HCommon::RefCounter<IRootSignature>
        {
        public:
            size_t hash = 0;
            HCommon::StaticVector<std::pair<ZWBindingLayoutHandle, RootParameterIndex>, gMaxBindingLayouts> pipelineLayouts;
            HCommon::RefCountPtr<ID3D12RootSignature> handle;
            uint32_t pushConstantByteSize = 0;
            RootParameterIndex rootParameterPushConstants = ~0u;

            ZWD3D12RootSignature(ZWD3D12DeviceResources& resources)
                : mResources(resources)
            {
            }

            ~ZWD3D12RootSignature() override;
            HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

        private:
            ZWD3D12DeviceResources& mResources;
        };

        class ZWD3D12Framebuffer : public HCommon::RefCounter<IFramebuffer>
        {
        public:
            ZWFramebufferDesc desc;
            ZWFramebufferInfoEx framebufferInfo;

            HCommon::StaticVector<ZWTextureHandle, gMaxRenderTargets + 1> textures;
            HCommon::StaticVector<DescriptorIndex, gMaxRenderTargets> RTVs;
            DescriptorIndex DSV = cInvalidDescriptorIndex;
            uint32_t rtWidth = 0;
            uint32_t rtHeight = 0;

            ZWD3D12Framebuffer(ZWD3D12DeviceResources& resources)
                : mResources(resources)
            {
            }

            ~ZWD3D12Framebuffer() override;

            const ZWFramebufferDesc& GetDesc() const override { return desc; }
            const ZWFramebufferInfoEx& GetFramebufferInfo() const override { return framebufferInfo; }

        private:
            ZWD3D12DeviceResources& mResources;
        }; 

        struct ZWD3D12ViewportState
        {
            UINT numViewports = 0;
            D3D12_VIEWPORT viewports[16] = {};
            UINT numScissorRects = 0;
            D3D12_RECT scissorRects[16] = {};
        };

        class ZWD3D12GraphicsPipeline : public HCommon::RefCounter<IGraphicsPipeline>
        {
        public:
            ZWGraphicsPipelineDesc desc;
            ZWFramebufferInfo framebufferInfo;

            HCommon::RefCountPtr<ZWD3D12RootSignature> rootSignature;
            HCommon::RefCountPtr<ID3D12PipelineState> pipelineState;

            bool requiresBlendFactor = false;

            const ZWGraphicsPipelineDesc& GetDesc() const override { return desc; }
            const ZWFramebufferInfo& GetFramebufferInfo() const override { return framebufferInfo; }
            HCommon::ZWObject GetNativeObject(ObjectType objectType) override;
        };

        class ComputePipeline : public HCommon::RefCounter<IComputePipeline>
        {
        public:
            ZWComputePipelineDesc desc;

            HCommon::RefCountPtr<ZWD3D12RootSignature> rootSignature;
            HCommon::RefCountPtr<ID3D12PipelineState> pipelineState;

            const ZWComputePipelineDesc& GetDesc() const override { return desc; }
            HCommon::ZWObject GetNativeObject(ObjectType objectType) override;
        };

        class ZWD3D12MeshletPipeline : public HCommon::RefCounter<IMeshletPipeline>
        {
        public:
            ZWMeshletPipelineDesc desc;
            ZWFramebufferInfo framebufferInfo;

            HCommon::RefCountPtr<ZWD3D12RootSignature> rootSignature;
            HCommon::RefCountPtr<ID3D12PipelineState> pipelineState;

            ZWD3D12ViewportState viewportState;

            bool requiresBlendFactor = false;

            const ZWMeshletPipelineDesc& GetDesc() const override { return desc; }
            const ZWFramebufferInfo& GetFramebufferInfo() const override { return framebufferInfo; }
            HCommon::ZWObject GetNativeObject(ObjectType objectType) override;
        };

        class ZWD3D12BindingSet : public HCommon::RefCounter<IBindingSet>
        {
        public:
            HCommon::RefCountPtr<ZWD3D12BindingLayout> layout;
            ZWBindingSetDesc desc;

            // ShaderType -> DescriptorIndex
            DescriptorIndex descriptorTableSRVetc = 0;
            DescriptorIndex descriptorTableSamplers = 0;
            RootParameterIndex rootParameterIndexSRVetc = 0;
            RootParameterIndex rootParameterIndexSamplers = 0;
            bool descriptorTableValidSRVetc = false;
            bool descriptorTableValidSamplers = false;
            bool hasUavBindings = false;

            HCommon::StaticVector<std::pair<RootParameterIndex, IBuffer*>, gMaxVolatileConstantBuffersPerLayout> rootParametersVolatileCB;

            std::vector<HCommon::RefCountPtr<IResource>> resources;

            std::vector<uint16_t> bindingsThatNeedTransitions;

            ZWD3D12BindingSet(const ZWD3D12Context& context, ZWD3D12DeviceResources& resources)
                : mContext(context)
                , mResources(resources)
            {
            }

            ~ZWD3D12BindingSet() override;

            void CreateDescriptors();

            const ZWBindingSetDesc* GetDesc() const override { return &desc; }
            IBindingLayout* GetLayout() const override { return layout; }

        private:
            const ZWD3D12Context& mContext;
            ZWD3D12DeviceResources& mResources;
        };

        class ZWD3D12DescriptorTable : public HCommon::RefCounter<IDescriptorTable>
        {
        public:
            uint32_t capacity = 0;
            DescriptorIndex firstDescriptor = 0;

            ZWD3D12DescriptorTable(ZWD3D12DeviceResources& resources)
                : mResources(resources)
            {
            }

            ~ZWD3D12DescriptorTable() override;

            const ZWBindingSetDesc* GetDesc() const override { return nullptr; }
            IBindingLayout* GetLayout() const override { return nullptr; }
            uint32_t GetCapacity() const override { return capacity; }
            uint32_t GetFirstDescriptorIndexInHeap() const override { return firstDescriptor; }

        private:
            ZWD3D12DeviceResources& mResources;
        };

        ZWD3D12ViewportState ConvertViewportState(const ZWRasterState& rasterState, const ZWFramebufferInfoEx& framebufferInfo, const ZWViewportState& vpState);

        class ZWD3D12TextureState
        {
        public:
            std::vector<OptionalResourceState> subresourceStates;
            bool enableUavBarriers = true;
            bool firstUavBarrierPlaced = false;
            bool permanentTransition = false;

            ZWD3D12TextureState(uint32_t numSubresources)
            {
                subresourceStates.resize(numSubresources, cResourceStateUnknown);
            }
        };

        class ZWD3D12BufferState
        {
        public:
            OptionalResourceState state = cResourceStateUnknown;
            bool enableUavBarriers = true;
            bool firstUavBarrierPlaced = false;
            D3D12_GPU_VIRTUAL_ADDRESS volatileData = 0;
            bool permanentTransition = false;
        };

        D3D12_RESOURCE_STATES ConvertResourceStates(EResourceStates stateBits);

        class ZWD3D12BufferChunk
        {
        public:
            static const uint64_t cSizeAlignment = 4096; // GPU page size

            HCommon::RefCountPtr<ID3D12Resource> buffer;
            uint64_t version = 0;
            uint64_t bufferSize = 0;
            uint64_t writePointer = 0;
            void* cpuVA = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS gpuVA = 0;
            uint32_t identifier = 0;

            ~ZWD3D12BufferChunk();
        };

        class ZWD3D12UploadManager
        {
        public:
            ZWD3D12UploadManager(const ZWD3D12Context& context, class ZWD3D12Queue* pQueue, size_t defaultChunkSize, uint64_t memoryLimit, bool isScratchBuffer);

            bool SuballocateBuffer(uint64_t size, ID3D12GraphicsCommandList* pCommandList, ID3D12Resource** pBuffer, size_t* pOffset, void** pCpuVA,
                D3D12_GPU_VIRTUAL_ADDRESS* pGpuVA, uint64_t currentVersion, uint32_t alignment = 256);

            void SubmitChunks(uint64_t currentVersion, uint64_t submittedVersion);

        private:
            const ZWD3D12Context& mContext;
            ZWD3D12Queue* m_Queue;
            size_t m_DefaultChunkSize = 0;
            uint64_t m_MemoryLimit = 0;
            uint64_t m_AllocatedMemory = 0;
            bool m_IsScratchBuffer = false;

            std::list<std::shared_ptr<ZWD3D12BufferChunk>> mChunkPool;
            std::shared_ptr<ZWD3D12BufferChunk> mCurrentChunk;

            [[nodiscard]] std::shared_ptr<ZWD3D12BufferChunk> CreateChunk(size_t size) const;
        };

        class ZWD3D12OpacityMicromap : public HCommon::RefCounter<Hrt::IOpacityMicromap>
        {
        public:
            HCommon::RefCountPtr<ZWD3D12Buffer> dataBuffer;
            Hrt::ZWOpacityMicromapDesc desc;
            bool allowUpdate = false;
            bool compacted = false;

            ZWD3D12OpacityMicromap()
            {
            }

            HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

            const Hrt::ZWOpacityMicromapDesc& GetDesc() const override { return desc; }
            bool IsCompacted() const override { return compacted; }
            uint64_t GetDeviceAddress() const override;
        };

        class ZWD3D12AccelStruct : public HCommon::RefCounter<Hrt::IAccelStruct>
        {
        public:
            HCommon::RefCountPtr<ZWD3D12Buffer> dataBuffer;
            std::vector<Hrt::ZWAccelStructHandle> bottomLevelASes;
            std::vector<D3D12_RAYTRACING_INSTANCE_DESC> dxrInstances;
            Hrt::ZWAccelStructDesc desc;
            bool allowUpdate = false;
            bool compacted = false;
            size_t rtxmuId = ~0ull;

            ZWD3D12AccelStruct(const ZWD3D12Context& context)
                : mContext(context)
            {
            }

            ~ZWD3D12AccelStruct() override;

            void CreateSRV(size_t descriptor) const;

            HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

            const Hrt::ZWAccelStructDesc& GetDesc() const override { return desc; }
            bool IsCompacted() const override { return compacted; }
            uint64_t GetDeviceAddress() const override;

        private:
            const ZWD3D12Context& mContext;
        };

        class ZWD3D12RayTracingPipeline : public HCommon::RefCounter<Hrt::IPipeline>
        {
        public:
            Hrt::ZWPipelineDesc desc;

            tsl::robin_map<IBindingLayout*, RootSignatureHandle> localRootSignatures;
            HCommon::RefCountPtr<ZWD3D12RootSignature> globalRootSignature;
            HCommon::RefCountPtr<ID3D12StateObject> pipelineState;
            HCommon::RefCountPtr<ID3D12StateObjectProperties> pipelineInfo;

            struct ZWD3D12ExportTableEntry
            {
                IBindingLayout* bindingLayout;
                const void* pShaderIdentifier;
            };

            tsl::robin_map<std::string, ZWD3D12ExportTableEntry> exports;
            uint32_t maxLocalRootParameters = 0;

            ZWD3D12RayTracingPipeline(const ZWD3D12Context& context, ZWD3D12Device* device)
                : mContext(context)
                , mDevice(device)
            {
            }

            const ZWD3D12ExportTableEntry* GetExport(const char* name);
            uint32_t GetShaderTableEntrySize() const;
            bool HasLocalResources() const { return maxLocalRootParameters != 0; }

            const Hrt::ZWPipelineDesc& GetDesc() const override { return desc; }
            Hrt::ZWShaderTableHandle CreateShaderTable(Hrt::ZWShaderTableDesc const& stDesc) override;

        private:
            const ZWD3D12Context& mContext;
            ZWD3D12Device* mDevice;
        };

        class ZWD3D12ShaderTableState
        {
        public:
            uint32_t committedVersion = 0;
            ID3D12DescriptorHeap* descriptorHeapSRV = nullptr;
            ID3D12DescriptorHeap* descriptorHeapSamplers = nullptr;
            D3D12_DISPATCH_RAYS_DESC dispatchRaysTemplate = {};
        };

        class ZWD3D12ShaderTable : public HCommon::RefCounter<Hrt::IShaderTable>
        {
        public:
            struct ZWD3D12Entry
            {
                const void* pShaderIdentifier;
                ZWBindingSetHandle localBindings;
            };

            HCommon::RefCountPtr<ZWD3D12RayTracingPipeline> pipeline;

            ZWD3D12Entry rayGenerationShader = {};
            std::vector<ZWD3D12Entry> missShaders;
            std::vector<ZWD3D12Entry> callableShaders;
            std::vector<ZWD3D12Entry> hitGroups;

            uint32_t version = 0;

            ZWBufferHandle cache;
            ZWD3D12ShaderTableState cacheState;

            ZWD3D12ShaderTable(const ZWD3D12Context& context, ZWD3D12RayTracingPipeline* _pipeline, Hrt::ZWShaderTableDesc const& desc)
                : pipeline(_pipeline)
                , mContext(context)
                , mDesc(desc)
            {
            }

            size_t GetUploadSize() const { return pipeline->GetShaderTableEntrySize() * size_t(GetNumEntries()); }
            bool IsStateValid(ZWD3D12ShaderTableState const& state, ZWD3D12DeviceResources const& resources) const;
            void Bake(uint8_t* cpuVA, D3D12_GPU_VIRTUAL_ADDRESS gpuVA, ZWD3D12DeviceResources& resources,
                ZWD3D12ShaderTableState& state);

            Hrt::ZWShaderTableDesc const& GetDesc() const override { return mDesc; }
            uint32_t GetNumEntries() const override;
            Hrt::IPipeline* GetPipeline() const override { return pipeline; }
            void SetRayGenerationShader(const char* exportName, IBindingSet* bindings = nullptr) override;
            int AddMissShader(const char* exportName, IBindingSet* bindings = nullptr) override;
            int AddHitGroup(const char* exportName, IBindingSet* bindings = nullptr) override;
            int AddCallableShader(const char* exportName, IBindingSet* bindings = nullptr) override;
            void ClearMissShaders() override;
            void ClearHitShaders() override;
            void ClearCallableShaders() override;

        private:
            const ZWD3D12Context& mContext;
            Hrt::ZWShaderTableDesc const mDesc;

            bool VerifyExport(const ZWD3D12RayTracingPipeline::ZWD3D12ExportTableEntry* pExport, IBindingSet* bindings) const;
        }; 

        class ZWD3D12Queue
        {
        public:
            HCommon::RefCountPtr<ID3D12CommandQueue> queue;
            HCommon::RefCountPtr<ID3D12Fence> fence;
            uint64_t lastSubmittedInstance = 0;
            uint64_t lastCompletedInstance = 0;
            std::atomic<uint64_t> recordingInstance = 1;
            std::deque<std::shared_ptr<class CommandListInstance>> commandListsInFlight;

            explicit ZWD3D12Queue(const ZWD3D12Context& context, ID3D12CommandQueue* queue);
            uint64_t UpdateLastCompletedInstance();

        private:
            const ZWD3D12Context& mContext;
        };

        class CommandListInstance
        {
        public:
            uint64_t submittedInstance = 0;
            ECommandQueue commandQueue = ECommandQueue::Graphics;
            HCommon::RefCountPtr<ID3D12Fence> fence;
            HCommon::RefCountPtr<ID3D12CommandAllocator> commandAllocator;
            HCommon::RefCountPtr<ID3D12CommandList> commandList;
            std::vector<HCommon::RefCountPtr<HCommon::IResource>> referencedResources;
            std::vector<HCommon::RefCountPtr<IUnknown>> referencedNativeResources;
            std::vector<HCommon::RefCountPtr<ZWD3D12StagingTexture>> referencedStagingTextures;
            std::vector<HCommon::RefCountPtr<ZWD3D12Buffer>> referencedStagingBuffers;
            std::vector<HCommon::RefCountPtr<ZWD3D12TimerQuery>> referencedTimerQueries;
#if HRHI_WITH_RTXMU
            std::vector<uint64_t> rtxmuBuildIds;
            std::vector<uint64_t> rtxmuCompactionIds;
#endif
        };

        class ZWD3D12InternalCommandList
        {
        public:
            HCommon::RefCountPtr<ID3D12CommandAllocator> allocator;
            HCommon::RefCountPtr<ID3D12GraphicsCommandList> commandList;
            HCommon::RefCountPtr<ID3D12GraphicsCommandList4> commandList4;
            HCommon::RefCountPtr<ID3D12GraphicsCommandList6> commandList6;
#if HRHI_D3D12_WITH_COOPVEC
            RefCountPtr<ID3D12GraphicsCommandListPreview> commandListPreview;
#endif
            uint64_t lastSubmittedInstance = 0;
        };

        class ZWD3D12CommandList final : public HCommon::RefCounter<HRHI::HD3D12::ICommandList>
        {
        public:

            // Internal interface functions

            ZWD3D12CommandList(class ZWD3D12Device* device, const ZWD3D12Context& context, ZWD3D12DeviceResources& resources, const ZWCommandListParameters& params);
            ~ZWD3D12CommandList() override;
            std::shared_ptr<CommandListInstance> Executed(ZWD3D12Queue* pQueue);
            void RequireTextureState(ITexture* texture, ZWTextureSubresourceSet subresources, EResourceStates state);
            void RequireSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, EResourceStates state);
            void RequireBufferState(IBuffer* buffer, EResourceStates state);
            ID3D12CommandList* GetD3D12CommandList() const { return mActiveCommandList->commandList; }

            // IResource implementation

            HCommon::ZWObject GetNativeObject(ObjectType objectType) override;

            // ICommandList implementation

            void Open() override;
            void Close() override;
            void ClearState() override;

            void ClearTextureFloat(ITexture* t, ZWTextureSubresourceSet subresources, const ZWColor& clearColor) override;
            void ClearDepthStencilTexture(ITexture* t, ZWTextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil) override;
            void ClearTextureUInt(ITexture* t, ZWTextureSubresourceSet subresources, uint32_t clearColor) override;
            void ClearSamplerFeedbackTexture(ISamplerFeedbackTexture* texture) override;
            void DecodeSamplerFeedbackTexture(IBuffer* buffer, ISamplerFeedbackTexture* texture, EFormat format) override;
            void SetSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, EResourceStates stateBits) override;

            void CopyTexture(ITexture* dest, const ZWTextureSlice& destSlice, ITexture* src, const ZWTextureSlice& srcSlice) override;
            void CopyTexture(IStagingTexture* dest, const ZWTextureSlice& destSlice, ITexture* src, const ZWTextureSlice& srcSlice) override;
            void CopyTexture(ITexture* dest, const ZWTextureSlice& destSlice, IStagingTexture* src, const ZWTextureSlice& srcSlice) override;
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
            void DispatchIndirect(uint32_t offsetBytes) override;

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
            void SetAccelStructState(Hrt::IAccelStruct* as, EResourceStates stateBits) override;

            void SetPermanentTextureState(ITexture* texture, EResourceStates stateBits) override;
            void SetPermanentBufferState(IBuffer* buffer, EResourceStates stateBits) override;

            void CommitBarriers() override;

            EResourceStates GetTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel) override;
            EResourceStates GetBufferState(IBuffer* buffer) override;

            HRHI::IDevice* GetDevice() override;
            const ZWCommandListParameters& GetDesc() override { return mDesc; }

            // D3D12 specific methods

            bool AllocateUploadBuffer(size_t size, void** pCpuAddress, D3D12_GPU_VIRTUAL_ADDRESS* pGpuAddress) override;
            bool AllocateDxrScratchBuffer(size_t size, void** pCpuAddress, D3D12_GPU_VIRTUAL_ADDRESS* pGpuAddress);
            bool CommitDescriptorHeaps() override;
            D3D12_GPU_VIRTUAL_ADDRESS GetBufferGpuVA(IBuffer* buffer) override;

            void UpdateGraphicsVolatileBuffers() override;
            void UpdateComputeVolatileBuffers() override;

            void SetComputeBindings(
                const BindingSetVector& bindings,
                uint32_t bindingUpdateMask,
                IBuffer* indirectParams,
                bool updateIndirectParams,
                const ZWD3D12RootSignature* rootSignature);

            void SetGraphicsBindings(
                const BindingSetVector& bindings,
                uint32_t bindingUpdateMask,
                IBuffer* indirectParams,
                bool updateIndirectParams,
                IBuffer* indirectCountBuffer,
                bool updateIndirectCountBuffer,
                const ZWD3D12RootSignature* rootSignature);

        private:
            const ZWD3D12Context& m_Context;
            ZWD3D12DeviceResources& m_Resources;

            struct VolatileConstantBufferBinding
            {
                uint32_t bindingPoint; // RootParameterIndex
                ZWD3D12Buffer* buffer;
                D3D12_GPU_VIRTUAL_ADDRESS address;
            };

            IDevice* mDevice;
            ZWD3D12Queue* mQueue;
            ZWD3D12UploadManager mUploadManager;
            ZWD3D12UploadManager mDxrScratchManager;
            ZWCommandListResourceStateTracker mStateTracker;
            bool mEnableAutomaticBarriers = true;

            ZWCommandListParameters mDesc;

            std::shared_ptr<ZWD3D12InternalCommandList> mActiveCommandList;
            std::list<std::shared_ptr<ZWD3D12InternalCommandList>> mCommandListPool;
            std::shared_ptr<CommandListInstance> mInstance;
            uint64_t mRecordingVersion = 0;

            // Cache for user-provided state

            ZWGraphicsState m_CurrentGraphicsState;
            ZWComputeState m_CurrentComputeState;
            ZWMeshletState m_CurrentMeshletState;
            Hrt::ZWState mCurrentRayTracingState;
            bool mCurrentGraphicsStateValid = false;
            bool mCurrentComputeStateValid = false;
            bool mCurrentMeshletStateValid = false;
            bool mCurrentRayTracingStateValid = false;
            bool mBindingStatesDirty = false;

            // Cache for internal state

            ID3D12DescriptorHeap* mCurrentHeapSRVetc = nullptr;
            ID3D12DescriptorHeap* mCurrentHeapSamplers = nullptr;
            ID3D12Resource* mCurrentUploadBuffer = nullptr;
            ZWSinglePassStereoState mCurrentSinglePassStereoState;

            tsl::robin_map<IBuffer*, D3D12_GPU_VIRTUAL_ADDRESS> mVolatileConstantBufferAddresses;
            bool mAnyVolatileBufferWrites = false;

            std::vector<D3D12_RESOURCE_BARRIER> mD3DBarriers; // Used locally in commitBarriers, member to avoid re-allocations

            // Bound volatile buffer state. Saves currently bound volatile buffers and their current GPU VAs.
            // Necessary to patch the bound VAs when a buffer is updated between setGraphicsState and draw, or between draws.

            HCommon::StaticVector<VolatileConstantBufferBinding, gMaxVolatileConstantBuffers> mCurrentGraphicsVolatileCBs;
            HCommon::StaticVector<VolatileConstantBufferBinding, gMaxVolatileConstantBuffers> mCurrentComputeVolatileCBs;

            tsl::robin_map<Hrt::IShaderTable*, std::unique_ptr<ZWD3D12ShaderTableState>> mUncachedShaderTableStates;
            ZWD3D12ShaderTableState& GetShaderTableState(Hrt::IShaderTable* shaderTable);

            void ClearStateCache();

            void BindGraphicsPipeline(ZWD3D12GraphicsPipeline* pso, bool updateRootSignature) const;
            void BindMeshletPipeline(ZWD3D12MeshletPipeline* pso, bool updateRootSignature) const;
            void BindFramebuffer(ZWD3D12Framebuffer* fb);
            void UnbindShadingRateState();

            std::shared_ptr<ZWD3D12InternalCommandList> CreateInternalCommandList() const;

            void BuildTopLevelAccelStructInternal(ZWD3D12AccelStruct* as, D3D12_GPU_VIRTUAL_ADDRESS instanceData, size_t numInstances, Hrt::EAccelStructBuildFlags buildFlags);
        };

        class ZWD3D12Device final : public HCommon::RefCounter<IDevice>
        {
        public:
            explicit ZWD3D12Device(const ZWDeviceDesc& desc);
            ~ZWD3D12Device() override;

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

            ZWEventQueryHandle CreateEventQuery() override;
            void SetEventQuery(IEventQuery* query, ECommandQueue queue) override;
            bool PollEventQuery(IEventQuery* query) override;
            void WaitEventQuery(IEventQuery* query) override;
            void ResetEventQuery(IEventQuery* query) override;

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

            HRHI::ZWCommandListHandle CreateCommandList(const ZWCommandListParameters& params = ZWCommandListParameters()) override;
            uint64_t ExecuteCommandLists(HRHI::ICommandList* const* pCommandLists, size_t numCommandLists, ECommandQueue executionQueue = ECommandQueue::Graphics) override;
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

            // HD3D12::IDevice implementation

            RootSignatureHandle BuildRootSignature(const HCommon::StaticVector<ZWBindingLayoutHandle, gMaxBindingLayouts>& pipelineLayouts, bool allowInputLayout, bool isLocal, const D3D12_ROOT_PARAMETER1* pCustomParameters = nullptr, uint32_t numCustomParameters = 0) override;
            ZWGraphicsPipelineHandle CreateHandleForNativeGraphicsPipeline(IRootSignature* rootSignature, ID3D12PipelineState* pipelineState, const ZWGraphicsPipelineDesc& desc, const ZWFramebufferInfo& framebufferInfo) override;
            ZWMeshletPipelineHandle CreateHandleForNativeMeshletPipeline(IRootSignature* rootSignature, ID3D12PipelineState* pipelineState, const ZWMeshletPipelineDesc& desc, const ZWFramebufferInfo& framebufferInfo) override;
            IDescriptorHeap* GetDescriptorHeap(EDescriptorHeapType heapType) override;

            // Internal interface
            ZWD3D12Queue* GetQueue(ECommandQueue type) { return mQueues[int(type)].get(); }

            ZWD3D12Context& GetContext() { return mContext; }

            bool SetHlslExtensionsUAV(uint32_t slot);

            bool GetAccelStructPreBuildInfo(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& outPreBuildInfo, const Hrt::ZWAccelStructDesc& desc) const;

            bool GetNvapiIsInitialized() const { return mNvapiIsInitialized; }
            bool GetOpacityMicromapSupported() const { return mOpacityMicromapSupported; }
            bool GetLinearSweptSpheresSupported() const { return mLinearSweptSpheresSupported; }
            bool GetSpheresSupported() const { return mSpheresSupported; }

        private:
            ZWD3D12Context mContext;
            ZWD3D12DeviceResources mResources;

            std::array<std::unique_ptr<ZWD3D12Queue>, (int)ECommandQueue::Count> mQueues;
            HANDLE mFenceEvent;

            std::mutex mMutex;

            std::vector<ID3D12CommandList*> mCommandListsToExecute; // used locally in executeCommandLists, member to avoid re-allocations

            bool mNvapiIsInitialized = false;
            bool mSinglePassStereoSupported = false;
            bool mHlslExtensionsSupported = false;
            bool mFastGeometryShaderSupported = false;
            bool mRayTracingSupported = false;
            bool mTraceRayInlineSupported = false;
            bool mMeshletsSupported = false;
            bool mVariableRateShadingSupported = false;
            bool mOpacityMicromapSupported = false;
            bool mRayTracingClustersSupported = false;
            bool mLinearSweptSpheresSupported = false;
            bool mSpheresSupported = false;
            bool mShaderExecutionReorderingSupported = false;
            bool mSamplerFeedbackSupported = false;
            bool mAftermathEnabled = false;
            bool mHeapDirectlyIndexedEnabled = false;
            bool mCoopVecInferencingSupported = false;
            bool mCoopVecTrainingSupported = false;
            HApp::ZWAftermathCrashDumpHelper mAftermathCrashDumpHelper;


            D3D12_FEATURE_DATA_D3D12_OPTIONS  mOptions = {};
            D3D12_FEATURE_DATA_D3D12_OPTIONS1 mOptions1 = {};
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 mOptions5 = {};
            D3D12_FEATURE_DATA_D3D12_OPTIONS6 mOptions6 = {};
            D3D12_FEATURE_DATA_D3D12_OPTIONS7 mOptions7 = {};

            HCommon::RefCountPtr<ZWD3D12RootSignature> GetRootSignature(const HCommon::StaticVector<ZWBindingLayoutHandle, gMaxBindingLayouts>& pipelineLayouts, bool allowInputLayout);
            HCommon::RefCountPtr<ID3D12PipelineState> CreatePipelineState(const ZWGraphicsPipelineDesc& desc, ZWD3D12RootSignature* pRS, const ZWFramebufferInfo& fbinfo) const;
            HCommon::RefCountPtr<ID3D12PipelineState> CreatePipelineState(const ZWComputePipelineDesc& desc, ZWD3D12RootSignature* pRS) const;
            HCommon::RefCountPtr<ID3D12PipelineState> CreatePipelineState(const ZWMeshletPipelineDesc& desc, ZWD3D12RootSignature* pRS, const ZWFramebufferInfo& fbinfo) const;

        };

        class ZWD3D12Backend final
        {
        public:
            static HWND GetWindowHandle(const HApp::ZWWindow& window);
        };
    }
}
