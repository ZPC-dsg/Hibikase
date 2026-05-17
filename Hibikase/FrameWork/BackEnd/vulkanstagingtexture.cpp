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

#include <BackEnd/vulkanbackend.h>

namespace HRHI
{

    extern vk::ImageAspectFlags GuessImageAspectFlags(vk::Format format);

    static size_t alignBufferOffset(size_t off)
    {
        return (off + 3u) & ~size_t(3u); // Round up to a multiple of 4
    }

    static size_t computePlacedBufferOffset(
        const PlacedSubresourceFootprint& footprint, uint32_t x, uint32_t y, uint32_t z)
    {
        const ZWFormatInfo& formatInfo = GetFormatInfo(footprint.format);

        uint32_t blockX = x / formatInfo.blockSize;
        uint32_t blockY = y / formatInfo.blockSize;
        uint32_t blockZ = z;

        return footprint.offset + (blockX + (blockY + blockZ * footprint.numRows) * footprint.rowPitch);
    }

    size_t StagingTexture::ComputeCopyableFootprints()
    {
        uint32_t width = desc.width;
        uint32_t height = desc.height;
        uint32_t depth = desc.depth;
        uint32_t numMips = desc.mipLevels;
        uint32_t arraySize = desc.arraySize;

        if (desc.dimension == ETextureDimension::Texture3D)
        {
            assert(desc.arraySize == 1);
            arraySize = 1;
        }
        else
        {
            assert(desc.depth == 1);
            depth = 1;
        }

        const ZWFormatInfo& formatInfo = GetFormatInfo(desc.format);

        size_t offset = 0;

        for (uint32_t mipLevel = 0; mipLevel < numMips; ++mipLevel)
        {
            uint32_t widthInBlocks = std::max((width + formatInfo.blockSize - 1) / formatInfo.blockSize, 1u);
            uint32_t heightInBlocks = std::max((height + formatInfo.blockSize - 1) / formatInfo.blockSize, 1u);

            PlacedSubresourceFootprint layout;
            layout.rowSizeInBytes = widthInBlocks * formatInfo.bytesPerBlock;
            layout.numRows = heightInBlocks;
            layout.totalBytes = size_t(depth) * heightInBlocks * layout.rowSizeInBytes;
            layout.format = desc.format;
            layout.width = width;
            layout.height = height;
            layout.depth = depth;
            layout.rowPitch = layout.rowSizeInBytes;

            for (uint32_t arraySlice = 0; arraySlice < arraySize; ++arraySlice)
            {
                layout.offset = alignBufferOffset(offset);
                offset += layout.totalBytes;
                placedFootprints.push_back(layout);
            }

            width = std::max(width >> 1u, 1u);
            height = std::max(height >> 1u, 1u);
            depth = std::max(depth >> 1u, 1u);
        }

        return offset; // total size in bytes of upload buffer
    }

    const PlacedSubresourceFootprint* StagingTexture::GetCopyableFootprint(MipLevel mipLevel, ArraySlice arraySlice)
    {
        uint32_t subresourceIndex = mipLevel * desc.arraySize + arraySlice;

        if (subresourceIndex >= placedFootprints.size())
            return nullptr;

        return &placedFootprints[subresourceIndex];
    }

    ZWStagingTextureHandle ZWVKDevice::CreateStagingTexture(const ZWTextureDesc& desc, ECpuAccessMode cpuAccess)
    {
        assert(cpuAccess != ECpuAccessMode::None);

        StagingTexture* tex = new StagingTexture();
        tex->desc = desc;

        size_t totalSizeInBytes = tex->ComputeCopyableFootprints();

        ZWBufferDesc bufDesc;
        bufDesc.byteSize = totalSizeInBytes;
        assert(bufDesc.byteSize > 0);
        bufDesc.debugName = desc.debugName;
        bufDesc.cpuAccess = cpuAccess;

        ZWBufferHandle internalBuffer = CreateBuffer(bufDesc);
        tex->buffer = static_cast<ZWVKBuffer*>(internalBuffer.Get());

        if (!tex->buffer)
        {
            delete tex;
            return nullptr;
        }

        return ZWStagingTextureHandle::Create(tex);
    }

    void* ZWVKDevice::MapStagingTexture(IStagingTexture* _tex, const ZWTextureSlice& slice, ECpuAccessMode cpuAccess, size_t* outRowPitch)
    {
        assert(slice.x == 0);
        assert(slice.y == 0);
        assert(cpuAccess != ECpuAccessMode::None);

        StagingTexture* tex = static_cast<StagingTexture*>(_tex);

        const ZWTextureSlice resolvedSlice = slice.resolve(tex->desc);

        const PlacedSubresourceFootprint* layout = tex->GetCopyableFootprint(
            resolvedSlice.mipLevel, resolvedSlice.arraySlice);

        assert(layout);
        if (!layout)
            return nullptr;

        assert((layout->offset & 0x3) == 0); // per vulkan spec
        assert(layout->totalBytes > 0);

        *outRowPitch = layout->rowPitch;

        return MapBuffer(tex->buffer, cpuAccess, layout->offset, layout->totalBytes);
    }

    void ZWVKDevice::UnmapStagingTexture(IStagingTexture* _tex)
    {
        StagingTexture* tex = static_cast<StagingTexture*>(_tex);

        UnmapBuffer(tex->buffer);
    }

    void ZWVKCommandList::CopyTexture(IStagingTexture* _dst, const ZWTextureSlice& dstSlice, ITexture* _src, const ZWTextureSlice& srcSlice)
    {
        ZWVKTexture* src = static_cast<ZWVKTexture*>(_src);
        StagingTexture* dst = static_cast<StagingTexture*>(_dst);

        const ZWTextureSlice resolvedSrcSlice = srcSlice.resolve(src->desc);
        const ZWTextureSlice resolvedDstSlice = dstSlice.resolve(dst->desc);

        assert(resolvedDstSlice.depth == 1);
        
        const PlacedSubresourceFootprint* dstFootprint = dst->GetCopyableFootprint(
            resolvedDstSlice.mipLevel, resolvedDstSlice.arraySlice);

        assert(dstFootprint);
        if (!dstFootprint)
            return;

        size_t dstBufferOffset = computePlacedBufferOffset(*dstFootprint,
            resolvedDstSlice.x, resolvedDstSlice.y, resolvedDstSlice.z);
        assert((dstBufferOffset & 0x3) == 0);  // per Vulkan spec

        ZWTextureSubresourceSet srcSubresource = ZWTextureSubresourceSet(
            resolvedSrcSlice.mipLevel, 1,
            resolvedSrcSlice.arraySlice, 1
        );

        auto imageCopy = vk::BufferImageCopy()
            .setBufferOffset(dstBufferOffset)
            .setBufferRowLength(dstFootprint->width)
            .setBufferImageHeight(dstFootprint->height)
            .setImageSubresource(
                vk::ImageSubresourceLayers()
                    .setAspectMask(GuessImageAspectFlags(src->imageInfo.format))
                    .setMipLevel(resolvedSrcSlice.mipLevel)
                    .setBaseArrayLayer(resolvedSrcSlice.arraySlice)
                    .setLayerCount(1))
            .setImageOffset(vk::Offset3D(resolvedSrcSlice.x, resolvedSrcSlice.y, resolvedSrcSlice.z))
            .setImageExtent(vk::Extent3D(resolvedSrcSlice.width, resolvedSrcSlice.height, resolvedSrcSlice.depth));

        assert(mCurrentCmdBuf);

        if (m_EnableAutomaticBarriers)
        {
            RequireBufferState(dst->buffer, EResourceStates::CopyDest);
            RequireTextureState(src, srcSubresource, EResourceStates::CopySource);
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        mCurrentCmdBuf->referencedResources.push_back(src);
        mCurrentCmdBuf->referencedResources.push_back(dst);
        mCurrentCmdBuf->referencedStagingBuffers.push_back(dst->buffer);

        mCurrentCmdBuf->cmdBuf.copyImageToBuffer(src->image, vk::ImageLayout::eTransferSrcOptimal,
                                      dst->buffer->buffer, 1, &imageCopy);
    }

    void ZWVKCommandList::CopyTexture(ITexture* _dst, const ZWTextureSlice& dstSlice, IStagingTexture* _src, const ZWTextureSlice& srcSlice)
    {
        StagingTexture* src = static_cast<StagingTexture*>(_src);
        ZWVKTexture* dst = static_cast<ZWVKTexture*>(_dst);

        const ZWTextureSlice resolvedSrcSlice = srcSlice.resolve(src->desc);
        const ZWTextureSlice resolvedDstSlice = dstSlice.resolve(dst->desc);

        const PlacedSubresourceFootprint* srcFootprint = src->GetCopyableFootprint(
            resolvedSrcSlice.mipLevel, resolvedSrcSlice.arraySlice);

        assert(srcFootprint);
        if (!srcFootprint)
            return;

        size_t srcBufferOffset = computePlacedBufferOffset(*srcFootprint, srcSlice.x, srcSlice.y, srcSlice.z);
        assert((srcBufferOffset & 0x3) == 0);  // per Vulkan spec

        ZWTextureSubresourceSet dstSubresource = ZWTextureSubresourceSet(
            resolvedDstSlice.mipLevel, 1,
            resolvedDstSlice.arraySlice, 1
        );

        auto imageCopy = vk::BufferImageCopy()
            .setBufferOffset(srcBufferOffset)
            .setBufferRowLength(srcFootprint->width)
            .setBufferImageHeight(srcFootprint->height)
            .setImageSubresource(
                vk::ImageSubresourceLayers()
                    .setAspectMask(GuessImageAspectFlags(dst->imageInfo.format))
                    .setMipLevel(resolvedDstSlice.mipLevel)
                    .setBaseArrayLayer(resolvedDstSlice.arraySlice)
                    .setLayerCount(1))
            .setImageOffset(vk::Offset3D(resolvedDstSlice.x, resolvedDstSlice.y, resolvedDstSlice.z))
            .setImageExtent(vk::Extent3D(resolvedSrcSlice.width, resolvedSrcSlice.height, resolvedSrcSlice.depth));

        assert(mCurrentCmdBuf);

        if (m_EnableAutomaticBarriers)
        {
            RequireBufferState(src->buffer, EResourceStates::CopySource);
            RequireTextureState(dst, dstSubresource, EResourceStates::CopyDest);
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        mCurrentCmdBuf->referencedResources.push_back(src);
        mCurrentCmdBuf->referencedResources.push_back(dst);
        mCurrentCmdBuf->referencedStagingBuffers.push_back(src->buffer);

        mCurrentCmdBuf->cmdBuf.copyBufferToImage(src->buffer->buffer,
                                      dst->image, vk::ImageLayout::eTransferDstOptimal,
                                      1, &imageCopy);
    }

} // namespace HRHI
