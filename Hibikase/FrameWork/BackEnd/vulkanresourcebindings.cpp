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
#include <Utils/stringtranslatehelper.h>

#include <cassert>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace HRHI
{

    ZWBindingLayoutHandle ZWVKDevice::CreateBindingLayout(const ZWBindingLayoutDesc& desc)
    {
        ZWVKBindingLayout* ret = new ZWVKBindingLayout(mContext, desc);

        ret->Bake();

        return ZWBindingLayoutHandle::Create(ret);
    }

    ZWBindingLayoutHandle ZWVKDevice::CreateBindlessLayout(const ZWBindlessLayoutDesc& desc)
    {
        ZWVKBindingLayout* ret = new ZWVKBindingLayout(mContext, desc);

        ret->Bake();

        return ZWBindingLayoutHandle::Create(ret);
    }

    static uint32_t GetRegisterOffsetForResourceType(ZWVulkanBindingOffsets const& bindingOffsets, EResourceType type)
    {
        switch (type)
        {
        case EResourceType::Texture_SRV:
        case EResourceType::TypedBuffer_SRV:
        case EResourceType::StructuredBuffer_SRV:
        case EResourceType::RawBuffer_SRV:
        case EResourceType::RayTracingAccelStruct:
            return bindingOffsets.shaderResource;

        case EResourceType::Texture_UAV:
        case EResourceType::TypedBuffer_UAV:
        case EResourceType::StructuredBuffer_UAV:
        case EResourceType::RawBuffer_UAV:
            return bindingOffsets.unorderedAccess;

        case EResourceType::ConstantBuffer:
        case EResourceType::VolatileConstantBuffer:
        case EResourceType::PushConstants:
            return bindingOffsets.constantBuffer;
            break;

        case EResourceType::Sampler:
            return bindingOffsets.sampler;

        default:
            assert(false);
            return 0;
        }        
    }

    ZWVKBindingLayout::ZWVKBindingLayout(const ZWVKContext& context, const ZWBindingLayoutDesc& _desc)
        : desc(_desc)
        , isBindless(false)
        , mContext(context)
    {
        vk::ShaderStageFlagBits shaderStageFlags = ConvertShaderTypeToShaderStageFlagBits(desc.visibility);

        // iterate over all binding types and add to map
        for (const ZWBindingLayoutItem& binding : desc.bindings)
        {
            if (binding.type == EResourceType::PushConstants)
            {
                // Don't need any descriptors for the push constants
                continue;
            }

            vk::DescriptorType const descriptorType = ConvertResourceType(binding.type);
            uint32_t const descriptorCount = binding.size;
            uint32_t const registerOffset = GetRegisterOffsetForResourceType(_desc.bindingOffsets, binding.type);

            const uint32_t bindingLocation = registerOffset + binding.slot;

            vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding = vk::DescriptorSetLayoutBinding()
                .setBinding(bindingLocation)
                .setDescriptorCount(descriptorCount)
                .setDescriptorType(descriptorType)
                .setStageFlags(shaderStageFlags);

            vulkanLayoutBindings.push_back(descriptorSetLayoutBinding);
        }
    }

    ZWVKBindingLayout::ZWVKBindingLayout(const ZWVKContext& context, const ZWBindlessLayoutDesc& _desc)
        : bindlessDesc(_desc)
        , isBindless(true)
        , mContext(context)
    {
        desc.visibility = bindlessDesc.visibility;
        vk::ShaderStageFlagBits shaderStageFlags = ConvertShaderTypeToShaderStageFlagBits(bindlessDesc.visibility);
        uint32_t bindingPoint = 0;
        uint32_t arraySize = bindlessDesc.maxCapacity;
        
        if (bindlessDesc.layoutType != ZWBindlessLayoutDesc::ELayoutType::Immutable)
        {
            if (!mContext.extensions.EXT_mutable_descriptor_type)
            {
                mContext.Error("Mutable descriptor types are not supported by this device. VK_EXT_mutable_descriptor_type extension is required for mutable bindless layouts.");
            }

            if (bindlessDesc.registerSpaces.size() > 0)
            {
                mContext.Error("Mutable descriptor sets cannot specify register spaces");
            }

            vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding = vk::DescriptorSetLayoutBinding()
                .setBinding(bindingPoint)
                .setDescriptorCount(arraySize)
                .setDescriptorType(vk::DescriptorType::eMutableEXT)
                .setStageFlags(shaderStageFlags);

            vulkanLayoutBindings.push_back(descriptorSetLayoutBinding);
        }
        else
        {
            // iterate over all binding types and add to map
            for (const ZWBindingLayoutItem& space : bindlessDesc.registerSpaces)
            {
                vk::DescriptorType const descriptorType = ConvertResourceType(space.type);
                
                if (space.type == EResourceType::VolatileConstantBuffer)
                    mContext.Error("Volatile constant buffers are not supported in bindless layouts");

                vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding = vk::DescriptorSetLayoutBinding()
                    .setBinding(bindingPoint)
                    .setDescriptorCount(arraySize)
                    .setDescriptorType(descriptorType)
                    .setStageFlags(shaderStageFlags);

                vulkanLayoutBindings.push_back(descriptorSetLayoutBinding);

                ++bindingPoint;
            }
        }
    }

    HCommon::ZWObject ZWVKBindingLayout::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case HRHIObjectTypes::gVKDescriptorSetLayout:
            return HCommon::ZWObject(descriptorSetLayout);
        default:
            return nullptr;
        }
    }

    vk::Result ZWVKBindingLayout::Bake()
    {
        // create the descriptor set layout object
        
        auto descriptorSetLayoutInfo = vk::DescriptorSetLayoutCreateInfo()
            .setBindingCount(uint32_t(vulkanLayoutBindings.size()))
            .setPBindings(vulkanLayoutBindings.data());

        std::vector<vk::DescriptorBindingFlags> bindFlag(vulkanLayoutBindings.size(), vk::DescriptorBindingFlagBits::ePartiallyBound);

        auto extendedInfo = vk::DescriptorSetLayoutBindingFlagsCreateInfo()
            .setBindingCount(uint32_t(vulkanLayoutBindings.size()))
            .setPBindingFlags(bindFlag.data());

        vk::DescriptorType cbvSrvUavTypes[] =
        {
            vk::DescriptorType::eSampledImage,
            vk::DescriptorType::eStorageImage,
            vk::DescriptorType::eUniformTexelBuffer,
            vk::DescriptorType::eStorageTexelBuffer,
            vk::DescriptorType::eUniformBuffer,
            vk::DescriptorType::eStorageBuffer
        };

        vk::DescriptorType counterTypes[] =
        {
            vk::DescriptorType::eStorageBuffer
        };

        vk::DescriptorType samplerTypes[] =
        {
            vk::DescriptorType::eSampler
        };

        auto cbvSrvUavTypesList = vk::MutableDescriptorTypeListEXT()
            .setDescriptorTypeCount(uint32_t(sizeof(cbvSrvUavTypes) / sizeof(cbvSrvUavTypes[0])))
            .setPDescriptorTypes(cbvSrvUavTypes);

        auto counterTypesList = vk::MutableDescriptorTypeListEXT()
            .setDescriptorTypeCount(uint32_t(sizeof(counterTypes) / sizeof(counterTypes[0])))
            .setPDescriptorTypes(counterTypes);

        auto samplerTypesList = vk::MutableDescriptorTypeListEXT()
            .setDescriptorTypeCount(uint32_t(sizeof(samplerTypes) / sizeof(samplerTypes[0])))
            .setPDescriptorTypes(samplerTypes);

        auto pMutableDescriptorTypeLists =
            bindlessDesc.layoutType == ZWBindlessLayoutDesc::ELayoutType::MutableCounters ? &counterTypesList :
            bindlessDesc.layoutType == ZWBindlessLayoutDesc::ELayoutType::MutableSampler ? &samplerTypesList :
            &cbvSrvUavTypesList;

        auto mutableDescriptorTypeCreateInfo = vk::MutableDescriptorTypeCreateInfoEXT()
            .setMutableDescriptorTypeListCount(1)
            .setPMutableDescriptorTypeLists(pMutableDescriptorTypeLists)
            .setPNext(&extendedInfo);

        if (isBindless)
        {
            if (bindlessDesc.layoutType != ZWBindlessLayoutDesc::ELayoutType::Immutable)
            {
                descriptorSetLayoutInfo.setPNext(&mutableDescriptorTypeCreateInfo);
            }
            else
            {
                descriptorSetLayoutInfo.setPNext(&extendedInfo);
            }
        }

        const vk::Result res = mContext.device.createDescriptorSetLayout(&descriptorSetLayoutInfo,
                                                                        mContext.allocationCallbacks,
                                                                        &descriptorSetLayout);
        CHECK_VK_RETURN(res)

        // count the number of descriptors required per type
        std::unordered_map<vk::DescriptorType, uint32_t> poolSizeMap;
        for (auto layoutBinding : vulkanLayoutBindings)
        {
            if (poolSizeMap.find(layoutBinding.descriptorType) == poolSizeMap.end())
            {
                poolSizeMap[layoutBinding.descriptorType] = 0;
            }

            poolSizeMap[layoutBinding.descriptorType] += layoutBinding.descriptorCount;
        }

        // compute descriptor pool size info
        for (auto poolSizeIter : poolSizeMap)
        {
            if (poolSizeIter.second > 0)
            {
                descriptorPoolSizeInfo.push_back(vk::DescriptorPoolSize()
                    .setType(poolSizeIter.first)
                    .setDescriptorCount(poolSizeIter.second));
            }
        }

        return vk::Result::eSuccess;
    }

    ZWVKBindingLayout::~ZWVKBindingLayout()
    {
        if (descriptorSetLayout)
        {
            mContext.device.destroyDescriptorSetLayout(descriptorSetLayout, mContext.allocationCallbacks);
            descriptorSetLayout = vk::DescriptorSetLayout();
        }
    }

    static ZWVKTexture::ETextureSubresourceViewType GetTextureViewType(EFormat bindingFormat, EFormat textureFormat)
    {
        EFormat format = (bindingFormat == EFormat::UNKNOWN) ? textureFormat : bindingFormat;

        const ZWFormatInfo& formatInfo = GetFormatInfo(format);

        if (formatInfo.hasDepth)
            return ZWVKTexture::ETextureSubresourceViewType::DepthOnly;
        else if (formatInfo.hasStencil)
            return ZWVKTexture::ETextureSubresourceViewType::StencilOnly;
        else
            return ZWVKTexture::ETextureSubresourceViewType::AllAspects;
    }

    ZWBindingSetHandle ZWVKDevice::CreateBindingSet(const ZWBindingSetDesc& desc, IBindingLayout* _layout)
    {
        ZWVKBindingLayout* layout = static_cast<ZWVKBindingLayout*>(_layout);

        ZWVKBindingSet *ret = new ZWVKBindingSet(mContext);
        ret->desc = desc;
        ret->layout = layout;

        const auto& descriptorSetLayout = layout->descriptorSetLayout;
        const auto& poolSizes = layout->descriptorPoolSizeInfo;

        // create descriptor pool to allocate a descriptor from
        auto poolInfo = vk::DescriptorPoolCreateInfo()
            .setPoolSizeCount(uint32_t(poolSizes.size()))
            .setPPoolSizes(poolSizes.data())
            .setMaxSets(1);

        vk::Result res = mContext.device.createDescriptorPool(&poolInfo,
                                                             mContext.allocationCallbacks,
                                                             &ret->descriptorPool);
        CHECK_VK_FAIL(res)
        
        // create the descriptor set
        auto descriptorSetAllocInfo = vk::DescriptorSetAllocateInfo()
            .setDescriptorPool(ret->descriptorPool)
            .setDescriptorSetCount(1)
            .setPSetLayouts(&descriptorSetLayout);

        res = mContext.device.allocateDescriptorSets(&descriptorSetAllocInfo,
            &ret->descriptorSet);
        CHECK_VK_FAIL(res)
        
        // collect all of the descriptor write data
        std::vector<vk::DescriptorImageInfo> descriptorImageInfo;
        std::vector<vk::DescriptorBufferInfo> descriptorBufferInfo;
        std::vector<vk::WriteDescriptorSet> descriptorWriteInfo;
        std::vector<vk::WriteDescriptorSetAccelerationStructureKHR> accelStructWriteInfo;
        descriptorImageInfo.reserve(desc.bindings.size());
        descriptorBufferInfo.reserve(desc.bindings.size());
        descriptorWriteInfo.reserve(desc.bindings.size());
        accelStructWriteInfo.reserve(desc.bindings.size());

        auto generateWriteDescriptorData =
            // generates a vk::WriteDescriptorSet struct in descriptorWriteInfo
            [&](uint32_t bindingLocation,
                uint32_t arrayElement,
                vk::DescriptorType descriptorType,
                vk::DescriptorImageInfo *imageInfo,
                vk::DescriptorBufferInfo *bufferInfo,
                vk::BufferView *bufferView,
                const void* pNext = nullptr)
        {
            descriptorWriteInfo.push_back(
                vk::WriteDescriptorSet()
                .setDstSet(ret->descriptorSet)
                .setDstBinding(bindingLocation)
                .setDstArrayElement(arrayElement)
                .setDescriptorCount(1)
                .setDescriptorType(descriptorType)
                .setPImageInfo(imageInfo)
                .setPBufferInfo(bufferInfo)
                .setPTexelBufferView(bufferView)
                .setPNext(pNext)
            );
        };

        for (size_t bindingIndex = 0; bindingIndex < desc.bindings.size(); bindingIndex++)
        {
            const ZWBindingSetItem& binding = desc.bindings[bindingIndex];

            if (binding.resourceHandle == nullptr)
            {
                continue;
            }

            ret->resources.push_back(binding.resourceHandle); // keep a strong reference to the resource

            vk::DescriptorType const descriptorType = ConvertResourceType(binding.type);
            uint32_t const registerOffset = GetRegisterOffsetForResourceType(layout->desc.bindingOffsets, binding.type);
            
            switch (binding.type)
            {
            case EResourceType::Texture_SRV:
            {
                const auto texture = static_cast<ZWVKTexture *>(binding.resourceHandle);

                const auto subresource = binding.subresources.resolve(texture->desc, false);
                const auto textureViewType = GetTextureViewType(binding.format, texture->desc.format);
                auto& view = texture->GetSubresourceView(subresource, binding.dimension, binding.format, vk::ImageUsageFlagBits::eSampled, textureViewType);

                auto& imageInfo = descriptorImageInfo.emplace_back();
                imageInfo = vk::DescriptorImageInfo()
                    .setImageView(view.view)
                    .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

                generateWriteDescriptorData(
                    registerOffset + binding.slot,
                    binding.arrayElement,
                    descriptorType,
                    &imageInfo, nullptr, nullptr);

                if (texture->permanentState == EResourceStates::Unknown)
                    ret->bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                else
                    VerifyPermanentResourceState(texture->permanentState,
                        EResourceStates::ShaderResource,
                        true, texture->desc.debugName, mContext.messageCallback);
            }

            break;

            case EResourceType::Texture_UAV:
            {
                const auto texture = static_cast<ZWVKTexture *>(binding.resourceHandle);

                const auto subresource = binding.subresources.resolve(texture->desc, true);
                const auto textureViewType = GetTextureViewType(binding.format, texture->desc.format);
                auto& view = texture->GetSubresourceView(subresource, binding.dimension, binding.format, vk::ImageUsageFlagBits::eStorage, textureViewType);

                auto& imageInfo = descriptorImageInfo.emplace_back();
                imageInfo = vk::DescriptorImageInfo()
                    .setImageView(view.view)
                    .setImageLayout(vk::ImageLayout::eGeneral);

                generateWriteDescriptorData(
                    registerOffset + binding.slot,
                    binding.arrayElement,
                    descriptorType,
                    &imageInfo, nullptr, nullptr);

                if (texture->permanentState == EResourceStates::Unknown)
                    ret->bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                else
                    VerifyPermanentResourceState(texture->permanentState,
                        EResourceStates::UnorderedAccess,
                        true, texture->desc.debugName, mContext.messageCallback);
            }

            break;

            case EResourceType::TypedBuffer_SRV:
            case EResourceType::TypedBuffer_UAV:
            {
                auto* buffer = static_cast<ZWVKBuffer *>(binding.resourceHandle);

                assert(buffer->desc.canHaveTypedViews);
                const bool isUAV = (binding.type == EResourceType::TypedBuffer_UAV);
                if (isUAV)
                    assert(buffer->desc.canHaveUAVs);

                EFormat format = binding.format;

                if (format == EFormat::UNKNOWN)
                {
                    format = buffer->desc.format;
                }

                auto vkformat = ConvertFormat(format);
                const auto range = binding.range.resolve(buffer->desc);

                size_t viewInfoHash = 0;
                HRHI::hash_combine(viewInfoHash, range.byteOffset);
                HRHI::hash_combine(viewInfoHash, range.byteSize);
                HRHI::hash_combine(viewInfoHash, (uint64_t)vkformat);

                const bool bufferViewExists = (buffer->viewCache.find(viewInfoHash) != buffer->viewCache.end());
                vk::BufferView& bufferViewRef = buffer->viewCache[viewInfoHash];
                if (!bufferViewExists)
                {
                    assert(format != EFormat::UNKNOWN);

                    auto bufferViewInfo = vk::BufferViewCreateInfo()
                        .setBuffer(buffer->buffer)
                        .setOffset(range.byteOffset)
                        .setRange(range.byteSize)
                        .setFormat(vk::Format(vkformat));

                    res = mContext.device.createBufferView(&bufferViewInfo, mContext.allocationCallbacks, &bufferViewRef);
                    ASSERT_VK_OK(res);
                }

                generateWriteDescriptorData(
                    registerOffset + binding.slot,
                    binding.arrayElement,
                    descriptorType,
                    nullptr, nullptr, &bufferViewRef);

                if (buffer->permanentState == EResourceStates::Unknown)
                    ret->bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                else
                    VerifyPermanentResourceState(buffer->permanentState, 
                        isUAV ? EResourceStates::UnorderedAccess : EResourceStates::ShaderResource,
                        false, buffer->desc.debugName, mContext.messageCallback);
            }
            break;

            case EResourceType::StructuredBuffer_SRV:
            case EResourceType::StructuredBuffer_UAV:
            case EResourceType::RawBuffer_SRV:
            case EResourceType::RawBuffer_UAV:
            case EResourceType::ConstantBuffer:
            case EResourceType::VolatileConstantBuffer:
            {
                const auto buffer = static_cast<ZWVKBuffer *>(binding.resourceHandle);

                if (binding.type == EResourceType::StructuredBuffer_UAV || binding.type == EResourceType::RawBuffer_UAV)
                    assert(buffer->desc.canHaveUAVs && (buffer->desc.bufferMode == EBufferMode::Structured || buffer->desc.bufferMode == EBufferMode::Raw));
                if (binding.type == EResourceType::StructuredBuffer_UAV || binding.type == EResourceType::StructuredBuffer_SRV)
                    assert(buffer->desc.elementStride != 0 && buffer->desc.bufferMode == EBufferMode::Structured);
                if (binding.type == EResourceType::RawBuffer_SRV|| binding.type == EResourceType::RawBuffer_UAV)
                    assert(buffer->desc.canHaveRawViews && buffer->desc.bufferMode == EBufferMode::Raw);

                const auto range = binding.range.resolve(buffer->desc);

                auto& bufferInfo = descriptorBufferInfo.emplace_back();
                bufferInfo = vk::DescriptorBufferInfo()
                    .setBuffer(buffer->buffer)
                    .setOffset(range.byteOffset)
                    .setRange(range.byteSize);

                assert(buffer->buffer);
                generateWriteDescriptorData(
                    registerOffset + binding.slot,
                    binding.arrayElement,
                    descriptorType,
                    nullptr, &bufferInfo, nullptr);

                if (binding.type == EResourceType::VolatileConstantBuffer) 
                {
                    assert(buffer->desc.isVolatile);
                    ret->volatileConstantBuffers.push_back(buffer);
                }
                else
                {
                    if (buffer->permanentState == EResourceStates::Unknown)
                        ret->bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                    else
                    {
                        EResourceStates requiredState;
                        if (binding.type == EResourceType::StructuredBuffer_UAV || binding.type == EResourceType::RawBuffer_UAV)
                            requiredState = EResourceStates::UnorderedAccess;
                        else if (binding.type == EResourceType::ConstantBuffer)
                            requiredState = EResourceStates::ConstantBuffer;
                        else
                            requiredState = EResourceStates::ShaderResource;

                        VerifyPermanentResourceState(buffer->permanentState, requiredState,
                            false, buffer->desc.debugName, mContext.messageCallback);
                    }
                }
            }

            break;

            case EResourceType::Sampler:
            {
                const auto sampler = static_cast<ZWVKSampler *>(binding.resourceHandle);

                auto& imageInfo = descriptorImageInfo.emplace_back();
                imageInfo = vk::DescriptorImageInfo()
                    .setSampler(sampler->sampler);

                generateWriteDescriptorData(
                    registerOffset + binding.slot,
                    binding.arrayElement,
                    descriptorType,
                    &imageInfo, nullptr, nullptr);
            }

            break;

            case EResourceType::RayTracingAccelStruct:
            {
                const auto as = static_cast<ZWVKAccelStruct*>(binding.resourceHandle);

                auto& accelStructWrite = accelStructWriteInfo.emplace_back();
                accelStructWrite.accelerationStructureCount = 1;
                accelStructWrite.pAccelerationStructures = &as->accelStruct;

                generateWriteDescriptorData(
                    registerOffset + binding.slot,
                    binding.arrayElement,
                    descriptorType,
                    nullptr, nullptr, nullptr, &accelStructWrite);

                ret->bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
            }

            break;

            case EResourceType::PushConstants:
                break;

            case EResourceType::None:
            case EResourceType::Count:
            default:
                assert(false);
                break;
            }

            // Update the hasUavBindings flag, it's cleaner to do it as a separate switch.
            switch (binding.type)
            {
            case EResourceType::Texture_UAV:
            case EResourceType::TypedBuffer_UAV:
            case EResourceType::StructuredBuffer_UAV:
            case EResourceType::RawBuffer_UAV:
            case EResourceType::SamplerFeedbackTexture_UAV:
                ret->hasUavBindings = true;
                break;
            default:
                break;
            }

        }

        mContext.device.updateDescriptorSets(uint32_t(descriptorWriteInfo.size()), descriptorWriteInfo.data(), 0, nullptr);

        return ZWBindingSetHandle::Create(ret);
    }

    ZWVKBindingSet::~ZWVKBindingSet()
    {
        if (descriptorPool)
        {
            mContext.device.destroyDescriptorPool(descriptorPool, mContext.allocationCallbacks);
            descriptorPool = vk::DescriptorPool();
            descriptorSet = vk::DescriptorSet();
        }
    }

    HCommon::ZWObject ZWVKBindingSet::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case HRHIObjectTypes::gVKDescriptorPool:
            return HCommon::ZWObject(descriptorPool);
        case HRHIObjectTypes::gVKDescriptorSet:
            return HCommon::ZWObject(descriptorSet);
        default:
            return nullptr;
        }
    }

    ZWDescriptorTableHandle ZWVKDevice::CreateDescriptorTable(IBindingLayout* _layout)
    { 
        ZWVKBindingLayout* layout = static_cast<ZWVKBindingLayout*>(_layout);

        ZWVKDescriptorTable* ret = new ZWVKDescriptorTable(mContext);
        ret->layout = layout;
        ret->capacity = layout->vulkanLayoutBindings[0].descriptorCount;

        const auto& descriptorSetLayout = layout->descriptorSetLayout;
        const auto& poolSizes = layout->descriptorPoolSizeInfo;

        // create descriptor pool to allocate a descriptor from
        auto poolInfo = vk::DescriptorPoolCreateInfo()
            .setPoolSizeCount(uint32_t(poolSizes.size()))
            .setPPoolSizes(poolSizes.data())
            .setMaxSets(1);

        vk::Result res = mContext.device.createDescriptorPool(&poolInfo,
                                                             mContext.allocationCallbacks,
                                                             &ret->descriptorPool);
        CHECK_VK_FAIL(res)

        // create the descriptor set
        auto descriptorSetAllocInfo = vk::DescriptorSetAllocateInfo()
            .setDescriptorPool(ret->descriptorPool)
            .setDescriptorSetCount(1)
            .setPSetLayouts(&descriptorSetLayout);

        res = mContext.device.allocateDescriptorSets(&descriptorSetAllocInfo,
            &ret->descriptorSet);
        CHECK_VK_FAIL(res)

        return ZWDescriptorTableHandle::Create(ret);
    }

    ZWVKDescriptorTable::~ZWVKDescriptorTable()
    {
        if (descriptorPool)
        {
            mContext.device.destroyDescriptorPool(descriptorPool, mContext.allocationCallbacks);
            descriptorPool = vk::DescriptorPool();
            descriptorSet = vk::DescriptorSet();
        }
    }

    HCommon::ZWObject ZWVKDescriptorTable::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case HRHIObjectTypes::gVKDescriptorPool:
            return HCommon::ZWObject(descriptorPool);
        case HRHIObjectTypes::gVKDescriptorSet:
            return HCommon::ZWObject(descriptorSet);
        default:
            return nullptr;
        }
    }

    void ZWVKDevice::ResizeDescriptorTable(IDescriptorTable* _descriptorTable, uint32_t newSize, bool keepContents)
    {
        assert(newSize <= static_cast<ZWVKDescriptorTable*>(_descriptorTable)->layout->GetBindlessDesc()->maxCapacity);
        (void)_descriptorTable;
        (void)newSize;
        (void)keepContents;
    }

    bool ZWVKDevice::WriteDescriptorTable(IDescriptorTable* _descriptorTable, const ZWBindingSetItem& binding)
    {
        ZWVKDescriptorTable* descriptorTable = static_cast<ZWVKDescriptorTable*>(_descriptorTable);
        ZWVKBindingLayout* layout = static_cast<ZWVKBindingLayout*>(descriptorTable->layout.Get());

        if (binding.slot >= descriptorTable->capacity)
            return false;

        if (binding.type == EResourceType::None)
        {
            // Vulkan doesn't support null descriptors, we use vk::DescriptorBindingFlagBits::ePartiallyBound
            return true;
        }

        // collect all of the descriptor write data
        HCommon::StaticVector<vk::DescriptorImageInfo, gMaxBindlessRegisterSpaces> descriptorImageInfo;
        HCommon::StaticVector<vk::DescriptorBufferInfo, gMaxBindlessRegisterSpaces> descriptorBufferInfo;
        HCommon::StaticVector<vk::WriteDescriptorSet, gMaxBindlessRegisterSpaces> descriptorWriteInfo;

        auto generateWriteDescriptorData =
            // generates a vk::WriteDescriptorSet struct in descriptorWriteInfo
            [&](uint32_t bindingLocation,
                vk::DescriptorType descriptorType,
                vk::DescriptorImageInfo* imageInfo,
                vk::DescriptorBufferInfo* bufferInfo,
                vk::BufferView* bufferView)
        {
            descriptorWriteInfo.push_back(
                vk::WriteDescriptorSet()
                .setDstSet(descriptorTable->descriptorSet)
                .setDstBinding(bindingLocation)
                .setDstArrayElement(binding.slot)
                .setDescriptorCount(1)
                .setDescriptorType(descriptorType)
                .setPImageInfo(imageInfo)
                .setPBufferInfo(bufferInfo)
                .setPTexelBufferView(bufferView)
            );
        };

        auto writeDescriptorForBinding = [&](const vk::DescriptorSetLayoutBinding& layoutBinding) -> void
        {
            switch (binding.type)
            {
            case EResourceType::Texture_SRV:
            {
                const auto& texture = static_cast<ZWVKTexture*>(binding.resourceHandle);

                const auto subresource = binding.subresources.resolve(texture->desc, false);
                const auto textureViewType = GetTextureViewType(binding.format, texture->desc.format);
                auto& view = texture->GetSubresourceView(subresource, binding.dimension, binding.format, vk::ImageUsageFlagBits::eSampled, textureViewType);

                auto& imageInfo = descriptorImageInfo.emplace_back();
                imageInfo = vk::DescriptorImageInfo()
                    .setImageView(view.view)
                    .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

                generateWriteDescriptorData(layoutBinding.binding,
                    ConvertResourceType(binding.type),
                    &imageInfo, nullptr, nullptr);
            }
            break;

            case EResourceType::Texture_UAV:
            {
                const auto texture = static_cast<ZWVKTexture*>(binding.resourceHandle);

                const auto subresource = binding.subresources.resolve(texture->desc, true);
                const auto textureViewType = GetTextureViewType(binding.format, texture->desc.format);
                auto& view = texture->GetSubresourceView(subresource, binding.dimension, binding.format, vk::ImageUsageFlagBits::eStorage, textureViewType);

                auto& imageInfo = descriptorImageInfo.emplace_back();
                imageInfo = vk::DescriptorImageInfo()
                    .setImageView(view.view)
                    .setImageLayout(vk::ImageLayout::eGeneral);

                generateWriteDescriptorData(layoutBinding.binding,
                    ConvertResourceType(binding.type),
                    &imageInfo, nullptr, nullptr);
            }
            break;

            case EResourceType::TypedBuffer_SRV:
            case EResourceType::TypedBuffer_UAV:
            {
                auto* buffer = static_cast<ZWVKBuffer*>(binding.resourceHandle);

                auto vkformat = ConvertFormat(binding.format);

                const auto range = binding.range.resolve(buffer->desc);
                size_t viewInfoHash = 0;
                HRHI::hash_combine(viewInfoHash, range.byteOffset);
                HRHI::hash_combine(viewInfoHash, range.byteSize);
                HRHI::hash_combine(viewInfoHash, (uint64_t)vkformat);

                const bool bufferViewExists = (buffer->viewCache.find(viewInfoHash) != buffer->viewCache.end());
                vk::BufferView& bufferViewRef = buffer->viewCache[viewInfoHash];
                if (!bufferViewExists)
                {
                    assert(binding.format != EFormat::UNKNOWN);

                    auto bufferViewInfo = vk::BufferViewCreateInfo()
                        .setBuffer(buffer->buffer)
                        .setOffset(range.byteOffset)
                        .setRange(range.byteSize)
                        .setFormat(vk::Format(vkformat));

                    vk::Result res = mContext.device.createBufferView(&bufferViewInfo, mContext.allocationCallbacks, &bufferViewRef);
                    ASSERT_VK_OK(res);
                }

                generateWriteDescriptorData(layoutBinding.binding,
                    ConvertResourceType(binding.type),
                    nullptr, nullptr, &bufferViewRef);
            }
            break;

            case EResourceType::StructuredBuffer_SRV:
            case EResourceType::StructuredBuffer_UAV:
            case EResourceType::RawBuffer_SRV:
            case EResourceType::RawBuffer_UAV:
            case EResourceType::ConstantBuffer:
            case EResourceType::VolatileConstantBuffer:
            {
                const auto buffer = static_cast<ZWVKBuffer*>(binding.resourceHandle);

                const auto range = binding.range.resolve(buffer->desc);

                auto& bufferInfo = descriptorBufferInfo.emplace_back();
                bufferInfo = vk::DescriptorBufferInfo()
                    .setBuffer(buffer->buffer)
                    .setOffset(range.byteOffset)
                    .setRange(range.byteSize);

                assert(buffer->buffer);
                generateWriteDescriptorData(layoutBinding.binding,
                    ConvertResourceType(binding.type),
                    nullptr, &bufferInfo, nullptr);
            }
            break;

            case EResourceType::Sampler:
            {
                const auto& sampler = static_cast<ZWVKSampler*>(binding.resourceHandle);

                auto& imageInfo = descriptorImageInfo.emplace_back();
                imageInfo = vk::DescriptorImageInfo()
                    .setSampler(sampler->sampler);

                generateWriteDescriptorData(layoutBinding.binding,
                    ConvertResourceType(binding.type),
                    &imageInfo, nullptr, nullptr);
            }
            break;

            case EResourceType::RayTracingAccelStruct:
                assert(false);
                return;

            case EResourceType::PushConstants:
                assert(false);
                return;

            case EResourceType::None:
            case EResourceType::Count:
            default:
                assert(false);
            }
        };

        if (layout->bindlessDesc.layoutType != ZWBindlessLayoutDesc::ELayoutType::Immutable)
        {
            // For mutable descriptor sets, there are no register spaces, so always use the first layout binding
            assert(layout->vulkanLayoutBindings.size() > 0);
            writeDescriptorForBinding(layout->vulkanLayoutBindings[0]);
        }
        else
        {
            // For regular bindless layouts, iterate through register spaces to find matching binding type
            for (uint32_t bindingLocation = 0; bindingLocation < uint32_t(layout->bindlessDesc.registerSpaces.size()); bindingLocation++)
            {
                if (layout->bindlessDesc.registerSpaces[bindingLocation].type == binding.type)
                {
                    const vk::DescriptorSetLayoutBinding& layoutBinding = layout->vulkanLayoutBindings[bindingLocation];
                    writeDescriptorForBinding(layoutBinding);
                }
            }
        }

        mContext.device.updateDescriptorSets(uint32_t(descriptorWriteInfo.size()), descriptorWriteInfo.data(), 0, nullptr);

        return true;
    }

    void ZWVKCommandList::BindBindingSets(vk::PipelineBindPoint bindPoint, vk::PipelineLayout pipelineLayout, const BindingSetVector& bindings, ZWVKBindingVector<uint32_t> const& descriptorSetIdxToBindingIdx)
    {
        const uint32_t numBindings = (uint32_t)bindings.size();
        const uint32_t numDescriptorSets = descriptorSetIdxToBindingIdx.empty() ? numBindings : (uint32_t)descriptorSetIdxToBindingIdx.size();

        ZWVKBindingVector<vk::DescriptorSet> descriptorSets;
        uint32_t nextDescriptorSetToBind = 0;
        std::vector<uint32_t> dynamicOffsets;
        for (uint32_t i = 0; i < numDescriptorSets; ++i)
        {
            IBindingSet* bindingSetHandle = nullptr;
            if (descriptorSetIdxToBindingIdx.empty())
            {
                bindingSetHandle = bindings[i];
            }
            else if(descriptorSetIdxToBindingIdx[i] != 0xffffffff)
            {
                bindingSetHandle = bindings[descriptorSetIdxToBindingIdx[i]];
            }

            if (bindingSetHandle == nullptr)
            {
                // This is a hole in the descriptor sets, so bind the contiguous descriptor sets we've got so far
                if (!descriptorSets.empty())
                {
                    mCurrentCmdBuf->cmdBuf.bindDescriptorSets(bindPoint, pipelineLayout,
                        /* firstSet = */ nextDescriptorSetToBind, uint32_t(descriptorSets.size()), descriptorSets.data(),
                        uint32_t(dynamicOffsets.size()), dynamicOffsets.data());

                    descriptorSets.resize(0);
                    dynamicOffsets.resize(0);
                }
                nextDescriptorSetToBind = i + 1;
            }
            else
            {
                const ZWBindingSetDesc* desc = bindingSetHandle->GetDesc();
                if (desc)
                {
                    ZWVKBindingSet* bindingSet = static_cast<ZWVKBindingSet*>(bindingSetHandle);
                    descriptorSets.push_back(bindingSet->descriptorSet);

                    for (ZWVKBuffer* constantBuffer : bindingSet->volatileConstantBuffers)
                    {
                        auto found = mVolatileBufferStates.find(constantBuffer);
                        if (found == mVolatileBufferStates.end())
                        {
                            std::stringstream ss;
                            ss << "Binding volatile constant buffer " << HApp::DebugNameToString(constantBuffer->desc.debugName)
                                << " before writing into it is invalid.";
                            mContext.Error(ss.str());

                            dynamicOffsets.push_back(0); // use zero offset just to use something
                        }
                        else
                        {
                            uint32_t version = found->second.latestVersion;
                            uint64_t offset = version * constantBuffer->desc.byteSize;
                            assert(offset < std::numeric_limits<uint32_t>::max());
                            dynamicOffsets.push_back(uint32_t(offset));
                        }
                    }

                    if (desc->trackLiveness)
                        mCurrentCmdBuf->referencedResources.push_back(bindingSetHandle);
                }
                else
                {
                    ZWVKDescriptorTable* table = static_cast<ZWVKDescriptorTable*>(bindingSetHandle);
                    descriptorSets.push_back(table->descriptorSet);
                }
            }
        }
        if (!descriptorSets.empty())
        {
            // Bind the remaining sets
            mCurrentCmdBuf->cmdBuf.bindDescriptorSets(bindPoint, pipelineLayout,
                /* firstSet = */ nextDescriptorSetToBind, uint32_t(descriptorSets.size()), descriptorSets.data(),
                uint32_t(dynamicOffsets.size()), dynamicOffsets.data());
        }
    }

    vk::Result CreatePipelineLayout(
        vk::PipelineLayout& outPipelineLayout,
        ZWVKBindingVector<HCommon::RefCountPtr<ZWVKBindingLayout>>& outBindingLayouts,
        vk::ShaderStageFlags& outPushConstantVisibility,
        ZWVKBindingVector<uint32_t>& outDescriptorSetIdxToBindingIdx,
        ZWVKContext const& context,
        BindingLayoutVector const& inBindingLayouts)
    {
        // Establish if we're going to use outDescriptorSetIdxToBindingIdx
        // We do this if the layout descs specify registerSpaceIsDescriptorSet
        // (Validation ensures all the binding layouts have it set to the same value)
        bool createDescriptorSetIdxToBindingIdx = false;
        for (ZWBindingLayoutHandle const& _layout : inBindingLayouts)
        {
            ZWVKBindingLayout const* layout = static_cast<ZWVKBindingLayout const*>(_layout.Get());
            if (!layout->isBindless)
            {
                createDescriptorSetIdxToBindingIdx = layout->GetDesc()->registerSpaceIsDescriptorSet;
                break;
            }
        }

        if (createDescriptorSetIdxToBindingIdx)
        {
            // Figure out how many descriptor sets we'll need in outBindingLayouts.
            // There's not necessarily a one-to-one relationship because there could potentially be
            // holes in binding layout.  E.g. if a binding layout uses register spaces 0 and 2
            // then we'll need to use 3 descriptor sets, with a hole at index 1 because Vulkan
            // descriptor set indices map to register spaces.
            // Bindless layouts are assumed to not need binding to specific descriptor set
            // indices, so we put those last
            uint32_t numRegularDescriptorSets = 0;
            for (ZWBindingLayoutHandle const& _layout : inBindingLayouts)
            {
                ZWVKBindingLayout const* layout = static_cast<ZWVKBindingLayout const*>(_layout.Get());
                if (!layout->isBindless)
                {
                    numRegularDescriptorSets = std::max(numRegularDescriptorSets, layout->GetDesc()->registerSpace + 1);
                }
            }

            // Now create the layout
            outBindingLayouts.resize(numRegularDescriptorSets);
            outDescriptorSetIdxToBindingIdx.resize(numRegularDescriptorSets);
            for (uint32_t i = 0; i < numRegularDescriptorSets; ++i)
            {
                outDescriptorSetIdxToBindingIdx[i] = 0xffffffff;
            }
            for (uint32_t i = 0; i < (uint32_t)inBindingLayouts.size(); ++i)
            {
                ZWVKBindingLayout* layout = static_cast<ZWVKBindingLayout*>(inBindingLayouts[i].Get());
                if (layout->isBindless)
                {
                    outBindingLayouts.push_back(layout);
                    // Let's always put the bindless ones at the end.
                    outDescriptorSetIdxToBindingIdx.push_back(i);
                }
                else
                {
                    uint32_t const descriptorSetIdx = layout->GetDesc()->registerSpace;
                    // Can't have multiple binding sets with the same registerSpace
                    // Should not have passed validation in validatePipelineBindingLayouts
                    assert(outBindingLayouts[descriptorSetIdx] == nullptr);
                    outBindingLayouts[descriptorSetIdx] = layout;
                    outDescriptorSetIdxToBindingIdx[descriptorSetIdx] = i;
                }
            }
        }
        else
        {
            // Legacy behaviour mode, where we don't fill in outDescriptorSetIdxToBindingIdx
            // In this mode, there can be no holes in the binding layout
            for (const ZWBindingLayoutHandle& _layout : inBindingLayouts)
            {
                ZWVKBindingLayout* layout = static_cast<ZWVKBindingLayout*>(_layout.Get());
                outBindingLayouts.push_back(layout);
            }
        }

        ZWVKBindingVector<vk::DescriptorSetLayout> descriptorSetLayouts;
        uint32_t pushConstantSize = 0;
        outPushConstantVisibility = vk::ShaderStageFlagBits();
        for (ZWVKBindingLayout const* layout : outBindingLayouts)
        {
            if (layout)
            {
                descriptorSetLayouts.push_back(layout->descriptorSetLayout);

                if (!layout->isBindless)
                {
                    for (const ZWBindingLayoutItem& item : layout->desc.bindings)
                    {
                        if (item.type == EResourceType::PushConstants)
                        {
                            pushConstantSize = item.size;
                            outPushConstantVisibility = ConvertShaderTypeToShaderStageFlagBits(layout->desc.visibility);
                            // assume there's only one push constant item in all layouts -- the validation layer makes sure of that
                            break;
                        }
                    }
                }
            }
            else
            {
                // Empty descriptor set
                descriptorSetLayouts.push_back(context.emptyDescriptorSetLayout);
            }
        }

        auto pushConstantRange = vk::PushConstantRange()
            .setOffset(0)
            .setSize(pushConstantSize)
            .setStageFlags(outPushConstantVisibility);

        auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
            .setSetLayoutCount(uint32_t(descriptorSetLayouts.size()))
            .setPSetLayouts(descriptorSetLayouts.data())
            .setPushConstantRangeCount(pushConstantSize ? 1 : 0)
            .setPPushConstantRanges(&pushConstantRange);

        vk::Result res = context.device.createPipelineLayout(&pipelineLayoutInfo,
            context.allocationCallbacks,
            &outPipelineLayout);

        return res;
    }

} // namespace HRHI
