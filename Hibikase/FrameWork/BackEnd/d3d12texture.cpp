#include <BackEnd/d3d12backend.h>
#include <Utils/stringtranslatehelper.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace HRHI::HD3D12
{
    namespace
    {
        bool HasSharedResourceFlag(ESharedResourceFlags value, ESharedResourceFlags flag)
        {
            return (value & flag) != 0;
        }

        void ReportUnsupportedTextureDimension(const ZWD3D12Context& context, const char* operation, const std::string& debugName)
        {
            std::stringstream messageBuilder;
            messageBuilder << "Texture " << HApp::DebugNameToString(debugName)
                << " uses an unsupported dimension for " << operation << ".";
            context.Error(messageBuilder.str());
        }

        D3D12_RESOURCE_DESC ConvertTextureDesc(const ZWTextureDesc& desc)
        {
            const ZWDxgiFormatMapping& formatMapping = GetDxgiFormatMapping(desc.format);
            const ZWFormatInfo& formatInfo = GetFormatInfo(desc.format);

            D3D12_RESOURCE_DESC resourceDesc = {};
            resourceDesc.Width = desc.width;
            resourceDesc.Height = desc.height;
            resourceDesc.DepthOrArraySize = 1;
            resourceDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
            resourceDesc.Format = desc.isTypeless ? formatMapping.resourceFormat : formatMapping.rtvFormat;
            resourceDesc.SampleDesc.Count = desc.sampleCount;
            resourceDesc.SampleDesc.Quality = desc.sampleQuality;

            switch (desc.dimension)
            {
            case ETextureDimension::Texture1D:
            case ETextureDimension::Texture1DArray:
                resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
                resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.arraySize);
                break;

            case ETextureDimension::Texture2D:
            case ETextureDimension::Texture2DArray:
            case ETextureDimension::TextureCube:
            case ETextureDimension::TextureCubeArray:
            case ETextureDimension::Texture2DMS:
            case ETextureDimension::Texture2DMSArray:
                resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.arraySize);
                break;

            case ETextureDimension::Texture3D:
                resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
                resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.depth);
                break;

            case ETextureDimension::Unknown:
            default:
                break;
            }

            if (!desc.isShaderResource)
            {
                resourceDesc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
            }

            if (desc.isRenderTarget)
            {
                if (formatInfo.hasDepth || formatInfo.hasStencil)
                {
                    resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
                }
                else
                {
                    resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                }
            }

            if (desc.isUAV)
            {
                resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            }

            return resourceDesc;
        }

        D3D12_CLEAR_VALUE ConvertTextureClearValue(const ZWTextureDesc& desc)
        {
            const ZWDxgiFormatMapping& formatMapping = GetDxgiFormatMapping(desc.format);
            const ZWFormatInfo& formatInfo = GetFormatInfo(desc.format);

            D3D12_CLEAR_VALUE clearValue = {};
            clearValue.Format = formatMapping.rtvFormat;

            if (formatInfo.hasDepth || formatInfo.hasStencil)
            {
                clearValue.DepthStencil.Depth = desc.clearValue.r;
                clearValue.DepthStencil.Stencil = static_cast<UINT8>(desc.clearValue.g);
            }
            else
            {
                clearValue.Color[0] = desc.clearValue.r;
                clearValue.Color[1] = desc.clearValue.g;
                clearValue.Color[2] = desc.clearValue.b;
                clearValue.Color[3] = desc.clearValue.a;
            }

            return clearValue;
        }
    }

    HCommon::ZWObject ZWD3D12Texture::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case HRHI::HRHIObjectTypes::gD3D12Resource:
            return HCommon::ZWObject(resource.Get());
        case HRHI::HRHIObjectTypes::gSharedHandle:
            return HCommon::ZWObject(sharedHandle);
        default:
            return nullptr;
        }
    }

    HCommon::ZWObject ZWD3D12Texture::GetNativeView(
        ObjectType objectType,
        EFormat format,
        ZWTextureSubresourceSet subresources,
        ETextureDimension dimension,
        bool isReadOnlyDSV)
    {
        switch (objectType)
        {
        case HRHI::HRHIObjectTypes::gD3D12ShaderResourceViewGpuDescripror:
        {
            ZWTextureBindingKey key(subresources, format);
            DescriptorIndex descriptorIndex = cInvalidDescriptorIndex;

            const auto found = mCustomSRVs.find(key);
            if (found == mCustomSRVs.end())
            {
                descriptorIndex = mResources.shaderResourceViewHeap.AllocateDescriptor();
                mCustomSRVs[key] = descriptorIndex;

                const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = mResources.shaderResourceViewHeap.GetCpuHandle(descriptorIndex);
                CreateSRV(cpuHandle.ptr, format, dimension, subresources);
                mResources.shaderResourceViewHeap.CopyToShaderVisibleHeap(descriptorIndex);
            }
            else
            {
                descriptorIndex = found->second;
            }

            return HCommon::ZWObject(mResources.shaderResourceViewHeap.GetGpuHandle(descriptorIndex).ptr);
        }

        case HRHI::HRHIObjectTypes::gD3D12UnorderedAccessViewGpuDescripror:
        {
            ZWTextureBindingKey key(subresources, format);
            DescriptorIndex descriptorIndex = cInvalidDescriptorIndex;

            const auto found = mCustomUAVs.find(key);
            if (found == mCustomUAVs.end())
            {
                descriptorIndex = mResources.shaderResourceViewHeap.AllocateDescriptor();
                mCustomUAVs[key] = descriptorIndex;

                const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = mResources.shaderResourceViewHeap.GetCpuHandle(descriptorIndex);
                CreateUAV(cpuHandle.ptr, format, dimension, subresources);
                mResources.shaderResourceViewHeap.CopyToShaderVisibleHeap(descriptorIndex);
            }
            else
            {
                descriptorIndex = found->second;
            }

            return HCommon::ZWObject(mResources.shaderResourceViewHeap.GetGpuHandle(descriptorIndex).ptr);
        }

        case HRHI::HRHIObjectTypes::gD3D12RenderTargetViewDescriptor:
        {
            ZWTextureBindingKey key(subresources, format);
            DescriptorIndex descriptorIndex = cInvalidDescriptorIndex;

            const auto found = mRenderTargetViews.find(key);
            if (found == mRenderTargetViews.end())
            {
                descriptorIndex = mResources.renderTargetViewHeap.AllocateDescriptor();
                mRenderTargetViews[key] = descriptorIndex;

                const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = mResources.renderTargetViewHeap.GetCpuHandle(descriptorIndex);
                CreateRTV(cpuHandle.ptr, format, subresources);
            }
            else
            {
                descriptorIndex = found->second;
            }

            return HCommon::ZWObject(mResources.renderTargetViewHeap.GetCpuHandle(descriptorIndex).ptr);
        }

        case HRHI::HRHIObjectTypes::gD3D12DepthStencilViewDescriptor:
        {
            ZWTextureBindingKey key(subresources, format, isReadOnlyDSV);
            DescriptorIndex descriptorIndex = cInvalidDescriptorIndex;

            const auto found = mDepthStencilViews.find(key);
            if (found == mDepthStencilViews.end())
            {
                descriptorIndex = mResources.depthStencilViewHeap.AllocateDescriptor();
                mDepthStencilViews[key] = descriptorIndex;

                const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = mResources.depthStencilViewHeap.GetCpuHandle(descriptorIndex);
                CreateDSV(cpuHandle.ptr, subresources, isReadOnlyDSV);
            }
            else
            {
                descriptorIndex = found->second;
            }

            return HCommon::ZWObject(mResources.depthStencilViewHeap.GetCpuHandle(descriptorIndex).ptr);
        }

        default:
            return nullptr;
        }
    }

    ZWD3D12Texture::~ZWD3D12Texture()
    {
        for (const auto& pair : mRenderTargetViews)
        {
            mResources.renderTargetViewHeap.ReleaseDescriptor(pair.second);
        }

        for (const auto& pair : mDepthStencilViews)
        {
            mResources.depthStencilViewHeap.ReleaseDescriptor(pair.second);
        }

        for (DescriptorIndex descriptorIndex : mClearMipLevelUAVs)
        {
            if (descriptorIndex != cInvalidDescriptorIndex)
            {
                mResources.shaderResourceViewHeap.ReleaseDescriptor(descriptorIndex);
            }
        }

        for (const auto& pair : mCustomSRVs)
        {
            mResources.shaderResourceViewHeap.ReleaseDescriptor(pair.second);
        }

        for (const auto& pair : mCustomUAVs)
        {
            mResources.shaderResourceViewHeap.ReleaseDescriptor(pair.second);
        }
    }

    uint8_t ZWD3D12DeviceResources::GetFormatPlaneCount(DXGI_FORMAT format)
    {
        uint8_t& planeCount = mDxgiFormatPlaneCounts[format];
        if (planeCount == 0)
        {
            D3D12_FEATURE_DATA_FORMAT_INFO formatInfo = { format, 1 };
            if (FAILED(mContext.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &formatInfo, sizeof(formatInfo))))
            {
                planeCount = 255;
            }
            else
            {
                planeCount = static_cast<uint8_t>(formatInfo.PlaneCount);
            }
        }

        if (planeCount == 255)
        {
            return 0;
        }

        return planeCount;
    }

    void ZWD3D12Texture::PostCreate()
    {
        if (!desc.debugName.empty())
        {
            const std::wstring debugName(desc.debugName.begin(), desc.debugName.end());
            resource->SetName(debugName.c_str());
        }

        if (desc.isUAV)
        {
            mClearMipLevelUAVs.resize(desc.mipLevels, cInvalidDescriptorIndex);
        }

        const uint8_t queriedPlaneCount = mResources.GetFormatPlaneCount(resourceDesc.Format);
        planeCount = queriedPlaneCount != 0 ? queriedPlaneCount : 1;
    }

    DescriptorIndex ZWD3D12Texture::GetClearMipLevelUAV(uint32_t mipLevel)
    {
        assert(desc.isUAV);

        DescriptorIndex descriptorIndex = mClearMipLevelUAVs[mipLevel];
        if (descriptorIndex != cInvalidDescriptorIndex)
        {
            return descriptorIndex;
        }

        descriptorIndex = mResources.shaderResourceViewHeap.AllocateDescriptor();
        const ZWTextureSubresourceSet subresources(mipLevel, 1, 0, ZWTextureSubresourceSet::AllArraySlices);
        CreateUAV(mResources.shaderResourceViewHeap.GetCpuHandle(descriptorIndex).ptr, EFormat::UNKNOWN, ETextureDimension::Unknown, subresources);
        mResources.shaderResourceViewHeap.CopyToShaderVisibleHeap(descriptorIndex);
        mClearMipLevelUAVs[mipLevel] = descriptorIndex;
        return descriptorIndex;
    }

    ZWTextureHandle ZWD3D12Device::CreateTexture(const ZWTextureDesc& desc)
    {
        D3D12_RESOURCE_DESC resourceDesc = ConvertTextureDesc(desc);
        D3D12_HEAP_PROPERTIES heapProperties = {};
        D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;

        bool isShared = false;
        if (HasSharedResourceFlag(desc.sharedResourceFlags, ESharedResourceFlags::Shared))
        {
            heapFlags |= D3D12_HEAP_FLAG_SHARED;
            isShared = true;
        }

        if (HasSharedResourceFlag(desc.sharedResourceFlags, ESharedResourceFlags::Shared_CrossAdapter))
        {
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
            heapFlags |= D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
            isShared = true;
        }

        if (desc.isTiled)
        {
            resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
        }

        ZWD3D12Texture* texture = new ZWD3D12Texture(mContext, mResources, desc, resourceDesc);
        const D3D12_CLEAR_VALUE clearValue = ConvertTextureClearValue(desc);

        if (desc.isVirtual)
        {
            return ZWTextureHandle::Create(texture);
        }

        HRESULT result = S_OK;
        if (desc.isTiled)
        {
            result = mContext.device->CreateReservedResource(
                &texture->resourceDesc,
                ConvertResourceStates(desc.initialState),
                desc.useClearValue ? &clearValue : nullptr,
                IID_PPV_ARGS(texture->resource.ReleaseAndGetAddressOf()));
        }
        else
        {
            heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
            result = mContext.device->CreateCommittedResource(
                &heapProperties,
                heapFlags,
                &texture->resourceDesc,
                ConvertResourceStates(desc.initialState),
                desc.useClearValue ? &clearValue : nullptr,
                IID_PPV_ARGS(texture->resource.ReleaseAndGetAddressOf()));
        }

        if (FAILED(result))
        {
            std::stringstream messageBuilder;
            messageBuilder << "Failed to create texture " << HApp::DebugNameToString(desc.debugName)
                << ", error code = 0x" << std::hex << result;
            mContext.Error(messageBuilder.str());

            delete texture;
            return nullptr;
        }

        if (isShared)
        {
            result = mContext.device->CreateSharedHandle(
                texture->resource.Get(),
                nullptr,
                GENERIC_ALL,
                nullptr,
                &texture->sharedHandle);

            if (FAILED(result))
            {
                std::stringstream messageBuilder;
                messageBuilder << "Failed to create shared handle " << HApp::DebugNameToString(desc.debugName)
                    << ", error code = 0x" << std::hex << result;
                mContext.Error(messageBuilder.str());

                delete texture;
                return nullptr;
            }
        }

        texture->PostCreate();
        return ZWTextureHandle::Create(texture);
    }

    ZWMemoryRequirements ZWD3D12Device::GetTextureMemoryRequirements(ITexture* texture)
    {
        ZWD3D12Texture* d3d12Texture = static_cast<ZWD3D12Texture*>(texture);
        const D3D12_RESOURCE_ALLOCATION_INFO allocationInfo = mContext.device->GetResourceAllocationInfo(1, 1, &d3d12Texture->resourceDesc);

        ZWMemoryRequirements memoryRequirements = {};
        memoryRequirements.alignment = allocationInfo.Alignment;
        memoryRequirements.size = allocationInfo.SizeInBytes;
        return memoryRequirements;
    }

    bool ZWD3D12Device::BindTextureMemory(ITexture* texture, IHeap* heap, uint64_t offset)
    {
        ZWD3D12Texture* d3d12Texture = static_cast<ZWD3D12Texture*>(texture);
        ZWD3D12Heap* d3d12Heap = static_cast<ZWD3D12Heap*>(heap);
        if (d3d12Texture->resource != nullptr || !d3d12Texture->desc.isVirtual)
        {
            return false;
        }

        const D3D12_CLEAR_VALUE clearValue = ConvertTextureClearValue(d3d12Texture->desc);
        const HRESULT result = mContext.device->CreatePlacedResource(
            d3d12Heap->heap.Get(),
            offset,
            &d3d12Texture->resourceDesc,
            ConvertResourceStates(d3d12Texture->desc.initialState),
            d3d12Texture->desc.useClearValue ? &clearValue : nullptr,
            IID_PPV_ARGS(d3d12Texture->resource.ReleaseAndGetAddressOf()));

        if (FAILED(result))
        {
            std::stringstream messageBuilder;
            messageBuilder << "Failed to create placed texture " << HApp::DebugNameToString(d3d12Texture->desc.debugName)
                << ", error code = 0x" << std::hex << result;
            mContext.Error(messageBuilder.str());
            return false;
        }

        d3d12Texture->heap = ZWHeapHandle::Create(d3d12Heap);
        d3d12Texture->PostCreate();
        return true;
    }

    ZWTextureHandle ZWD3D12Device::CreateHandleForNativeTexture(ObjectType objectType, HCommon::ZWObject nativeTexture, const ZWTextureDesc& desc)
    {
        if (nativeTexture.pointer == nullptr || objectType != HRHIObjectTypes::gD3D12Resource)
        {
            return nullptr;
        }

        ID3D12Resource* resourceHandle = static_cast<ID3D12Resource*>(nativeTexture.pointer);
        ZWD3D12Texture* texture = new ZWD3D12Texture(mContext, mResources, desc, resourceHandle->GetDesc());
        texture->resource = resourceHandle;
        texture->PostCreate();
        return ZWTextureHandle::Create(texture);
    }

    ZWStagingTextureHandle ZWD3D12Device::CreateStagingTexture(const ZWTextureDesc& desc, ECpuAccessMode cpuAccess)
    {
        assert(cpuAccess != ECpuAccessMode::None);

        ZWD3D12StagingTexture* stagingTexture = new ZWD3D12StagingTexture();
        stagingTexture->desc = desc;
        stagingTexture->resourceDesc = ConvertTextureDesc(desc);
        stagingTexture->ComputeSubresourceOffsets(mContext.device.Get());

        ZWBufferDesc bufferDesc = {};
        bufferDesc.byteSize = stagingTexture->GetSizeInBytes(mContext.device.Get());
        bufferDesc.elementStride = 0;
        bufferDesc.debugName = desc.debugName;
        bufferDesc.cpuAccess = cpuAccess;

        ZWBufferHandle buffer = CreateBuffer(bufferDesc);
        stagingTexture->buffer = static_cast<ZWD3D12Buffer*>(buffer.Get());
        if (stagingTexture->buffer == nullptr)
        {
            delete stagingTexture;
            return nullptr;
        }

        stagingTexture->cpuAccess = cpuAccess;
        return ZWStagingTextureHandle::Create(stagingTexture);
    }

    void ZWD3D12Texture::CreateSRV(size_t descriptor, EFormat format, ETextureDimension dimension, ZWTextureSubresourceSet subresources) const
    {
        subresources = subresources.resolve(desc, false);

        if (dimension == ETextureDimension::Unknown)
        {
            dimension = desc.dimension;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
        viewDesc.Format = GetDxgiFormatMapping(format == EFormat::UNKNOWN ? desc.format : format).srvFormat;
        viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        const uint32_t planeSlice = viewDesc.Format == DXGI_FORMAT_X24_TYPELESS_G8_UINT ? 1u : 0u;

        switch (dimension)
        {
        case ETextureDimension::Texture1D:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture1D.MipLevels = subresources.numMipLevels;
            break;

        case ETextureDimension::Texture1DArray:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture1DArray.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture1DArray.MipLevels = subresources.numMipLevels;
            break;

        case ETextureDimension::Texture2D:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture2D.MipLevels = subresources.numMipLevels;
            viewDesc.Texture2D.PlaneSlice = planeSlice;
            break;

        case ETextureDimension::Texture2DArray:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture2DArray.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture2DArray.MipLevels = subresources.numMipLevels;
            viewDesc.Texture2DArray.PlaneSlice = planeSlice;
            break;

        case ETextureDimension::TextureCube:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            viewDesc.TextureCube.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.TextureCube.MipLevels = subresources.numMipLevels;
            break;

        case ETextureDimension::TextureCubeArray:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
            viewDesc.TextureCubeArray.First2DArrayFace = subresources.baseArraySlice;
            viewDesc.TextureCubeArray.NumCubes = subresources.numArraySlices / 6;
            viewDesc.TextureCubeArray.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.TextureCubeArray.MipLevels = subresources.numMipLevels;
            break;

        case ETextureDimension::Texture2DMS:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
            break;

        case ETextureDimension::Texture2DMSArray:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
            viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
            break;

        case ETextureDimension::Texture3D:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture3D.MipLevels = subresources.numMipLevels;
            break;

        case ETextureDimension::Unknown:
            return;
        }

        mContext.device->CreateShaderResourceView(resource.Get(), &viewDesc, { descriptor });
    }

    void ZWD3D12Texture::CreateUAV(size_t descriptor, EFormat format, ETextureDimension dimension, ZWTextureSubresourceSet subresources) const
    {
        subresources = subresources.resolve(desc, true);

        if (dimension == ETextureDimension::Unknown)
        {
            dimension = desc.dimension;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
        viewDesc.Format = GetDxgiFormatMapping(format == EFormat::UNKNOWN ? desc.format : format).srvFormat;

        switch (dimension)
        {
        case ETextureDimension::Texture1D:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Texture1DArray:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Texture2D:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Texture2DArray:
        case ETextureDimension::TextureCube:
        case ETextureDimension::TextureCubeArray:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Texture3D:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.FirstWSlice = 0;
            viewDesc.Texture3D.WSize = desc.depth;
            viewDesc.Texture3D.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Texture2DMS:
        case ETextureDimension::Texture2DMSArray:
            ReportUnsupportedTextureDimension(mContext, "UAV creation", desc.debugName);
            return;

        case ETextureDimension::Unknown:
            return;
        }

        mContext.device->CreateUnorderedAccessView(resource.Get(), nullptr, &viewDesc, { descriptor });
    }

    void ZWD3D12Texture::CreateRTV(size_t descriptor, EFormat format, ZWTextureSubresourceSet subresources) const
    {
        subresources = subresources.resolve(desc, true);

        D3D12_RENDER_TARGET_VIEW_DESC viewDesc = {};
        viewDesc.Format = GetDxgiFormatMapping(format == EFormat::UNKNOWN ? desc.format : format).rtvFormat;

        switch (desc.dimension)
        {
        case ETextureDimension::Texture1D:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Texture1DArray:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Texture2D:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Texture2DArray:
        case ETextureDimension::TextureCube:
        case ETextureDimension::TextureCubeArray:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Texture2DMS:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            break;

        case ETextureDimension::Texture2DMSArray:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
            viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
            break;

        case ETextureDimension::Texture3D:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.FirstWSlice = subresources.baseArraySlice;
            viewDesc.Texture3D.WSize = subresources.numArraySlices;
            viewDesc.Texture3D.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Unknown:
            return;
        }

        mContext.device->CreateRenderTargetView(resource.Get(), &viewDesc, { descriptor });
    }

    void ZWD3D12Texture::CreateDSV(size_t descriptor, ZWTextureSubresourceSet subresources, bool isReadOnly) const
    {
        subresources = subresources.resolve(desc, true);

        D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
        viewDesc.Format = GetDxgiFormatMapping(desc.format).rtvFormat;

        if (isReadOnly)
        {
            viewDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
            if (viewDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || viewDesc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
            {
                viewDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
            }
        }

        switch (desc.dimension)
        {
        case ETextureDimension::Texture1D:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Texture1DArray:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Texture2D:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Texture2DArray:
        case ETextureDimension::TextureCube:
        case ETextureDimension::TextureCubeArray:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
            break;

        case ETextureDimension::Texture2DMS:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
            break;

        case ETextureDimension::Texture2DMSArray:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
            viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
            break;

        case ETextureDimension::Texture3D:
            ReportUnsupportedTextureDimension(mContext, "DSV creation", desc.debugName);
            return;

        case ETextureDimension::Unknown:
            return;
        }

        mContext.device->CreateDepthStencilView(resource.Get(), &viewDesc, { descriptor });
    }

    ZWD3D12StagingTexture::ZWSliceRegion ZWD3D12StagingTexture::GetSliceRegion(ID3D12Device* device, const ZWTextureSlice& slice)
    {
        ZWSliceRegion region = {};
        const UINT subresource = CalcSubresource(slice.mipLevel, slice.arraySlice, 0, desc.mipLevels, desc.arraySize);
        assert(subresource < subresourceOffsets.size());

        UINT64 sizeInBytes = 0;
        device->GetCopyableFootprints(&resourceDesc, subresource, 1, subresourceOffsets[subresource], &region.footprint, nullptr, nullptr, &sizeInBytes);
        region.offset = static_cast<off_t>(region.footprint.Offset);
        region.size = static_cast<size_t>(sizeInBytes);
        return region;
    }

    size_t ZWD3D12StagingTexture::GetSizeInBytes(ID3D12Device* device)
    {
        const UINT lastSubresource = CalcSubresource(desc.mipLevels - 1u, desc.arraySize - 1u, 0, desc.mipLevels, desc.arraySize);
        assert(lastSubresource < subresourceOffsets.size());

        UINT64 lastSubresourceSize = 0;
        device->GetCopyableFootprints(&resourceDesc, lastSubresource, 1, 0, nullptr, nullptr, nullptr, &lastSubresourceSize);
        return static_cast<size_t>(subresourceOffsets[lastSubresource] + lastSubresourceSize);
    }

    void ZWD3D12StagingTexture::ComputeSubresourceOffsets(ID3D12Device* device)
    {
        const UINT lastSubresource = CalcSubresource(desc.mipLevels - 1u, desc.arraySize - 1u, 0, desc.mipLevels, desc.arraySize);
        const UINT numSubresources = lastSubresource + 1u;

        subresourceOffsets.resize(numSubresources);

        UINT64 baseOffset = 0;
        for (UINT index = 0; index < numSubresources; ++index)
        {
            UINT64 subresourceSize = 0;
            device->GetCopyableFootprints(&resourceDesc, index, 1, 0, nullptr, nullptr, nullptr, &subresourceSize);

            subresourceOffsets[index] = baseOffset;
            baseOffset += subresourceSize;
            baseOffset = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT
                * ((baseOffset + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u) / D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        }
    }

    HCommon::ZWObject ZWD3D12StagingTexture::GetNativeObject(ObjectType objectType)
    {
        if (objectType == HRHI::HRHIObjectTypes::gD3D12Resource && buffer != nullptr)
        {
            return HCommon::ZWObject(buffer->resource.Get());
        }

        return nullptr;
    }

    HCommon::ZWObject ZWD3D12SamplerFeedbackTexture::GetNativeObject(ObjectType objectType)
    {
        if (objectType == HRHI::HRHIObjectTypes::gD3D12Resource)
        {
            return HCommon::ZWObject(resource.Get());
        }

        return nullptr;
    }

    void ZWD3D12SamplerFeedbackTexture::CreateUAV(size_t descriptor) const
    {
        if (mContext.device8 == nullptr)
        {
            mContext.Error("Sampler feedback UAV creation requires ID3D12Device8.");
            return;
        }

        ZWD3D12Texture* pairedD3D12Texture = static_cast<ZWD3D12Texture*>(pairedTexture.Get());
        if (pairedD3D12Texture == nullptr)
        {
            return;
        }

        mContext.device8->CreateSamplerFeedbackUnorderedAccessView(
            pairedD3D12Texture->resource.Get(),
            resource.Get(),
            { descriptor });
    }

    void* ZWD3D12Device::MapStagingTexture(IStagingTexture* texture, const ZWTextureSlice& slice, ECpuAccessMode cpuAccess, size_t* outRowPitch)
    {
        ZWD3D12StagingTexture* d3d12Texture = static_cast<ZWD3D12StagingTexture*>(texture);

        assert(slice.x == 0);
        assert(slice.y == 0);
        assert(cpuAccess != ECpuAccessMode::None);
        assert(d3d12Texture->mappedRegion.size == 0);
        assert(d3d12Texture->mappedAccess == ECpuAccessMode::None);

        const ZWTextureSlice resolvedSlice = slice.resolve(d3d12Texture->desc);
        const ZWD3D12StagingTexture::ZWSliceRegion region = d3d12Texture->GetSliceRegion(mContext.device.Get(), resolvedSlice);

        if (d3d12Texture->lastUseFence != nullptr)
        {
            WaitForFence(d3d12Texture->lastUseFence.Get(), d3d12Texture->lastUseFenceValue, mFenceEvent);
            d3d12Texture->lastUseFence = nullptr;
        }

        D3D12_RANGE range = {};
        if (cpuAccess == ECpuAccessMode::Read)
        {
            range.Begin = static_cast<SIZE_T>(region.offset);
            range.End = static_cast<SIZE_T>(region.offset + region.size);
        }

        uint8_t* mappedData = nullptr;
        const HRESULT result = d3d12Texture->buffer->resource->Map(0, &range, reinterpret_cast<void**>(&mappedData));
        if (FAILED(result))
        {
            std::stringstream messageBuilder;
            messageBuilder << "Map call failed for texture " << HApp::DebugNameToString(d3d12Texture->desc.debugName)
                << ", HRESULT = 0x" << std::hex << std::setw(8) << result;
            mContext.Error(messageBuilder.str());
            return nullptr;
        }

        d3d12Texture->mappedRegion = region;
        d3d12Texture->mappedAccess = cpuAccess;

        if (outRowPitch != nullptr)
        {
            *outRowPitch = region.footprint.Footprint.RowPitch;
        }

        return mappedData + d3d12Texture->mappedRegion.offset;
    }

    void ZWD3D12Device::UnmapStagingTexture(IStagingTexture* texture)
    {
        ZWD3D12StagingTexture* d3d12Texture = static_cast<ZWD3D12StagingTexture*>(texture);

        assert(d3d12Texture->mappedRegion.size != 0);
        assert(d3d12Texture->mappedAccess != ECpuAccessMode::None);

        D3D12_RANGE range = {};
        if (d3d12Texture->mappedAccess == ECpuAccessMode::Write)
        {
            range.Begin = static_cast<SIZE_T>(d3d12Texture->mappedRegion.offset);
            range.End = static_cast<SIZE_T>(d3d12Texture->mappedRegion.offset + d3d12Texture->mappedRegion.size);
        }

        d3d12Texture->buffer->resource->Unmap(0, &range);
        d3d12Texture->mappedRegion.size = 0;
        d3d12Texture->mappedAccess = ECpuAccessMode::None;
    }

    void ZWD3D12Device::GetTextureTiling(
        ITexture* texture,
        uint32_t* numTiles,
        ZWPackedMipDesc* packedMipDesc,
        ZWTileShape* tileShape,
        uint32_t* subresourceTilingsNum,
        ZWSubresourceTiling* subresourceTilings)
    {
        ZWD3D12Texture* d3d12Texture = static_cast<ZWD3D12Texture*>(texture);
        if (d3d12Texture == nullptr || d3d12Texture->resource == nullptr)
        {
            return;
        }

        D3D12_PACKED_MIP_INFO d3d12PackedMipInfo = {};
        D3D12_TILE_SHAPE d3d12TileShape = {};
        std::vector<D3D12_SUBRESOURCE_TILING> d3d12SubresourceTilings;

        if (subresourceTilingsNum != nullptr && *subresourceTilingsNum > 0)
        {
            d3d12SubresourceTilings.resize(*subresourceTilingsNum);
        }

        mContext.device->GetResourceTiling(
            d3d12Texture->resource.Get(),
            numTiles,
            packedMipDesc != nullptr ? &d3d12PackedMipInfo : nullptr,
            tileShape != nullptr ? &d3d12TileShape : nullptr,
            subresourceTilingsNum,
            0,
            d3d12SubresourceTilings.empty() ? nullptr : d3d12SubresourceTilings.data());

        if (packedMipDesc != nullptr)
        {
            packedMipDesc->numStandardMips = d3d12PackedMipInfo.NumStandardMips;
            packedMipDesc->numPackedMips = d3d12PackedMipInfo.NumPackedMips;
            packedMipDesc->startTileIndexInOverallResource = d3d12PackedMipInfo.StartTileIndexInOverallResource;
            packedMipDesc->numTilesForPackedMips = d3d12PackedMipInfo.NumTilesForPackedMips;
        }

        if (tileShape != nullptr)
        {
            tileShape->widthInTexels = d3d12TileShape.WidthInTexels;
            tileShape->heightInTexels = d3d12TileShape.HeightInTexels;
            tileShape->depthInTexels = d3d12TileShape.DepthInTexels;
        }

        if (subresourceTilingsNum != nullptr && subresourceTilings != nullptr)
        {
            for (uint32_t index = 0; index < *subresourceTilingsNum; ++index)
            {
                subresourceTilings[index].widthInTiles = d3d12SubresourceTilings[index].WidthInTiles;
                subresourceTilings[index].heightInTiles = d3d12SubresourceTilings[index].HeightInTiles;
                subresourceTilings[index].depthInTiles = d3d12SubresourceTilings[index].DepthInTiles;
                subresourceTilings[index].startTileIndexInOverallResource = d3d12SubresourceTilings[index].StartTileIndexInOverallResource;
            }
        }
    }

    void ZWD3D12Device::UpdateTextureTileMappings(
        ITexture* texture,
        const ZWTextureTilesMapping* tileMappings,
        uint32_t numTileMappings,
        ECommandQueue executionQueue)
    {
        ZWD3D12Queue* queue = GetQueue(executionQueue);
        ZWD3D12Texture* d3d12Texture = static_cast<ZWD3D12Texture*>(texture);
        if (queue == nullptr || d3d12Texture == nullptr || d3d12Texture->resource == nullptr)
        {
            return;
        }

        D3D12_TILE_SHAPE tileShape = {};
        D3D12_SUBRESOURCE_TILING subresourceTiling = {};
        mContext.device->GetResourceTiling(d3d12Texture->resource.Get(), nullptr, nullptr, &tileShape, nullptr, 0, &subresourceTiling);

        for (uint32_t mappingIndex = 0; mappingIndex < numTileMappings; ++mappingIndex)
        {
            ID3D12Heap* heap = tileMappings[mappingIndex].heap != nullptr
                ? static_cast<ZWD3D12Heap*>(tileMappings[mappingIndex].heap)->heap.Get()
                : nullptr;

            const uint32_t numRegions = tileMappings[mappingIndex].numTextureRegions;
            std::vector<D3D12_TILED_RESOURCE_COORDINATE> resourceCoordinates(numRegions);
            std::vector<D3D12_TILE_REGION_SIZE> regionSizes(numRegions);
            std::vector<D3D12_TILE_RANGE_FLAGS> rangeFlags(numRegions, heap != nullptr ? D3D12_TILE_RANGE_FLAG_NONE : D3D12_TILE_RANGE_FLAG_NULL);
            std::vector<UINT> heapStartOffsets(numRegions);
            std::vector<UINT> rangeTileCounts(numRegions);

            for (uint32_t regionIndex = 0; regionIndex < numRegions; ++regionIndex)
            {
                const ZWTiledTextureCoordinate& tiledTextureCoordinate = tileMappings[mappingIndex].tiledTextureCoordinates[regionIndex];
                const ZWTiledTextureRegion& tiledTextureRegion = tileMappings[mappingIndex].tiledTextureRegions[regionIndex];

                resourceCoordinates[regionIndex].Subresource = tiledTextureCoordinate.mipLevel * d3d12Texture->desc.arraySize + tiledTextureCoordinate.arrayLevel;
                resourceCoordinates[regionIndex].X = tiledTextureCoordinate.x;
                resourceCoordinates[regionIndex].Y = tiledTextureCoordinate.y;
                resourceCoordinates[regionIndex].Z = tiledTextureCoordinate.z;

                if (tiledTextureRegion.tilesNum != 0)
                {
                    regionSizes[regionIndex].NumTiles = tiledTextureRegion.tilesNum;
                    regionSizes[regionIndex].UseBox = false;
                }
                else
                {
                    const uint32_t tilesX = (tiledTextureRegion.width + (tileShape.WidthInTexels - 1)) / tileShape.WidthInTexels;
                    const uint32_t tilesY = (tiledTextureRegion.height + (tileShape.HeightInTexels - 1)) / tileShape.HeightInTexels;
                    const uint32_t tilesZ = (tiledTextureRegion.depth + (tileShape.DepthInTexels - 1)) / tileShape.DepthInTexels;

                    regionSizes[regionIndex].Width = tilesX;
                    regionSizes[regionIndex].Height = static_cast<UINT16>(tilesY);
                    regionSizes[regionIndex].Depth = static_cast<UINT16>(tilesZ);
                    regionSizes[regionIndex].NumTiles = tilesX * tilesY * tilesZ;
                    regionSizes[regionIndex].UseBox = true;
                }

                if (heap != nullptr)
                {
                    heapStartOffsets[regionIndex] = static_cast<UINT>(tileMappings[mappingIndex].byteOffsets[regionIndex] / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);
                }

                rangeTileCounts[regionIndex] = regionSizes[regionIndex].NumTiles;
            }

            queue->queue->UpdateTileMappings(
                d3d12Texture->resource.Get(),
                numRegions,
                resourceCoordinates.data(),
                regionSizes.data(),
                heap,
                numRegions,
                rangeFlags.data(),
                heap != nullptr ? heapStartOffsets.data() : nullptr,
                rangeTileCounts.data(),
                D3D12_TILE_MAPPING_FLAG_NONE);
        }
    }

    ZWSamplerFeedbackTextureHandle ZWD3D12Device::CreateSamplerFeedbackTexture(ITexture* pairedTexture, const ZWSamplerFeedbackTextureDesc& desc)
    {
        if (mContext.device8 == nullptr)
        {
            mContext.Error("Sampler feedback texture creation requires ID3D12Device8.");
            return nullptr;
        }

        ZWD3D12Texture* pairedD3D12Texture = static_cast<ZWD3D12Texture*>(pairedTexture);
        if (pairedD3D12Texture == nullptr || pairedD3D12Texture->resource == nullptr)
        {
            return nullptr;
        }

        const ZWTextureDesc pairedTextureDesc = pairedD3D12Texture->desc;
        const D3D12_RESOURCE_DESC pairedResourceDesc = pairedD3D12Texture->resourceDesc;

        D3D12_RESOURCE_DESC1 feedbackResourceDesc = {};
        std::memcpy(&feedbackResourceDesc, &pairedResourceDesc, sizeof(D3D12_RESOURCE_DESC));
        feedbackResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        feedbackResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        switch (desc.samplerFeedbackFormat)
        {
        case ESamplerFeedbackFormat::MinMipOpaque:
            feedbackResourceDesc.Format = DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE;
            break;

        case ESamplerFeedbackFormat::MipRegionUsedOpaque:
            feedbackResourceDesc.Format = DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE;
            break;
        }

        feedbackResourceDesc.SamplerFeedbackMipRegion = D3D12_MIP_REGION{
            desc.samplerFeedbackMipRegionX,
            desc.samplerFeedbackMipRegionY,
            desc.samplerFeedbackMipRegionZ
        };

        D3D12_HEAP_PROPERTIES heapProperties = {};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

        ZWTextureDesc trackingDesc = pairedTexture->GetDesc();
        trackingDesc.initialState = desc.initialState;
        trackingDesc.keepInitialState = desc.keepInitialState;

        ZWD3D12SamplerFeedbackTexture* texture = new ZWD3D12SamplerFeedbackTexture(mContext, desc, trackingDesc, pairedTexture);
        const HRESULT result = mContext.device8->CreateCommittedResource2(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &feedbackResourceDesc,
            ConvertResourceStates(desc.initialState),
            nullptr,
            nullptr,
            IID_PPV_ARGS(texture->resource.ReleaseAndGetAddressOf()));

        if (FAILED(result))
        {
            std::stringstream messageBuilder;
            messageBuilder << "Failed to create sampler feedback texture for " << HApp::DebugNameToString(pairedTextureDesc.debugName)
                << ", error code = 0x" << std::hex << result;
            mContext.Error(messageBuilder.str());

            delete texture;
            return nullptr;
        }

        std::wstring debugName = L"Sampler Feedback Texture";
        if (!pairedTextureDesc.debugName.empty())
        {
            debugName += L": ";
            debugName += std::wstring(pairedTextureDesc.debugName.begin(), pairedTextureDesc.debugName.end());
        }

        texture->resource->SetName(debugName.c_str());
        return ZWSamplerFeedbackTextureHandle::Create(texture);
    }

    ZWSamplerFeedbackTextureHandle ZWD3D12Device::CreateSamplerFeedbackForNativeTexture(ObjectType objectType, HCommon::ZWObject nativeTexture, ITexture* pairedTexture)
    {
        if (nativeTexture.pointer == nullptr || objectType != HRHIObjectTypes::gD3D12Resource)
        {
            return nullptr;
        }

        ID3D12Resource* resourceHandle = static_cast<ID3D12Resource*>(nativeTexture.pointer);
        HCommon::RefCountPtr<ID3D12Resource2> resourceHandle2;
        if (FAILED(resourceHandle->QueryInterface(IID_PPV_ARGS(resourceHandle2.ReleaseAndGetAddressOf()))))
        {
            mContext.Error("Failed to query ID3D12Resource2 for a native sampler feedback texture.");
            return nullptr;
        }

        const D3D12_RESOURCE_DESC1 feedbackResourceDesc = resourceHandle2->GetDesc1();

        ZWSamplerFeedbackTextureDesc desc = {};
        desc.samplerFeedbackFormat = feedbackResourceDesc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE
            ? ESamplerFeedbackFormat::MinMipOpaque
            : ESamplerFeedbackFormat::MipRegionUsedOpaque;
        desc.samplerFeedbackMipRegionX = feedbackResourceDesc.SamplerFeedbackMipRegion.Width;
        desc.samplerFeedbackMipRegionY = feedbackResourceDesc.SamplerFeedbackMipRegion.Height;
        desc.samplerFeedbackMipRegionZ = feedbackResourceDesc.SamplerFeedbackMipRegion.Depth;

        ZWTextureDesc trackingDesc = pairedTexture->GetDesc();
        trackingDesc.initialState = EResourceStates::Unknown;
        trackingDesc.keepInitialState = false;

        ZWD3D12SamplerFeedbackTexture* texture = new ZWD3D12SamplerFeedbackTexture(mContext, desc, trackingDesc, pairedTexture);
        texture->resource = resourceHandle;
        return ZWSamplerFeedbackTextureHandle::Create(texture);
    }

    void ZWD3D12CommandList::ClearTextureFloat(ITexture* texture, ZWTextureSubresourceSet subresources, const ZWColor& clearColor)
    {
        ZWD3D12Texture* d3d12Texture = static_cast<ZWD3D12Texture*>(texture);
        if (d3d12Texture == nullptr)
        {
            return;
        }

        subresources = subresources.resolve(d3d12Texture->desc, false);
        mInstance->referencedResources.push_back(d3d12Texture);

        if (d3d12Texture->desc.isRenderTarget)
        {
            if (mEnableAutomaticBarriers)
            {
                RequireTextureState(d3d12Texture, subresources, EResourceStates::RenderTarget);
                mBindingStatesDirty = true;
            }

            CommitBarriers();

            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; ++mipLevel)
            {
                const ZWTextureSubresourceSet mipSubresources(mipLevel, 1, subresources.baseArraySlice, subresources.numArraySlices);
                const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = {
                    d3d12Texture->GetNativeView(
                        HRHI::HRHIObjectTypes::gD3D12RenderTargetViewDescriptor,
                        EFormat::UNKNOWN,
                        mipSubresources,
                        ETextureDimension::Unknown).integer
                };

                mActiveCommandList->commandList->ClearRenderTargetView(renderTargetView, &clearColor.r, 0, nullptr);
            }
        }
        else
        {
            if (mEnableAutomaticBarriers)
            {
                RequireTextureState(d3d12Texture, subresources, EResourceStates::UnorderedAccess);
                mBindingStatesDirty = true;
            }

            CommitBarriers();
            CommitDescriptorHeaps();

            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; ++mipLevel)
            {
                const DescriptorIndex clearUav = d3d12Texture->GetClearMipLevelUAV(mipLevel);
                mActiveCommandList->commandList->ClearUnorderedAccessViewFloat(
                    m_Resources.shaderResourceViewHeap.GetGpuHandle(clearUav),
                    m_Resources.shaderResourceViewHeap.GetCpuHandle(clearUav),
                    d3d12Texture->resource.Get(),
                    &clearColor.r,
                    0,
                    nullptr);
            }
        }
    }

    void ZWD3D12CommandList::ClearDepthStencilTexture(
        ITexture* texture,
        ZWTextureSubresourceSet subresources,
        bool clearDepth,
        float depth,
        bool clearStencil,
        uint8_t stencil)
    {
        if (!clearDepth && !clearStencil)
        {
            return;
        }

        ZWD3D12Texture* d3d12Texture = static_cast<ZWD3D12Texture*>(texture);
        if (d3d12Texture == nullptr)
        {
            return;
        }

        subresources = subresources.resolve(d3d12Texture->desc, false);
        mInstance->referencedResources.push_back(d3d12Texture);

        if (mEnableAutomaticBarriers)
        {
            RequireTextureState(d3d12Texture, subresources, EResourceStates::DepthWrite);
            mBindingStatesDirty = true;
        }

        CommitBarriers();

        D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
        if (!clearDepth)
        {
            clearFlags = D3D12_CLEAR_FLAG_STENCIL;
        }
        else if (!clearStencil)
        {
            clearFlags = D3D12_CLEAR_FLAG_DEPTH;
        }

        for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; ++mipLevel)
        {
            const ZWTextureSubresourceSet mipSubresources(mipLevel, 1, subresources.baseArraySlice, subresources.numArraySlices);
            const D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView = {
                d3d12Texture->GetNativeView(
                    HRHI::HRHIObjectTypes::gD3D12DepthStencilViewDescriptor,
                    EFormat::UNKNOWN,
                    mipSubresources,
                    ETextureDimension::Unknown).integer
            };

            mActiveCommandList->commandList->ClearDepthStencilView(depthStencilView, clearFlags, depth, stencil, 0, nullptr);
        }
    }

    void ZWD3D12CommandList::ClearTextureUInt(ITexture* texture, ZWTextureSubresourceSet subresources, uint32_t clearColor)
    {
        ZWD3D12Texture* d3d12Texture = static_cast<ZWD3D12Texture*>(texture);
        if (d3d12Texture == nullptr)
        {
            return;
        }

        subresources = subresources.resolve(d3d12Texture->desc, false);
        mInstance->referencedResources.push_back(d3d12Texture);

        const uint32_t clearValues[4] = { clearColor, clearColor, clearColor, clearColor };

        if (d3d12Texture->desc.isUAV)
        {
            if (mEnableAutomaticBarriers)
            {
                RequireTextureState(d3d12Texture, subresources, EResourceStates::UnorderedAccess);
                mBindingStatesDirty = true;
            }

            CommitBarriers();
            CommitDescriptorHeaps();

            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; ++mipLevel)
            {
                const DescriptorIndex clearUav = d3d12Texture->GetClearMipLevelUAV(mipLevel);
                mActiveCommandList->commandList->ClearUnorderedAccessViewUint(
                    m_Resources.shaderResourceViewHeap.GetGpuHandle(clearUav),
                    m_Resources.shaderResourceViewHeap.GetCpuHandle(clearUav),
                    d3d12Texture->resource.Get(),
                    clearValues,
                    0,
                    nullptr);
            }
        }
        else if (d3d12Texture->desc.isRenderTarget)
        {
            if (mEnableAutomaticBarriers)
            {
                RequireTextureState(d3d12Texture, subresources, EResourceStates::RenderTarget);
                mBindingStatesDirty = true;
            }

            CommitBarriers();

            const float floatColor[4] = {
                static_cast<float>(clearColor),
                static_cast<float>(clearColor),
                static_cast<float>(clearColor),
                static_cast<float>(clearColor)
            };

            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; ++mipLevel)
            {
                const ZWTextureSubresourceSet mipSubresources(mipLevel, 1, subresources.baseArraySlice, subresources.numArraySlices);
                const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = {
                    d3d12Texture->GetNativeView(
                        HRHI::HRHIObjectTypes::gD3D12RenderTargetViewDescriptor,
                        EFormat::UNKNOWN,
                        mipSubresources,
                        ETextureDimension::Unknown).integer
                };

                mActiveCommandList->commandList->ClearRenderTargetView(renderTargetView, floatColor, 0, nullptr);
            }
        }
    }

    void ZWD3D12CommandList::ClearSamplerFeedbackTexture(ISamplerFeedbackTexture* texture)
    {
        ZWD3D12SamplerFeedbackTexture* d3d12Texture = static_cast<ZWD3D12SamplerFeedbackTexture*>(texture);
        if (d3d12Texture == nullptr)
        {
            return;
        }

        if (d3d12Texture->clearDescriptorIndex == cInvalidDescriptorIndex)
        {
            d3d12Texture->clearDescriptorIndex = m_Resources.shaderResourceViewHeap.AllocateDescriptor();
            d3d12Texture->CreateUAV(m_Resources.shaderResourceViewHeap.GetCpuHandle(d3d12Texture->clearDescriptorIndex).ptr);
            m_Resources.shaderResourceViewHeap.CopyToShaderVisibleHeap(d3d12Texture->clearDescriptorIndex);
        }

        CommitDescriptorHeaps();

        const UINT clearValues[4] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
        mActiveCommandList->commandList->ClearUnorderedAccessViewUint(
            m_Resources.shaderResourceViewHeap.GetGpuHandle(d3d12Texture->clearDescriptorIndex),
            m_Resources.shaderResourceViewHeap.GetCpuHandle(d3d12Texture->clearDescriptorIndex),
            d3d12Texture->resource.Get(),
            clearValues,
            0,
            nullptr);
    }

    void ZWD3D12CommandList::DecodeSamplerFeedbackTexture(IBuffer* buffer, ISamplerFeedbackTexture* texture, EFormat format)
    {
        ZWD3D12Buffer* d3d12Buffer = static_cast<ZWD3D12Buffer*>(buffer);
        ZWD3D12SamplerFeedbackTexture* d3d12Texture = static_cast<ZWD3D12SamplerFeedbackTexture*>(texture);
        if (d3d12Buffer == nullptr || d3d12Texture == nullptr || mActiveCommandList->commandList4 == nullptr)
        {
            return;
        }

        if (mEnableAutomaticBarriers)
        {
            RequireBufferState(d3d12Buffer, EResourceStates::ResolveDest);
            RequireSamplerFeedbackTextureState(d3d12Texture, EResourceStates::ResolveSource);
            mBindingStatesDirty = true;
        }

        CommitBarriers();

        const ZWDxgiFormatMapping& formatMapping = GetDxgiFormatMapping(format);
        mActiveCommandList->commandList4->ResolveSubresourceRegion(
            d3d12Buffer->resource.Get(),
            0,
            0,
            0,
            d3d12Texture->resource.Get(),
            0,
            nullptr,
            formatMapping.srvFormat,
            D3D12_RESOLVE_MODE_DECODE_SAMPLER_FEEDBACK);
    }

    void ZWD3D12CommandList::SetSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, EResourceStates stateBits)
    {
        RequireSamplerFeedbackTextureState(texture, stateBits);
    }

    void ZWD3D12CommandList::CopyTexture(ITexture* dest, const ZWTextureSlice& destSlice, ITexture* src, const ZWTextureSlice& srcSlice)
    {
        ZWD3D12Texture* destTexture = static_cast<ZWD3D12Texture*>(dest);
        ZWD3D12Texture* srcTexture = static_cast<ZWD3D12Texture*>(src);
        if (destTexture == nullptr || srcTexture == nullptr)
        {
            return;
        }

        const ZWTextureSlice resolvedDestSlice = destSlice.resolve(destTexture->desc);
        const ZWTextureSlice resolvedSrcSlice = srcSlice.resolve(srcTexture->desc);

        D3D12_TEXTURE_COPY_LOCATION destLocation = {};
        destLocation.pResource = destTexture->resource.Get();
        destLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destLocation.SubresourceIndex = CalcSubresource(
            resolvedDestSlice.mipLevel,
            resolvedDestSlice.arraySlice,
            0,
            destTexture->desc.mipLevels,
            destTexture->desc.arraySize);

        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource = srcTexture->resource.Get();
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex = CalcSubresource(
            resolvedSrcSlice.mipLevel,
            resolvedSrcSlice.arraySlice,
            0,
            srcTexture->desc.mipLevels,
            srcTexture->desc.arraySize);

        D3D12_BOX srcBox = {};
        srcBox.left = resolvedSrcSlice.x;
        srcBox.top = resolvedSrcSlice.y;
        srcBox.front = resolvedSrcSlice.z;
        srcBox.right = resolvedSrcSlice.x + resolvedSrcSlice.width;
        srcBox.bottom = resolvedSrcSlice.y + resolvedSrcSlice.height;
        srcBox.back = resolvedSrcSlice.z + resolvedSrcSlice.depth;

        if (mEnableAutomaticBarriers)
        {
            RequireTextureState(destTexture, ZWTextureSubresourceSet(resolvedDestSlice.mipLevel, 1, resolvedDestSlice.arraySlice, 1), EResourceStates::CopyDest);
            RequireTextureState(srcTexture, ZWTextureSubresourceSet(resolvedSrcSlice.mipLevel, 1, resolvedSrcSlice.arraySlice, 1), EResourceStates::CopySource);
            mBindingStatesDirty = true;
        }

        CommitBarriers();

        mInstance->referencedResources.push_back(destTexture);
        mInstance->referencedResources.push_back(srcTexture);

        mActiveCommandList->commandList->CopyTextureRegion(
            &destLocation,
            resolvedDestSlice.x,
            resolvedDestSlice.y,
            resolvedDestSlice.z,
            &srcLocation,
            &srcBox);
    }

    void ZWD3D12CommandList::CopyTexture(ITexture* dest, const ZWTextureSlice& destSlice, IStagingTexture* src, const ZWTextureSlice& srcSlice)
    {
        ZWD3D12Texture* destTexture = static_cast<ZWD3D12Texture*>(dest);
        ZWD3D12StagingTexture* srcTexture = static_cast<ZWD3D12StagingTexture*>(src);
        if (destTexture == nullptr || srcTexture == nullptr)
        {
            return;
        }

        const ZWTextureSlice resolvedDestSlice = destSlice.resolve(destTexture->desc);
        const ZWTextureSlice resolvedSrcSlice = srcSlice.resolve(srcTexture->desc);

        if (mEnableAutomaticBarriers)
        {
            RequireTextureState(destTexture, ZWTextureSubresourceSet(resolvedDestSlice.mipLevel, 1, resolvedDestSlice.arraySlice, 1), EResourceStates::CopyDest);
            RequireBufferState(srcTexture->buffer.Get(), EResourceStates::CopySource);
            mBindingStatesDirty = true;
        }

        CommitBarriers();

        mInstance->referencedResources.push_back(destTexture);
        mInstance->referencedStagingTextures.push_back(srcTexture);

        const ZWD3D12StagingTexture::ZWSliceRegion srcRegion = srcTexture->GetSliceRegion(m_Context.device.Get(), resolvedSrcSlice);

        D3D12_TEXTURE_COPY_LOCATION destLocation = {};
        destLocation.pResource = destTexture->resource.Get();
        destLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destLocation.SubresourceIndex = CalcSubresource(
            resolvedDestSlice.mipLevel,
            resolvedDestSlice.arraySlice,
            0,
            destTexture->desc.mipLevels,
            destTexture->desc.arraySize);

        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource = srcTexture->buffer->resource.Get();
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLocation.PlacedFootprint = srcRegion.footprint;

        D3D12_BOX srcBox = {};
        srcBox.left = resolvedSrcSlice.x;
        srcBox.top = resolvedSrcSlice.y;
        srcBox.front = resolvedSrcSlice.z;
        srcBox.right = resolvedSrcSlice.x + resolvedSrcSlice.width;
        srcBox.bottom = resolvedSrcSlice.y + resolvedSrcSlice.height;
        srcBox.back = resolvedSrcSlice.z + resolvedSrcSlice.depth;

        mActiveCommandList->commandList->CopyTextureRegion(
            &destLocation,
            resolvedDestSlice.x,
            resolvedDestSlice.y,
            resolvedDestSlice.z,
            &srcLocation,
            &srcBox);
    }

    void ZWD3D12CommandList::CopyTexture(IStagingTexture* dest, const ZWTextureSlice& destSlice, ITexture* src, const ZWTextureSlice& srcSlice)
    {
        ZWD3D12StagingTexture* destTexture = static_cast<ZWD3D12StagingTexture*>(dest);
        ZWD3D12Texture* srcTexture = static_cast<ZWD3D12Texture*>(src);
        if (destTexture == nullptr || srcTexture == nullptr)
        {
            return;
        }

        const ZWTextureSlice resolvedDestSlice = destSlice.resolve(destTexture->desc);
        const ZWTextureSlice resolvedSrcSlice = srcSlice.resolve(srcTexture->desc);

        if (mEnableAutomaticBarriers)
        {
            RequireTextureState(srcTexture, ZWTextureSubresourceSet(resolvedSrcSlice.mipLevel, 1, resolvedSrcSlice.arraySlice, 1), EResourceStates::CopySource);
            RequireBufferState(destTexture->buffer.Get(), EResourceStates::CopyDest);
            mBindingStatesDirty = true;
        }

        CommitBarriers();

        mInstance->referencedResources.push_back(srcTexture);
        mInstance->referencedStagingTextures.push_back(destTexture);

        const ZWD3D12StagingTexture::ZWSliceRegion destRegion = destTexture->GetSliceRegion(m_Context.device.Get(), resolvedDestSlice);

        D3D12_TEXTURE_COPY_LOCATION destLocation = {};
        destLocation.pResource = destTexture->buffer->resource.Get();
        destLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destLocation.PlacedFootprint = destRegion.footprint;

        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource = srcTexture->resource.Get();
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex = CalcSubresource(
            resolvedSrcSlice.mipLevel,
            resolvedSrcSlice.arraySlice,
            0,
            srcTexture->desc.mipLevels,
            srcTexture->desc.arraySize);

        D3D12_BOX srcBox = {};
        srcBox.left = resolvedSrcSlice.x;
        srcBox.top = resolvedSrcSlice.y;
        srcBox.front = resolvedSrcSlice.z;
        srcBox.right = resolvedSrcSlice.x + resolvedSrcSlice.width;
        srcBox.bottom = resolvedSrcSlice.y + resolvedSrcSlice.height;
        srcBox.back = resolvedSrcSlice.z + resolvedSrcSlice.depth;

        mActiveCommandList->commandList->CopyTextureRegion(
            &destLocation,
            resolvedDestSlice.x,
            resolvedDestSlice.y,
            resolvedDestSlice.z,
            &srcLocation,
            &srcBox);
    }

    void ZWD3D12CommandList::WriteTexture(ITexture* dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch)
    {
        ZWD3D12Texture* destTexture = static_cast<ZWD3D12Texture*>(dest);
        if (destTexture == nullptr)
        {
            return;
        }

        if (mEnableAutomaticBarriers)
        {
            RequireTextureState(destTexture, ZWTextureSubresourceSet(mipLevel, 1, arraySlice, 1), EResourceStates::CopyDest);
            mBindingStatesDirty = true;
        }

        CommitBarriers();

        const uint32_t subresource = CalcSubresource(mipLevel, arraySlice, 0, destTexture->desc.mipLevels, destTexture->desc.arraySize);

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        uint32_t numRows = 0;
        uint64_t rowSizeInBytes = 0;
        uint64_t totalBytes = 0;
        m_Context.device->GetCopyableFootprints(
            &destTexture->resourceDesc,
            subresource,
            1,
            0,
            &footprint,
            &numRows,
            &rowSizeInBytes,
            &totalBytes);

        void* cpuAddress = nullptr;
        ID3D12Resource* uploadBuffer = nullptr;
        size_t uploadOffset = 0;
        if (!mUploadManager.SuballocateBuffer(
            totalBytes,
            nullptr,
            &uploadBuffer,
            &uploadOffset,
            &cpuAddress,
            nullptr,
            mRecordingVersion,
            D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT))
        {
            m_Context.Error("Couldn't suballocate an upload buffer.");
            return;
        }

        footprint.Offset = static_cast<uint64_t>(uploadOffset);

        for (uint32_t depthSlice = 0; depthSlice < footprint.Footprint.Depth; ++depthSlice)
        {
            for (uint32_t row = 0; row < numRows; ++row)
            {
                void* destAddress = static_cast<char*>(cpuAddress)
                    + static_cast<uint64_t>(footprint.Footprint.RowPitch) * static_cast<uint64_t>(row + depthSlice * numRows);
                const void* srcAddress = static_cast<const char*>(data) + rowPitch * row + depthPitch * depthSlice;
                std::memcpy(destAddress, srcAddress, static_cast<size_t>(std::min<uint64_t>(rowPitch, rowSizeInBytes)));
            }
        }

        D3D12_TEXTURE_COPY_LOCATION destLocation = {};
        destLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destLocation.SubresourceIndex = subresource;
        destLocation.pResource = destTexture->resource.Get();

        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLocation.PlacedFootprint = footprint;
        srcLocation.pResource = uploadBuffer;

        mInstance->referencedResources.push_back(destTexture);

        if (uploadBuffer != mCurrentUploadBuffer)
        {
            mInstance->referencedNativeResources.push_back(uploadBuffer);
            mCurrentUploadBuffer = uploadBuffer;
        }

        mActiveCommandList->commandList->CopyTextureRegion(&destLocation, 0, 0, 0, &srcLocation, nullptr);
    }

    void ZWD3D12CommandList::ResolveTexture(
        ITexture* dest,
        const ZWTextureSubresourceSet& dstSubresources,
        ITexture* src,
        const ZWTextureSubresourceSet& srcSubresources)
    {
        ZWD3D12Texture* destTexture = static_cast<ZWD3D12Texture*>(dest);
        ZWD3D12Texture* srcTexture = static_cast<ZWD3D12Texture*>(src);
        if (destTexture == nullptr || srcTexture == nullptr)
        {
            return;
        }

        const ZWTextureSubresourceSet resolvedDestSubresources = dstSubresources.resolve(destTexture->desc, false);
        const ZWTextureSubresourceSet resolvedSrcSubresources = srcSubresources.resolve(srcTexture->desc, false);

        if (resolvedDestSubresources.numArraySlices != resolvedSrcSubresources.numArraySlices
            || resolvedDestSubresources.numMipLevels != resolvedSrcSubresources.numMipLevels)
        {
            return;
        }

        if (mEnableAutomaticBarriers)
        {
            RequireTextureState(destTexture, dstSubresources, EResourceStates::ResolveDest);
            RequireTextureState(srcTexture, srcSubresources, EResourceStates::ResolveSource);
            mBindingStatesDirty = true;
        }

        CommitBarriers();

        const ZWDxgiFormatMapping& formatMapping = GetDxgiFormatMapping(destTexture->desc.format);
        for (uint32_t plane = 0; plane < destTexture->planeCount; ++plane)
        {
            for (ArraySlice arraySlice = 0; arraySlice < resolvedDestSubresources.numArraySlices; ++arraySlice)
            {
                for (MipLevel mipLevel = 0; mipLevel < resolvedDestSubresources.numMipLevels; ++mipLevel)
                {
                    const uint32_t destSubresource = CalcSubresource(
                        mipLevel + resolvedDestSubresources.baseMipLevel,
                        arraySlice + resolvedDestSubresources.baseArraySlice,
                        plane,
                        destTexture->desc.mipLevels,
                        destTexture->desc.arraySize);
                    const uint32_t srcSubresource = CalcSubresource(
                        mipLevel + resolvedSrcSubresources.baseMipLevel,
                        arraySlice + resolvedSrcSubresources.baseArraySlice,
                        plane,
                        srcTexture->desc.mipLevels,
                        srcTexture->desc.arraySize);

                    mActiveCommandList->commandList->ResolveSubresource(
                        destTexture->resource.Get(),
                        destSubresource,
                        srcTexture->resource.Get(),
                        srcSubresource,
                        formatMapping.rtvFormat);
                }
            }
        }
    }
}
