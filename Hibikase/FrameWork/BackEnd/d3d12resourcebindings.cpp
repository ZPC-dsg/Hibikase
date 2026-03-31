#include <BackEnd/d3d12backend.h>
#include <Utils/stringtranslatehelper.h>

#include <cassert>
#include <iomanip>
#include <sstream>

namespace HRHI::HD3D12
{
    namespace
    {
        constexpr ObjectType cD3D12RootSignatureObjectType = 0x00020009u;

        EResourceType GetNormalizedResourceType(EResourceType type)
        {
            switch (type)
            {
            case EResourceType::StructuredBuffer_UAV:
            case EResourceType::RawBuffer_UAV:
                return EResourceType::TypedBuffer_UAV;

            case EResourceType::StructuredBuffer_SRV:
            case EResourceType::RawBuffer_SRV:
                return EResourceType::TypedBuffer_SRV;

            default:
                return type;
            }
        }

        bool AreResourceTypesCompatible(EResourceType left, EResourceType right)
        {
            if (left == right)
            {
                return true;
            }

            left = GetNormalizedResourceType(left);
            right = GetNormalizedResourceType(right);

            if ((left == EResourceType::TypedBuffer_SRV && right == EResourceType::Texture_SRV)
                || (right == EResourceType::TypedBuffer_SRV && left == EResourceType::Texture_SRV)
                || (left == EResourceType::TypedBuffer_SRV && right == EResourceType::RayTracingAccelStruct)
                || (left == EResourceType::Texture_SRV && right == EResourceType::RayTracingAccelStruct)
                || (right == EResourceType::TypedBuffer_SRV && left == EResourceType::RayTracingAccelStruct)
                || (right == EResourceType::Texture_SRV && left == EResourceType::RayTracingAccelStruct))
            {
                return true;
            }

            if ((left == EResourceType::TypedBuffer_UAV && right == EResourceType::Texture_UAV)
                || (right == EResourceType::TypedBuffer_UAV && left == EResourceType::Texture_UAV))
            {
                return true;
            }

            return false;
        }
    }

    void ZWD3D12BindingSet::CreateDescriptors()
    {
        for (const std::pair<RootParameterIndex, D3D12_ROOT_DESCRIPTOR1>& parameter : layout->rootParametersVolatileCB)
        {
            IBuffer* foundBuffer = nullptr;

            const RootParameterIndex rootParameterIndex = parameter.first;
            const D3D12_ROOT_DESCRIPTOR1& rootDescriptor = parameter.second;

            for (const auto& binding : desc.bindings)
            {
                if (binding.type == EResourceType::VolatileConstantBuffer && binding.slot == rootDescriptor.ShaderRegister)
                {
                    ZWD3D12Buffer* buffer = static_cast<ZWD3D12Buffer*>(binding.resourceHandle);
                    resources.push_back(buffer);
                    foundBuffer = buffer;
                    break;
                }
            }

            rootParametersVolatileCB.push_back(std::make_pair(rootParameterIndex, foundBuffer));
        }

        if (layout->descriptorTableSizeSamplers > 0)
        {
            const DescriptorIndex descriptorTableBaseIndex = mResources.samplerHeap.AllocateDescriptors(layout->descriptorTableSizeSamplers);
            descriptorTableSamplers = descriptorTableBaseIndex;
            rootParameterIndexSamplers = layout->rootParameterSamplers;
            descriptorTableValidSamplers = true;

            for (const auto& range : layout->descriptorRangesSamplers)
            {
                for (uint32_t itemInRange = 0; itemInRange < range.NumDescriptors; ++itemInRange)
                {
                    const uint32_t slot = range.BaseShaderRegister + itemInRange;
                    bool found = false;
                    const D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = mResources.samplerHeap.GetCpuHandle(
                        descriptorTableBaseIndex + range.OffsetInDescriptorsFromTableStart + itemInRange);

                    for (const auto& binding : desc.bindings)
                    {
                        if (binding.type == EResourceType::Sampler && binding.slot + binding.arrayElement == slot)
                        {
                            ZWD3D12Sampler* sampler = static_cast<ZWD3D12Sampler*>(binding.resourceHandle);
                            resources.push_back(sampler);

                            sampler->CreateDescriptor(descriptorHandle.ptr);
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        D3D12_SAMPLER_DESC samplerDesc = {};
                        mContext.device->CreateSampler(&samplerDesc, descriptorHandle);
                    }
                }
            }

            mResources.samplerHeap.CopyToShaderVisibleHeap(descriptorTableBaseIndex, layout->descriptorTableSizeSamplers);
        }

        if (layout->descriptorTableSizeSRVetc > 0)
        {
            const DescriptorIndex descriptorTableBaseIndex = mResources.shaderResourceViewHeap.AllocateDescriptors(layout->descriptorTableSizeSRVetc);
            descriptorTableSRVetc = descriptorTableBaseIndex;
            rootParameterIndexSRVetc = layout->rootParameterSRVetc;
            descriptorTableValidSRVetc = true;

            for (const auto& range : layout->descriptorRangesSRVetc)
            {
                for (uint32_t itemInRange = 0; itemInRange < range.NumDescriptors; ++itemInRange)
                {
                    const uint32_t slot = range.BaseShaderRegister + itemInRange;
                    bool found = false;
                    const D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = mResources.shaderResourceViewHeap.GetCpuHandle(
                        descriptorTableBaseIndex + range.OffsetInDescriptorsFromTableStart + itemInRange);

                    HCommon::IResource* resource = nullptr;

                    for (size_t bindingIndex = 0; bindingIndex < desc.bindings.size(); ++bindingIndex)
                    {
                        const ZWBindingSetItem& binding = desc.bindings[bindingIndex];
                        if (binding.slot + binding.arrayElement != slot)
                        {
                            continue;
                        }

                        const EResourceType bindingType = GetNormalizedResourceType(binding.type);

                        if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && bindingType == EResourceType::TypedBuffer_SRV)
                        {
                            if (binding.resourceHandle != nullptr)
                            {
                                ZWD3D12Buffer* buffer = static_cast<ZWD3D12Buffer*>(binding.resourceHandle);
                                resource = buffer;

                                buffer->CreateSRV(descriptorHandle.ptr, binding.format, binding.range, binding.type);

                                if (buffer->permanentState == EResourceStates::Unknown)
                                {
                                    bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                                }
                                else
                                {
                                    VerifyPermanentResourceState(
                                        buffer->permanentState,
                                        EResourceStates::ShaderResource,
                                        false,
                                        buffer->desc.debugName,
                                        mContext.messageCallback);
                                }
                            }
                            else
                            {
                                ZWD3D12Buffer::CreateNullSRV(descriptorHandle.ptr, binding.format, mContext);
                            }

                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && bindingType == EResourceType::TypedBuffer_UAV)
                        {
                            if (binding.resourceHandle != nullptr)
                            {
                                ZWD3D12Buffer* buffer = static_cast<ZWD3D12Buffer*>(binding.resourceHandle);
                                resource = buffer;

                                buffer->CreateUAV(descriptorHandle.ptr, binding.format, binding.range, binding.type);

                                if (buffer->permanentState == EResourceStates::Unknown)
                                {
                                    bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                                }
                                else
                                {
                                    VerifyPermanentResourceState(
                                        buffer->permanentState,
                                        EResourceStates::UnorderedAccess,
                                        false,
                                        buffer->desc.debugName,
                                        mContext.messageCallback);
                                }
                            }
                            else
                            {
                                ZWD3D12Buffer::CreateNullUAV(descriptorHandle.ptr, binding.format, mContext);
                            }

                            hasUavBindings = true;
                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && bindingType == EResourceType::Texture_SRV)
                        {
                            ZWD3D12Texture* texture = static_cast<ZWD3D12Texture*>(binding.resourceHandle);
                            const ZWTextureSubresourceSet subresources = binding.subresources;

                            texture->CreateSRV(descriptorHandle.ptr, binding.format, binding.dimension, subresources);
                            resource = texture;

                            if (texture->permanentState == EResourceStates::Unknown)
                            {
                                bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                            }
                            else
                            {
                                VerifyPermanentResourceState(
                                    texture->permanentState,
                                    EResourceStates::ShaderResource,
                                    true,
                                    texture->desc.debugName,
                                    mContext.messageCallback);
                            }

                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && bindingType == EResourceType::Texture_UAV)
                        {
                            ZWD3D12Texture* texture = static_cast<ZWD3D12Texture*>(binding.resourceHandle);
                            const ZWTextureSubresourceSet subresources = binding.subresources;

                            texture->CreateUAV(descriptorHandle.ptr, binding.format, binding.dimension, subresources);
                            resource = texture;

                            if (texture->permanentState == EResourceStates::Unknown)
                            {
                                bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                            }
                            else
                            {
                                VerifyPermanentResourceState(
                                    texture->permanentState,
                                    EResourceStates::UnorderedAccess,
                                    true,
                                    texture->desc.debugName,
                                    mContext.messageCallback);
                            }

                            hasUavBindings = true;
                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && bindingType == EResourceType::RayTracingAccelStruct)
                        {
                            ZWD3D12AccelStruct* accelStruct = static_cast<ZWD3D12AccelStruct*>(binding.resourceHandle);
                            accelStruct->CreateSRV(descriptorHandle.ptr);
                            resource = accelStruct;

                            bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));

                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV && bindingType == EResourceType::ConstantBuffer)
                        {
                            ZWD3D12Buffer* buffer = static_cast<ZWD3D12Buffer*>(binding.resourceHandle);
                            buffer->CreateCBV(descriptorHandle.ptr, binding.range);
                            resource = buffer;

                            if (buffer->desc.isVolatile)
                            {
                                std::stringstream messageBuilder;
                                messageBuilder << "Attempted to bind a volatile constant buffer "
                                    << HApp::DebugNameToString(buffer->desc.debugName)
                                    << " to a non-volatile CB layout at slot b" << binding.slot;
                                mContext.Error(messageBuilder.str());
                                found = false;
                                break;
                            }

                            if (buffer->permanentState == EResourceStates::Unknown)
                            {
                                bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                            }
                            else
                            {
                                VerifyPermanentResourceState(
                                    buffer->permanentState,
                                    EResourceStates::ConstantBuffer,
                                    false,
                                    buffer->desc.debugName,
                                    mContext.messageCallback);
                            }

                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && bindingType == EResourceType::SamplerFeedbackTexture_UAV)
                        {
                            ZWD3D12SamplerFeedbackTexture* texture = static_cast<ZWD3D12SamplerFeedbackTexture*>(binding.resourceHandle);
                            if (texture == nullptr)
                            {
                                break;
                            }

                            texture->CreateUAV(descriptorHandle.ptr);
                            resource = texture;

                            hasUavBindings = true;
                            found = true;
                            break;
                        }
                    }

                    if (resource != nullptr)
                    {
                        resources.push_back(resource);
                    }

                    if (!found)
                    {
                        switch (range.RangeType)
                        {
                        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                            ZWD3D12Buffer::CreateNullSRV(descriptorHandle.ptr, EFormat::UNKNOWN, mContext);
                            break;

                        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                            ZWD3D12Buffer::CreateNullUAV(descriptorHandle.ptr, EFormat::UNKNOWN, mContext);
                            break;

                        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                            mContext.device->CreateConstantBufferView(nullptr, descriptorHandle);
                            break;

                        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                        default:
                            assert(false);
                            break;
                        }
                    }
                }
            }

            mResources.shaderResourceViewHeap.CopyToShaderVisibleHeap(descriptorTableBaseIndex, layout->descriptorTableSizeSRVetc);
        }
    }

    ZWBindingLayoutHandle ZWD3D12Device::CreateBindingLayout(const ZWBindingLayoutDesc& desc)
    {
        return ZWBindingLayoutHandle::Create(new ZWD3D12BindingLayout(desc));
    }

    ZWBindingLayoutHandle ZWD3D12Device::CreateBindlessLayout(const ZWBindlessLayoutDesc& desc)
    {
        return ZWBindingLayoutHandle::Create(new ZWD3D12BindlessLayout(desc));
    }

    ZWBindingSetHandle ZWD3D12Device::CreateBindingSet(const ZWBindingSetDesc& desc, IBindingLayout* layout)
    {
        ZWD3D12BindingSet* bindingSet = new ZWD3D12BindingSet(mContext, mResources);
        bindingSet->desc = desc;
        bindingSet->layout = static_cast<ZWD3D12BindingLayout*>(layout);
        bindingSet->CreateDescriptors();
        return ZWBindingSetHandle::Create(bindingSet);
    }

    ZWDescriptorTableHandle ZWD3D12Device::CreateDescriptorTable(IBindingLayout* layout)
    {
        (void)layout;

        ZWD3D12DescriptorTable* descriptorTable = new ZWD3D12DescriptorTable(mResources);
        descriptorTable->capacity = 0;
        descriptorTable->firstDescriptor = 0;
        return ZWDescriptorTableHandle::Create(descriptorTable);
    }

    ZWD3D12BindingSet::~ZWD3D12BindingSet()
    {
        mResources.shaderResourceViewHeap.ReleaseDescriptors(descriptorTableSRVetc, layout->descriptorTableSizeSRVetc);
        mResources.samplerHeap.ReleaseDescriptors(descriptorTableSamplers, layout->descriptorTableSizeSamplers);
    }

    ZWD3D12DescriptorTable::~ZWD3D12DescriptorTable()
    {
        mResources.shaderResourceViewHeap.ReleaseDescriptors(firstDescriptor, capacity);
    }

    ZWD3D12BindingLayout::ZWD3D12BindingLayout(const ZWBindingLayoutDesc& bindingLayoutDesc)
        : desc(bindingLayoutDesc)
    {
        EResourceType currentType = static_cast<EResourceType>(-1);
        uint32_t currentSlot = ~0u;

        D3D12_ROOT_CONSTANTS rootConstants = {};

        for (const ZWBindingLayoutItem& binding : desc.bindings)
        {
            if (binding.type == EResourceType::VolatileConstantBuffer)
            {
                D3D12_ROOT_DESCRIPTOR1 rootDescriptor = {};
                rootDescriptor.ShaderRegister = binding.slot;
                rootDescriptor.RegisterSpace = desc.registerSpace;
                rootDescriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;

                rootParametersVolatileCB.push_back(std::make_pair(cInvalidRootParameterIndex, rootDescriptor));
            }
            else if (binding.type == EResourceType::PushConstants)
            {
                pushConstantByteSize = binding.size;
                rootConstants.ShaderRegister = binding.slot;
                rootConstants.RegisterSpace = desc.registerSpace;
                rootConstants.Num32BitValues = binding.size / 4;
            }
            else if (!AreResourceTypesCompatible(binding.type, currentType) || binding.slot != currentSlot + 1)
            {
                if (binding.type == EResourceType::Sampler)
                {
                    descriptorRangesSamplers.resize(descriptorRangesSamplers.size() + 1);
                    D3D12_DESCRIPTOR_RANGE1& range = descriptorRangesSamplers[descriptorRangesSamplers.size() - 1];

                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                    range.NumDescriptors = binding.size;
                    range.BaseShaderRegister = binding.slot;
                    range.RegisterSpace = desc.registerSpace;
                    range.OffsetInDescriptorsFromTableStart = descriptorTableSizeSamplers;
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

                    descriptorTableSizeSamplers += binding.size;
                }
                else
                {
                    descriptorRangesSRVetc.resize(descriptorRangesSRVetc.size() + 1);
                    D3D12_DESCRIPTOR_RANGE1& range = descriptorRangesSRVetc[descriptorRangesSRVetc.size() - 1];

                    switch (binding.type)
                    {
                    case EResourceType::Texture_SRV:
                    case EResourceType::TypedBuffer_SRV:
                    case EResourceType::StructuredBuffer_SRV:
                    case EResourceType::RawBuffer_SRV:
                    case EResourceType::RayTracingAccelStruct:
                        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                        break;

                    case EResourceType::Texture_UAV:
                    case EResourceType::TypedBuffer_UAV:
                    case EResourceType::StructuredBuffer_UAV:
                    case EResourceType::RawBuffer_UAV:
                    case EResourceType::SamplerFeedbackTexture_UAV:
                        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                        break;

                    case EResourceType::ConstantBuffer:
                        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                        break;

                    default:
                        assert(false);
                        continue;
                    }

                    range.NumDescriptors = binding.size;
                    range.BaseShaderRegister = binding.slot;
                    range.RegisterSpace = desc.registerSpace;
                    range.OffsetInDescriptorsFromTableStart = descriptorTableSizeSRVetc;
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

                    descriptorTableSizeSRVetc += binding.size;
                    bindingLayoutsSRVetc.push_back(binding);
                }

                currentType = binding.type;
                currentSlot = binding.slot;
            }
            else
            {
                if (binding.type == EResourceType::Sampler)
                {
                    assert(!descriptorRangesSamplers.empty());
                    D3D12_DESCRIPTOR_RANGE1& range = descriptorRangesSamplers[descriptorRangesSamplers.size() - 1];
                    range.NumDescriptors += binding.size;
                    descriptorTableSizeSamplers += binding.size;
                }
                else
                {
                    assert(!descriptorRangesSRVetc.empty());
                    D3D12_DESCRIPTOR_RANGE1& range = descriptorRangesSRVetc[descriptorRangesSRVetc.size() - 1];
                    range.NumDescriptors += binding.size;
                    descriptorTableSizeSRVetc += binding.size;
                    bindingLayoutsSRVetc.push_back(binding);
                }

                currentSlot = binding.slot;
            }
        }

        rootParameters.resize(0);

        if (rootConstants.Num32BitValues != 0)
        {
            D3D12_ROOT_PARAMETER1& parameter = rootParameters.emplace_back();
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameter.ShaderVisibility = ConvertShaderStage(desc.visibility);
            parameter.Constants = rootConstants;
            rootParameterPushConstants = RootParameterIndex(rootParameters.size() - 1);
        }

        for (std::pair<RootParameterIndex, D3D12_ROOT_DESCRIPTOR1>& rootParameterVolatileCb : rootParametersVolatileCB)
        {
            rootParameters.resize(rootParameters.size() + 1);
            D3D12_ROOT_PARAMETER1& parameter = rootParameters[rootParameters.size() - 1];

            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameter.ShaderVisibility = ConvertShaderStage(desc.visibility);
            parameter.Descriptor = rootParameterVolatileCb.second;

            rootParameterVolatileCb.first = RootParameterIndex(rootParameters.size() - 1);
        }

        if (descriptorTableSizeSamplers > 0)
        {
            rootParameters.resize(rootParameters.size() + 1);
            D3D12_ROOT_PARAMETER1& parameter = rootParameters[rootParameters.size() - 1];

            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.ShaderVisibility = ConvertShaderStage(desc.visibility);
            parameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(descriptorRangesSamplers.size());
            parameter.DescriptorTable.pDescriptorRanges = descriptorRangesSamplers.data();

            rootParameterSamplers = RootParameterIndex(rootParameters.size() - 1);
        }

        if (descriptorTableSizeSRVetc > 0)
        {
            rootParameters.resize(rootParameters.size() + 1);
            D3D12_ROOT_PARAMETER1& parameter = rootParameters[rootParameters.size() - 1];

            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.ShaderVisibility = ConvertShaderStage(desc.visibility);
            parameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(descriptorRangesSRVetc.size());
            parameter.DescriptorTable.pDescriptorRanges = descriptorRangesSRVetc.data();

            rootParameterSRVetc = RootParameterIndex(rootParameters.size() - 1);
        }
    }

    ZWD3D12BindlessLayout::ZWD3D12BindlessLayout(const ZWBindlessLayoutDesc& bindlessLayoutDesc)
        : desc(bindlessLayoutDesc)
    {
        descriptorRanges.resize(0);

        for (const ZWBindingLayoutItem& item : desc.registerSpaces)
        {
            D3D12_DESCRIPTOR_RANGE_TYPE rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

            switch (item.type)
            {
            case EResourceType::Texture_SRV:
            case EResourceType::TypedBuffer_SRV:
            case EResourceType::StructuredBuffer_SRV:
            case EResourceType::RawBuffer_SRV:
            case EResourceType::RayTracingAccelStruct:
                rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                break;

            case EResourceType::ConstantBuffer:
                rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                break;

            case EResourceType::Texture_UAV:
            case EResourceType::TypedBuffer_UAV:
            case EResourceType::StructuredBuffer_UAV:
            case EResourceType::RawBuffer_UAV:
            case EResourceType::SamplerFeedbackTexture_UAV:
                rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                break;

            case EResourceType::Sampler:
                rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                break;

            default:
                assert(false);
                continue;
            }

            D3D12_DESCRIPTOR_RANGE1& descriptorRange = descriptorRanges.emplace_back();
            descriptorRange.RangeType = rangeType;
            descriptorRange.NumDescriptors = ~0u;
            descriptorRange.BaseShaderRegister = desc.firstSlot;
            descriptorRange.RegisterSpace = item.slot;
            descriptorRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
            descriptorRange.OffsetInDescriptorsFromTableStart = 0;
        }

        if (desc.layoutType == ZWBindlessLayoutDesc::ELayoutType::Immutable)
        {
            rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParameter.ShaderVisibility = ConvertShaderStage(desc.visibility);
            rootParameter.DescriptorTable.NumDescriptorRanges = static_cast<uint32_t>(descriptorRanges.size());
            rootParameter.DescriptorTable.pDescriptorRanges = descriptorRanges.data();
        }
    }

    RootSignatureHandle ZWD3D12Device::BuildRootSignature(
        const HCommon::StaticVector<ZWBindingLayoutHandle, gMaxBindingLayouts>& pipelineLayouts,
        bool allowInputLayout,
        bool isLocal,
        const D3D12_ROOT_PARAMETER1* customParameters,
        uint32_t numCustomParameters)
    {
        ZWD3D12RootSignature* rootSignature = new ZWD3D12RootSignature(mResources);

        std::vector<D3D12_ROOT_PARAMETER1> rootParameters;
        for (uint32_t index = 0; index < numCustomParameters; ++index)
        {
            rootParameters.push_back(customParameters[index]);
        }

        bool usesResourceDescriptorHeap = false;
        bool usesSamplerDescriptorHeap = false;

        for (uint32_t layoutIndex = 0; layoutIndex < static_cast<uint32_t>(pipelineLayouts.size()); ++layoutIndex)
        {
            if (pipelineLayouts[layoutIndex]->GetDesc() != nullptr)
            {
                ZWD3D12BindingLayout* layout = static_cast<ZWD3D12BindingLayout*>(pipelineLayouts[layoutIndex].Get());
                const RootParameterIndex rootParameterOffset = RootParameterIndex(rootParameters.size());

                rootSignature->pipelineLayouts.push_back(std::make_pair(layout, rootParameterOffset));
                rootParameters.insert(rootParameters.end(), layout->rootParameters.begin(), layout->rootParameters.end());

                if (layout->pushConstantByteSize != 0)
                {
                    rootSignature->pushConstantByteSize = layout->pushConstantByteSize;
                    rootSignature->rootParameterPushConstants = layout->rootParameterPushConstants + rootParameterOffset;
                }
            }
            else if (pipelineLayouts[layoutIndex]->GetBindlessDesc() != nullptr)
            {
                ZWD3D12BindlessLayout* layout = static_cast<ZWD3D12BindlessLayout*>(pipelineLayouts[layoutIndex].Get());
                const auto layoutType = layout->GetBindlessDesc()->layoutType;

                if (layoutType != ZWBindlessLayoutDesc::ELayoutType::Immutable)
                {
                    rootSignature->pipelineLayouts.push_back(std::make_pair(layout, cInvalidRootParameterIndex));

                    if (layoutType == ZWBindlessLayoutDesc::ELayoutType::MutableSampler)
                    {
                        usesSamplerDescriptorHeap = true;
                    }
                    else
                    {
                        usesResourceDescriptorHeap = true;
                    }
                }
                else
                {
                    const RootParameterIndex rootParameterOffset = RootParameterIndex(rootParameters.size());
                    rootSignature->pipelineLayouts.push_back(std::make_pair(layout, rootParameterOffset));
                    rootParameters.push_back(layout->rootParameter);
                }
            }
        }

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (allowInputLayout)
        {
            rootSignatureDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        }

        if (isLocal)
        {
            rootSignatureDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        }

        if (mHeapDirectlyIndexedEnabled)
        {
            if (usesSamplerDescriptorHeap)
            {
                rootSignatureDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
            }

            if (usesResourceDescriptorHeap)
            {
                rootSignatureDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
            }
        }

        if (!rootParameters.empty())
        {
            rootSignatureDesc.Desc_1_1.pParameters = rootParameters.data();
            rootSignatureDesc.Desc_1_1.NumParameters = static_cast<UINT>(rootParameters.size());
        }

        HCommon::RefCountPtr<ID3DBlob> rootSignatureBlob;
        HCommon::RefCountPtr<ID3DBlob> errorBlob;
        const HRESULT serializeResult = D3D12SerializeVersionedRootSignature(
            &rootSignatureDesc,
            rootSignatureBlob.ReleaseAndGetAddressOf(),
            errorBlob.ReleaseAndGetAddressOf());

        if (FAILED(serializeResult))
        {
            std::stringstream messageBuilder;
            messageBuilder << "D3D12SerializeVersionedRootSignature call failed, HRESULT = 0x"
                << std::hex << std::setw(8) << serializeResult;

            if (errorBlob != nullptr)
            {
                messageBuilder << std::endl << static_cast<const char*>(errorBlob->GetBufferPointer());
            }

            mContext.Error(messageBuilder.str());
            delete rootSignature;
            return nullptr;
        }

        const HRESULT createResult = mContext.device->CreateRootSignature(
            0,
            rootSignatureBlob->GetBufferPointer(),
            rootSignatureBlob->GetBufferSize(),
            IID_PPV_ARGS(rootSignature->handle.ReleaseAndGetAddressOf()));

        if (FAILED(createResult))
        {
            std::stringstream messageBuilder;
            messageBuilder << "CreateRootSignature call failed, HRESULT = 0x"
                << std::hex << std::setw(8) << createResult;
            mContext.Error(messageBuilder.str());
            delete rootSignature;
            return nullptr;
        }

        return RootSignatureHandle::Create(rootSignature);
    }

    HCommon::RefCountPtr<ZWD3D12RootSignature> ZWD3D12Device::GetRootSignature(
        const HCommon::StaticVector<ZWBindingLayoutHandle, gMaxBindingLayouts>& pipelineLayouts,
        bool allowInputLayout)
    {
        size_t hash = 0;
        for (const ZWBindingLayoutHandle& pipelineLayout : pipelineLayouts)
        {
            HRHI::hash_combine(hash, pipelineLayout.Get());
        }

        HRHI::hash_combine(hash, allowInputLayout ? 1u : 0u);

        HCommon::RefCountPtr<ZWD3D12RootSignature> rootSignature = mResources.rootsigCache[hash];
        if (!rootSignature)
        {
            rootSignature = static_cast<ZWD3D12RootSignature*>(BuildRootSignature(pipelineLayouts, allowInputLayout, false).Get());
            if (!rootSignature)
            {
                return nullptr;
            }

            rootSignature->hash = hash;
            mResources.rootsigCache[hash] = rootSignature.Get();
        }

        return rootSignature;
    }

    ZWD3D12RootSignature::~ZWD3D12RootSignature()
    {
        const auto rootSignatureIt = mResources.rootsigCache.find(hash);
        if (rootSignatureIt != mResources.rootsigCache.end())
        {
            mResources.rootsigCache.erase(rootSignatureIt);
        }
    }

    HCommon::ZWObject ZWD3D12RootSignature::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case cD3D12RootSignatureObjectType:
            return HCommon::ZWObject(handle.Get());

        default:
            return nullptr;
        }
    }

    bool ZWD3D12Device::WriteDescriptorTable(IDescriptorTable* descriptorTable, const ZWBindingSetItem& item)
    {
        ZWD3D12DescriptorTable* d3d12DescriptorTable = static_cast<ZWD3D12DescriptorTable*>(descriptorTable);
        if (item.slot >= d3d12DescriptorTable->capacity)
        {
            return false;
        }

        const D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = mResources.shaderResourceViewHeap.GetCpuHandle(
            d3d12DescriptorTable->firstDescriptor + item.slot);

        switch (item.type)
        {
        case EResourceType::None:
            ZWD3D12Buffer::CreateNullSRV(descriptorHandle.ptr, EFormat::UNKNOWN, mContext);
            break;

        case EResourceType::Texture_SRV:
            static_cast<ZWD3D12Texture*>(item.resourceHandle)->CreateSRV(
                descriptorHandle.ptr,
                item.format,
                item.dimension,
                item.subresources);
            break;

        case EResourceType::Texture_UAV:
            static_cast<ZWD3D12Texture*>(item.resourceHandle)->CreateUAV(
                descriptorHandle.ptr,
                item.format,
                item.dimension,
                item.subresources);
            break;

        case EResourceType::SamplerFeedbackTexture_UAV:
            static_cast<ZWD3D12SamplerFeedbackTexture*>(item.resourceHandle)->CreateUAV(descriptorHandle.ptr);
            break;

        case EResourceType::TypedBuffer_SRV:
        case EResourceType::StructuredBuffer_SRV:
        case EResourceType::RawBuffer_SRV:
            static_cast<ZWD3D12Buffer*>(item.resourceHandle)->CreateSRV(
                descriptorHandle.ptr,
                item.format,
                item.range,
                item.type);
            break;

        case EResourceType::TypedBuffer_UAV:
        case EResourceType::StructuredBuffer_UAV:
        case EResourceType::RawBuffer_UAV:
            static_cast<ZWD3D12Buffer*>(item.resourceHandle)->CreateUAV(
                descriptorHandle.ptr,
                item.format,
                item.range,
                item.type);
            break;

        case EResourceType::ConstantBuffer:
            static_cast<ZWD3D12Buffer*>(item.resourceHandle)->CreateCBV(descriptorHandle.ptr, item.range);
            break;

        case EResourceType::RayTracingAccelStruct:
            static_cast<ZWD3D12AccelStruct*>(item.resourceHandle)->CreateSRV(descriptorHandle.ptr);
            break;

        case EResourceType::VolatileConstantBuffer:
            mContext.Error("Attempted to bind a volatile constant buffer to a bindless set.");
            return false;

        case EResourceType::Sampler:
        case EResourceType::PushConstants:
        case EResourceType::Count:
        default:
            assert(false);
            return false;
        }

        mResources.shaderResourceViewHeap.CopyToShaderVisibleHeap(d3d12DescriptorTable->firstDescriptor + item.slot, 1);
        return true;
    }

    void ZWD3D12Device::ResizeDescriptorTable(IDescriptorTable* descriptorTable, uint32_t newSize, bool keepContents)
    {
        ZWD3D12DescriptorTable* d3d12DescriptorTable = static_cast<ZWD3D12DescriptorTable*>(descriptorTable);
        if (newSize == d3d12DescriptorTable->capacity)
        {
            return;
        }

        if (newSize < d3d12DescriptorTable->capacity)
        {
            mResources.shaderResourceViewHeap.ReleaseDescriptors(
                d3d12DescriptorTable->firstDescriptor + newSize,
                d3d12DescriptorTable->capacity - newSize);
            d3d12DescriptorTable->capacity = newSize;
            return;
        }

        const uint32_t originalFirstDescriptor = d3d12DescriptorTable->firstDescriptor;
        if (!keepContents && d3d12DescriptorTable->capacity > 0)
        {
            mResources.shaderResourceViewHeap.ReleaseDescriptors(
                d3d12DescriptorTable->firstDescriptor,
                d3d12DescriptorTable->capacity);
        }

        d3d12DescriptorTable->firstDescriptor = mResources.shaderResourceViewHeap.AllocateDescriptors(newSize);

        if (keepContents && d3d12DescriptorTable->capacity > 0)
        {
            mContext.device->CopyDescriptorsSimple(
                d3d12DescriptorTable->capacity,
                mResources.shaderResourceViewHeap.GetCpuHandle(d3d12DescriptorTable->firstDescriptor),
                mResources.shaderResourceViewHeap.GetCpuHandle(originalFirstDescriptor),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            mContext.device->CopyDescriptorsSimple(
                d3d12DescriptorTable->capacity,
                mResources.shaderResourceViewHeap.GetCpuHandleShaderVisible(d3d12DescriptorTable->firstDescriptor),
                mResources.shaderResourceViewHeap.GetCpuHandle(originalFirstDescriptor),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            mResources.shaderResourceViewHeap.ReleaseDescriptors(originalFirstDescriptor, d3d12DescriptorTable->capacity);
        }

        d3d12DescriptorTable->capacity = newSize;
    }

    void ZWD3D12CommandList::SetComputeBindings(
        const BindingSetVector& bindings,
        uint32_t bindingUpdateMask,
        IBuffer* indirectParams,
        bool updateIndirectParams,
        const ZWD3D12RootSignature* rootSignature)
    {
        if (rootSignature == nullptr)
        {
            return;
        }

        if (bindingUpdateMask != 0)
        {
            HCommon::StaticVector<VolatileConstantBufferBinding, gMaxVolatileConstantBuffers> newVolatileConstantBuffers;

            for (uint32_t bindingSetIndex = 0; bindingSetIndex < static_cast<uint32_t>(bindings.size()); ++bindingSetIndex)
            {
                IBindingSet* bindingSetInterface = bindings[bindingSetIndex];
                if (bindingSetInterface == nullptr || bindingSetIndex >= rootSignature->pipelineLayouts.size())
                {
                    continue;
                }

                const bool updateThisSet = (bindingUpdateMask & (1u << bindingSetIndex)) != 0;
                const auto& layoutAndOffset = rootSignature->pipelineLayouts[bindingSetIndex];
                const RootParameterIndex rootParameterOffset = layoutAndOffset.second;

                if (bindingSetInterface->GetDesc() != nullptr)
                {
                    ZWD3D12BindingSet* bindingSet = static_cast<ZWD3D12BindingSet*>(bindingSetInterface);

                    for (size_t volatileCbIndex = 0; volatileCbIndex < bindingSet->rootParametersVolatileCB.size(); ++volatileCbIndex)
                    {
                        const auto& parameter = bindingSet->rootParametersVolatileCB[volatileCbIndex];
                        const RootParameterIndex rootParameterIndex = rootParameterOffset + parameter.first;

                        if (parameter.second != nullptr)
                        {
                            ZWD3D12Buffer* buffer = static_cast<ZWD3D12Buffer*>(parameter.second);

                            if (buffer->desc.isVolatile)
                            {
                                const D3D12_GPU_VIRTUAL_ADDRESS volatileGpuAddress = mVolatileConstantBufferAddresses[buffer];
                                if (volatileGpuAddress == 0)
                                {
                                    std::stringstream messageBuilder;
                                    messageBuilder << "Attempted use of a volatile constant buffer "
                                        << HApp::DebugNameToString(buffer->desc.debugName)
                                        << " before it was written into.";
                                    m_Context.Error(messageBuilder.str());
                                    continue;
                                }

                                const bool bindingNeedsUpdate = updateThisSet
                                    || newVolatileConstantBuffers.size() >= mCurrentComputeVolatileCBs.size()
                                    || volatileGpuAddress != mCurrentComputeVolatileCBs[newVolatileConstantBuffers.size()].address;

                                if (bindingNeedsUpdate)
                                {
                                    mActiveCommandList->commandList->SetComputeRootConstantBufferView(rootParameterIndex, volatileGpuAddress);
                                }

                                newVolatileConstantBuffers.push_back({ rootParameterIndex, buffer, volatileGpuAddress });
                            }
                            else if (updateThisSet)
                            {
                                mActiveCommandList->commandList->SetComputeRootConstantBufferView(rootParameterIndex, buffer->gpuVA);
                            }
                        }
                        else if (updateThisSet)
                        {
                            mActiveCommandList->commandList->SetComputeRootConstantBufferView(rootParameterIndex, 0);
                        }
                    }

                    if (updateThisSet)
                    {
                        if (bindingSet->descriptorTableValidSamplers)
                        {
                            mActiveCommandList->commandList->SetComputeRootDescriptorTable(
                                rootParameterOffset + bindingSet->rootParameterIndexSamplers,
                                m_Resources.samplerHeap.GetGpuHandle(bindingSet->descriptorTableSamplers));
                        }

                        if (bindingSet->descriptorTableValidSRVetc)
                        {
                            mActiveCommandList->commandList->SetComputeRootDescriptorTable(
                                rootParameterOffset + bindingSet->rootParameterIndexSRVetc,
                                m_Resources.shaderResourceViewHeap.GetGpuHandle(bindingSet->descriptorTableSRVetc));
                        }

                        if (bindingSet->desc.trackLiveness)
                        {
                            mInstance->referencedResources.push_back(bindingSet);
                        }
                    }

                    if (mEnableAutomaticBarriers && (mBindingStatesDirty || updateThisSet || bindingSet->hasUavBindings))
                    {
                        SetResourceStatesForBindingSet(bindingSet);
                    }
                }
                else if (rootParameterOffset != cInvalidRootParameterIndex)
                {
                    ZWD3D12DescriptorTable* descriptorTable = static_cast<ZWD3D12DescriptorTable*>(bindingSetInterface);
                    mActiveCommandList->commandList->SetComputeRootDescriptorTable(
                        rootParameterOffset,
                        m_Resources.shaderResourceViewHeap.GetGpuHandle(descriptorTable->firstDescriptor));
                }
            }

            mCurrentComputeVolatileCBs = newVolatileConstantBuffers;
        }

        if (indirectParams != nullptr && updateIndirectParams)
        {
            if (mEnableAutomaticBarriers)
            {
                RequireBufferState(indirectParams, EResourceStates::IndirectArgument);
            }

            mInstance->referencedResources.push_back(indirectParams);
        }

        const uint32_t bindingMask = (1u << static_cast<uint32_t>(bindings.size())) - 1u;
        if ((bindingUpdateMask & bindingMask) == bindingMask)
        {
            mAnyVolatileBufferWrites = false;
        }
    }

    void ZWD3D12CommandList::SetGraphicsBindings(
        const BindingSetVector& bindings,
        uint32_t bindingUpdateMask,
        IBuffer* indirectParams,
        bool updateIndirectParams,
        IBuffer* indirectCountBuffer,
        bool updateIndirectCountBuffer,
        const ZWD3D12RootSignature* rootSignature)
    {
        if (rootSignature == nullptr)
        {
            return;
        }

        if (bindingUpdateMask != 0)
        {
            HCommon::StaticVector<VolatileConstantBufferBinding, gMaxVolatileConstantBuffers> newVolatileConstantBuffers;

            for (uint32_t bindingSetIndex = 0; bindingSetIndex < static_cast<uint32_t>(bindings.size()); ++bindingSetIndex)
            {
                IBindingSet* bindingSetInterface = bindings[bindingSetIndex];
                if (bindingSetInterface == nullptr || bindingSetIndex >= rootSignature->pipelineLayouts.size())
                {
                    continue;
                }

                const bool updateThisSet = (bindingUpdateMask & (1u << bindingSetIndex)) != 0;
                const auto& layoutAndOffset = rootSignature->pipelineLayouts[bindingSetIndex];
                const RootParameterIndex rootParameterOffset = layoutAndOffset.second;

                if (bindingSetInterface->GetDesc() != nullptr)
                {
                    ZWD3D12BindingSet* bindingSet = static_cast<ZWD3D12BindingSet*>(bindingSetInterface);

                    for (size_t volatileCbIndex = 0; volatileCbIndex < bindingSet->rootParametersVolatileCB.size(); ++volatileCbIndex)
                    {
                        const auto& parameter = bindingSet->rootParametersVolatileCB[volatileCbIndex];
                        const RootParameterIndex rootParameterIndex = rootParameterOffset + parameter.first;

                        if (parameter.second != nullptr)
                        {
                            ZWD3D12Buffer* buffer = static_cast<ZWD3D12Buffer*>(parameter.second);

                            if (buffer->desc.isVolatile)
                            {
                                const D3D12_GPU_VIRTUAL_ADDRESS volatileGpuAddress = mVolatileConstantBufferAddresses[buffer];
                                if (volatileGpuAddress == 0)
                                {
                                    std::stringstream messageBuilder;
                                    messageBuilder << "Attempted use of a volatile constant buffer "
                                        << HApp::DebugNameToString(buffer->desc.debugName)
                                        << " before it was written into.";
                                    m_Context.Error(messageBuilder.str());
                                    continue;
                                }

                                const bool bindingNeedsUpdate = updateThisSet
                                    || newVolatileConstantBuffers.size() >= mCurrentGraphicsVolatileCBs.size()
                                    || volatileGpuAddress != mCurrentGraphicsVolatileCBs[newVolatileConstantBuffers.size()].address;

                                if (bindingNeedsUpdate)
                                {
                                    mActiveCommandList->commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, volatileGpuAddress);
                                }

                                newVolatileConstantBuffers.push_back({ rootParameterIndex, buffer, volatileGpuAddress });
                            }
                            else if (updateThisSet)
                            {
                                mActiveCommandList->commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, buffer->gpuVA);
                            }
                        }
                        else if (updateThisSet)
                        {
                            mActiveCommandList->commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, 0);
                        }
                    }

                    if (updateThisSet)
                    {
                        if (bindingSet->descriptorTableValidSamplers)
                        {
                            mActiveCommandList->commandList->SetGraphicsRootDescriptorTable(
                                rootParameterOffset + bindingSet->rootParameterIndexSamplers,
                                m_Resources.samplerHeap.GetGpuHandle(bindingSet->descriptorTableSamplers));
                        }

                        if (bindingSet->descriptorTableValidSRVetc)
                        {
                            mActiveCommandList->commandList->SetGraphicsRootDescriptorTable(
                                rootParameterOffset + bindingSet->rootParameterIndexSRVetc,
                                m_Resources.shaderResourceViewHeap.GetGpuHandle(bindingSet->descriptorTableSRVetc));
                        }

                        if (bindingSet->desc.trackLiveness)
                        {
                            mInstance->referencedResources.push_back(bindingSet);
                        }
                    }

                    if (mEnableAutomaticBarriers && (mBindingStatesDirty || updateThisSet || bindingSet->hasUavBindings))
                    {
                        SetResourceStatesForBindingSet(bindingSet);
                    }
                }
                else if (rootParameterOffset != cInvalidRootParameterIndex)
                {
                    ZWD3D12DescriptorTable* descriptorTable = static_cast<ZWD3D12DescriptorTable*>(bindingSetInterface);
                    mActiveCommandList->commandList->SetGraphicsRootDescriptorTable(
                        rootParameterOffset,
                        m_Resources.shaderResourceViewHeap.GetGpuHandle(descriptorTable->firstDescriptor));
                }
            }

            mCurrentGraphicsVolatileCBs = newVolatileConstantBuffers;
        }

        if (indirectParams != nullptr && updateIndirectParams)
        {
            if (mEnableAutomaticBarriers)
            {
                RequireBufferState(indirectParams, EResourceStates::IndirectArgument);
            }

            mInstance->referencedResources.push_back(indirectParams);
        }

        if (indirectCountBuffer != nullptr && updateIndirectCountBuffer)
        {
            if (mEnableAutomaticBarriers)
            {
                RequireBufferState(indirectCountBuffer, EResourceStates::IndirectArgument);
            }

            mInstance->referencedResources.push_back(indirectCountBuffer);
        }

        const uint32_t bindingMask = (1u << static_cast<uint32_t>(bindings.size())) - 1u;
        if ((bindingUpdateMask & bindingMask) == bindingMask)
        {
            mAnyVolatileBufferWrites = false;
        }
    }
}
