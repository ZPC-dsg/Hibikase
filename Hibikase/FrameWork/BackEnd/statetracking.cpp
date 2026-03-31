#include <BackEnd/statetracking.h>
#include <Utils/stringtranslatehelper.h>

#include <sstream>

namespace HRHI
{
    bool VerifyPermanentResourceState(EResourceStates permanentState, EResourceStates requiredState, bool isTexture, const std::string& debugName, IMessageCallback* messageCallback)
    {
        if ((permanentState & requiredState) != requiredState)
        {
            std::stringstream ss;
            ss << "Permanent " << (isTexture ? "texture " : "buffer ") << HApp::DebugNameToString(debugName)
                << " doesn't have the right state bits. Required: 0x" << std::hex << uint32_t(requiredState)
                << ", present: 0x" << std::hex << uint32_t(permanentState);
            messageCallback->message(EMessageSeverity::Error, ss.str().c_str());
            return false;
        }

        return true;
    }

    static uint32_t CalcSubresource(MipLevel mipLevel, ArraySlice arraySlice, const ZWTextureDesc& desc)
    {
        return mipLevel + arraySlice * desc.mipLevels;
    }

    void ZWCommandListResourceStateTracker::SetEnableUavBarriersForTexture(ZWTextureStateExtension* texture, bool enableBarriers)
    {
        ZWTextureState* tracking = GetTextureStateTracking(texture, true);

        tracking->enableUavBarriers = enableBarriers;
        tracking->firstUavBarrierPlaced = false;
    }

    void ZWCommandListResourceStateTracker::SetEnableUavBarriersForBuffer(ZWBufferStateExtension* buffer, bool enableBarriers)
    {
        ZWBufferState* tracking = GetBufferStateTracking(buffer, true);

        tracking->enableUavBarriers = enableBarriers;
        tracking->firstUavBarrierPlaced = false;
    }

    void ZWCommandListResourceStateTracker::BeginTrackingTextureState(ZWTextureStateExtension* texture, ZWTextureSubresourceSet subresources, EResourceStates stateBits)
    {
        const ZWTextureDesc& desc = texture->descRef;

        ZWTextureState* tracking = GetTextureStateTracking(texture, true);

        subresources = subresources.resolve(desc, false);

        if (subresources.isEntireTexture(desc))
        {
            tracking->state = stateBits;
            tracking->subresourceStates.clear();
        }
        else
        {
            tracking->subresourceStates.resize(desc.mipLevels * desc.arraySize, tracking->state);
            tracking->state = EResourceStates::Unknown;

            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
            {
                for (ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < subresources.baseArraySlice + subresources.numArraySlices; arraySlice++)
                {
                    uint32_t subresource = CalcSubresource(mipLevel, arraySlice, desc);
                    tracking->subresourceStates[subresource] = stateBits;
                }
            }
        }
    }

    void ZWCommandListResourceStateTracker::BeginTrackingBufferState(ZWBufferStateExtension* buffer, EResourceStates stateBits)
    {
        ZWBufferState* tracking = GetBufferStateTracking(buffer, true);

        tracking->state = stateBits;
    }

    void ZWCommandListResourceStateTracker::SetPermanentTextureState(ZWTextureStateExtension* texture, ZWTextureSubresourceSet subresources, EResourceStates stateBits)
    {
        const ZWTextureDesc& desc = texture->descRef;

        subresources = subresources.resolve(desc, false);

        bool permanent = true;
        if (!subresources.isEntireTexture(desc))
        {
            std::stringstream ss;
            ss << "Attempted to perform a permanent state transition on a subset of subresources of texture "
                << HApp::DebugNameToString(desc.debugName);
            mMessageCallback->message(EMessageSeverity::Error, ss.str().c_str());
            permanent = false;
        }

        RequireTextureState(texture, subresources, stateBits);

        if (permanent)
        {
            mPermanentTextureStates.push_back(std::make_pair(texture, stateBits));
            GetTextureStateTracking(texture, true)->permanentTransition = true;
        }
    }

    void ZWCommandListResourceStateTracker::SetPermanentBufferState(ZWBufferStateExtension* buffer, EResourceStates stateBits)
    {
        RequireBufferState(buffer, stateBits);

        mPermanentBufferStates.push_back(std::make_pair(buffer, stateBits));
    }

    EResourceStates ZWCommandListResourceStateTracker::GetTextureSubresourceState(ZWTextureStateExtension* texture, ArraySlice arraySlice, MipLevel mipLevel)
    {
        ZWTextureState* tracking = GetTextureStateTracking(texture, false);
        if (!tracking)
        {
            return texture->descRef.keepInitialState ?
                (texture->stateInitialized ? texture->descRef.initialState : EResourceStates::Common) :
                EResourceStates::Unknown;
        }

        // whole resource
        if (tracking->subresourceStates.empty())
            return tracking->state;

        uint32_t subresource = CalcSubresource(mipLevel, arraySlice, texture->descRef);
        return tracking->subresourceStates[subresource];
    }

    EResourceStates ZWCommandListResourceStateTracker::GetBufferState(ZWBufferStateExtension* buffer)
    {
        ZWBufferState* tracking = GetBufferStateTracking(buffer, false);

        if (!tracking)
            return EResourceStates::Unknown;

        return tracking->state;
    }

    void ZWCommandListResourceStateTracker::RequireTextureState(ZWTextureStateExtension* texture, ZWTextureSubresourceSet subresources, EResourceStates state)
    {
        if (texture->permanentState != 0)
        {
            VerifyPermanentResourceState(texture->permanentState, state, true, texture->descRef.debugName, mMessageCallback);
            return;
        }

        subresources = subresources.resolve(texture->descRef, false);

        ZWTextureState* tracking = GetTextureStateTracking(texture, true);

        if (tracking->subresourceStates.empty() && tracking->state == EResourceStates::Unknown)
        {
            std::stringstream ss;
            ss << "Unknown prior state of texture " << HApp::DebugNameToString(texture->descRef.debugName) << ". "
                "Call CommandList::beginTrackingTextureState(...) before using the texture or use the "
                "keepInitialState and initialState members of TextureDesc.";
            mMessageCallback->message(EMessageSeverity::Error, ss.str().c_str());
        }

        if (subresources.isEntireTexture(texture->descRef) && tracking->subresourceStates.empty())
        {
            // We're requiring state for the entire texture, and it's been tracked as entire texture too

            bool transitionNecessary = tracking->state != state;
            bool uavNecessary = ((state & EResourceStates::UnorderedAccess) != 0)
                && (tracking->enableUavBarriers || !tracking->firstUavBarrierPlaced);

            if (transitionNecessary || uavNecessary)
            {
                ZWTextureBarrier barrier;
                barrier.texture = texture;
                barrier.entireTexture = true;
                barrier.stateBefore = tracking->state;
                barrier.stateAfter = state;
                mTextureBarriers.push_back(barrier);
            }

            tracking->state = state;

            if (uavNecessary && !transitionNecessary)
            {
                tracking->firstUavBarrierPlaced = true;
            }
        }
        else
        {
            // Transition individual subresources

            // Make sure that we're tracking the texture on subresource level
            bool stateExpanded = false;
            if (tracking->subresourceStates.empty())
            {
                tracking->subresourceStates.resize(texture->descRef.mipLevels * texture->descRef.arraySize, tracking->state);
                tracking->state = EResourceStates::Unknown;
                stateExpanded = true;
            }

            bool anyUavBarrier = false;

            for (ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < subresources.baseArraySlice + subresources.numArraySlices; arraySlice++)
            {
                for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
                {
                    uint32_t subresourceIndex = CalcSubresource(mipLevel, arraySlice, texture->descRef);

                    auto priorState = tracking->subresourceStates[subresourceIndex];

                    if (priorState == EResourceStates::Unknown && !stateExpanded)
                    {
                        std::stringstream ss;
                        ss << "Unknown prior state of texture " << HApp::DebugNameToString(texture->descRef.debugName)
                            << " subresource (MipLevel = " << mipLevel << ", ArraySlice = " << arraySlice << "). "
                            "Call CommandList::beginTrackingTextureState(...) before using the texture or use the "
                            "keepInitialState and initialState members of TextureDesc.";
                        mMessageCallback->message(EMessageSeverity::Error, ss.str().c_str());
                    }

                    bool transitionNecessary = priorState != state;
                    bool uavNecessary = ((state & EResourceStates::UnorderedAccess) != 0)
                        && !anyUavBarrier && (tracking->enableUavBarriers || !tracking->firstUavBarrierPlaced);

                    if (transitionNecessary || uavNecessary)
                    {
                        ZWTextureBarrier barrier;
                        barrier.texture = texture;
                        barrier.entireTexture = false;
                        barrier.mipLevel = mipLevel;
                        barrier.arraySlice = arraySlice;
                        barrier.stateBefore = priorState;
                        barrier.stateAfter = state;
                        mTextureBarriers.push_back(barrier);
                    }

                    tracking->subresourceStates[subresourceIndex] = state;

                    if (uavNecessary && !transitionNecessary)
                    {
                        anyUavBarrier = true;
                        tracking->firstUavBarrierPlaced = true;
                    }
                }
            }
        }
    }

    void ZWCommandListResourceStateTracker::RequireBufferState(ZWBufferStateExtension* buffer, EResourceStates state)
    {
        if (buffer->descRef.isVolatile)
            return;

        if (buffer->permanentState != 0)
        {
            VerifyPermanentResourceState(buffer->permanentState, state, false, buffer->descRef.debugName, mMessageCallback);

            return;
        }

        if (buffer->descRef.cpuAccess != ECpuAccessMode::None)
        {
            // CPU-visible buffers can't change state
            return;
        }

        ZWBufferState* tracking = GetBufferStateTracking(buffer, true);

        if (tracking->state == EResourceStates::Unknown)
        {
            std::stringstream ss;
            ss << "Unknown prior state of buffer " << HApp::DebugNameToString(buffer->descRef.debugName) << ". "
                "Call CommandList::beginTrackingBufferState(...) before using the buffer or use the "
                "keepInitialState and initialState members of BufferDesc.";
            mMessageCallback->message(EMessageSeverity::Error, ss.str().c_str());
        }

        bool transitionNecessary = tracking->state != state;
        bool uavNecessary = ((state & EResourceStates::UnorderedAccess) != 0)
            && (tracking->enableUavBarriers || !tracking->firstUavBarrierPlaced);

        if (transitionNecessary)
        {
            // See if this buffer is already used for a different purpose in this batch.
            // If it is, combine the state bits.
            // Example: same buffer used as index and vertex buffer, or as SRV and indirect arguments.
            for (ZWBufferBarrier& barrier : mBufferBarriers)
            {
                if (barrier.buffer == buffer)
                {
                    barrier.stateAfter = EResourceStates(barrier.stateAfter | state);
                    tracking->state = barrier.stateAfter;
                    return;
                }
            }
        }

        if (transitionNecessary || uavNecessary)
        {
            ZWBufferBarrier barrier;
            barrier.buffer = buffer;
            barrier.stateBefore = tracking->state;
            barrier.stateAfter = state;
            mBufferBarriers.push_back(barrier);
        }

        if (uavNecessary && !transitionNecessary)
        {
            tracking->firstUavBarrierPlaced = true;
        }

        tracking->state = state;
    }

    void ZWCommandListResourceStateTracker::KeepBufferInitialStates()
    {
        for (auto& [buffer, tracking] : mBufferStates)
        {
            if (buffer->descRef.keepInitialState &&
                !buffer->permanentState &&
                !buffer->descRef.isVolatile &&
                !tracking->permanentTransition)
            {
                RequireBufferState(buffer, buffer->descRef.initialState);
            }
        }
    }

    void ZWCommandListResourceStateTracker::KeepTextureInitialStates()
    {
        for (auto& [texture, tracking] : mTextureStates)
        {
            if (texture->descRef.keepInitialState &&
                !texture->permanentState &&
                !tracking->permanentTransition)
            {
                RequireTextureState(texture, sAllSubresources, texture->descRef.initialState);
            }
        }
    }

    void ZWCommandListResourceStateTracker::CommandListSubmitted()
    {
        for (auto [texture, state] : mPermanentTextureStates)
        {
            if (texture->permanentState != 0 && texture->permanentState != state)
            {
                std::stringstream ss;
                ss << "Attempted to switch permanent state of texture " << HApp::DebugNameToString(texture->descRef.debugName)
                    << " from 0x" << std::hex << uint32_t(texture->permanentState) << " to 0x" << std::hex << uint32_t(state);
                mMessageCallback->message(EMessageSeverity::Error, ss.str().c_str());
                continue;
            }

            texture->permanentState = state;
        }
        mPermanentTextureStates.clear();

        for (auto [buffer, state] : mPermanentBufferStates)
        {
            if (buffer->permanentState != 0 && buffer->permanentState != state)
            {
                std::stringstream ss;
                ss << "Attempted to switch permanent state of buffer " << HApp::DebugNameToString(buffer->descRef.debugName)
                    << " from 0x" << std::hex << uint32_t(buffer->permanentState) << " to 0x" << std::hex << uint32_t(state);
                mMessageCallback->message(EMessageSeverity::Error, ss.str().c_str());
                continue;
            }

            buffer->permanentState = state;
        }
        mPermanentBufferStates.clear();

        for (const auto& [texture, stateTracking] : mTextureStates)
        {
            if (texture->descRef.keepInitialState && !texture->stateInitialized)
                texture->stateInitialized = true;
        }

        mTextureStates.clear();
        mBufferStates.clear();
    }

    ZWTextureState* ZWCommandListResourceStateTracker::GetTextureStateTracking(ZWTextureStateExtension* texture, bool allowCreate)
    {
        auto it = mTextureStates.find(texture);

        if (it != mTextureStates.end())
        {
            return it->second.get();
        }

        if (!allowCreate)
            return nullptr;

        std::unique_ptr<ZWTextureState> trackingRef = std::make_unique<ZWTextureState>();

        ZWTextureState* tracking = trackingRef.get();
        mTextureStates.insert(std::make_pair(texture, std::move(trackingRef)));

        if (texture->descRef.keepInitialState)
        {
            tracking->state = texture->stateInitialized ? texture->descRef.initialState : EResourceStates::Common;
        }

        return tracking;
    }

    ZWBufferState* ZWCommandListResourceStateTracker::GetBufferStateTracking(ZWBufferStateExtension* buffer, bool allowCreate)
    {
        auto it = mBufferStates.find(buffer);

        if (it != mBufferStates.end())
        {
            return it->second.get();
        }

        if (!allowCreate)
            return nullptr;

        std::unique_ptr<ZWBufferState> trackingRef = std::make_unique<ZWBufferState>();

        ZWBufferState* tracking = trackingRef.get();
        mBufferStates.insert(std::make_pair(buffer, std::move(trackingRef)));

        if (buffer->descRef.keepInitialState)
        {
            tracking->state = buffer->descRef.initialState;
        }

        return tracking;
    }
}