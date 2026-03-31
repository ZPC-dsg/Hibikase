#include <BackEnd/d3d12backend.h>

namespace HRHI::HD3D12
{
    void ZWD3D12CommandList::SetResourceStatesForBindingSet(IBindingSet* bindingSet)
    {
        if (bindingSet == nullptr || bindingSet->GetDesc() == nullptr)
        {
            return;
        }

        ZWD3D12BindingSet* d3d12BindingSet = static_cast<ZWD3D12BindingSet*>(bindingSet);

        for (uint16_t bindingIndex : d3d12BindingSet->bindingsThatNeedTransitions)
        {
            const ZWBindingSetItem& binding = d3d12BindingSet->desc.bindings[bindingIndex];

            switch (binding.type)
            {
            case EResourceType::Texture_SRV:
                RequireTextureState(static_cast<ITexture*>(binding.resourceHandle), binding.subresources, EResourceStates::ShaderResource);
                break;

            case EResourceType::Texture_UAV:
                RequireTextureState(static_cast<ITexture*>(binding.resourceHandle), binding.subresources, EResourceStates::UnorderedAccess);
                break;

            case EResourceType::TypedBuffer_SRV:
            case EResourceType::StructuredBuffer_SRV:
            case EResourceType::RawBuffer_SRV:
                RequireBufferState(static_cast<IBuffer*>(binding.resourceHandle), EResourceStates::ShaderResource);
                break;

            case EResourceType::TypedBuffer_UAV:
            case EResourceType::StructuredBuffer_UAV:
            case EResourceType::RawBuffer_UAV:
                RequireBufferState(static_cast<IBuffer*>(binding.resourceHandle), EResourceStates::UnorderedAccess);
                break;

            case EResourceType::ConstantBuffer:
                RequireBufferState(static_cast<IBuffer*>(binding.resourceHandle), EResourceStates::ConstantBuffer);
                break;

            case EResourceType::RayTracingAccelStruct:
            {
                ZWD3D12AccelStruct* accelStruct = static_cast<ZWD3D12AccelStruct*>(binding.resourceHandle);
                if (accelStruct != nullptr && accelStruct->dataBuffer != nullptr)
                {
                    RequireBufferState(accelStruct->dataBuffer.Get(), EResourceStates::AccelStructRead);
                }
                break;
            }

            case EResourceType::SamplerFeedbackTexture_UAV:
                RequireSamplerFeedbackTextureState(static_cast<ISamplerFeedbackTexture*>(binding.resourceHandle), EResourceStates::UnorderedAccess);
                break;

            default:
                break;
            }
        }
    }

    void ZWD3D12CommandList::RequireTextureState(ITexture* texture, ZWTextureSubresourceSet subresources, EResourceStates state)
    {
        mStateTracker.RequireTextureState(static_cast<ZWD3D12Texture*>(texture), subresources, state);
    }

    void ZWD3D12CommandList::RequireSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, EResourceStates state)
    {
        mStateTracker.RequireTextureState(static_cast<ZWD3D12SamplerFeedbackTexture*>(texture), sAllSubresources, state);
    }

    void ZWD3D12CommandList::SetEnableAutomaticBarriers(bool enable)
    {
        mEnableAutomaticBarriers = enable;
    }

    void ZWD3D12CommandList::SetEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers)
    {
        mStateTracker.SetEnableUavBarriersForTexture(static_cast<ZWD3D12Texture*>(texture), enableBarriers);
    }

    void ZWD3D12CommandList::SetEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers)
    {
        mStateTracker.SetEnableUavBarriersForBuffer(static_cast<ZWD3D12Buffer*>(buffer), enableBarriers);
    }

    void ZWD3D12CommandList::BeginTrackingTextureState(ITexture* texture, ZWTextureSubresourceSet subresources, EResourceStates stateBits)
    {
        mStateTracker.BeginTrackingTextureState(static_cast<ZWD3D12Texture*>(texture), subresources, stateBits);
    }

    void ZWD3D12CommandList::BeginTrackingBufferState(IBuffer* buffer, EResourceStates stateBits)
    {
        mStateTracker.BeginTrackingBufferState(static_cast<ZWD3D12Buffer*>(buffer), stateBits);
    }

    void ZWD3D12CommandList::SetTextureState(ITexture* texture, ZWTextureSubresourceSet subresources, EResourceStates stateBits)
    {
        ZWD3D12Texture* d3d12Texture = static_cast<ZWD3D12Texture*>(texture);
        mStateTracker.RequireTextureState(d3d12Texture, subresources, stateBits);

        if (mInstance != nullptr)
        {
            mInstance->referencedResources.push_back(d3d12Texture);
        }
    }

    void ZWD3D12CommandList::SetBufferState(IBuffer* buffer, EResourceStates stateBits)
    {
        ZWD3D12Buffer* d3d12Buffer = static_cast<ZWD3D12Buffer*>(buffer);
        mStateTracker.RequireBufferState(d3d12Buffer, stateBits);

        if (mInstance != nullptr)
        {
            mInstance->referencedResources.push_back(d3d12Buffer);
        }
    }

    void ZWD3D12CommandList::SetAccelStructState(Hrt::IAccelStruct* accelStruct, EResourceStates stateBits)
    {
        ZWD3D12AccelStruct* d3d12AccelStruct = static_cast<ZWD3D12AccelStruct*>(accelStruct);
        if (d3d12AccelStruct == nullptr || d3d12AccelStruct->dataBuffer == nullptr)
        {
            return;
        }

        mStateTracker.RequireBufferState(d3d12AccelStruct->dataBuffer.Get(), stateBits);

        if (mInstance != nullptr)
        {
            mInstance->referencedResources.push_back(d3d12AccelStruct);
        }
    }

    void ZWD3D12CommandList::SetPermanentTextureState(ITexture* texture, EResourceStates stateBits)
    {
        ZWD3D12Texture* d3d12Texture = static_cast<ZWD3D12Texture*>(texture);
        mStateTracker.SetPermanentTextureState(d3d12Texture, sAllSubresources, stateBits);

        if (mInstance != nullptr)
        {
            mInstance->referencedResources.push_back(d3d12Texture);
        }
    }

    void ZWD3D12CommandList::SetPermanentBufferState(IBuffer* buffer, EResourceStates stateBits)
    {
        ZWD3D12Buffer* d3d12Buffer = static_cast<ZWD3D12Buffer*>(buffer);
        mStateTracker.SetPermanentBufferState(d3d12Buffer, stateBits);

        if (mInstance != nullptr)
        {
            mInstance->referencedResources.push_back(d3d12Buffer);
        }
    }

    EResourceStates ZWD3D12CommandList::GetTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel)
    {
        return mStateTracker.GetTextureSubresourceState(static_cast<ZWD3D12Texture*>(texture), arraySlice, mipLevel);
    }

    EResourceStates ZWD3D12CommandList::GetBufferState(IBuffer* buffer)
    {
        return mStateTracker.GetBufferState(static_cast<ZWD3D12Buffer*>(buffer));
    }
}
