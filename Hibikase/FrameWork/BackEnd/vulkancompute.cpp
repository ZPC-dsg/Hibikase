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

    ZWComputePipelineHandle ZWVKDevice::CreateComputePipeline(const ZWComputePipelineDesc& desc)
    {
        vk::Result res;

        assert(desc.CS);
        
        ZWVKComputePipeline* pso = new ZWVKComputePipeline(mContext);
        pso->desc = desc;

        res = CreatePipelineLayout(
            pso->pipelineLayout,
            pso->pipelineBindingLayouts,
            pso->pushConstantVisibility,
            pso->descriptorSetIdxToBindingIdx,
            mContext,
            desc.bindingLayouts);
        CHECK_VK_FAIL(res)

        ZWVKShader* cs = static_cast<ZWVKShader*>(desc.CS.Get());

        // See CreateGraphicsPipeline() for a more expanded implementation
        // of shader specializations with multiple shaders in the pipeline

        size_t numShaders = 0;
        size_t numShadersWithSpecializations = 0;
        size_t numSpecializationConstants = 0;

        CountSpecializationConstants(cs, numShaders, numShadersWithSpecializations, numSpecializationConstants);

        assert(numShaders == 1);

        std::vector<vk::SpecializationInfo> specInfos;
        std::vector<vk::SpecializationMapEntry> specMapEntries;
        std::vector<uint32_t> specData;

        specInfos.reserve(numShadersWithSpecializations);
        specMapEntries.reserve(numSpecializationConstants);
        specData.reserve(numSpecializationConstants);

        auto shaderStageInfo = MakeShaderStageCreateInfo(cs, 
            specInfos, specMapEntries, specData);
        
        auto pipelineInfo = vk::ComputePipelineCreateInfo()
                                .setStage(shaderStageInfo)
                                .setLayout(pso->pipelineLayout);

        res = mContext.device.createComputePipelines(mContext.pipelineCache,
                                                    1, &pipelineInfo,
                                                    mContext.allocationCallbacks,
                                                    &pso->pipeline);

        CHECK_VK_FAIL(res)

        return ZWComputePipelineHandle::Create(pso);
    }

    ZWVKComputePipeline::~ZWVKComputePipeline()
    {
        if (pipeline)
        {
            mContext.device.destroyPipeline(pipeline, mContext.allocationCallbacks);
            pipeline = nullptr;
        }

        if (pipelineLayout)
        {
            mContext.device.destroyPipelineLayout(pipelineLayout, mContext.allocationCallbacks);
            pipelineLayout = nullptr;
        }
    }

    HCommon::ZWObject ZWVKComputePipeline::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case HRHIObjectTypes::gVKPipelineLayout:
            return HCommon::ZWObject(pipelineLayout);
        case HRHIObjectTypes::gVKPipeline:
            return HCommon::ZWObject(pipeline);
        default:
            return nullptr;
        }
    }

    void ZWVKCommandList::SetComputeState(const ZWComputeState& state)
    {
        EndRenderPass();

        assert(mCurrentCmdBuf);

        ZWVKComputePipeline* pso = static_cast<ZWVKComputePipeline*>(state.pipeline);

        if (m_EnableAutomaticBarriers)
        {
            InsertComputeResourceBarriers(state);
        }

        if (mCurrentComputeState.pipeline != state.pipeline)
        {
            mCurrentCmdBuf->cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, pso->pipeline);

            mCurrentCmdBuf->referencedResources.push_back(state.pipeline);
        }

        if (state.indirectParams && state.indirectParams != mCurrentComputeState.indirectParams)
        {
            mCurrentCmdBuf->referencedResources.push_back(state.indirectParams);
        }

        if (ArraysAreDifferent(mCurrentComputeState.bindings, state.bindings) || mAnyVolatileBufferWrites)
        {
            BindBindingSets(vk::PipelineBindPoint::eCompute, pso->pipelineLayout, state.bindings, pso->descriptorSetIdxToBindingIdx);
        }

        mCurrentPipelineLayout = pso->pipelineLayout;
        mCurrentPushConstantsVisibility = pso->pushConstantVisibility;

        CommitBarriers();

        mCurrentGraphicsState = ZWGraphicsState();
        mCurrentComputeState = state;
        mCurrentMeshletState = ZWMeshletState();
        mCurrentRayTracingState = Hrt::ZWState();
        mAnyVolatileBufferWrites = false;
    }

    void ZWVKCommandList::UpdateComputeVolatileBuffers()
    {
        if (mAnyVolatileBufferWrites && mCurrentComputeState.pipeline)
        {
            ZWVKComputePipeline* pso = static_cast<ZWVKComputePipeline*>(mCurrentComputeState.pipeline);

            BindBindingSets(vk::PipelineBindPoint::eCompute, pso->pipelineLayout, mCurrentComputeState.bindings, pso->descriptorSetIdxToBindingIdx);

            mAnyVolatileBufferWrites = false;
        }
    }

    void ZWVKCommandList::Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        assert(mCurrentCmdBuf);

        UpdateComputeVolatileBuffers();

        mCurrentCmdBuf->cmdBuf.dispatch(groupsX, groupsY, groupsZ);
    }

    void ZWVKCommandList::DispatchIndirect(uint32_t offsetBytes)
    {
        assert(mCurrentCmdBuf);

        UpdateComputeVolatileBuffers();

        ZWVKBuffer* indirectParams = static_cast<ZWVKBuffer*>(mCurrentComputeState.indirectParams);
        assert(indirectParams);

        mCurrentCmdBuf->cmdBuf.dispatchIndirect(indirectParams->buffer, offsetBytes);
    }

} // namespace HRHI
