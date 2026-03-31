#pragma once

#include <BackEnd/RHIinterface.h>

#include <memory>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <tsl/robin_map.h>

namespace HRHI
{
    struct ZWBufferStateExtension
    {
        const ZWBufferDesc& descRef;
        EResourceStates permanentState = EResourceStates::Unknown;

        explicit ZWBufferStateExtension(const ZWBufferDesc& desc)
            : descRef(desc)
        {
        }
    };

    struct ZWTextureStateExtension
    {
        const ZWTextureDesc& descRef;
        EResourceStates permanentState = EResourceStates::Unknown;
        bool stateInitialized = false;
        bool isSamplerFeedback = false;

        explicit ZWTextureStateExtension(const ZWTextureDesc& desc)
            : descRef(desc)
        {
        }
    };

    struct ZWTextureState
    {
        std::vector<EResourceStates> subresourceStates;
        EResourceStates state = EResourceStates::Unknown;
        bool enableUavBarriers = true;
        bool firstUavBarrierPlaced = false;
        bool permanentTransition = false;
    };

    struct ZWBufferState
    {
        EResourceStates state = EResourceStates::Unknown;
        bool enableUavBarriers = true;
        bool firstUavBarrierPlaced = false;
        bool permanentTransition = false;
    };

    struct ZWTextureBarrier
    {
        ZWTextureStateExtension* texture = nullptr;
        MipLevel mipLevel = 0;
        ArraySlice arraySlice = 0;
        bool entireTexture = false;
        EResourceStates stateBefore = EResourceStates::Unknown;
        EResourceStates stateAfter = EResourceStates::Unknown;
    };

    struct ZWBufferBarrier
    {
        ZWBufferStateExtension* buffer = nullptr;
        EResourceStates stateBefore = EResourceStates::Unknown;
        EResourceStates stateAfter = EResourceStates::Unknown;
    };

    class ZWCommandListResourceStateTracker
    {
        public:
            explicit ZWCommandListResourceStateTracker(IMessageCallback* messageCallback)
                : mMessageCallback(messageCallback)
            {
            }

            // ICommandList-like interface

            void SetEnableUavBarriersForTexture(ZWTextureStateExtension* texture, bool enableBarriers);
            void SetEnableUavBarriersForBuffer(ZWBufferStateExtension* buffer, bool enableBarriers);

            void BeginTrackingTextureState(ZWTextureStateExtension* texture, ZWTextureSubresourceSet subresources, EResourceStates stateBits);
            void BeginTrackingBufferState(ZWBufferStateExtension* buffer, EResourceStates stateBits);

            void SetPermanentTextureState(ZWTextureStateExtension* texture, ZWTextureSubresourceSet subresources, EResourceStates stateBits);
            void SetPermanentBufferState(ZWBufferStateExtension* buffer, EResourceStates stateBits);

            EResourceStates GetTextureSubresourceState(ZWTextureStateExtension* texture, ArraySlice arraySlice, MipLevel mipLevel);
            EResourceStates GetBufferState(ZWBufferStateExtension* buffer);

            // Internal interface

            void RequireTextureState(ZWTextureStateExtension* texture, ZWTextureSubresourceSet subresources, EResourceStates state);
            void RequireBufferState(ZWBufferStateExtension* buffer, EResourceStates state);

            void KeepBufferInitialStates();
            void KeepTextureInitialStates();
            void CommandListSubmitted();

            [[nodiscard]] const std::vector<ZWTextureBarrier>& GetTextureBarriers() const { return mTextureBarriers; }
            [[nodiscard]] const std::vector<ZWBufferBarrier>& GetBufferBarriers() const { return mBufferBarriers; }
            void ClearBarriers() { mTextureBarriers.clear(); mBufferBarriers.clear(); }

        private:
            IMessageCallback* mMessageCallback;

            tsl::robin_map<ZWTextureStateExtension*, std::unique_ptr<ZWTextureState>> mTextureStates;
            tsl::robin_map<ZWBufferStateExtension*, std::unique_ptr<ZWBufferState>> mBufferStates;

            // Deferred transitions of textures and buffers to permanent states.
            // They are executed only when the command list is executed, not when the app calls setPermanentTextureState or setPermanentBufferState.
            std::vector<std::pair<ZWTextureStateExtension*, EResourceStates>> mPermanentTextureStates;
            std::vector<std::pair<ZWBufferStateExtension*, EResourceStates>> mPermanentBufferStates;

            std::vector<ZWTextureBarrier> mTextureBarriers;
            std::vector<ZWBufferBarrier> mBufferBarriers;

            ZWTextureState* GetTextureStateTracking(ZWTextureStateExtension* texture, bool allowCreate);
            ZWBufferState* GetBufferStateTracking(ZWBufferStateExtension* buffer, bool allowCreate);
    };

    bool VerifyPermanentResourceState(EResourceStates permanentState, EResourceStates requiredState, bool isTexture, const std::string& debugName, IMessageCallback* messageCallback);

}
