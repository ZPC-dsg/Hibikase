#include <BackEnd/RHIinterface.h>

namespace HRHI
{
	ZWVertexBufferBinding& ZWVertexBufferBinding::setBuffer(IBuffer* value) { buffer = value; return *this; }

	ZWIndexBufferBinding& ZWIndexBufferBinding::setBuffer(IBuffer* value) { buffer = value; return *this; }

	ZWGraphicsState& ZWGraphicsState::setPipeline(IGraphicsPipeline* value) { pipeline = value; return *this; }
	ZWGraphicsState& ZWGraphicsState::setFramebuffer(IFramebuffer* value) { framebuffer = value; return *this; }
	ZWGraphicsState& ZWGraphicsState::addBindingSet(IBindingSet* value) { bindings.push_back(value); return *this; }
	ZWGraphicsState& ZWGraphicsState::setIndirectParams(IBuffer* value) { indirectParams = value; return *this; }
	ZWGraphicsState& ZWGraphicsState::setIndirectCountBuffer(IBuffer* value) { indirectCountBuffer = value; return *this; }

	ZWComputeState& ZWComputeState::setPipeline(IComputePipeline* value) { pipeline = value; return *this; }
	ZWComputeState& ZWComputeState::addBindingSet(IBindingSet* value) { bindings.push_back(value); return *this; }
	ZWComputeState& ZWComputeState::setIndirectParams(IBuffer* value) { indirectParams = value; return *this; }

	ZWMeshletState& ZWMeshletState::setPipeline(IMeshletPipeline* value) { pipeline = value; return *this; }
	ZWMeshletState& ZWMeshletState::setFramebuffer(IFramebuffer* value) { framebuffer = value; return *this; }

	ZWMeshletState& ZWMeshletState::addBindingSet(IBindingSet* value) { bindings.push_back(value); return *this; }
	ZWMeshletState& ZWMeshletState::setIndirectParams(IBuffer* value) { indirectParams = value; return *this; }

	namespace Hrt
	{
		ZWGeometryTriangles& ZWGeometryTriangles::setIndexBuffer(IBuffer* value) { indexBuffer = value; return *this; }
		ZWGeometryTriangles& ZWGeometryTriangles::setVertexBuffer(IBuffer* value) { vertexBuffer = value; return *this; }
		ZWGeometryTriangles& ZWGeometryTriangles::setOpacityMicromap(IOpacityMicromap* value) { opacityMicromap = value; return *this; }
		ZWGeometryTriangles& ZWGeometryTriangles::setOmmIndexBuffer(IBuffer* value) { ommIndexBuffer = value; return *this; }

		ZWGeometryAABBs& ZWGeometryAABBs::setBuffer(IBuffer* value) { buffer = value; return *this; }

		ZWGeometrySpheres& ZWGeometrySpheres::setIndexBuffer(IBuffer* value) { indexBuffer = value; return *this; }
		ZWGeometrySpheres& ZWGeometrySpheres::setVertexBuffer(IBuffer* value) { vertexBuffer = value; return *this; }

		ZWGeometryLss& ZWGeometryLss::setIndexBuffer(IBuffer* value) { indexBuffer = value; return *this; }
		ZWGeometryLss& ZWGeometryLss::setVertexBuffer(IBuffer* value) { vertexBuffer = value; return *this; }

		ZWInstanceDesc& ZWInstanceDesc::setBLAS(IAccelStruct* value) { bottomLevelAS = value; return *this; }

		ZWState& ZWState::setShaderTable(IShaderTable* value) { shaderTable = value; return *this; }
		ZWState& ZWState::addBindingSet(IBindingSet* value) { bindings.push_back(value); return *this; }
	}

    bool verifyHeaderVersion(uint32_t version)
    {
        return version == cHeaderVersion;
    }

	ZWTextureSlice ZWTextureSlice::resolve(const ZWTextureDesc& desc) const
	{
		ZWTextureSlice ret(*this);

		assert(mipLevel < desc.mipLevels);

		if (width == uint32_t(-1))
			ret.width = std::max(desc.width >> mipLevel, 1u);

		if (height == uint32_t(-1))
			ret.height = std::max(desc.height >> mipLevel, 1u);

		if (depth == uint32_t(-1))
		{
			if (desc.dimension == ETextureDimension::Texture3D)
				ret.depth = std::max(desc.depth >> mipLevel, 1u);
			else
				ret.depth = 1;
		}

		return ret;
	}

    ZWTextureSubresourceSet ZWTextureSubresourceSet::resolve(const ZWTextureDesc& desc, bool singleMipLevel) const
    {
        ZWTextureSubresourceSet ret;
        ret.baseMipLevel = baseMipLevel;

        if (singleMipLevel)
        {
            ret.numMipLevels = 1;
        }
        else
        {
            int lastMipLevelPlusOne = std::min(baseMipLevel + numMipLevels, desc.mipLevels);
            ret.numMipLevels = MipLevel(std::max(0u, lastMipLevelPlusOne - baseMipLevel));
        }

        switch (desc.dimension)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case ETextureDimension::Texture1DArray:
        case ETextureDimension::Texture2DArray:
        case ETextureDimension::TextureCube:
        case ETextureDimension::TextureCubeArray:
        case ETextureDimension::Texture2DMSArray: {
            ret.baseArraySlice = baseArraySlice;
            int lastArraySlicePlusOne = std::min(baseArraySlice + numArraySlices, desc.arraySize);
            ret.numArraySlices = ArraySlice(std::max(0u, lastArraySlicePlusOne - baseArraySlice));
            break;
        }
        default:
            ret.baseArraySlice = 0;
            ret.numArraySlices = 1;
            break;
        }

        return ret;
    }

    bool ZWTextureSubresourceSet::isEntireTexture(const ZWTextureDesc& desc) const
    {
        if (baseMipLevel > 0u || baseMipLevel + numMipLevels < desc.mipLevels)
            return false;

        switch (desc.dimension)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case ETextureDimension::Texture1DArray:
        case ETextureDimension::Texture2DArray:
        case ETextureDimension::TextureCube:
        case ETextureDimension::TextureCubeArray:
        case ETextureDimension::Texture2DMSArray:
            if (baseArraySlice > 0u || baseArraySlice + numArraySlices < desc.arraySize)
                return false;
        default:
            return true;
        }
    }

    ZWBufferRange ZWBufferRange::resolve(const ZWBufferDesc& desc) const
    {
        ZWBufferRange result;
        result.byteOffset = std::min(byteOffset, desc.byteSize);
        if (byteSize == 0)
            result.byteSize = desc.byteSize - result.byteOffset;
        else
            result.byteSize = std::min(byteSize, desc.byteSize - result.byteOffset);
        return result;
    }

    bool ZWBlendState::ZWRenderTarget::usesConstantColor() const
    {
        return srcBlend == EBlendFactor::ConstantColor || srcBlend == EBlendFactor::OneMinusConstantColor ||
            destBlend == EBlendFactor::ConstantColor || destBlend == EBlendFactor::OneMinusConstantColor ||
            srcBlendAlpha == EBlendFactor::ConstantColor || srcBlendAlpha == EBlendFactor::OneMinusConstantColor ||
            destBlendAlpha == EBlendFactor::ConstantColor || destBlendAlpha == EBlendFactor::OneMinusConstantColor;
    }

    bool ZWBlendState::usesConstantColor(uint32_t numTargets) const
    {
        for (uint32_t rt = 0; rt < numTargets; rt++)
        {
            if (targets[rt].usesConstantColor())
                return true;
        }

        return false;
    }

    ZWFramebufferInfo::ZWFramebufferInfo(const ZWFramebufferDesc& desc)
    {
        for (size_t i = 0; i < desc.colorAttachments.size(); i++)
        {
            const ZWFramebufferAttachment& attachment = desc.colorAttachments[i];
            colorFormats.push_back(attachment.format == EFormat::UNKNOWN && attachment.texture ? attachment.texture->GetDesc().format : attachment.format);
        }

        if (desc.depthAttachment.valid())
        {
            const ZWTextureDesc& textureDesc = desc.depthAttachment.texture->GetDesc();
            depthFormat = textureDesc.format;
            sampleCount = textureDesc.sampleCount;
            sampleQuality = textureDesc.sampleQuality;
        }
        else if (!desc.colorAttachments.empty() && desc.colorAttachments[0].valid())
        {
            const ZWTextureDesc& textureDesc = desc.colorAttachments[0].texture->GetDesc();
            sampleCount = textureDesc.sampleCount;
            sampleQuality = textureDesc.sampleQuality;
        }
    }

    ZWFramebufferInfoEx::ZWFramebufferInfoEx(const ZWFramebufferDesc& desc)
        : ZWFramebufferInfo(desc)
    {
        if (desc.depthAttachment.valid())
        {
            const ZWTextureDesc& textureDesc = desc.depthAttachment.texture->GetDesc();
            ZWTextureSubresourceSet const subresources = desc.depthAttachment.subresources.resolve(textureDesc, true);
            width = std::max(textureDesc.width >> subresources.baseMipLevel, 1u);
            height = std::max(textureDesc.height >> subresources.baseMipLevel, 1u);
            arraySize = subresources.numArraySlices;
        }
        else if (!desc.colorAttachments.empty() && desc.colorAttachments[0].valid())
        {
            const ZWTextureDesc& textureDesc = desc.colorAttachments[0].texture->GetDesc();
            ZWTextureSubresourceSet const subresources = desc.colorAttachments[0].subresources.resolve(textureDesc, true);
            width = std::max(textureDesc.width >> subresources.baseMipLevel, 1u);
            height = std::max(textureDesc.height >> subresources.baseMipLevel, 1u);
            arraySize = subresources.numArraySlices;
        }
    }

    void ICommandList::SetResourceStatesForFramebuffer(IFramebuffer* framebuffer)
    {
        const ZWFramebufferDesc& desc = framebuffer->GetDesc();

        for (const auto& attachment : desc.colorAttachments)
        {
            SetTextureState(attachment.texture, attachment.subresources,
                EResourceStates::RenderTarget);
        }

        if (desc.depthAttachment.valid())
        {
            SetTextureState(desc.depthAttachment.texture, desc.depthAttachment.subresources,
                desc.depthAttachment.isReadOnly ? EResourceStates::DepthRead : EResourceStates::DepthWrite);
        }

        if (desc.shadingRateAttachment.valid())
        {
            SetTextureState(desc.shadingRateAttachment.texture, desc.shadingRateAttachment.subresources,
                HRHI::EResourceStates::ShadingRateSurface);
        }
    }

    namespace HCoopVec
    {
        size_t getDataTypeSize(EDataType type)
        {
            switch (type)
            {
            case EDataType::UInt8:
            case EDataType::SInt8:
                return 1;
            case EDataType::UInt8Packed:
            case EDataType::SInt8Packed:
                // Not sure if this is correct or even relevant because packed types
                // cannot be used in matrices accessible from the host side.
                return 1;
            case EDataType::UInt16:
            case EDataType::SInt16:
                return 2;
            case EDataType::UInt32:
            case EDataType::SInt32:
                return 4;
            case EDataType::UInt64:
            case EDataType::SInt64:
                return 8;
            case EDataType::FloatE4M3:
            case EDataType::FloatE5M2:
                return 1;
            case EDataType::Float16:
            case EDataType::BFloat16:
                return 2;
            case EDataType::Float32:
                return 4;
            case EDataType::Float64:
                return 8;
            default:
                assert(!"Invalid Enumeration Value");
                return 0;
            }
        }

        size_t getOptimalMatrixStride(EDataType type, EMatrixLayout layout, uint32_t rows, uint32_t columns)
        {
            size_t const dataTypeSize = getDataTypeSize(type);

            switch (layout)
            {
            case EMatrixLayout::RowMajor:
                return dataTypeSize * columns;
                break;
            case EMatrixLayout::ColumnMajor:
                return dataTypeSize * rows;
                break;
            default:
                return 0;
            }
        }
    }
}