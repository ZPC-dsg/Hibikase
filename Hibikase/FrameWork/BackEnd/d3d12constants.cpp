#include <BackEnd/d3d12backend.h>

namespace HRHI::HD3D12
{
    D3D12_SHADER_VISIBILITY ConvertShaderStage(EShaderType s)
    {
        switch (s)
        {
        case EShaderType::Vertex:
            return D3D12_SHADER_VISIBILITY_VERTEX;
        case EShaderType::Hull:
            return D3D12_SHADER_VISIBILITY_HULL;
        case EShaderType::Domain:
            return D3D12_SHADER_VISIBILITY_DOMAIN;
        case EShaderType::Geometry:
            return D3D12_SHADER_VISIBILITY_GEOMETRY;
        case EShaderType::Pixel:
            return D3D12_SHADER_VISIBILITY_PIXEL;
        case EShaderType::Amplification:
            return D3D12_SHADER_VISIBILITY_AMPLIFICATION;
        case EShaderType::Mesh:
            return D3D12_SHADER_VISIBILITY_MESH;
        default:
            return D3D12_SHADER_VISIBILITY_ALL;
        }
    }

    D3D12_BLEND ConvertBlendValue(EBlendFactor value)
    {
        switch (value)
        {
        case EBlendFactor::Zero:
            return D3D12_BLEND_ZERO;
        case EBlendFactor::One:
            return D3D12_BLEND_ONE;
        case EBlendFactor::SrcColor:
            return D3D12_BLEND_SRC_COLOR;
        case EBlendFactor::InvSrcColor:
            return D3D12_BLEND_INV_SRC_COLOR;
        case EBlendFactor::SrcAlpha:
            return D3D12_BLEND_SRC_ALPHA;
        case EBlendFactor::InvSrcAlpha:
            return D3D12_BLEND_INV_SRC_ALPHA;
        case EBlendFactor::DstAlpha:
            return D3D12_BLEND_DEST_ALPHA;
        case EBlendFactor::InvDstAlpha:
            return D3D12_BLEND_INV_DEST_ALPHA;
        case EBlendFactor::DstColor:
            return D3D12_BLEND_DEST_COLOR;
        case EBlendFactor::InvDstColor:
            return D3D12_BLEND_INV_DEST_COLOR;
        case EBlendFactor::SrcAlphaSaturate:
            return D3D12_BLEND_SRC_ALPHA_SAT;
        case EBlendFactor::ConstantColor:
            return D3D12_BLEND_BLEND_FACTOR;
        case EBlendFactor::InvConstantColor:
            return D3D12_BLEND_INV_BLEND_FACTOR;
        case EBlendFactor::Src1Color:
            return D3D12_BLEND_SRC1_COLOR;
        case EBlendFactor::InvSrc1Color:
            return D3D12_BLEND_INV_SRC1_COLOR;
        case EBlendFactor::Src1Alpha:
            return D3D12_BLEND_SRC1_ALPHA;
        case EBlendFactor::InvSrc1Alpha:
            return D3D12_BLEND_INV_SRC1_ALPHA;
        default:
            return D3D12_BLEND_ZERO;
        }
    }

    D3D12_BLEND_OP ConvertBlendOp(EBlendOp value)
    {
        switch (value)
        {
        case EBlendOp::Add:
            return D3D12_BLEND_OP_ADD;
        case EBlendOp::Subtract:
            return D3D12_BLEND_OP_SUBTRACT;
        case EBlendOp::ReverseSubtract:
            return D3D12_BLEND_OP_REV_SUBTRACT;
        case EBlendOp::Min:
            return D3D12_BLEND_OP_MIN;
        case EBlendOp::Max:
            return D3D12_BLEND_OP_MAX;
        default:
            return D3D12_BLEND_OP_ADD;
        }
    }

    D3D12_STENCIL_OP ConvertStencilOp(EStencilOp value)
    {
        switch (value)
        {
        case EStencilOp::Keep:
            return D3D12_STENCIL_OP_KEEP;
        case EStencilOp::Zero:
            return D3D12_STENCIL_OP_ZERO;
        case EStencilOp::Replace:
            return D3D12_STENCIL_OP_REPLACE;
        case EStencilOp::IncrementAndClamp:
            return D3D12_STENCIL_OP_INCR_SAT;
        case EStencilOp::DecrementAndClamp:
            return D3D12_STENCIL_OP_DECR_SAT;
        case EStencilOp::Invert:
            return D3D12_STENCIL_OP_INVERT;
        case EStencilOp::IncrementAndWrap:
            return D3D12_STENCIL_OP_INCR;
        case EStencilOp::DecrementAndWrap:
            return D3D12_STENCIL_OP_DECR;
        default:
            return D3D12_STENCIL_OP_KEEP;
        }
    }

    D3D12_COMPARISON_FUNC ConvertComparisonFunc(EComparisonFunc value)
    {
        switch (value)
        {
        case EComparisonFunc::Never:
            return D3D12_COMPARISON_FUNC_NEVER;
        case EComparisonFunc::Less:
            return D3D12_COMPARISON_FUNC_LESS;
        case EComparisonFunc::Equal:
            return D3D12_COMPARISON_FUNC_EQUAL;
        case EComparisonFunc::LessOrEqual:
            return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case EComparisonFunc::Greater:
            return D3D12_COMPARISON_FUNC_GREATER;
        case EComparisonFunc::NotEqual:
            return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case EComparisonFunc::GreaterOrEqual:
            return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case EComparisonFunc::Always:
            return D3D12_COMPARISON_FUNC_ALWAYS;
        default:
            return D3D12_COMPARISON_FUNC_NEVER;
        }
    }

    D3D_PRIMITIVE_TOPOLOGY ConvertPrimitiveType(EPrimitiveType pt, uint32_t controlPoints)
    {
        switch (pt)
        {
        case EPrimitiveType::PointList:
            return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case EPrimitiveType::LineList:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case EPrimitiveType::LineStrip:
            return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case EPrimitiveType::TriangleList:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case EPrimitiveType::TriangleStrip:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case EPrimitiveType::TriangleListWithAdjacency:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
        case EPrimitiveType::TriangleStripWithAdjacency:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
        case EPrimitiveType::PatchList:
            if (controlPoints == 0 || controlPoints > 32)
            {
                return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
            }

            return D3D_PRIMITIVE_TOPOLOGY(
                D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + (controlPoints - 1));
        case EPrimitiveType::TriangleFan:
        default:
            return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }
    }

    D3D12_TEXTURE_ADDRESS_MODE ConvertSamplerAddressMode(ESamplerAddressMode mode)
    {
        switch (mode)
        {
        case ESamplerAddressMode::Clamp:
            return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case ESamplerAddressMode::Wrap:
            return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case ESamplerAddressMode::Border:
            return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        case ESamplerAddressMode::Mirror:
            return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case ESamplerAddressMode::MirrorOnce:
            return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
        default:
            return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        }
    }

    UINT ConvertSamplerReductionType(ESamplerReductionType reductionType)
    {
        switch (reductionType)
        {
        case ESamplerReductionType::Standard:
            return D3D12_FILTER_REDUCTION_TYPE_STANDARD;
        case ESamplerReductionType::Comparison:
            return D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
        case ESamplerReductionType::Minimum:
            return D3D12_FILTER_REDUCTION_TYPE_MINIMUM;
        case ESamplerReductionType::Maximum:
            return D3D12_FILTER_REDUCTION_TYPE_MAXIMUM;
        default:
            return D3D12_FILTER_REDUCTION_TYPE_STANDARD;
        }
    }

    D3D12_RESOURCE_STATES ConvertResourceStates(EResourceStates stateBits)
    {
        if (stateBits == EResourceStates::Common)
        {
            return D3D12_RESOURCE_STATE_COMMON;
        }

        D3D12_RESOURCE_STATES result = D3D12_RESOURCE_STATE_COMMON;

        if ((stateBits & EResourceStates::ConstantBuffer) != 0) result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        if ((stateBits & EResourceStates::VertexBuffer) != 0) result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        if ((stateBits & EResourceStates::IndexBuffer) != 0) result |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
        if ((stateBits & EResourceStates::IndirectArgument) != 0) result |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        if ((stateBits & EResourceStates::ShaderResource) != 0) result |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if ((stateBits & EResourceStates::UnorderedAccess) != 0) result |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if ((stateBits & EResourceStates::RenderTarget) != 0) result |= D3D12_RESOURCE_STATE_RENDER_TARGET;
        if ((stateBits & EResourceStates::DepthWrite) != 0) result |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
        if ((stateBits & EResourceStates::DepthRead) != 0) result |= D3D12_RESOURCE_STATE_DEPTH_READ;
        if ((stateBits & EResourceStates::StreamOut) != 0) result |= D3D12_RESOURCE_STATE_STREAM_OUT;
        if ((stateBits & EResourceStates::CopyDest) != 0) result |= D3D12_RESOURCE_STATE_COPY_DEST;
        if ((stateBits & EResourceStates::CopySource) != 0) result |= D3D12_RESOURCE_STATE_COPY_SOURCE;
        if ((stateBits & EResourceStates::ResolveDest) != 0) result |= D3D12_RESOURCE_STATE_RESOLVE_DEST;
        if ((stateBits & EResourceStates::ResolveSource) != 0) result |= D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
        if ((stateBits & EResourceStates::Present) != 0) result |= D3D12_RESOURCE_STATE_PRESENT;
        if ((stateBits & EResourceStates::AccelStructRead) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        if ((stateBits & EResourceStates::AccelStructWrite) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        if ((stateBits & EResourceStates::AccelStructBuildInput) != 0) result |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if ((stateBits & EResourceStates::AccelStructBuildBlas) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        if ((stateBits & EResourceStates::ShadingRateSurface) != 0) result |= D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
        if ((stateBits & EResourceStates::OpacityMicromapBuildInput) != 0) result |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if ((stateBits & EResourceStates::OpacityMicromapWrite) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        if ((stateBits & EResourceStates::ConvertCoopVecMatrixInput) != 0) result |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if ((stateBits & EResourceStates::ConvertCoopVecMatrixOutput) != 0) result |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        return result;
    }

    D3D12_SHADING_RATE ConvertPixelShadingRate(EVariableShadingRate shadingRate)
    {
        switch (shadingRate)
        {
        case EVariableShadingRate::e1x2:
            return D3D12_SHADING_RATE_1X2;
        case EVariableShadingRate::e2x1:
            return D3D12_SHADING_RATE_2X1;
        case EVariableShadingRate::e2x2:
            return D3D12_SHADING_RATE_2X2;
        case EVariableShadingRate::e2x4:
            return D3D12_SHADING_RATE_2X4;
        case EVariableShadingRate::e4x2:
            return D3D12_SHADING_RATE_4X2;
        case EVariableShadingRate::e4x4:
            return D3D12_SHADING_RATE_4X4;
        case EVariableShadingRate::e1x1:
        default:
            return D3D12_SHADING_RATE_1X1;
        }
    }

    D3D12_SHADING_RATE_COMBINER ConvertShadingRateCombiner(EShadingRateCombiner combiner)
    {
        switch (combiner)
        {
        case EShadingRateCombiner::Override:
            return D3D12_SHADING_RATE_COMBINER_OVERRIDE;
        case EShadingRateCombiner::Min:
            return D3D12_SHADING_RATE_COMBINER_MIN;
        case EShadingRateCombiner::Max:
            return D3D12_SHADING_RATE_COMBINER_MAX;
        case EShadingRateCombiner::ApplyRelative:
            return D3D12_SHADING_RATE_COMBINER_SUM;
        case EShadingRateCombiner::Passthrough:
        default:
            return D3D12_SHADING_RATE_COMBINER_PASSTHROUGH;
        }
    }

#if HRHI_D3D12_WITH_COOPVEC
    D3D12_LINEAR_ALGEBRA_DATATYPE ConvertCoopVecDataType(HCoopVec::EDataType type)
    {
        switch (type)
        {
        case HCoopVec::EDataType::UInt8:
            return D3D12_LINEAR_ALGEBRA_DATATYPE_UINT8;
        case HCoopVec::EDataType::SInt8:
            return D3D12_LINEAR_ALGEBRA_DATATYPE_SINT8;
        case HCoopVec::EDataType::UInt8Packed:
            return D3D12_LINEAR_ALGEBRA_DATATYPE_UINT8_T4_PACKED;
        case HCoopVec::EDataType::SInt8Packed:
            return D3D12_LINEAR_ALGEBRA_DATATYPE_SINT8_T4_PACKED;
        case HCoopVec::EDataType::UInt16:
            return D3D12_LINEAR_ALGEBRA_DATATYPE_UINT16;
        case HCoopVec::EDataType::SInt16:
            return D3D12_LINEAR_ALGEBRA_DATATYPE_SINT16;
        case HCoopVec::EDataType::UInt32:
            return D3D12_LINEAR_ALGEBRA_DATATYPE_UINT32;
        case HCoopVec::EDataType::SInt32:
            return D3D12_LINEAR_ALGEBRA_DATATYPE_SINT32;
        case HCoopVec::EDataType::FloatE4M3:
            return D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT_E4M3;
        case HCoopVec::EDataType::FloatE5M2:
            return D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT_E5M2;
        case HCoopVec::EDataType::Float16:
            return D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT16;
        case HCoopVec::EDataType::Float32:
            return D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT32;
        default:
            return D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT32;
        }
    }

    HCoopVec::EDataType ConvertCoopVecDataType(D3D12_LINEAR_ALGEBRA_DATATYPE type)
    {
        switch (type)
        {
        case D3D12_LINEAR_ALGEBRA_DATATYPE_UINT8:
            return HCoopVec::EDataType::UInt8;
        case D3D12_LINEAR_ALGEBRA_DATATYPE_SINT8:
            return HCoopVec::EDataType::SInt8;
        case D3D12_LINEAR_ALGEBRA_DATATYPE_UINT8_T4_PACKED:
            return HCoopVec::EDataType::UInt8Packed;
        case D3D12_LINEAR_ALGEBRA_DATATYPE_SINT8_T4_PACKED:
            return HCoopVec::EDataType::SInt8Packed;
        case D3D12_LINEAR_ALGEBRA_DATATYPE_UINT16:
            return HCoopVec::EDataType::UInt16;
        case D3D12_LINEAR_ALGEBRA_DATATYPE_SINT16:
            return HCoopVec::EDataType::SInt16;
        case D3D12_LINEAR_ALGEBRA_DATATYPE_UINT32:
            return HCoopVec::EDataType::UInt32;
        case D3D12_LINEAR_ALGEBRA_DATATYPE_SINT32:
            return HCoopVec::EDataType::SInt32;
        case D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT_E4M3:
            return HCoopVec::EDataType::FloatE4M3;
        case D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT_E5M2:
            return HCoopVec::EDataType::FloatE5M2;
        case D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT16:
            return HCoopVec::EDataType::Float16;
        case D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT32:
            return HCoopVec::EDataType::Float32;
        default:
            return HCoopVec::EDataType::Float32;
        }
    }

    D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT ConvertCoopVecMatrixLayout(HCoopVec::EMatrixLayout layout)
    {
        switch (layout)
        {
        case HCoopVec::EMatrixLayout::RowMajor:
            return D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT_ROW_MAJOR;
        case HCoopVec::EMatrixLayout::ColumnMajor:
            return D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT_COLUMN_MAJOR;
        case HCoopVec::EMatrixLayout::InferencingOptimal:
            return D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT_INFERENCING_OPTIMAL;
        case HCoopVec::EMatrixLayout::TrainingOptimal:
            return D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT_TRAINING_OPTIMAL;
        default:
            return D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT_ROW_MAJOR;
        }
    }
#endif
}
