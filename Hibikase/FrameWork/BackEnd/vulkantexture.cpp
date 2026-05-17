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

#include <algorithm>
#include <cstring>
#include <limits>

#include <BackEnd/vulkanbackend.h>
#include <Utils/stringtranslatehelper.h>

namespace HRHI
{

    static vk::ImageType textureDimensionToImageType(ETextureDimension dimension)
    {
        switch (dimension)
        {
        case ETextureDimension::Texture1D:
        case ETextureDimension::Texture1DArray:
            return vk::ImageType::e1D;

        case ETextureDimension::Texture2D:
        case ETextureDimension::Texture2DArray:
        case ETextureDimension::TextureCube:
        case ETextureDimension::TextureCubeArray:
        case ETextureDimension::Texture2DMS:
        case ETextureDimension::Texture2DMSArray:
            return vk::ImageType::e2D;

        case ETextureDimension::Texture3D:
            return vk::ImageType::e3D;

        case ETextureDimension::Unknown:
        default:
            assert(false);
            return vk::ImageType::e2D;
        }
    }
    
    
    static vk::ImageViewType textureDimensionToImageViewType(ETextureDimension dimension)
    {
        switch (dimension)
        {
        case ETextureDimension::Texture1D:
            return vk::ImageViewType::e1D;

        case ETextureDimension::Texture1DArray:
            return vk::ImageViewType::e1DArray;
            
        case ETextureDimension::Texture2D:
        case ETextureDimension::Texture2DMS:
            return vk::ImageViewType::e2D;

        case ETextureDimension::Texture2DArray:
        case ETextureDimension::Texture2DMSArray:
            return vk::ImageViewType::e2DArray;
            
        case ETextureDimension::TextureCube:
            return vk::ImageViewType::eCube;
            
        case ETextureDimension::TextureCubeArray:
            return vk::ImageViewType::eCubeArray;
            
        case ETextureDimension::Texture3D:
            return vk::ImageViewType::e3D;

        case ETextureDimension::Unknown:
        default:
            assert(false);
            return vk::ImageViewType::e2D;
        }
    }

    static vk::Extent3D pickImageExtent(const ZWTextureDesc& d)
    {
        return vk::Extent3D(d.width, d.height, d.depth);
    }

    static uint32_t pickImageLayers(const ZWTextureDesc& d)
    {
        return d.arraySize;
    }

    static vk::ImageUsageFlags pickImageUsage(const ZWTextureDesc& d)
    {
        const ZWFormatInfo& formatInfo = GetFormatInfo(d.format);
        
        vk::ImageUsageFlags ret = vk::ImageUsageFlagBits::eTransferSrc |
                                  vk::ImageUsageFlagBits::eTransferDst;
        
        if (d.isShaderResource)
            ret |= vk::ImageUsageFlagBits::eSampled;

        if (d.isRenderTarget)
        {
            if (formatInfo.hasDepth || formatInfo.hasStencil)
            {
                ret |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
            } else {
                ret |= vk::ImageUsageFlagBits::eColorAttachment;
            }
        }

        if (d.isUAV)
            ret |= vk::ImageUsageFlagBits::eStorage;

        if (d.isShadingRateSurface)
            ret |= vk::ImageUsageFlagBits::eFragmentShadingRateAttachmentKHR;

        return ret;
    }

    static vk::SampleCountFlagBits pickImageSampleCount(const ZWTextureDesc& d)
    {
        switch(d.sampleCount)
        {
            case 1:
                return vk::SampleCountFlagBits::e1;

            case 2:
                return vk::SampleCountFlagBits::e2;

            case 4:
                return vk::SampleCountFlagBits::e4;

            case 8:
                return vk::SampleCountFlagBits::e8;

            case 16:
                return vk::SampleCountFlagBits::e16;

            case 32:
                return vk::SampleCountFlagBits::e32;

            case 64:
                return vk::SampleCountFlagBits::e64;

            default:
                assert(false);
                return vk::SampleCountFlagBits::e1;
        }
    }

    // infer aspect flags for a given image format
    vk::ImageAspectFlags GuessImageAspectFlags(vk::Format format)
    {
        switch(format)  // NOLINT(clang-diagnostic-switch-enum)
        {
            case vk::Format::eD16Unorm:
            case vk::Format::eX8D24UnormPack32:
            case vk::Format::eD32Sfloat:
                return vk::ImageAspectFlagBits::eDepth;

            case vk::Format::eS8Uint:
                return vk::ImageAspectFlagBits::eStencil;

            case vk::Format::eD16UnormS8Uint:
            case vk::Format::eD24UnormS8Uint:
            case vk::Format::eD32SfloatS8Uint:
                return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;

            default:
                return vk::ImageAspectFlagBits::eColor;
        }
    }

    // a subresource usually shouldn't have both stencil and depth aspect flag bits set; this enforces that depending on viewType param
    static vk::ImageAspectFlags GuessSubresourceImageAspectFlags(vk::Format format, ZWVKTexture::ETextureSubresourceViewType viewType)
    {
        vk::ImageAspectFlags flags = GuessImageAspectFlags(format);
        if ((flags & (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil))
            == (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil))
        {
            if (viewType == ZWVKTexture::ETextureSubresourceViewType::DepthOnly)
            {
                flags = flags & (~vk::ImageAspectFlagBits::eStencil);
            }
            else if (viewType == ZWVKTexture::ETextureSubresourceViewType::StencilOnly)
            {
                flags = flags & (~vk::ImageAspectFlagBits::eDepth);
            }
        }
        return flags;
    }

    static vk::ImageCreateFlags pickImageFlags(const ZWTextureDesc& d)
    {
        vk::ImageCreateFlags flags = vk::ImageCreateFlags(0);

        if (d.dimension == ETextureDimension::TextureCube || 
            d.dimension == ETextureDimension::TextureCubeArray)
            flags |= vk::ImageCreateFlagBits::eCubeCompatible;

        if (d.isTypeless)
            flags |= vk::ImageCreateFlagBits::eMutableFormat | vk::ImageCreateFlagBits::eExtendedUsage;

        if (d.isTiled)
            flags |= vk::ImageCreateFlagBits::eSparseBinding | vk::ImageCreateFlagBits::eSparseResidency;

        return flags;
    }

    // fills out all info fields in Texture based on a TextureDesc
    static void fillTextureInfo(ZWVKTexture* texture, const ZWTextureDesc& desc)
    {
        texture->desc = desc;

        vk::ImageType type = textureDimensionToImageType(desc.dimension);
        vk::Extent3D extent = pickImageExtent(desc);
        uint32_t numLayers = pickImageLayers(desc);
        vk::Format format = vk::Format(ConvertFormat(desc.format));
        vk::ImageUsageFlags usage = pickImageUsage(desc);
        vk::SampleCountFlagBits sampleCount = pickImageSampleCount(desc);
        vk::ImageCreateFlags flags = pickImageFlags(desc);

        texture->imageInfo = vk::ImageCreateInfo()
                                .setImageType(type)
                                .setExtent(extent)
                                .setMipLevels(desc.mipLevels)
                                .setArrayLayers(numLayers)
                                .setFormat(format)
                                .setInitialLayout(vk::ImageLayout::eUndefined)
                                .setUsage(usage)
                                .setSharingMode(vk::SharingMode::eExclusive)
                                .setSamples(sampleCount)
                                .setFlags(flags);
        
#if _WIN32
        const auto handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
#else
        const auto handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
#endif

        texture->externalMemoryImageInfo = vk::ExternalMemoryImageCreateInfo()
            .setHandleTypes(handleType);

        if (desc.sharedResourceFlags == ESharedResourceFlags::Shared)
            texture->imageInfo.setPNext(&texture->externalMemoryImageInfo);
    }

    ZWVKTextureSubresourceView& ZWVKTexture::GetSubresourceView(const ZWTextureSubresourceSet& subresource, ETextureDimension dimension,
        EFormat format, vk::ImageUsageFlags usage, ETextureSubresourceViewType viewtype)
    {
        // This function is called from createBindingSet etc. and therefore free-threaded.
        // It modifies the subresourceViews map associated with the texture.
        std::lock_guard lockGuard(mMutex);

        if (dimension == ETextureDimension::Unknown)
            dimension = desc.dimension;

        if (format == EFormat::UNKNOWN)
            format = desc.format;

        // Only use VkImageViewUsageCreateInfo when the image is typeless, i.e. it was created
        // with the MUTABLE_FORMAT and EXTENDED_USAGE bits.
        if (!desc.isTypeless)
            usage = vk::ImageUsageFlags(0);

        auto cachekey = std::make_tuple(subresource, viewtype, dimension, format, usage);
        auto iter = subresourceViews.find(cachekey);
        if (iter != subresourceViews.end())
        {
            return iter->second;
        }

        auto iter_pair = subresourceViews.emplace(cachekey, *this);
        auto& view = std::get<0>(iter_pair)->second;

        view.subresource = subresource;

        auto vkFormat = ConvertFormat(format);

        vk::ImageAspectFlags aspectFlags = GuessSubresourceImageAspectFlags(vk::Format(vkFormat), viewtype);
        view.subresourceRange = vk::ImageSubresourceRange()
                                    .setAspectMask(aspectFlags)
                                    .setBaseMipLevel(subresource.baseMipLevel)
                                    .setLevelCount(subresource.numMipLevels)
                                    .setBaseArrayLayer(subresource.baseArraySlice)
                                    .setLayerCount(subresource.numArraySlices);

        vk::ImageViewType imageViewType = textureDimensionToImageViewType(dimension);

        auto viewInfo = vk::ImageViewCreateInfo()
                        .setImage(image)
                        .setViewType(imageViewType)
                        .setFormat(vk::Format(vkFormat))
                        .setSubresourceRange(view.subresourceRange);

        auto usageInfo = vk::ImageViewUsageCreateInfo()
                        .setUsage(usage);

        if (uint32_t(usage) != 0)
            viewInfo.setPNext(&usageInfo);

        if (viewtype == ETextureSubresourceViewType::StencilOnly)
        {
            // D3D / HLSL puts stencil values in the second component to keep the illusion of combined depth/stencil.
            // Set a component swizzle so we appear to do the same.
            viewInfo.components.setG(vk::ComponentSwizzle::eR);
        }

        const vk::Result res = mContext.device.createImageView(&viewInfo, mContext.allocationCallbacks, &view.view);
        ASSERT_VK_OK(res);

        const std::string debugName = std::string("ImageView for: ") + HApp::DebugNameToString(desc.debugName);
        mContext.NameVKObject(VkImageView(view.view), vk::ObjectType::eImageView, vk::DebugReportObjectTypeEXT::eImageView, debugName.c_str());

        return view;
    }

    ZWTextureHandle ZWVKDevice::CreateTexture(const ZWTextureDesc& desc)
    {
        ZWVKTexture* texture = new ZWVKTexture(mContext, mAllocator);
        assert(texture);
        fillTextureInfo(texture, desc);

        vk::Result res = mContext.device.createImage(&texture->imageInfo, mContext.allocationCallbacks, &texture->image);
        ASSERT_VK_OK(res);
        CHECK_VK_FAIL(res)

        mContext.NameVKObject(texture->image, vk::ObjectType::eImage, vk::DebugReportObjectTypeEXT::eImage, desc.debugName.c_str());

        if (!desc.isVirtual && !desc.isTiled)
        {
            res = mAllocator.AllocateTextureMemory(texture);
            ASSERT_VK_OK(res);
            CHECK_VK_FAIL(res)

            if ((desc.sharedResourceFlags & ESharedResourceFlags::Shared) != 0)
            {
#ifdef _WIN32
                texture->sharedHandle = mContext.device.getMemoryWin32HandleKHR({ texture->memory, vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32 });
#else
                texture->sharedHandle = (void*)(size_t)mContext.device.getMemoryFdKHR({ texture->memory, vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd });
#endif
            }

            mContext.NameVKObject(texture->memory, vk::ObjectType::eDeviceMemory, vk::DebugReportObjectTypeEXT::eDeviceMemory, desc.debugName.c_str());
        }

        return ZWTextureHandle::Create(texture);
    }

    ZWMemoryRequirements ZWVKDevice::GetTextureMemoryRequirements(ITexture* _texture)
    {
        ZWVKTexture* texture = static_cast<ZWVKTexture*>(_texture);

        vk::MemoryRequirements vulkanMemReq;
        mContext.device.getImageMemoryRequirements(texture->image, &vulkanMemReq);

        ZWMemoryRequirements memReq;
        memReq.alignment = vulkanMemReq.alignment;
        memReq.size = vulkanMemReq.size;
        return memReq;
    }

    bool ZWVKDevice::BindTextureMemory(ITexture* _texture, IHeap* _heap, uint64_t offset)
    {
        ZWVKTexture* texture = static_cast<ZWVKTexture*>(_texture);
        ZWVKHeap* heap = static_cast<ZWVKHeap*>(_heap);

        if (texture->heap)
            return false;

        if (!texture->desc.isVirtual)
            return false;

        mContext.device.bindImageMemory(texture->image, heap->memory, offset);

        texture->heap = heap;

        return true;
    }

    void ZWVKCommandList::CopyTexture(ITexture* _dst, const ZWTextureSlice& dstSlice,
                                  ITexture* _src, const ZWTextureSlice& srcSlice)
    {
        ZWVKTexture* dst = static_cast<ZWVKTexture*>(_dst);
        ZWVKTexture* src = static_cast<ZWVKTexture*>(_src);

        auto resolvedDstSlice = dstSlice.resolve(dst->desc);
        auto resolvedSrcSlice = srcSlice.resolve(src->desc);

        assert(mCurrentCmdBuf);

        mCurrentCmdBuf->referencedResources.push_back(dst);
        mCurrentCmdBuf->referencedResources.push_back(src);

        ZWTextureSubresourceSet srcSubresource = ZWTextureSubresourceSet(
            resolvedSrcSlice.mipLevel, 1,
            resolvedSrcSlice.arraySlice, 1
        );

        auto srcFormat = ConvertFormat(src->desc.format);
        vk::ImageAspectFlags srcAspectFlags = GuessSubresourceImageAspectFlags(vk::Format(srcFormat), ZWVKTexture::ETextureSubresourceViewType::AllAspects);

        ZWTextureSubresourceSet dstSubresource = ZWTextureSubresourceSet(
            resolvedDstSlice.mipLevel, 1,
            resolvedDstSlice.arraySlice, 1
        );

        auto dstFormat = ConvertFormat(dst->desc.format);
        vk::ImageAspectFlags dstAspectFlags = GuessSubresourceImageAspectFlags(vk::Format(dstFormat), ZWVKTexture::ETextureSubresourceViewType::AllAspects);


        // When copying between block-compressed and uint textures, the extents and offsets are scaled by the block size.
        // To simplify the logic here, assume that one of (src, dst) is compressed, therefore its extents are smaller, and use that.
        // See https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkCmdCopyImage.html
        const VkExtent3D extent = vk::Extent3D(
            std::min(resolvedSrcSlice.width, resolvedDstSlice.width),
            std::min(resolvedSrcSlice.height, resolvedDstSlice.height),
            std::min(resolvedSrcSlice.depth, resolvedDstSlice.depth));

        auto imageCopy = vk::ImageCopy()
                            .setSrcSubresource(vk::ImageSubresourceLayers()
                                                .setAspectMask(srcAspectFlags)
                                                .setMipLevel(srcSubresource.baseMipLevel)
                                                .setBaseArrayLayer(srcSubresource.baseArraySlice)
                                                .setLayerCount(srcSubresource.numArraySlices))
                            .setSrcOffset(vk::Offset3D(resolvedSrcSlice.x, resolvedSrcSlice.y, resolvedSrcSlice.z))
                            .setDstSubresource(vk::ImageSubresourceLayers()
                                                .setAspectMask(dstAspectFlags)
                                                .setMipLevel(dstSubresource.baseMipLevel)
                                                .setBaseArrayLayer(dstSubresource.baseArraySlice)
                                                .setLayerCount(dstSubresource.numArraySlices))
                            .setDstOffset(vk::Offset3D(resolvedDstSlice.x, resolvedDstSlice.y, resolvedDstSlice.z))
                            .setExtent(extent);
        
        if (m_EnableAutomaticBarriers)
        {
            RequireTextureState(src, ZWTextureSubresourceSet(resolvedSrcSlice.mipLevel, 1, resolvedSrcSlice.arraySlice, 1), EResourceStates::CopySource);
            RequireTextureState(dst, ZWTextureSubresourceSet(resolvedDstSlice.mipLevel, 1, resolvedDstSlice.arraySlice, 1), EResourceStates::CopyDest);
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        mCurrentCmdBuf->cmdBuf.copyImage(src->image, vk::ImageLayout::eTransferSrcOptimal,
                              dst->image, vk::ImageLayout::eTransferDstOptimal,
                              { imageCopy });
    }

    static void computeMipLevelInformation(const ZWTextureDesc& desc, uint32_t mipLevel, uint32_t* widthOut, uint32_t* heightOut, uint32_t* depthOut)
    {
        uint32_t width = std::max(desc.width >> mipLevel, uint32_t(1));
        uint32_t height = std::max(desc.height >> mipLevel, uint32_t(1));
        uint32_t depth = std::max(desc.depth >> mipLevel, uint32_t(1));

        if (widthOut)
            *widthOut = width;
        if (heightOut)
            *heightOut = height;
        if (depthOut)
            *depthOut = depth;
    }

    void ZWVKCommandList::WriteTexture(ITexture* _dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch)
    {
        EndRenderPass();

        ZWVKTexture* dest = static_cast<ZWVKTexture*>(_dest);

        ZWTextureDesc desc = dest->GetDesc();

        uint32_t mipWidth, mipHeight, mipDepth;
        computeMipLevelInformation(desc, mipLevel, &mipWidth, &mipHeight, &mipDepth);

        const ZWFormatInfo& formatInfo = GetFormatInfo(desc.format);
        uint32_t deviceNumCols = (mipWidth + formatInfo.blockSize - 1) / formatInfo.blockSize;
        uint32_t deviceNumRows = (mipHeight + formatInfo.blockSize - 1) / formatInfo.blockSize;
        uint32_t deviceRowPitch = deviceNumCols * formatInfo.bytesPerBlock;
        uint64_t deviceMemSize = uint64_t(deviceRowPitch) * uint64_t(deviceNumRows) * mipDepth;

        ZWVKBuffer* uploadBuffer;
        uint64_t uploadOffset;
        void* uploadCpuVA;
        m_UploadManager->SuballocateBuffer(
            deviceMemSize,
            &uploadBuffer,
            &uploadOffset,
            &uploadCpuVA,
            MakeVersion(mCurrentCmdBuf->recordingID, mCommandListParameters.queueType, false));

        size_t minRowPitch = std::min(size_t(deviceRowPitch), rowPitch);
        uint8_t* mappedPtr = (uint8_t*)uploadCpuVA;
        for (uint32_t slice = 0; slice < mipDepth; slice++)
        {
            const uint8_t* sourcePtr = (const uint8_t*)data + depthPitch * slice;
            for (uint32_t row = 0; row < deviceNumRows; row++)
            {
                memcpy(mappedPtr, sourcePtr, minRowPitch);
                mappedPtr += deviceRowPitch;
                sourcePtr += rowPitch;
            }
        }

        auto imageCopy = vk::BufferImageCopy()
            .setBufferOffset(uploadOffset)
            .setBufferRowLength(deviceNumCols * formatInfo.blockSize)
            .setBufferImageHeight(deviceNumRows * formatInfo.blockSize)
            .setImageSubresource(vk::ImageSubresourceLayers()
                .setAspectMask(GuessImageAspectFlags(dest->imageInfo.format))
                .setMipLevel(mipLevel)
                .setBaseArrayLayer(arraySlice)
                .setLayerCount(1))
            .setImageExtent(vk::Extent3D().setWidth(mipWidth).setHeight(mipHeight).setDepth(mipDepth));

        assert(mCurrentCmdBuf);

        if (m_EnableAutomaticBarriers)
        {
            RequireTextureState(dest, ZWTextureSubresourceSet(mipLevel, 1, arraySlice, 1), EResourceStates::CopyDest);
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        mCurrentCmdBuf->referencedResources.push_back(dest);

        mCurrentCmdBuf->cmdBuf.copyBufferToImage(uploadBuffer->buffer,
            dest->image, vk::ImageLayout::eTransferDstOptimal,
            1, &imageCopy);
    }

    void ZWVKCommandList::ResolveTexture(ITexture* _dest, const ZWTextureSubresourceSet& dstSubresources, ITexture* _src, const ZWTextureSubresourceSet& srcSubresources)
    {
        EndRenderPass();

        ZWVKTexture* dest = static_cast<ZWVKTexture*>(_dest);
        ZWVKTexture* src = static_cast<ZWVKTexture*>(_src);

        ZWTextureSubresourceSet dstSR = dstSubresources.resolve(dest->desc, false);
        ZWTextureSubresourceSet srcSR = srcSubresources.resolve(src->desc, false);

        if (dstSR.numArraySlices != srcSR.numArraySlices || dstSR.numMipLevels != srcSR.numMipLevels)
            // let the validation layer handle the messages
            return;

        assert(mCurrentCmdBuf);

        std::vector<vk::ImageResolve> regions;

        for (MipLevel mipLevel = 0; mipLevel < dstSR.numMipLevels; mipLevel++)
        {
            vk::ImageSubresourceLayers dstLayers(vk::ImageAspectFlagBits::eColor, mipLevel + dstSR.baseMipLevel, dstSR.baseArraySlice, dstSR.numArraySlices);
            vk::ImageSubresourceLayers srcLayers(vk::ImageAspectFlagBits::eColor, mipLevel + srcSR.baseMipLevel, srcSR.baseArraySlice, srcSR.numArraySlices);

            regions.push_back(vk::ImageResolve()
                .setSrcSubresource(srcLayers)
                .setDstSubresource(dstLayers)
                .setExtent(vk::Extent3D(
                    std::max(dest->desc.width >> dstLayers.mipLevel, 1u), 
                    std::max(dest->desc.height >> dstLayers.mipLevel, 1u),
                    std::max(dest->desc.depth >> dstLayers.mipLevel, 1u))));
        }

        if (m_EnableAutomaticBarriers)
        {
            RequireTextureState(src, srcSR, EResourceStates::ResolveSource);
            RequireTextureState(dest, dstSR, EResourceStates::ResolveDest);
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        mCurrentCmdBuf->cmdBuf.resolveImage(src->image, vk::ImageLayout::eTransferSrcOptimal, dest->image, vk::ImageLayout::eTransferDstOptimal, regions);
    }

    void ZWVKCommandList::ClearTexture(ITexture* _texture, ZWTextureSubresourceSet subresources, const vk::ClearColorValue& clearValue)
    {
        EndRenderPass();

        ZWVKTexture* texture = static_cast<ZWVKTexture*>(_texture);
        assert(texture);
        assert(mCurrentCmdBuf);
        
        subresources = subresources.resolve(texture->desc, false);

        if (m_EnableAutomaticBarriers)
        {
            RequireTextureState(texture, subresources, EResourceStates::CopyDest);
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        vk::ImageSubresourceRange subresourceRange = vk::ImageSubresourceRange()
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseArrayLayer(subresources.baseArraySlice)
            .setLayerCount(subresources.numArraySlices)
            .setBaseMipLevel(subresources.baseMipLevel)
            .setLevelCount(subresources.numMipLevels);
        
        mCurrentCmdBuf->cmdBuf.clearColorImage(texture->image,
            vk::ImageLayout::eTransferDstOptimal,
            &clearValue,
            1, &subresourceRange);
    }

    void ZWVKCommandList::ClearTextureFloat(ITexture* texture, ZWTextureSubresourceSet subresources, const ZWColor& clearColor)
    {
        auto clearValue = vk::ClearColorValue()
            .setFloat32({ clearColor.r, clearColor.g, clearColor.b, clearColor.a });

        ClearTexture(texture, subresources, clearValue);
    }

    void ZWVKCommandList::ClearDepthStencilTexture(ITexture* _texture, ZWTextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil)
    {
        EndRenderPass();

        if (!clearDepth && !clearStencil)
        {
            return;
        }

        ZWVKTexture* texture = static_cast<ZWVKTexture*>(_texture);
        assert(texture);
        assert(mCurrentCmdBuf);
        
        subresources = subresources.resolve(texture->desc, false);

        if (m_EnableAutomaticBarriers)
        {
            RequireTextureState(texture, subresources, EResourceStates::CopyDest);
        }
        CommitBarriers();

        vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlags();

        if (clearDepth)
            aspectFlags |= vk::ImageAspectFlagBits::eDepth;

        if (clearStencil)
            aspectFlags |= vk::ImageAspectFlagBits::eStencil;

        vk::ImageSubresourceRange subresourceRange = vk::ImageSubresourceRange()
            .setAspectMask(aspectFlags)
            .setBaseArrayLayer(subresources.baseArraySlice)
            .setLayerCount(subresources.numArraySlices)
            .setBaseMipLevel(subresources.baseMipLevel)
            .setLevelCount(subresources.numMipLevels);

        auto clearValue = vk::ClearDepthStencilValue(depth, uint32_t(stencil));
        mCurrentCmdBuf->cmdBuf.clearDepthStencilImage(texture->image,
            vk::ImageLayout::eTransferDstOptimal,
            &clearValue,
            1, &subresourceRange);
    }

    void ZWVKCommandList::ClearTextureUInt(ITexture* texture, ZWTextureSubresourceSet subresources, uint32_t clearColor)
    {
        int clearColorInt = int(clearColor);

        auto clearValue = vk::ClearColorValue()
            .setUint32({ clearColor, clearColor, clearColor, clearColor })
            .setInt32({ clearColorInt, clearColorInt, clearColorInt, clearColorInt });

        ClearTexture(texture, subresources, clearValue);
    }

    void ZWVKCommandList::ClearSamplerFeedbackTexture(ISamplerFeedbackTexture* texture)
    {
        (void)texture;

        mContext.Warning("Sampler feedback textures are not supported in Vulkan backend yet.");
    }

    void ZWVKCommandList::DecodeSamplerFeedbackTexture(IBuffer* buffer, ISamplerFeedbackTexture* texture, EFormat format)
    {
        (void)buffer;
        (void)texture;
        (void)format;

        mContext.Warning("Sampler feedback decode is not supported in Vulkan backend yet.");
    }

    void ZWVKCommandList::SetSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, EResourceStates stateBits)
    {
        (void)texture;
        (void)stateBits;

        mContext.Warning("Sampler feedback texture states are not supported in Vulkan backend yet.");
    }

    HCommon::ZWObject ZWVKTexture::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case HRHIObjectTypes::gVKImage:
            return HCommon::ZWObject(image);
        case HRHIObjectTypes::gVKDeviceMemory:
            return HCommon::ZWObject(memory);
        case HRHIObjectTypes::gSharedHandle:
            return HCommon::ZWObject(sharedHandle);
        case HRHIObjectTypes::gVKImageCreateInfo:
            return HCommon::ZWObject(&imageInfo);
        default:
            return nullptr;
        }
    }

    HCommon::ZWObject ZWVKTexture::GetNativeView(ObjectType objectType, EFormat format, ZWTextureSubresourceSet subresources, ETextureDimension dimension, bool /*isReadOnlyDSV*/)
    {
        switch (objectType)
        {
        case HRHIObjectTypes::gVKImageView: 
        {
            if (format == EFormat::UNKNOWN)
                format = desc.format;

            const ZWFormatInfo& formatInfo = GetFormatInfo(format);

            ETextureSubresourceViewType viewType = ETextureSubresourceViewType::AllAspects;
            if (formatInfo.hasDepth && !formatInfo.hasStencil)
                viewType = ETextureSubresourceViewType::DepthOnly;
            else if(!formatInfo.hasDepth && formatInfo.hasStencil)
                viewType = ETextureSubresourceViewType::StencilOnly;

            // Note: we don't have the intended usage information here, so VkImageViewUsageCreateInfo won't be added to the view.
            return HCommon::ZWObject(GetSubresourceView(subresources, dimension, format, vk::ImageUsageFlags(0), viewType).view);
        }
        default:
            return nullptr;
        }
    }

    uint32_t ZWVKTexture::GetNumSubresources() const
    {
        return desc.mipLevels * desc.arraySize;
    }

    uint32_t ZWVKTexture::GetSubresourceIndex(uint32_t mipLevel, uint32_t arrayLayer) const
    {
        return mipLevel * desc.arraySize + arrayLayer;
    }

    ZWVKTexture::~ZWVKTexture()
    {
        for (auto& viewIter : subresourceViews)
        {
            auto& view = viewIter.second.view;
            mContext.device.destroyImageView(view, mContext.allocationCallbacks);
            view = vk::ImageView();
        }
        subresourceViews.clear();

        if (managed)
        {
            if (image)
            {
                mContext.device.destroyImage(image, mContext.allocationCallbacks);
                image = vk::Image();
            }

            if (memory)
            {
                mAllocator.FreeTextureMemory(this);
                memory = vk::DeviceMemory();
            }
        }
    }

    ZWTextureHandle ZWVKDevice::CreateHandleForNativeTexture(ObjectType objectType, HCommon::ZWObject _texture, const ZWTextureDesc& desc)
    {
        if (_texture.pointer == nullptr)
            return nullptr;

        if (objectType != HRHIObjectTypes::gVKImage)
            return nullptr;

        vk::Image image(VkImage(_texture.integer));

        ZWVKTexture* texture = new ZWVKTexture(mContext, mAllocator);
        fillTextureInfo(texture, desc);

        texture->image = image;
        texture->managed = false;

        return ZWTextureHandle::Create(texture);
    }

    static vk::BorderColor pickSamplerBorderColor(const ZWSamplerDesc& d)
    {
        if (d.borderColor.r == 0.f && d.borderColor.g == 0.f && d.borderColor.b == 0.f)
        {
            if (d.borderColor.a == 0.f)
            {
                return vk::BorderColor::eFloatTransparentBlack;
            }

            if (d.borderColor.a == 1.f)
            {
                return vk::BorderColor::eFloatOpaqueBlack;
            }
        }

        if (d.borderColor.r == 1.f && d.borderColor.g == 1.f && d.borderColor.b == 1.f)
        {
            if (d.borderColor.a == 1.f)
            {
                return vk::BorderColor::eFloatOpaqueWhite;
            }
        }

        assert(false);
        return vk::BorderColor::eFloatOpaqueBlack;
    }

    ZWSamplerHandle ZWVKDevice::CreateSampler(const ZWSamplerDesc& desc)
    {
        ZWVKSampler* sampler = new ZWVKSampler(mContext);

        const bool anisotropyEnable = desc.maxAnisotropy > 1.0f;

        sampler->desc = desc;
        sampler->samplerInfo = vk::SamplerCreateInfo()
                            .setMagFilter(desc.magFilter ? vk::Filter::eLinear : vk::Filter::eNearest)
                            .setMinFilter(desc.minFilter ? vk::Filter::eLinear : vk::Filter::eNearest)
                            .setMipmapMode(desc.mipFilter ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest)
                            .setAddressModeU(ConvertSamplerAddressMode(desc.addressU))
                            .setAddressModeV(ConvertSamplerAddressMode(desc.addressV))
                            .setAddressModeW(ConvertSamplerAddressMode(desc.addressW))
                            .setMipLodBias(desc.mipBias)
                            .setAnisotropyEnable(anisotropyEnable)
                            .setMaxAnisotropy(anisotropyEnable ? desc.maxAnisotropy : 1.f)
                            .setCompareEnable(desc.reductionType == ESamplerReductionType::Comparison)
                            .setCompareOp(vk::CompareOp::eLess)
                            .setMinLod(0.f)
                            .setMaxLod(std::numeric_limits<float>::max())
                            .setBorderColor(pickSamplerBorderColor(desc));

        vk::SamplerReductionModeCreateInfoEXT samplerReductionCreateInfo;
        if (desc.reductionType == ESamplerReductionType::Minimum || desc.reductionType == ESamplerReductionType::Maximum)
        {
            vk::SamplerReductionModeEXT reductionMode =
                desc.reductionType == ESamplerReductionType::Maximum ? vk::SamplerReductionModeEXT::eMax : vk::SamplerReductionModeEXT::eMin;
            samplerReductionCreateInfo.setReductionMode(reductionMode);

            sampler->samplerInfo.setPNext(&samplerReductionCreateInfo);
        }

        const vk::Result res = mContext.device.createSampler(&sampler->samplerInfo, mContext.allocationCallbacks, &sampler->sampler);
        CHECK_VK_FAIL(res)
        
        return ZWSamplerHandle::Create(sampler);
    }

    HCommon::ZWObject ZWVKSampler::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case HRHIObjectTypes::gVKSampler:
            return HCommon::ZWObject(sampler);
        default:
            return nullptr;
        }
    }

    ZWVKSampler::~ZWVKSampler() 
    { 
        mContext.device.destroySampler(sampler);
    }

} // namespace HRHI

