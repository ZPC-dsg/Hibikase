/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <BackEnd/vulkanbackend.h>

namespace HRHI
{
    template <typename TContainer>
    static bool ArraysAreDifferent(const TContainer& left, const TContainer& right)
    {
        if (left.size() != right.size())
            return true;

        for (size_t i = 0; i < left.size(); i++)
        {
            if (left[i] != right[i])
                return true;
        }

        return false;
    }

    template <typename TContainer>
    static uint32_t ArrayDifferenceMask(const TContainer& left, const TContainer& right)
    {
        if (left.size() != right.size())
            return ~0u;

        uint32_t mask = 0;
        for (size_t i = 0; i < left.size(); i++)
        {
            if (left[i] != right[i])
                mask |= (1u << i);
        }

        return mask;
    }
    
    void ZWVKCommandList::SetResourceStatesForBindingSet(IBindingSet* _bindingSet)
    {
        if (_bindingSet == nullptr)
            return;
        if (_bindingSet->GetDesc() == nullptr)
            return; // is bindless

        ZWVKBindingSet* bindingSet = static_cast<ZWVKBindingSet*>(_bindingSet);

        for (auto bindingIndex : bindingSet->bindingsThatNeedTransitions)
        {
            const ZWBindingSetItem& binding = bindingSet->desc.bindings[bindingIndex];

            switch(binding.type)  // NOLINT(clang-diagnostic-switch-enum)
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
                    RequireBufferState(static_cast<ZWVKAccelStruct*>(binding.resourceHandle)->dataBuffer, EResourceStates::AccelStructRead);

                default:
                    // do nothing
                    break;
            }
        }
    }

    void ZWVKCommandList::InsertResourceBarriersForBindingSets(const BindingSetVector& newBindings, const BindingSetVector& oldBindings)
    {
        uint32_t bindingUpdateMask = 0;

        if (mBindingStatesDirty)
            bindingUpdateMask = ~0u;

        if (bindingUpdateMask == 0)
            bindingUpdateMask = ArrayDifferenceMask(newBindings, oldBindings);

        if (bindingUpdateMask != 0)
        {
            for (size_t i = 0; i < newBindings.size(); i++)
            {
                if (newBindings[i]->GetDesc() == nullptr) // Ignore bindless sets
                    continue;

                ZWVKBindingSet const* bindingSet = static_cast<ZWVKBindingSet const*>(newBindings[i]);

                bool const updateThisSet = (bindingUpdateMask & (1u << i)) != 0;
                if (updateThisSet || bindingSet->hasUavBindings) // UAV bindings may place UAV barriers on the same binding set
                    SetResourceStatesForBindingSet(newBindings[i]);
            }
        }
    }

    void ZWVKCommandList::InsertGraphicsResourceBarriers(const ZWGraphicsState& state)
    {
        InsertResourceBarriersForBindingSets(state.bindings, mCurrentGraphicsState.bindings);

        if (state.indexBuffer.buffer && (mBindingStatesDirty || state.indexBuffer.buffer != mCurrentGraphicsState.indexBuffer.buffer))
        {
            RequireBufferState(state.indexBuffer.buffer, EResourceStates::IndexBuffer);
        }

        if (mBindingStatesDirty || ArraysAreDifferent(state.vertexBuffers, mCurrentGraphicsState.vertexBuffers))
        {
            for (const auto& vb : state.vertexBuffers)
            {
                RequireBufferState(vb.buffer, EResourceStates::VertexBuffer);
            }
        }

        if (mBindingStatesDirty || mCurrentGraphicsState.framebuffer != state.framebuffer)
        {
            SetResourceStatesForFramebuffer(state.framebuffer);
        }

        if (state.indirectParams && (mBindingStatesDirty || state.indirectParams != mCurrentGraphicsState.indirectParams))
        {
            RequireBufferState(state.indirectParams, EResourceStates::IndirectArgument);
        }

        mBindingStatesDirty = false;
    }

    void ZWVKCommandList::InsertComputeResourceBarriers(const ZWComputeState& state)
    {
        InsertResourceBarriersForBindingSets(state.bindings, mCurrentComputeState.bindings);

        if (state.indirectParams && (mBindingStatesDirty || state.indirectParams != mCurrentComputeState.indirectParams))
        {
            ZWVKBuffer* indirectParams = static_cast<ZWVKBuffer*>(state.indirectParams);

            RequireBufferState(indirectParams, EResourceStates::IndirectArgument);
        }

        mBindingStatesDirty = false;
    }

    void ZWVKCommandList::InsertMeshletResourceBarriers(const ZWMeshletState& state)
    {
        InsertResourceBarriersForBindingSets(state.bindings, mCurrentMeshletState.bindings);

        if (mBindingStatesDirty || mCurrentMeshletState.framebuffer != state.framebuffer)
        {
            SetResourceStatesForFramebuffer(state.framebuffer);
        }

        if (state.indirectParams && (mBindingStatesDirty || state.indirectParams != mCurrentMeshletState.indirectParams))
        {
            RequireBufferState(state.indirectParams, EResourceStates::IndirectArgument);
        }

        mBindingStatesDirty = false;
    }

    void ZWVKCommandList::InsertRayTracingResourceBarriers(const Hrt::ZWState& state)
    {
        InsertResourceBarriersForBindingSets(state.bindings, mCurrentRayTracingState.bindings);

        mBindingStatesDirty = false;
    }

    void ZWVKCommandList::RequireTextureState(ITexture* _texture, ZWTextureSubresourceSet subresources, EResourceStates state)
    {
        ZWVKTexture* texture = static_cast<ZWVKTexture*>(_texture);

        mStateTracker.RequireTextureState(texture, subresources, state);
    }

    void ZWVKCommandList::RequireBufferState(IBuffer* _buffer, EResourceStates state)
    {
        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);

        mStateTracker.RequireBufferState(buffer, state);
    }

    bool ZWVKCommandList::AnyBarriers() const
    {
        return !mStateTracker.GetBufferBarriers().empty() || !mStateTracker.GetTextureBarriers().empty();
    }

    void ZWVKCommandList::CommitBarriersInternal()
    {
        std::vector<vk::ImageMemoryBarrier2> imageBarriers;
        std::vector<vk::BufferMemoryBarrier2> bufferBarriers;

        for (const ZWTextureBarrier& barrier : mStateTracker.GetTextureBarriers())
        {
            ZWVKResourceStateMapping before = ConvertResourceState(barrier.stateBefore, true);
            ZWVKResourceStateMapping after = ConvertResourceState(barrier.stateAfter, true);

            assert(after.imageLayout != vk::ImageLayout::eUndefined);

            ZWVKTexture* texture = static_cast<ZWVKTexture*>(barrier.texture);

            const ZWFormatInfo& formatInfo = GetFormatInfo(texture->desc.format);

            vk::ImageAspectFlags aspectMask = (vk::ImageAspectFlagBits)0;
            if (formatInfo.hasDepth) aspectMask |= vk::ImageAspectFlagBits::eDepth;
            if (formatInfo.hasStencil) aspectMask |= vk::ImageAspectFlagBits::eStencil;
            if (!aspectMask) aspectMask = vk::ImageAspectFlagBits::eColor;

            vk::ImageSubresourceRange subresourceRange = vk::ImageSubresourceRange()
                .setBaseArrayLayer(barrier.entireTexture ? 0 : barrier.arraySlice)
                .setLayerCount(barrier.entireTexture ? texture->desc.arraySize : 1)
                .setBaseMipLevel(barrier.entireTexture ? 0 : barrier.mipLevel)
                .setLevelCount(barrier.entireTexture ? texture->desc.mipLevels : 1)
                .setAspectMask(aspectMask);

            imageBarriers.push_back(vk::ImageMemoryBarrier2()
                .setSrcAccessMask(before.accessMask)
                .setDstAccessMask(after.accessMask)
                .setSrcStageMask(before.stageFlags)
                .setDstStageMask(after.stageFlags)
                .setOldLayout(before.imageLayout)
                .setNewLayout(after.imageLayout)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setImage(texture->image)
                .setSubresourceRange(subresourceRange));
        }

        if (!imageBarriers.empty())
        {
            vk::DependencyInfo dep_info;
            dep_info.setImageMemoryBarriers(imageBarriers);

            mCurrentCmdBuf->cmdBuf.pipelineBarrier2(dep_info);
        }

        imageBarriers.clear();

        for (const ZWBufferBarrier& barrier : mStateTracker.GetBufferBarriers())
        {
            ZWVKResourceStateMapping before = ConvertResourceState(barrier.stateBefore, false);
            ZWVKResourceStateMapping after = ConvertResourceState(barrier.stateAfter, false);

            ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(barrier.buffer);

            bufferBarriers.push_back(vk::BufferMemoryBarrier2()
                .setSrcAccessMask(before.accessMask)
                .setDstAccessMask(after.accessMask)
                .setSrcStageMask(before.stageFlags)
                .setDstStageMask(after.stageFlags)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setBuffer(buffer->buffer)
                .setOffset(0)
                .setSize(buffer->desc.byteSize));
        }

        if (!bufferBarriers.empty())
        {
            vk::DependencyInfo dep_info;
            dep_info.setBufferMemoryBarriers(bufferBarriers);

            mCurrentCmdBuf->cmdBuf.pipelineBarrier2(dep_info);
        }
        bufferBarriers.clear();

        mStateTracker.ClearBarriers();
    }

    void ZWVKCommandList::CommitBarriers()
    {
        if (mStateTracker.GetBufferBarriers().empty() && mStateTracker.GetTextureBarriers().empty())
            return;

        EndRenderPass();

        CommitBarriersInternal();
    }

    void ZWVKCommandList::BeginTrackingTextureState(ITexture* _texture, ZWTextureSubresourceSet subresources, EResourceStates stateBits)
    {
        ZWVKTexture* texture = static_cast<ZWVKTexture*>(_texture);

        mStateTracker.BeginTrackingTextureState(texture, subresources, stateBits);
    }

    void ZWVKCommandList::BeginTrackingBufferState(IBuffer* _buffer, EResourceStates stateBits)
    {
        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);

        mStateTracker.BeginTrackingBufferState(buffer, stateBits);
    }

    void ZWVKCommandList::SetTextureState(ITexture* _texture, ZWTextureSubresourceSet subresources, EResourceStates stateBits)
    {
        ZWVKTexture* texture = static_cast<ZWVKTexture*>(_texture);

        mStateTracker.RequireTextureState(texture, subresources, stateBits);

        if (mCurrentCmdBuf)
            mCurrentCmdBuf->referencedResources.push_back(texture);
    }

    void ZWVKCommandList::SetBufferState(IBuffer* _buffer, EResourceStates stateBits)
    {
        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);

        mStateTracker.RequireBufferState(buffer, stateBits);
        
        if (mCurrentCmdBuf)
            mCurrentCmdBuf->referencedResources.push_back(buffer);
    }
    
    void ZWVKCommandList::SetAccelStructState(Hrt::IAccelStruct* _as, EResourceStates stateBits)
    {
        ZWVKAccelStruct* as = static_cast<ZWVKAccelStruct*>(_as);

        if (as->dataBuffer)
        {
            ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(as->dataBuffer.Get());
            mStateTracker.RequireBufferState(buffer, stateBits);

            if (mCurrentCmdBuf)
                mCurrentCmdBuf->referencedResources.push_back(as);
        }
    }

    void ZWVKCommandList::SetPermanentTextureState(ITexture* _texture, EResourceStates stateBits)
    {
        ZWVKTexture* texture = static_cast<ZWVKTexture*>(_texture);

        mStateTracker.SetPermanentTextureState(texture, sAllSubresources, stateBits);

        if (mCurrentCmdBuf)
            mCurrentCmdBuf->referencedResources.push_back(texture);
    }

    void ZWVKCommandList::SetPermanentBufferState(IBuffer* _buffer, EResourceStates stateBits)
    {
        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);

        mStateTracker.SetPermanentBufferState(buffer, stateBits);
        
        if (mCurrentCmdBuf)
            mCurrentCmdBuf->referencedResources.push_back(buffer);
    }

    EResourceStates ZWVKCommandList::GetTextureSubresourceState(ITexture* _texture, ArraySlice arraySlice, MipLevel mipLevel)
    {
        ZWVKTexture* texture = static_cast<ZWVKTexture*>(_texture);

        return mStateTracker.GetTextureSubresourceState(texture, arraySlice, mipLevel);
    }

    EResourceStates ZWVKCommandList::GetBufferState(IBuffer* _buffer)
    {
        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);

        return mStateTracker.GetBufferState(buffer);
    }

    void ZWVKCommandList::SetEnableAutomaticBarriers(bool enable)
    {
        m_EnableAutomaticBarriers = enable;
    }

    void ZWVKCommandList::SetEnableUavBarriersForTexture(ITexture* _texture, bool enableBarriers)
    {
        ZWVKTexture* texture = static_cast<ZWVKTexture*>(_texture);

        mStateTracker.SetEnableUavBarriersForTexture(texture, enableBarriers);
    }

    void ZWVKCommandList::SetEnableUavBarriersForBuffer(IBuffer* _buffer, bool enableBarriers)
    {
        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);

        mStateTracker.SetEnableUavBarriersForBuffer(buffer, enableBarriers);
    }

} // namespace HRHI
