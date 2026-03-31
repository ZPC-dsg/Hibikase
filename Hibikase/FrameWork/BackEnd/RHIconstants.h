#pragma once

#include <BackEnd/RHItypedefs.h>

#include <cstdint>

namespace HRHI
{
    constexpr uint32_t gMaxRenderTargets = 8;
    constexpr uint32_t gMaxViewports = 16;
    constexpr uint32_t gMaxVertexAttributes = 16;
    constexpr uint32_t gMaxBindingLayouts = 8;
    constexpr uint32_t gMaxBindlessRegisterSpaces = 16;
    constexpr uint32_t gMaxVolatileConstantBuffersPerLayout = 6;
    constexpr uint32_t gMaxVolatileConstantBuffers = 32;
    constexpr uint32_t gMaxPushConstantSize = 128; // D3D12: root signature is 256 bytes max., Vulkan: 128 bytes of push constants guaranteed
    constexpr uint32_t gConstantBufferOffsetSizeAlignment = 256; // Partially bound constant buffers must have offsets aligned to this and sizes multiple of this

    namespace HRHIObjectTypes
    {
        constexpr ObjectType gSharedHandle = 0x00000001;

        constexpr ObjectType gD3D12Device = 0x00020001;
        constexpr ObjectType gD3D12CommandQueue = 0x00020002;
        constexpr ObjectType gD3D12GraphicsCommandList = 0x00020003;
        constexpr ObjectType gD3D12Resource = 0x00020004;
        constexpr ObjectType gD3D12RenderTargetViewDescriptor = 0x00020005;
        constexpr ObjectType gD3D12DepthStencilViewDescriptor = 0x00020006;
        constexpr ObjectType gD3D12ShaderResourceViewGpuDescripror = 0x00020007;
        constexpr ObjectType gD3D12UnorderedAccessViewGpuDescripror = 0x00020008;
        constexpr ObjectType gD3D12RootSignature = 0x00020009;
        constexpr ObjectType gD3D12PipelineState = 0x0002000a;
        constexpr ObjectType gD3D12CommandAllocator = 0x0002000b;

        constexpr ObjectType gVKDevice = 0x00030001;
        constexpr ObjectType gVKPhysicalDevice = 0x00030002;
        constexpr ObjectType gVKInstance = 0x00030003;
        constexpr ObjectType gVKQueue = 0x00030004;
        constexpr ObjectType gVKCommandBuffer = 0x00030005;
        constexpr ObjectType gVKDeviceMemory = 0x00030006;
        constexpr ObjectType gVKBuffer = 0x00030007;
        constexpr ObjectType gVKImage = 0x00030008;
        constexpr ObjectType gVKImageView = 0x00030009;
        constexpr ObjectType gVKAccelerationStructureKHR = 0x0003000a;
        constexpr ObjectType gVKSampler = 0x0003000b;
        constexpr ObjectType gVKShaderModule = 0x0003000c;
        [[deprecated]]
        constexpr ObjectType gVKRenderPass = 0x0003000d;
        [[deprecated]]
        constexpr ObjectType gVKFramebuffer = 0x0003000e;
        constexpr ObjectType gVKDescriptorPool = 0x0003000f;
        constexpr ObjectType gVKDescriptorSetLayout = 0x00030010;
        constexpr ObjectType gVKDescriptorSet = 0x00030011;
        constexpr ObjectType gVKPipelineLayout = 0x00030012;
        constexpr ObjectType gVKPipeline = 0x00030013;
        constexpr ObjectType gVKMicromap = 0x00030014;
        constexpr ObjectType gVKImageCreateInfo = 0x00030015;
    };
}