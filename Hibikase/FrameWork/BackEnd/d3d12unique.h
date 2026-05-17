#pragma once

#include <BackEnd/RHIinterface.h>

#include <d3d12.h>

namespace HRHI
{
	namespace HD3D12ObjectTypes
	{
		typedef uint32_t ObjectType;

		constexpr ObjectType Nvrhi_D3D12_Device = 0x00020101;
		constexpr ObjectType Nvrhi_D3D12_CommandList = 0x00020102;
	}
	namespace HD3D12
	{
        typedef uint32_t DescriptorIndex;
        typedef uint32_t RootParameterIndex;
        typedef uint32_t OptionalResourceState; // D3D12_RESOURCE_STATES + unknown value

        constexpr RootParameterIndex cInvalidRootParameterIndex = ~0u; // Used to skip mutable descriptor set
        constexpr DescriptorIndex cInvalidDescriptorIndex = ~0u;
        constexpr OptionalResourceState cResourceStateUnknown = ~0u;

        class IRootSignature : public HCommon::IResource
        {
        };
        typedef HCommon::RefCountPtr<IRootSignature> RootSignatureHandle;

        class ICommandList : public HRHI::ICommandList
        {
        public:
            virtual bool AllocateUploadBuffer(size_t size, void** pCpuAddress, D3D12_GPU_VIRTUAL_ADDRESS* pGpuAddress) = 0;
            virtual bool CommitDescriptorHeaps() = 0;
            virtual D3D12_GPU_VIRTUAL_ADDRESS GetBufferGpuVA(IBuffer* buffer) = 0;

            virtual void UpdateGraphicsVolatileBuffers() = 0;
            virtual void UpdateComputeVolatileBuffers() = 0;
        };
        typedef HCommon::RefCountPtr<ICommandList> ZWCommandListHandle;

        class IDescriptorHeap
        {
        protected:
            IDescriptorHeap() = default;
            virtual ~IDescriptorHeap() = default;
        public:
            virtual DescriptorIndex AllocateDescriptors(uint32_t count) = 0;
            virtual DescriptorIndex AllocateDescriptor() = 0;
            virtual void ReleaseDescriptors(DescriptorIndex baseIndex, uint32_t count) = 0;
            virtual void ReleaseDescriptor(DescriptorIndex index) = 0;
            virtual D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(DescriptorIndex index) = 0;
            virtual D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandleShaderVisible(DescriptorIndex index) = 0;
            virtual D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(DescriptorIndex index) = 0;
            [[nodiscard]] virtual ID3D12DescriptorHeap* GetHeap() const = 0;
            [[nodiscard]] virtual ID3D12DescriptorHeap* GetShaderVisibleHeap() const = 0;

            IDescriptorHeap(const IDescriptorHeap&) = delete;
            IDescriptorHeap(const IDescriptorHeap&&) = delete;
            IDescriptorHeap& operator=(const IDescriptorHeap&) = delete;
            IDescriptorHeap& operator=(const IDescriptorHeap&&) = delete;
        };

        enum class EDescriptorHeapType
        {
            RenderTargetView,
            DepthStencilView,
            ShaderResourceView,
            Sampler
        };

        class IDevice : public HRHI::IDevice
        {
        public:
            // D3D12-specific methods
            virtual RootSignatureHandle BuildRootSignature(const HCommon::StaticVector<ZWBindingLayoutHandle, gMaxBindingLayouts>& pipelineLayouts, bool allowInputLayout, bool isLocal, const D3D12_ROOT_PARAMETER1* pCustomParameters = nullptr, uint32_t numCustomParameters = 0) = 0;
            virtual ZWGraphicsPipelineHandle CreateHandleForNativeGraphicsPipeline(IRootSignature* rootSignature, ID3D12PipelineState* pipelineState, const ZWGraphicsPipelineDesc& desc, const ZWFramebufferInfo& framebufferInfo) = 0;
            virtual ZWMeshletPipelineHandle CreateHandleForNativeMeshletPipeline(IRootSignature* rootSignature, ID3D12PipelineState* pipelineState, const ZWMeshletPipelineDesc& desc, const ZWFramebufferInfo& framebufferInfo) = 0;
            [[nodiscard]] virtual IDescriptorHeap* GetDescriptorHeap(EDescriptorHeapType heapType) = 0;
        };
        typedef HCommon::RefCountPtr<IDevice> ZWDeviceHandle;

        struct ZWDeviceDesc
        {
            IMessageCallback* errorCB = nullptr;
            ID3D12Device* pDevice = nullptr;
            ID3D12CommandQueue* pGraphicsCommandQueue = nullptr;
            ID3D12CommandQueue* pComputeCommandQueue = nullptr;
            ID3D12CommandQueue* pCopyCommandQueue = nullptr;

            uint32_t renderTargetViewHeapSize = 1024;
            uint32_t depthStencilViewHeapSize = 1024;
            uint32_t shaderResourceViewHeapSize = 16384;
            uint32_t samplerHeapSize = 1024;
            uint32_t maxTimerQueries = 256;

            // If enabled and the device has the capability,
            // create RootSignatures with D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED 
            // and D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED
            bool enableHeapDirectlyIndexed = false;

            bool aftermathEnabled = false;

            // Enable logging the buffer lifetime to IMessageCallback
            // Useful for debugging resource lifetimes
            bool logBufferLifetime = false;
        };

        bool TryEnableAftermath(
            const char* applicationName = "Hibikase",
            const char* applicationVersion = "initial",
            const char* commandLine = nullptr,
            IMessageCallback* messageCallback = nullptr);

        void WaitForAftermathCrashDump(uint32_t timeoutMs = 3000, IMessageCallback* messageCallback = nullptr);

        ZWDeviceHandle CreateDevice(const ZWDeviceDesc& desc);

        DXGI_FORMAT ConvertFormat(EFormat format);

        struct ZWDxgiFormatMapping
        {
            EFormat abstractFormat;
            DXGI_FORMAT resourceFormat;
            DXGI_FORMAT srvFormat;
            DXGI_FORMAT rtvFormat;
        };

        const ZWDxgiFormatMapping& GetDxgiFormatMapping(EFormat abstractFormat);
	}
}