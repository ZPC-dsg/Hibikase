#include <BackEnd/d3d12backend.h>
#include <Utils/stringtranslatehelper.h>

#include <cassert>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace HRHI::HD3D12
{
    namespace
    {
        template <typename TValue, typename TAlignment>
        constexpr TValue AlignValue(TValue value, TAlignment alignment)
        {
            const TValue alignmentValue = static_cast<TValue>(alignment);
            return (value + alignmentValue - TValue(1)) & ~(alignmentValue - TValue(1));
        }

        bool HasSharedResourceFlag(ESharedResourceFlags value, ESharedResourceFlags flag)
        {
            return (value & flag) != 0;
        }

        void ReportInvalidResourceType(const ZWD3D12Context& context)
        {
            context.Error("Invalid buffer resource type.");
        }
    }

    HCommon::ZWObject ZWD3D12Buffer::GetNativeObject(ObjectType objectType)
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

    ZWD3D12Buffer::~ZWD3D12Buffer()
    {
        if (mContext.logBufferLifetime)
        {
            std::stringstream messageBuilder;
            messageBuilder << "Release buffer: " << desc.debugName << " 0x" << std::hex << GetGpuVirtualAddress();
            mContext.Info(messageBuilder.str());
        }

        if (mClearUAV != cInvalidDescriptorIndex)
        {
            mResources.shaderResourceViewHeap.ReleaseDescriptor(mClearUAV);
            mClearUAV = cInvalidDescriptorIndex;
        }
    }

    ZWBufferHandle ZWD3D12Device::CreateBuffer(const ZWBufferDesc& bufferDesc)
    {
        ZWBufferDesc adjustedDesc = bufferDesc;
        if (adjustedDesc.isConstantBuffer)
        {
            adjustedDesc.byteSize = AlignValue(bufferDesc.byteSize, 256ull);
        }

        ZWD3D12Buffer* buffer = new ZWD3D12Buffer(mContext, mResources, adjustedDesc);

        if (bufferDesc.isVolatile)
        {
            return ZWBufferHandle::Create(buffer);
        }

        D3D12_RESOURCE_DESC& resourceDescription = buffer->resourceDesc;
        resourceDescription.Width = buffer->desc.byteSize;
        resourceDescription.Height = 1;
        resourceDescription.DepthOrArraySize = 1;
        resourceDescription.MipLevels = 1;
        resourceDescription.Format = DXGI_FORMAT_UNKNOWN;
        resourceDescription.SampleDesc.Count = 1;
        resourceDescription.SampleDesc.Quality = 0;
        resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (buffer->desc.canHaveUAVs)
        {
            resourceDescription.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }

        if (bufferDesc.isVirtual)
        {
            return ZWBufferHandle::Create(buffer);
        }

        D3D12_HEAP_PROPERTIES heapProperties = {};
        D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;

        bool isShared = false;
        if (HasSharedResourceFlag(bufferDesc.sharedResourceFlags, ESharedResourceFlags::Shared))
        {
            heapFlags |= D3D12_HEAP_FLAG_SHARED;
            isShared = true;
        }

        if (HasSharedResourceFlag(bufferDesc.sharedResourceFlags, ESharedResourceFlags::Shared_CrossAdapter))
        {
            resourceDescription.Flags |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
            heapFlags |= D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
            isShared = true;
        }

        switch (buffer->desc.cpuAccess)
        {
        case ECpuAccessMode::None:
            heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
            initialState = ConvertResourceStates(bufferDesc.initialState);
            if (initialState != D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)
            {
                initialState = D3D12_RESOURCE_STATE_COMMON;
            }
            break;

        case ECpuAccessMode::Read:
            heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
            initialState = D3D12_RESOURCE_STATE_COPY_DEST;
            break;

        case ECpuAccessMode::Write:
            heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
            initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
            break;
        }

        if (buffer->desc.cpuAccess == ECpuAccessMode::Read && bufferDesc.initialState == EResourceStates::ResolveDest)
        {
            heapProperties.Type = D3D12_HEAP_TYPE_CUSTOM;
            heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
            heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
            initialState = D3D12_RESOURCE_STATE_COMMON;
        }

        const HRESULT result = mContext.device->CreateCommittedResource(
            &heapProperties,
            heapFlags,
            &resourceDescription,
            initialState,
            nullptr,
            IID_PPV_ARGS(buffer->resource.ReleaseAndGetAddressOf()));

        if (FAILED(result))
        {
            std::stringstream messageBuilder;
            messageBuilder << "CreateCommittedResource call failed for buffer " << HApp::DebugNameToString(bufferDesc.debugName)
                << ", HRESULT = 0x" << std::hex << std::setw(8) << result;
            mContext.Error(messageBuilder.str());

            delete buffer;
            return nullptr;
        }

        if (isShared)
        {
            const HRESULT sharedHandleResult = mContext.device->CreateSharedHandle(
                buffer->resource.Get(),
                nullptr,
                GENERIC_ALL,
                nullptr,
                &buffer->sharedHandle);

            if (FAILED(sharedHandleResult))
            {
                std::stringstream messageBuilder;
                messageBuilder << "Failed to create shared handle " << HApp::DebugNameToString(bufferDesc.debugName)
                    << ", error code = 0x" << std::hex << sharedHandleResult;
                mContext.Error(messageBuilder.str());

                delete buffer;
                return nullptr;
            }
        }

        buffer->PostCreate();
        return ZWBufferHandle::Create(buffer);
    }

    void ZWD3D12Buffer::PostCreate()
    {
        gpuVA = resource->GetGPUVirtualAddress();

        if (!desc.debugName.empty())
        {
            const std::wstring debugName(desc.debugName.begin(), desc.debugName.end());
            resource->SetName(debugName.c_str());
        }

        if (mContext.logBufferLifetime)
        {
            size_t displaySize = desc.byteSize;
            const char* displayUnit = "B";

            if (desc.byteSize > (1u << 20))
            {
                displaySize = static_cast<size_t>(desc.byteSize >> 20);
                displayUnit = "MB";
            }
            else if (desc.byteSize > (1u << 10))
            {
                displaySize = static_cast<size_t>(desc.byteSize >> 10);
                displayUnit = "KB";
            }

            std::stringstream messageBuilder;
            messageBuilder << "Create buffer: " << desc.debugName
                << " Res:0x" << std::hex << reinterpret_cast<uintptr_t>(resource.Get())
                << " Gpu:0x" << std::hex << GetGpuVirtualAddress()
                << "->0x" << std::hex << (GetGpuVirtualAddress() + desc.byteSize);

            if (desc.elementStride != 0)
            {
                messageBuilder << " (n:" << std::dec << (desc.byteSize / desc.elementStride)
                    << " stride:" << desc.elementStride
                    << "B size:" << displaySize << displayUnit << ")";
            }
            else
            {
                messageBuilder << " (size:" << std::dec << displaySize << displayUnit << ")";
            }

            mContext.Info(messageBuilder.str());
        }
    }

    DescriptorIndex ZWD3D12Buffer::GetClearUAV()
    {
        assert(desc.canHaveUAVs);

        if (mClearUAV != cInvalidDescriptorIndex)
        {
            return mClearUAV;
        }

        mClearUAV = mResources.shaderResourceViewHeap.AllocateDescriptor();
        CreateUAV(mResources.shaderResourceViewHeap.GetCpuHandle(mClearUAV).ptr, EFormat::R32_UINT, sEntireBuffer, EResourceType::TypedBuffer_UAV);
        mResources.shaderResourceViewHeap.CopyToShaderVisibleHeap(mClearUAV);
        return mClearUAV;
    }

    void* ZWD3D12Device::MapBuffer(IBuffer* buffer, ECpuAccessMode cpuAccess)
    {
        ZWD3D12Buffer* d3d12Buffer = static_cast<ZWD3D12Buffer*>(buffer);

        if (d3d12Buffer->lastUseFence != nullptr)
        {
            WaitForFence(d3d12Buffer->lastUseFence.Get(), d3d12Buffer->lastUseFenceValue, mFenceEvent);
            d3d12Buffer->lastUseFence = nullptr;
        }

        D3D12_RANGE range = {};
        if (cpuAccess == ECpuAccessMode::Read)
        {
            range.Begin = 0;
            range.End = SIZE_T(d3d12Buffer->desc.byteSize);
        }

        void* mappedBuffer = nullptr;
        const HRESULT result = d3d12Buffer->resource->Map(0, &range, &mappedBuffer);
        if (FAILED(result))
        {
            std::stringstream messageBuilder;
            messageBuilder << "Map call failed for buffer " << HApp::DebugNameToString(d3d12Buffer->desc.debugName)
                << ", HRESULT = 0x" << std::hex << std::setw(8) << result;
            mContext.Error(messageBuilder.str());
            return nullptr;
        }

        return mappedBuffer;
    }

    void ZWD3D12Device::UnmapBuffer(IBuffer* buffer)
    {
        ZWD3D12Buffer* d3d12Buffer = static_cast<ZWD3D12Buffer*>(buffer);
        d3d12Buffer->resource->Unmap(0, nullptr);
    }

    ZWMemoryRequirements ZWD3D12Device::GetBufferMemoryRequirements(IBuffer* buffer)
    {
        ZWD3D12Buffer* d3d12Buffer = static_cast<ZWD3D12Buffer*>(buffer);
        const D3D12_RESOURCE_ALLOCATION_INFO allocationInfo = mContext.device->GetResourceAllocationInfo(1, 1, &d3d12Buffer->resourceDesc);

        ZWMemoryRequirements memoryRequirements;
        memoryRequirements.alignment = allocationInfo.Alignment;
        memoryRequirements.size = allocationInfo.SizeInBytes;
        return memoryRequirements;
    }

    bool ZWD3D12Device::BindBufferMemory(IBuffer* buffer, IHeap* heap, uint64_t offset)
    {
        ZWD3D12Buffer* d3d12Buffer = static_cast<ZWD3D12Buffer*>(buffer);
        ZWD3D12Heap* d3d12Heap = static_cast<ZWD3D12Heap*>(heap);

        if (d3d12Buffer->resource != nullptr || !d3d12Buffer->desc.isVirtual)
        {
            return false;
        }

        D3D12_RESOURCE_STATES initialState = ConvertResourceStates(d3d12Buffer->desc.initialState);
        if (initialState != D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)
        {
            initialState = D3D12_RESOURCE_STATE_COMMON;
        }

        const HRESULT result = mContext.device->CreatePlacedResource(
            d3d12Heap->heap.Get(),
            offset,
            &d3d12Buffer->resourceDesc,
            initialState,
            nullptr,
            IID_PPV_ARGS(d3d12Buffer->resource.ReleaseAndGetAddressOf()));

        if (FAILED(result))
        {
            std::stringstream messageBuilder;
            messageBuilder << "Failed to create placed buffer " << HApp::DebugNameToString(d3d12Buffer->desc.debugName)
                << ", error code = 0x" << std::hex << result;
            mContext.Error(messageBuilder.str());
            return false;
        }

        d3d12Buffer->heap = ZWHeapHandle::Create(d3d12Heap);
        d3d12Buffer->PostCreate();
        return true;
    }

    ZWBufferHandle ZWD3D12Device::CreateHandleForNativeBuffer(ObjectType objectType, HCommon::ZWObject nativeBuffer, const ZWBufferDesc& bufferDesc)
    {
        if (nativeBuffer.pointer == nullptr || objectType != HRHI::HRHIObjectTypes::gD3D12Resource)
        {
            return nullptr;
        }

        ID3D12Resource* resourceHandle = static_cast<ID3D12Resource*>(nativeBuffer.pointer);

        ZWD3D12Buffer* buffer = new ZWD3D12Buffer(mContext, mResources, bufferDesc);
        buffer->resource = resourceHandle;
        buffer->PostCreate();
        return ZWBufferHandle::Create(buffer);
    }

    void ZWD3D12Buffer::CreateCBV(size_t descriptor, ZWBufferRange range) const
    {
        assert(desc.isConstantBuffer);

        range = range.resolve(desc);
        assert(range.byteSize <= UINT_MAX);

        D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc = {};
        viewDesc.BufferLocation = resource->GetGPUVirtualAddress() + range.byteOffset;
        viewDesc.SizeInBytes = AlignValue(static_cast<UINT>(range.byteSize), gConstantBufferOffsetSizeAlignment);
        mContext.device->CreateConstantBufferView(&viewDesc, { descriptor });
    }

    void ZWD3D12Buffer::CreateNullSRV(size_t descriptor, EFormat format, const ZWD3D12Context& context)
    {
        const ZWDxgiFormatMapping& formatMapping = GetDxgiFormatMapping(format == EFormat::UNKNOWN ? EFormat::R32_UINT : format);

        D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
        viewDesc.Format = formatMapping.srvFormat;
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        context.device->CreateShaderResourceView(nullptr, &viewDesc, { descriptor });
    }

    void ZWD3D12Buffer::CreateSRV(size_t descriptor, EFormat format, ZWBufferRange range, EResourceType type) const
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        if (format == EFormat::UNKNOWN)
        {
            format = desc.format;
        }

        range = range.resolve(desc);

        switch (type)
        {
        case EResourceType::StructuredBuffer_SRV:
            assert(desc.elementStride != 0 && desc.bufferMode == EBufferMode::Structured);
            viewDesc.Format = DXGI_FORMAT_UNKNOWN;
            viewDesc.Buffer.FirstElement = static_cast<UINT>(range.byteOffset / desc.elementStride);
            viewDesc.Buffer.NumElements = static_cast<UINT>(range.byteSize / desc.elementStride);
            viewDesc.Buffer.StructureByteStride = desc.elementStride;
            break;

        case EResourceType::RawBuffer_SRV:
            assert(desc.bufferMode == EBufferMode::Raw);
            viewDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            viewDesc.Buffer.FirstElement = static_cast<UINT>(range.byteOffset / 4);
            viewDesc.Buffer.NumElements = static_cast<UINT>(range.byteSize / 4);
            viewDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            break;

        case EResourceType::TypedBuffer_SRV:
        {
            assert(format != EFormat::UNKNOWN && desc.bufferMode == EBufferMode::Formatted);
            const ZWDxgiFormatMapping& formatMapping = GetDxgiFormatMapping(format);
            const ZWFormatInfo& formatInfo = GetFormatInfo(format);

            viewDesc.Format = formatMapping.srvFormat;
            viewDesc.Buffer.FirstElement = static_cast<UINT>(range.byteOffset / formatInfo.bytesPerBlock);
            viewDesc.Buffer.NumElements = static_cast<UINT>(range.byteSize / formatInfo.bytesPerBlock);
            break;
        }

        default:
            ReportInvalidResourceType(mContext);
            return;
        }

        mContext.device->CreateShaderResourceView(resource.Get(), &viewDesc, { descriptor });
    }

    void ZWD3D12Buffer::CreateNullUAV(size_t descriptor, EFormat format, const ZWD3D12Context& context)
    {
        const ZWDxgiFormatMapping& formatMapping = GetDxgiFormatMapping(format == EFormat::UNKNOWN ? EFormat::R32_UINT : format);

        D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
        viewDesc.Format = formatMapping.srvFormat;
        viewDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        context.device->CreateUnorderedAccessView(nullptr, nullptr, &viewDesc, { descriptor });
    }

    void ZWD3D12Buffer::CreateUAV(size_t descriptor, EFormat format, ZWBufferRange range, EResourceType type) const
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
        viewDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

        if (format == EFormat::UNKNOWN)
        {
            format = desc.format;
        }

        range = range.resolve(desc);

        switch (type)
        {
        case EResourceType::StructuredBuffer_UAV:
            assert(desc.elementStride != 0 && desc.bufferMode == EBufferMode::Structured);
            viewDesc.Format = DXGI_FORMAT_UNKNOWN;
            viewDesc.Buffer.FirstElement = static_cast<UINT>(range.byteOffset / desc.elementStride);
            viewDesc.Buffer.NumElements = static_cast<UINT>(range.byteSize / desc.elementStride);
            viewDesc.Buffer.StructureByteStride = desc.elementStride;
            break;

        case EResourceType::RawBuffer_UAV:
            assert(desc.bufferMode == EBufferMode::Raw);
            viewDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            viewDesc.Buffer.FirstElement = static_cast<UINT>(range.byteOffset / 4);
            viewDesc.Buffer.NumElements = static_cast<UINT>(range.byteSize / 4);
            viewDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            break;

        case EResourceType::TypedBuffer_UAV:
        {
            assert(format != EFormat::UNKNOWN && desc.bufferMode == EBufferMode::Formatted);
            const ZWDxgiFormatMapping& formatMapping = GetDxgiFormatMapping(format);
            const ZWFormatInfo& formatInfo = GetFormatInfo(format);

            viewDesc.Format = formatMapping.srvFormat;
            viewDesc.Buffer.FirstElement = static_cast<UINT>(range.byteOffset / formatInfo.bytesPerBlock);
            viewDesc.Buffer.NumElements = static_cast<UINT>(range.byteSize / formatInfo.bytesPerBlock);
            break;
        }

        default:
            ReportInvalidResourceType(mContext);
            return;
        }

        mContext.device->CreateUnorderedAccessView(resource.Get(), nullptr, &viewDesc, { descriptor });
    }

    void ZWD3D12CommandList::WriteBuffer(IBuffer* buffer, const void* data, size_t dataSize, uint64_t destOffsetBytes)
    {
        ZWD3D12Buffer* d3d12Buffer = static_cast<ZWD3D12Buffer*>(buffer);

        void* cpuVa = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpuVa = 0;
        ID3D12Resource* uploadBuffer = nullptr;
        size_t offsetInUploadBuffer = 0;

        if (!mUploadManager.SuballocateBuffer(dataSize, nullptr, &uploadBuffer, &offsetInUploadBuffer, &cpuVa, &gpuVa, mRecordingVersion,
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
        {
            m_Context.Error("Couldn't suballocate an upload buffer.");
            return;
        }

        if (uploadBuffer != mCurrentUploadBuffer)
        {
            mInstance->referencedNativeResources.push_back(uploadBuffer);
            mCurrentUploadBuffer = uploadBuffer;
        }

        std::memcpy(cpuVa, data, dataSize);

        if (d3d12Buffer->desc.isVolatile)
        {
            mVolatileConstantBufferAddresses[d3d12Buffer] = gpuVa;
            mAnyVolatileBufferWrites = true;
            return;
        }

        if (mEnableAutomaticBarriers)
        {
            RequireBufferState(d3d12Buffer, EResourceStates::CopyDest);
            mBindingStatesDirty = true;
        }

        CommitBarriers();

        mInstance->referencedResources.push_back(d3d12Buffer);
        mActiveCommandList->commandList->CopyBufferRegion(d3d12Buffer->resource.Get(), destOffsetBytes, uploadBuffer, offsetInUploadBuffer, dataSize);
    }

    void ZWD3D12CommandList::ClearBufferUInt(IBuffer* buffer, uint32_t clearValue)
    {
        ZWD3D12Buffer* d3d12Buffer = static_cast<ZWD3D12Buffer*>(buffer);

        if (!d3d12Buffer->desc.canHaveUAVs)
        {
            std::stringstream messageBuilder;
            messageBuilder << "Cannot clear buffer " << HApp::DebugNameToString(d3d12Buffer->desc.debugName)
                << " because it was created with canHaveUAVs = false";
            m_Context.Error(messageBuilder.str());
            return;
        }

        if (mEnableAutomaticBarriers)
        {
            RequireBufferState(d3d12Buffer, EResourceStates::UnorderedAccess);
            mBindingStatesDirty = true;
        }

        CommitBarriers();
        CommitDescriptorHeaps();

        const DescriptorIndex clearUav = d3d12Buffer->GetClearUAV();
        assert(clearUav != cInvalidDescriptorIndex);

        mInstance->referencedResources.push_back(d3d12Buffer);

        const uint32_t clearValues[4] = { clearValue, clearValue, clearValue, clearValue };
        mActiveCommandList->commandList->ClearUnorderedAccessViewUint(
            m_Resources.shaderResourceViewHeap.GetGpuHandle(clearUav),
            m_Resources.shaderResourceViewHeap.GetCpuHandle(clearUav),
            d3d12Buffer->resource.Get(),
            clearValues,
            0,
            nullptr);
    }

    void ZWD3D12CommandList::CopyBuffer(IBuffer* dest, uint64_t destOffsetBytes, IBuffer* src, uint64_t srcOffsetBytes, uint64_t dataSizeBytes)
    {
        ZWD3D12Buffer* destBuffer = static_cast<ZWD3D12Buffer*>(dest);
        ZWD3D12Buffer* srcBuffer = static_cast<ZWD3D12Buffer*>(src);

        if (mEnableAutomaticBarriers)
        {
            RequireBufferState(destBuffer, EResourceStates::CopyDest);
            RequireBufferState(srcBuffer, EResourceStates::CopySource);
            mBindingStatesDirty = true;
        }

        CommitBarriers();

        if (srcBuffer->desc.cpuAccess != ECpuAccessMode::None)
        {
            mInstance->referencedStagingBuffers.push_back(srcBuffer);
        }
        else
        {
            mInstance->referencedResources.push_back(srcBuffer);
        }

        if (destBuffer->desc.cpuAccess != ECpuAccessMode::None)
        {
            mInstance->referencedStagingBuffers.push_back(destBuffer);
        }
        else
        {
            mInstance->referencedResources.push_back(destBuffer);
        }

        mActiveCommandList->commandList->CopyBufferRegion(destBuffer->resource.Get(), destOffsetBytes, srcBuffer->resource.Get(), srcOffsetBytes, dataSizeBytes);
    }
}
