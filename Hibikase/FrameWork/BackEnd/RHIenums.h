#pragma once

#include <cstdint>

#define HRHI_ENUM_CLASS_FLAG_OPERATORS(T) \
    inline T operator | (T a, T b) { return T(uint32_t(a) | uint32_t(b)); } \
    inline T operator & (T a, T b) { return T(uint32_t(a) & uint32_t(b)); } \
    inline T operator ~ (T a) { return T(~uint32_t(a)); } \
    inline bool operator !(T a) { return uint32_t(a) == 0; } \
    inline bool operator ==(T a, uint32_t b) { return uint32_t(a) == b; } \
    inline bool operator !=(T a, uint32_t b) { return uint32_t(a) != b; }

namespace HRHI
{
    // 目前仅支持D3D12和Vulkan
    enum class EGraphicsAPI : uint8_t
    {
        D3D12,
        VULKAN
    };

    enum class EFormat : uint8_t
    {
        UNKNOWN,

        R8_UINT,
        R8_SINT,
        R8_UNORM,
        R8_SNORM,
        RG8_UINT,
        RG8_SINT,
        RG8_UNORM,
        RG8_SNORM,
        R16_UINT,
        R16_SINT,
        R16_UNORM,
        R16_SNORM,
        R16_FLOAT,
        BGRA4_UNORM,
        B5G6R5_UNORM,
        B5G5R5A1_UNORM,
        RGBA8_UINT,
        RGBA8_SINT,
        RGBA8_UNORM,
        RGBA8_SNORM,
        BGRA8_UNORM,
        BGRX8_UNORM,
        SRGBA8_UNORM,
        SBGRA8_UNORM,
        SBGRX8_UNORM,
        R10G10B10A2_UNORM,
        R11G11B10_FLOAT,
        RG16_UINT,
        RG16_SINT,
        RG16_UNORM,
        RG16_SNORM,
        RG16_FLOAT,
        R32_UINT,
        R32_SINT,
        R32_FLOAT,
        RGBA16_UINT,
        RGBA16_SINT,
        RGBA16_FLOAT,
        RGBA16_UNORM,
        RGBA16_SNORM,
        RG32_UINT,
        RG32_SINT,
        RG32_FLOAT,
        RGB32_UINT,
        RGB32_SINT,
        RGB32_FLOAT,
        RGBA32_UINT,
        RGBA32_SINT,
        RGBA32_FLOAT,

        D16,
        D24S8,
        X24G8_UINT,
        D32,
        D32S8,
        X32G8_UINT,

        BC1_UNORM,
        BC1_UNORM_SRGB,
        BC2_UNORM,
        BC2_UNORM_SRGB,
        BC3_UNORM,
        BC3_UNORM_SRGB,
        BC4_UNORM,
        BC4_SNORM,
        BC5_UNORM,
        BC5_SNORM,
        BC6H_UFLOAT,
        BC6H_SFLOAT,
        BC7_UNORM,
        BC7_UNORM_SRGB,

        COUNT,
    };

    enum class EFormatKind : uint8_t
    {
        Integer,
        Normalized,
        Float,
        DepthStencil
    };

    enum class EFormatSupport : uint32_t
    {
        None = 0,

        Buffer = 0x00000001,
        IndexBuffer = 0x00000002,
        VertexBuffer = 0x00000004,

        Texture = 0x00000008,
        DepthStencil = 0x00000010,
        RenderTarget = 0x00000020,
        Blendable = 0x00000040,

        ShaderLoad = 0x00000080,
        ShaderSample = 0x00000100,
        ShaderUavLoad = 0x00000200,
        ShaderUavStore = 0x00000400,
        ShaderAtomic = 0x00000800,
    };
    HRHI_ENUM_CLASS_FLAG_OPERATORS(EFormatSupport)

    enum class EHeapType : uint8_t
    {
        DeviceLocal,
        Upload,
        Readback
    };

    enum class ETextureDimension : uint8_t
    {
        Unknown,
        Texture1D,
        Texture1DArray,
        Texture2D,
        Texture2DArray,
        TextureCube,
        TextureCubeArray,
        Texture2DMS,
        Texture2DMSArray,
        Texture3D
    };

    enum class ECpuAccessMode : uint8_t
    {
        None,
        Read,
        Write
    };

    enum class EResourceStates : uint32_t
    {
        Unknown = 0,
        Common = 0x00000001,
        ConstantBuffer = 0x00000002,
        VertexBuffer = 0x00000004,
        IndexBuffer = 0x00000008,
        IndirectArgument = 0x00000010,
        ShaderResource = 0x00000020,
        UnorderedAccess = 0x00000040,
        RenderTarget = 0x00000080,
        DepthWrite = 0x00000100,
        DepthRead = 0x00000200,
        StreamOut = 0x00000400,
        CopyDest = 0x00000800,
        CopySource = 0x00001000,
        ResolveDest = 0x00002000,
        ResolveSource = 0x00004000,
        Present = 0x00008000,
        AccelStructRead = 0x00010000,
        AccelStructWrite = 0x00020000,
        AccelStructBuildInput = 0x00040000,
        AccelStructBuildBlas = 0x00080000,
        ShadingRateSurface = 0x00100000,
        OpacityMicromapWrite = 0x00200000,
        OpacityMicromapBuildInput = 0x00400000,
        ConvertCoopVecMatrixInput = 0x00800000,
        ConvertCoopVecMatrixOutput = 0x01000000,
    };
    HRHI_ENUM_CLASS_FLAG_OPERATORS(EResourceStates)

    // Flags for resources that need to be shared with other graphics APIs or other GPU devices.
    enum class ESharedResourceFlags : uint32_t
    {
        None = 0,

        // D3D11: adds D3D11_RESOURCE_MISC_SHARED
        // D3D12: adds D3D12_HEAP_FLAG_SHARED
        // Vulkan: adds vk::ExternalMemoryImageCreateInfo and vk::ExportMemoryAllocateInfo/vk::ExternalMemoryBufferCreateInfo
        Shared = 0x01,

        // D3D11: adds (D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
        // D3D12, Vulkan: ignored
        Shared_NTHandle = 0x02,

        // D3D12: adds D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER and D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER
        // D3D11, Vulkan: ignored
        Shared_CrossAdapter = 0x04,
    };
    HRHI_ENUM_CLASS_FLAG_OPERATORS(ESharedResourceFlags)

    enum class EShaderType : uint16_t
    {
        None = 0x0000,

        Compute = 0x0020,

        Vertex = 0x0001,
        Hull = 0x0002,
        Domain = 0x0004,
        Geometry = 0x0008,
        Pixel = 0x0010,
        Amplification = 0x0040,
        Mesh = 0x0080,
        AllGraphics = 0x00DF,

        RayGeneration = 0x0100,
        AnyHit = 0x0200,
        ClosestHit = 0x0400,
        Miss = 0x0800,
        Intersection = 0x1000,
        Callable = 0x2000,
        AllRayTracing = 0x3F00,

        All = 0x3FFF,
    };
    HRHI_ENUM_CLASS_FLAG_OPERATORS(EShaderType)

    enum class EFastGeometryShaderFlags : uint8_t
    {
        ForceFastGS = 0x01,
        UseViewportMask = 0x02,
        OffsetTargetIndexByViewportIndex = 0x04,
        StrictApiOrder = 0x08
    };
    HRHI_ENUM_CLASS_FLAG_OPERATORS(EFastGeometryShaderFlags)

    enum class EBlendFactor : uint8_t
    {
        Zero = 1,
        One = 2,
        SrcColor = 3,
        InvSrcColor = 4,
        SrcAlpha = 5,
        InvSrcAlpha = 6,
        DstAlpha = 7,
        InvDstAlpha = 8,
        DstColor = 9,
        InvDstColor = 10,
        SrcAlphaSaturate = 11,
        ConstantColor = 14,
        InvConstantColor = 15,
        Src1Color = 16,
        InvSrc1Color = 17,
        Src1Alpha = 18,
        InvSrc1Alpha = 19,

        OneMinusSrcColor = InvSrcColor,
        OneMinusSrcAlpha = InvSrcAlpha,
        OneMinusDstAlpha = InvDstAlpha,
        OneMinusDstColor = InvDstColor,
        OneMinusConstantColor = InvConstantColor,
        OneMinusSrc1Color = InvSrc1Color,
        OneMinusSrc1Alpha = InvSrc1Alpha,
    };

    enum class EBlendOp : uint8_t
    {
        Add = 1,
        Subtract = 2,
        ReverseSubtract = 3,
        Min = 4,
        Max = 5
    };

    enum class EColorMask : uint8_t
    {
        Red = 1,
        Green = 2,
        Blue = 4,
        Alpha = 8,
        All = 0xF
    };
    HRHI_ENUM_CLASS_FLAG_OPERATORS(EColorMask)

    enum class ERasterFillMode : uint8_t
    {
        Solid,
        Wireframe,

        Fill = Solid,
        Line = Wireframe
    };

    enum class ERasterCullMode : uint8_t
    {
        Back,
        Front,
        None
    };

    enum class EStencilOp : uint8_t
    {
        Keep = 1,
        Zero = 2,
        Replace = 3,
        IncrementAndClamp = 4,
        DecrementAndClamp = 5,
        Invert = 6,
        IncrementAndWrap = 7,
        DecrementAndWrap = 8
    };

    enum class EComparisonFunc : uint8_t
    {
        Never = 1,
        Less = 2,
        Equal = 3,
        LessOrEqual = 4,
        Greater = 5,
        NotEqual = 6,
        GreaterOrEqual = 7,
        Always = 8
    };

    enum class ESamplerAddressMode : uint8_t
    {
        Clamp,
        Wrap,
        Border,
        Mirror,
        MirrorOnce,

        ClampToEdge = Clamp,
        Repeat = Wrap,
        ClampToBorder = Border,
        MirroredRepeat = Mirror,
        MirrorClampToEdge = MirrorOnce
    };

    enum class ESamplerReductionType : uint8_t
    {
        Standard,
        Comparison,
        Minimum,
        Maximum
    };

    enum class EResourceType : uint8_t
    {
        None,
        Texture_SRV,
        Texture_UAV,
        TypedBuffer_SRV,
        TypedBuffer_UAV,
        StructuredBuffer_SRV,
        StructuredBuffer_UAV,
        RawBuffer_SRV,
        RawBuffer_UAV,
        ConstantBuffer,
        VolatileConstantBuffer,
        Sampler,
        RayTracingAccelStruct,
        PushConstants,
        SamplerFeedbackTexture_UAV,

        Count
    };

    enum class EPrimitiveType : uint8_t
    {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
        TriangleFan,
        TriangleListWithAdjacency,
        TriangleStripWithAdjacency,
        PatchList
    };

    enum class EVariableShadingRate : uint8_t
    {
        e1x1,
        e1x2,
        e2x1,
        e2x2,
        e2x4,
        e4x2,
        e4x4
    };

    enum class EShadingRateCombiner : uint8_t
    {
        Passthrough,
        Override,
        Min,
        Max,
        ApplyRelative
    };

    enum class EFeature : uint8_t
    {
        ComputeQueue,
        ConservativeRasterization,
        ConstantBufferRanges,
        CopyQueue,
        DeferredCommandLists,
        FastGeometryShader,
        HeapDirectlyIndexed,
        HlslExtensionUAV,
        LinearSweptSpheres,
        Meshlets,
        RayQuery,
        RayTracingAccelStruct,
        RayTracingClusters,
        RayTracingOpacityMicromap,
        RayTracingPipeline,
        SamplerFeedback,
        ShaderExecutionReordering,
        ShaderSpecializations,
        SinglePassStereo,
        Spheres,
        VariableRateShading,
        VirtualResources,
        WaveLaneCountMinMax,
        CooperativeVectorInferencing,
        CooperativeVectorTraining
    };

    enum class EMessageSeverity : uint8_t
    {
        Info,
        Warning,
        Error,
        Fatal
    };

    enum class ECommandQueue : uint8_t
    {
        Graphics = 0,
        Compute,
        Copy,

        Count
    };

    enum ESamplerFeedbackFormat : uint8_t
    {
        MinMipOpaque = 0x0,
        MipRegionUsedOpaque = 0x1,
    };

    namespace Hrt
    {
        enum class EOpacityMicromapFormat
        {
            OC1_2_State = 1,
            OC1_4_State = 2,
        };

        enum class EOpacityMicromapBuildFlags : uint8_t
        {
            None = 0,
            FastTrace = 1,
            FastBuild = 2,
            AllowCompaction = 4
        };
        HRHI_ENUM_CLASS_FLAG_OPERATORS(EOpacityMicromapBuildFlags)

        enum class EGeometryFlags : uint8_t
        {
            None = 0,
            Opaque = 1,
            NoDuplicateAnyHitInvocation = 2
        };
        HRHI_ENUM_CLASS_FLAG_OPERATORS(EGeometryFlags)

        enum class GeometryType : uint8_t
        {
            Triangles = 0,
            AABBs = 1,
            Spheres = 2,
            Lss = 3
        };

        enum class EGeometryLssPrimitiveFormat : uint8_t
        {
            List = 0,
            SuccessiveImplicit = 1
        };

        enum class EGeometryLssEndcapMode : uint8_t
        {
            None = 0,
            Chained = 1
        };

        enum class EInstanceFlags : unsigned
        {
            None = 0,
            TriangleCullDisable = 1,
            TriangleFrontCounterclockwise = 2,
            ForceOpaque = 4,
            ForceNonOpaque = 8,
            ForceOMM2State = 16,
            DisableOMMs = 32,
        };
        HRHI_ENUM_CLASS_FLAG_OPERATORS(EInstanceFlags)

        enum class EAccelStructBuildFlags : uint8_t
        {
            None = 0,
            AllowUpdate = 1,
            AllowCompaction = 2,
            PreferFastTrace = 4,
            PreferFastBuild = 8,
            MinimizeMemory = 0x10,
            PerformUpdate = 0x20,

            // Removes the errors or warnings that HRHI validation layer issues when a TLAS
            // includes an instance that points at a NULL BLAS or has a zero instance mask.
            // Only affects the validation layer, doesn't translate to Vk/DX12 AS build flags.
            AllowEmptyInstances = 0x80
        };
        HRHI_ENUM_CLASS_FLAG_OPERATORS(EAccelStructBuildFlags)

        namespace HCluster
        {
            enum class EOperationType : uint8_t
            {
                Move,                       // Moves CLAS, CLAS Templates, or Cluster BLAS
                ClasBuild,                  // Builds CLAS from clusters of triangles
                ClasBuildTemplates,         // Builds CLAS templates from triangles
                ClasInstantiateTemplates,   // Instantiates CLAS templates
                BlasBuild                   // Builds Cluster BLAS from CLAS
            };

            enum class EOperationMoveType : uint8_t
            {
                BottomLevel,                // Moved objects are Clustered BLAS
                ClusterLevel,               // Moved objects are CLAS
                Template                    // Moved objects are Cluster Templates
            };

            enum class EOperationMode : uint8_t
            {
                ImplicitDestinations,       // Provide total buffer space, driver places results within, returns VAs and actual sizes
                ExplicitDestinations,       // Provide individual target VAs, driver places them there, returns actual sizes
                GetSizes                    // Get minimum size per element
            };

            enum class EOperationFlags : uint8_t
            {
                None = 0x0,
                FastTrace = 0x1,
                FastBuild = 0x2,
                NoOverlap = 0x4,
                AllowOMM = 0x8
            };
            HRHI_ENUM_CLASS_FLAG_OPERATORS(EOperationFlags);

            enum class EOperationIndexFormat : uint8_t
            {
                IndexFormat8bit = 1,
                IndexFormat16bit = 2,
                IndexFormat32bit = 4
            };
        }
    }

    namespace HCoopVec
    {
        enum class EDataType
        {
            UInt8,
            SInt8,
            UInt8Packed,
            SInt8Packed,
            UInt16,
            SInt16,
            UInt32,
            SInt32,
            UInt64,
            SInt64,
            FloatE4M3,
            FloatE5M2,
            Float16,
            BFloat16,
            Float32,
            Float64
        };

        enum class EMatrixLayout
        {
            RowMajor,
            ColumnMajor,
            InferencingOptimal,
            TrainingOptimal
        };
    }
}

#undef HRHI_ENUM_CLASS_FLAG_OPERATORS