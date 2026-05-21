#pragma once

#include <cmath>
#include <string>
#include <vector>

#include <BackEnd/RHIenums.h>
#include <BackEnd/RHIconstants.h>
#include <BackEnd/RHItypedefs.h>
#include <Common/resource.h>

namespace HRHI
{
    static constexpr uint32_t cHeaderVersion = 23;

    struct ZWColor
    {
        float r, g, b, a;

        ZWColor() : r(0.f), g(0.f), b(0.f), a(0.f) {}
        ZWColor(float c) : r(c), g(c), b(c), a(c) {}
        ZWColor(float _r, float _g, float _b, float _a) : r(_r), g(_g), b(_b), a(_a) {}

        bool operator ==(const ZWColor& _b) const { return r == _b.r && g == _b.g && b == _b.b && a == _b.a; }
        bool operator !=(const ZWColor& _b) const { return !(*this == _b); }
    };

    struct ZWViewport
    {
        float minX, maxX;
        float minY, maxY;
        float minZ, maxZ;

        ZWViewport() : minX(0.f), maxX(0.f), minY(0.f), maxY(0.f), minZ(0.f), maxZ(1.f) {}

        ZWViewport(float width, float height) : minX(0.f), maxX(width), minY(0.f), maxY(height), minZ(0.f), maxZ(1.f) {}

        ZWViewport(float _minX, float _maxX, float _minY, float _maxY, float _minZ, float _maxZ)
            : minX(_minX), maxX(_maxX), minY(_minY), maxY(_maxY), minZ(_minZ), maxZ(_maxZ)
        {
        }

        bool operator ==(const ZWViewport& b) const
        {
            return minX == b.minX
                && minY == b.minY
                && minZ == b.minZ
                && maxX == b.maxX
                && maxY == b.maxY
                && maxZ == b.maxZ;
        }
        bool operator !=(const ZWViewport& b) const { return !(*this == b); }

        [[nodiscard]] float width() const { return maxX - minX; }
        [[nodiscard]] float height() const { return maxY - minY; }
    };

    struct ZWRect
    {
        int32_t minX, maxX;
        int32_t minY, maxY;

        ZWRect() : minX(0), maxX(0), minY(0), maxY(0) {}
        ZWRect(int32_t width, int32_t height) : minX(0), maxX(width), minY(0), maxY(height) {}
        ZWRect(int32_t _minX, int32_t _maxX, int32_t _minY, int32_t _maxY) : minX(_minX), maxX(_maxX), minY(_minY), maxY(_maxY) {}
        explicit ZWRect(const ZWViewport& viewport)
            : minX(int32_t(floorf(viewport.minX)))
            , maxX(int32_t(ceilf(viewport.maxX)))
            , minY(int32_t(floorf(viewport.minY)))
            , maxY(int32_t(ceilf(viewport.maxY)))
        {
        }

        bool operator ==(const ZWRect& b) const {
            return minX == b.minX && minY == b.minY && maxX == b.maxX && maxY == b.maxY;
        }
        bool operator !=(const ZWRect& b) const { return !(*this == b); }

        [[nodiscard]] int32_t width() const { return maxX - minX; }
        [[nodiscard]] int32_t height() const { return maxY - minY; }
    };

    struct ZWFormatInfo
    {
        EFormat format;
        const char* name;
        uint8_t bytesPerBlock;
        uint8_t blockSize;
        EFormatKind kind;
        bool hasRed : 1;
        bool hasGreen : 1;
        bool hasBlue : 1;
        bool hasAlpha : 1;
        bool hasDepth : 1;
        bool hasStencil : 1;
        bool isSigned : 1;
        bool isSRGB : 1;
    };

    const ZWFormatInfo& GetFormatInfo(EFormat format);

    struct ZWMemoryRequirements
    {
        uint64_t size = 0;
        uint64_t alignment = 0;
    };

    struct ZWTextureDesc;

    // Describes a 2D or 3D section of a single mip level, single array slice of a texture.
    struct ZWTextureSlice
    {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t z = 0;
        // -1 means the entire dimension is part of the region
        // resolve() below will translate these values into actual dimensions
        uint32_t width = uint32_t(-1);
        uint32_t height = uint32_t(-1);
        uint32_t depth = uint32_t(-1);

        MipLevel mipLevel = 0;
        ArraySlice arraySlice = 0;

        [[nodiscard]] ZWTextureSlice resolve(const ZWTextureDesc& desc) const;

        constexpr ZWTextureSlice& setOrigin(uint32_t vx = 0, uint32_t vy = 0, uint32_t vz = 0) { x = vx; y = vy; z = vz; return *this; }
        constexpr ZWTextureSlice& setWidth(uint32_t value) { width = value; return *this; }
        constexpr ZWTextureSlice& setHeight(uint32_t value) { height = value; return *this; }
        constexpr ZWTextureSlice& setDepth(uint32_t value) { depth = value; return *this; }
        constexpr ZWTextureSlice& setSize(uint32_t vx = uint32_t(-1), uint32_t vy = uint32_t(-1), uint32_t vz = uint32_t(-1)) { width = vx; height = vy; depth = vz; return *this; }
        constexpr ZWTextureSlice& setMipLevel(MipLevel level) { mipLevel = level; return *this; }
        constexpr ZWTextureSlice& setArraySlice(ArraySlice slice) { arraySlice = slice; return *this; }
    };

    struct ZWTextureSubresourceSet
    {
        static constexpr MipLevel AllMipLevels = MipLevel(-1);
        static constexpr ArraySlice AllArraySlices = ArraySlice(-1);

        MipLevel baseMipLevel = 0;
        MipLevel numMipLevels = 1;
        ArraySlice baseArraySlice = 0;
        ArraySlice numArraySlices = 1;

        ZWTextureSubresourceSet() = default;

        ZWTextureSubresourceSet(MipLevel _baseMipLevel, MipLevel _numMipLevels, ArraySlice _baseArraySlice, ArraySlice _numArraySlices)
            : baseMipLevel(_baseMipLevel)
            , numMipLevels(_numMipLevels)
            , baseArraySlice(_baseArraySlice)
            , numArraySlices(_numArraySlices)
        {
        }

        [[nodiscard]] ZWTextureSubresourceSet resolve(const ZWTextureDesc& desc, bool singleMipLevel) const;
        [[nodiscard]] bool isEntireTexture(const ZWTextureDesc& desc) const;

        bool operator ==(const ZWTextureSubresourceSet& other) const
        {
            return baseMipLevel == other.baseMipLevel &&
                numMipLevels == other.numMipLevels &&
                baseArraySlice == other.baseArraySlice &&
                numArraySlices == other.numArraySlices;
        }
        bool operator !=(const ZWTextureSubresourceSet& other) const { return !(*this == other); }

        constexpr ZWTextureSubresourceSet& setBaseMipLevel(MipLevel value) { baseMipLevel = value; return *this; }
        constexpr ZWTextureSubresourceSet& setNumMipLevels(MipLevel value) { numMipLevels = value; return *this; }
        constexpr ZWTextureSubresourceSet& setMipLevels(MipLevel base, MipLevel num) { baseMipLevel = base; numMipLevels = num; return *this; }
        constexpr ZWTextureSubresourceSet& setBaseArraySlice(ArraySlice value) { baseArraySlice = value; return *this; }
        constexpr ZWTextureSubresourceSet& setNumArraySlices(ArraySlice value) { numArraySlices = value; return *this; }
        constexpr ZWTextureSubresourceSet& setArraySlices(ArraySlice base, ArraySlice num) { baseArraySlice = base; numArraySlices = num; return *this; }
    };

    static const ZWTextureSubresourceSet sAllSubresources = ZWTextureSubresourceSet(0, ZWTextureSubresourceSet::AllMipLevels, 0, ZWTextureSubresourceSet::AllArraySlices);

    struct ZWTiledTextureCoordinate
    {
        uint16_t mipLevel = 0;
        uint16_t arrayLevel = 0;
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t z = 0;
    };

    struct ZWTiledTextureRegion
    {
        uint32_t tilesNum = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 0;
    };

    class IHeap;
    struct ZWTextureTilesMapping
    {
        ZWTiledTextureCoordinate* tiledTextureCoordinates = nullptr;
        ZWTiledTextureRegion* tiledTextureRegions = nullptr;
        uint64_t* byteOffsets = nullptr;
        uint32_t numTextureRegions = 0;
        IHeap* heap = nullptr;
    };

    struct ZWPackedMipDesc
    {
        uint32_t numStandardMips = 0;
        uint32_t numPackedMips = 0;
        uint32_t numTilesForPackedMips = 0;
        uint32_t startTileIndexInOverallResource = 0;
    };

    struct ZWTileShape
    {
        uint32_t widthInTexels = 0;
        uint32_t heightInTexels = 0;
        uint32_t depthInTexels = 0;
    };

    struct ZWSubresourceTiling
    {
        uint32_t widthInTiles = 0;
        uint32_t heightInTiles = 0;
        uint32_t depthInTiles = 0;
        uint32_t startTileIndexInOverallResource = 0;
    };

    struct ZWBufferDesc
    {
        uint64_t byteSize = 0;
        uint32_t structStride = 0; // if non-zero it's structured
        uint32_t maxVersions = 0; // only valid and required to be nonzero for volatile buffers on Vulkan
        std::string debugName;
        EFormat format = EFormat::UNKNOWN; // for typed buffer views
        bool canHaveUAVs = false;
        bool canHaveTypedViews = false;
        bool canHaveRawViews = false;
        bool isVertexBuffer = false;
        bool isIndexBuffer = false;
        bool isConstantBuffer = false;
        bool isDrawIndirectArgs = false;
        bool isAccelStructBuildInput = false;
        bool isAccelStructStorage = false;
        bool isShaderBindingTable = false;

        // A dynamic/upload buffer whose contents only live in the current command list
        bool isVolatile = false;

        // Indicates that the buffer is created with no backing memory,
        // and memory is bound to the buffer later using bindBufferMemory.
        // On DX12, the buffer resource is created at the time of memory binding.
        bool isVirtual = false;

        EResourceStates initialState = EResourceStates::Common;

        // see TextureDesc::keepInitialState
        bool keepInitialState = false;

        ECpuAccessMode cpuAccess = ECpuAccessMode::None;

        ESharedResourceFlags sharedResourceFlags = ESharedResourceFlags::None;

        constexpr ZWBufferDesc& setByteSize(uint64_t value) { byteSize = value; return *this; }
        constexpr ZWBufferDesc& setStructStride(uint32_t value) { structStride = value; return *this; }
        constexpr ZWBufferDesc& setMaxVersions(uint32_t value) { maxVersions = value; return *this; }
        ZWBufferDesc& setDebugName(const std::string& value) { debugName = value; return *this; }
        constexpr ZWBufferDesc& setFormat(EFormat value) { format = value; return *this; }
        constexpr ZWBufferDesc& setCanHaveUAVs(bool value) { canHaveUAVs = value; return *this; }
        constexpr ZWBufferDesc& setCanHaveTypedViews(bool value) { canHaveTypedViews = value; return *this; }
        constexpr ZWBufferDesc& setCanHaveRawViews(bool value) { canHaveRawViews = value; return *this; }
        constexpr ZWBufferDesc& setIsVertexBuffer(bool value) { isVertexBuffer = value; return *this; }
        constexpr ZWBufferDesc& setIsIndexBuffer(bool value) { isIndexBuffer = value; return *this; }
        constexpr ZWBufferDesc& setIsConstantBuffer(bool value) { isConstantBuffer = value; return *this; }
        constexpr ZWBufferDesc& setIsDrawIndirectArgs(bool value) { isDrawIndirectArgs = value; return *this; }
        constexpr ZWBufferDesc& setIsAccelStructBuildInput(bool value) { isAccelStructBuildInput = value; return *this; }
        constexpr ZWBufferDesc& setIsAccelStructStorage(bool value) { isAccelStructStorage = value; return *this; }
        constexpr ZWBufferDesc& setIsShaderBindingTable(bool value) { isShaderBindingTable = value; return *this; }
        constexpr ZWBufferDesc& setIsVolatile(bool value) { isVolatile = value; return *this; }
        constexpr ZWBufferDesc& setIsVirtual(bool value) { isVirtual = value; return *this; }
        constexpr ZWBufferDesc& setInitialState(EResourceStates value) { initialState = value; return *this; }
        constexpr ZWBufferDesc& setKeepInitialState(bool value) { keepInitialState = value; return *this; }
        constexpr ZWBufferDesc& setCpuAccess(ECpuAccessMode value) { cpuAccess = value; return *this; }

        // Equivalent to .setInitialState(_initialState).setKeepInitialState(true)
        constexpr ZWBufferDesc& enableAutomaticStateTracking(EResourceStates _initialState)
        {
            initialState = _initialState;
            keepInitialState = true;
            return *this;
        }
    };

    struct ZWBufferRange
    {
        uint64_t byteOffset = 0;
        uint64_t byteSize = 0;

        ZWBufferRange() = default;

        ZWBufferRange(uint64_t _byteOffset, uint64_t _byteSize)
            : byteOffset(_byteOffset)
            , byteSize(_byteSize)
        {
        }

        [[nodiscard]] ZWBufferRange resolve(const ZWBufferDesc& desc) const;
        [[nodiscard]] constexpr bool isEntireBuffer(const ZWBufferDesc& desc) const { return (byteOffset == 0) && (byteSize == ~0ull || byteSize == desc.byteSize); }
        constexpr bool operator== (const ZWBufferRange& other) const { return byteOffset == other.byteOffset && byteSize == other.byteSize; }

        constexpr ZWBufferRange& setByteOffset(uint64_t value) { byteOffset = value; return *this; }
        constexpr ZWBufferRange& setByteSize(uint64_t value) { byteSize = value; return *this; }
    };

    static const ZWBufferRange sEntireBuffer = ZWBufferRange(0, ~0ull);

    struct CustomSemantic
    {
        enum Type
        {
            Undefined = 0,
            XRight = 1,
            ViewportMask = 2
        };

        Type type;
        std::string name;

        constexpr CustomSemantic& setType(Type value) { type = value; return *this; }
        CustomSemantic& setName(const std::string& value) { name = value; return *this; }
    };

    struct ZWShaderSpecialization
    {
        uint32_t constantID = 0;
        union
        {
            uint32_t u = 0;
            int32_t i;
            float f;
        } value;

        static ZWShaderSpecialization UInt32(uint32_t constantID, uint32_t u)
        {
            ZWShaderSpecialization s;
            s.constantID = constantID;
            s.value.u = u;
            return s;
        }

        static ZWShaderSpecialization Int32(uint32_t constantID, int32_t i)
        {
            ZWShaderSpecialization s;
            s.constantID = constantID;
            s.value.i = i;
            return s;
        }

        static ZWShaderSpecialization Float(uint32_t constantID, float f)
        {
            ZWShaderSpecialization s;
            s.constantID = constantID;
            s.value.f = f;
            return s;
        }
    };

    struct ZWBlendState
    {
        struct ZWRenderTarget
        {
            bool        blendEnable = false;
            bool        logicBlendEnable = false;
            EBlendFactor srcBlend = EBlendFactor::One;
            EBlendFactor destBlend = EBlendFactor::Zero;
            EBlendOp     blendOp = EBlendOp::Add;
            EBlendFactor srcBlendAlpha = EBlendFactor::One;
            EBlendFactor destBlendAlpha = EBlendFactor::Zero;
            EBlendOp     blendOpAlpha = EBlendOp::Add;
            ELogicOp     logicBlendOp = ELogicOp::Noop;
            EColorMask   colorWriteMask = EColorMask::All;

            constexpr ZWRenderTarget& setBlendEnable(bool enable) { blendEnable = enable; return *this; }
            constexpr ZWRenderTarget& setLogicBlendEnable(bool enable) { logicBlendEnable = enable; return *this; }
            constexpr ZWRenderTarget& enableBlend() { blendEnable = true; return *this; }
            constexpr ZWRenderTarget& disableBlend() { blendEnable = false; return *this; }
            constexpr ZWRenderTarget& enableLogicBlend() { logicBlendEnable = true; return *this; }
            constexpr ZWRenderTarget& disableLogicBlend() { logicBlendEnable = false; return *this; }
            constexpr ZWRenderTarget& setSrcBlend(EBlendFactor value) { srcBlend = value; return *this; }
            constexpr ZWRenderTarget& setDestBlend(EBlendFactor value) { destBlend = value; return *this; }
            constexpr ZWRenderTarget& setBlendOp(EBlendOp value) { blendOp = value; return *this; }
            constexpr ZWRenderTarget& setSrcBlendAlpha(EBlendFactor value) { srcBlendAlpha = value; return *this; }
            constexpr ZWRenderTarget& setDestBlendAlpha(EBlendFactor value) { destBlendAlpha = value; return *this; }
            constexpr ZWRenderTarget& setBlendOpAlpha(EBlendOp value) { blendOpAlpha = value; return *this; }
            constexpr ZWRenderTarget& setLogicBlendOp(ELogicOp value) { logicBlendOp = value; return *this; }
            constexpr ZWRenderTarget& setColorWriteMask(EColorMask value) { colorWriteMask = value; return *this; }

            [[nodiscard]] bool usesConstantColor() const;

            constexpr bool operator ==(const ZWRenderTarget& other) const
            {
                return blendEnable == other.blendEnable
                    && logicBlendEnable == other.logicBlendEnable
                    && srcBlend == other.srcBlend
                    && destBlend == other.destBlend
                    && blendOp == other.blendOp
                    && srcBlendAlpha == other.srcBlendAlpha
                    && destBlendAlpha == other.destBlendAlpha
                    && blendOpAlpha == other.blendOpAlpha
                    && logicBlendOp == other.logicBlendOp
                    && colorWriteMask == other.colorWriteMask;
            }

            constexpr bool operator !=(const ZWRenderTarget& other) const
            {
                return !(*this == other);
            }
        };

        ZWRenderTarget targets[gMaxRenderTargets];
        bool alphaToCoverageEnable = false;
        bool independentBlendEnabled = false;

        constexpr ZWBlendState& setRenderTarget(uint32_t index, const ZWRenderTarget& target) { targets[index] = target; return *this; }
        constexpr ZWBlendState& setAlphaToCoverageEnable(bool enable) { alphaToCoverageEnable = enable; return *this; }
        constexpr ZWBlendState& setIndependentBlendEnable(bool enable) { independentBlendEnabled = enable; return *this; }
        constexpr ZWBlendState& enableAlphaToCoverage() { alphaToCoverageEnable = true; return *this; }
        constexpr ZWBlendState& disableAlphaToCoverage() { alphaToCoverageEnable = false; return *this; }
        constexpr ZWBlendState& enableIndependentBlend() { independentBlendEnabled = true; return *this; }
        constexpr ZWBlendState& disableIndependentBlend() { independentBlendEnabled = false; return *this; }


        [[nodiscard]] bool usesConstantColor(uint32_t numTargets) const;

        constexpr bool operator ==(const ZWBlendState& other) const
        {
            if (alphaToCoverageEnable != other.alphaToCoverageEnable || independentBlendEnabled != other.independentBlendEnabled)
                return false;

            for (uint32_t i = 0; i < gMaxRenderTargets; ++i)
            {
                if (targets[i] != other.targets[i])
                    return false;
            }

            return true;
        }

        constexpr bool operator !=(const ZWBlendState& other) const
        {
            return !(*this == other);
        }
    };

    struct ZWRasterState
    {
        ERasterFillMode fillMode = ERasterFillMode::Solid;
        ERasterCullMode cullMode = ERasterCullMode::Back;
        bool frontCounterClockwise = false;
        bool depthClipEnable = false;
        bool scissorEnable = false;
        bool multisampleEnable = false;
        bool antialiasedLineEnable = false;
        int32_t depthBias = 0;
        float depthBiasClamp = 0.f;
        float slopeScaledDepthBias = 0.f;

        // Extended rasterizer state supported by Maxwell
        uint8_t forcedSampleCount = 0;
        bool programmableSamplePositionsEnable = false;
        bool conservativeRasterEnable = false;
        bool quadFillEnable = false;
        char samplePositionsX[16]{};
        char samplePositionsY[16]{};

        constexpr ZWRasterState& setFillMode(ERasterFillMode value) { fillMode = value; return *this; }
        constexpr ZWRasterState& setFillSolid() { fillMode = ERasterFillMode::Solid; return *this; }
        constexpr ZWRasterState& setFillWireframe() { fillMode = ERasterFillMode::Wireframe; return *this; }
        constexpr ZWRasterState& setCullMode(ERasterCullMode value) { cullMode = value; return *this; }
        constexpr ZWRasterState& setCullBack() { cullMode = ERasterCullMode::Back; return *this; }
        constexpr ZWRasterState& setCullFront() { cullMode = ERasterCullMode::Front; return *this; }
        constexpr ZWRasterState& setCullNone() { cullMode = ERasterCullMode::None; return *this; }
        constexpr ZWRasterState& setFrontCounterClockwise(bool value) { frontCounterClockwise = value; return *this; }
        constexpr ZWRasterState& setDepthClipEnable(bool value) { depthClipEnable = value; return *this; }
        constexpr ZWRasterState& enableDepthClip() { depthClipEnable = true; return *this; }
        constexpr ZWRasterState& disableDepthClip() { depthClipEnable = false; return *this; }
        constexpr ZWRasterState& setScissorEnable(bool value) { scissorEnable = value; return *this; }
        constexpr ZWRasterState& enableScissor() { scissorEnable = true; return *this; }
        constexpr ZWRasterState& disableScissor() { scissorEnable = false; return *this; }
        constexpr ZWRasterState& setMultisampleEnable(bool value) { multisampleEnable = value; return *this; }
        constexpr ZWRasterState& enableMultisample() { multisampleEnable = true; return *this; }
        constexpr ZWRasterState& disableMultisample() { multisampleEnable = false; return *this; }
        constexpr ZWRasterState& setAntialiasedLineEnable(bool value) { antialiasedLineEnable = value; return *this; }
        constexpr ZWRasterState& enableAntialiasedLine() { antialiasedLineEnable = true; return *this; }
        constexpr ZWRasterState& disableAntialiasedLine() { antialiasedLineEnable = false; return *this; }
        constexpr ZWRasterState& setDepthBias(int32_t value) { depthBias = value; return *this; }
        constexpr ZWRasterState& setDepthBiasClamp(float value) { depthBiasClamp = value; return *this; }
        constexpr ZWRasterState& setSlopeScaleDepthBias(float value) { slopeScaledDepthBias = value; return *this; }
        constexpr ZWRasterState& setForcedSampleCount(uint8_t value) { forcedSampleCount = value; return *this; }
        constexpr ZWRasterState& setProgrammableSamplePositionsEnable(bool value) { programmableSamplePositionsEnable = value; return *this; }
        constexpr ZWRasterState& enableProgrammableSamplePositions() { programmableSamplePositionsEnable = true; return *this; }
        constexpr ZWRasterState& disableProgrammableSamplePositions() { programmableSamplePositionsEnable = false; return *this; }
        constexpr ZWRasterState& setConservativeRasterEnable(bool value) { conservativeRasterEnable = value; return *this; }
        constexpr ZWRasterState& enableConservativeRaster() { conservativeRasterEnable = true; return *this; }
        constexpr ZWRasterState& disableConservativeRaster() { conservativeRasterEnable = false; return *this; }
        constexpr ZWRasterState& setQuadFillEnable(bool value) { quadFillEnable = value; return *this; }
        constexpr ZWRasterState& enableQuadFill() { quadFillEnable = true; return *this; }
        constexpr ZWRasterState& disableQuadFill() { quadFillEnable = false; return *this; }
        constexpr ZWRasterState& setSamplePositions(const char* x, const char* y, int count) { for (int i = 0; i < count; i++) { samplePositionsX[i] = x[i]; samplePositionsY[i] = y[i]; } return *this; }
    };

    struct ZWDepthStencilState
    {
        struct ZWStencilOpDesc
        {
            EStencilOp failOp = EStencilOp::Keep;
            EStencilOp depthFailOp = EStencilOp::Keep;
            EStencilOp passOp = EStencilOp::Keep;
            EComparisonFunc stencilFunc = EComparisonFunc::Always;

            constexpr ZWStencilOpDesc& setFailOp(EStencilOp value) { failOp = value; return *this; }
            constexpr ZWStencilOpDesc& setDepthFailOp(EStencilOp value) { depthFailOp = value; return *this; }
            constexpr ZWStencilOpDesc& setPassOp(EStencilOp value) { passOp = value; return *this; }
            constexpr ZWStencilOpDesc& setStencilFunc(EComparisonFunc value) { stencilFunc = value; return *this; }
        };

        bool            depthTestEnable = true;
        bool            depthWriteEnable = true;
        EComparisonFunc  depthFunc = EComparisonFunc::Less;
        bool            stencilEnable = false;
        uint8_t         stencilReadMask = 0xff;
        uint8_t         stencilWriteMask = 0xff;
        uint8_t         stencilRefValue = 0;
        bool            dynamicStencilRef = false;
        ZWStencilOpDesc   frontFaceStencil;
        ZWStencilOpDesc   backFaceStencil;

        constexpr ZWDepthStencilState& setDepthTestEnable(bool value) { depthTestEnable = value; return *this; }
        constexpr ZWDepthStencilState& enableDepthTest() { depthTestEnable = true; return *this; }
        constexpr ZWDepthStencilState& disableDepthTest() { depthTestEnable = false; return *this; }
        constexpr ZWDepthStencilState& setDepthWriteEnable(bool value) { depthWriteEnable = value; return *this; }
        constexpr ZWDepthStencilState& enableDepthWrite() { depthWriteEnable = true; return *this; }
        constexpr ZWDepthStencilState& disableDepthWrite() { depthWriteEnable = false; return *this; }
        constexpr ZWDepthStencilState& setDepthFunc(EComparisonFunc value) { depthFunc = value; return *this; }
        constexpr ZWDepthStencilState& setStencilEnable(bool value) { stencilEnable = value; return *this; }
        constexpr ZWDepthStencilState& enableStencil() { stencilEnable = true; return *this; }
        constexpr ZWDepthStencilState& disableStencil() { stencilEnable = false; return *this; }
        constexpr ZWDepthStencilState& setStencilReadMask(uint8_t value) { stencilReadMask = value; return *this; }
        constexpr ZWDepthStencilState& setStencilWriteMask(uint8_t value) { stencilWriteMask = value; return *this; }
        constexpr ZWDepthStencilState& setStencilRefValue(uint8_t value) { stencilRefValue = value; return *this; }
        constexpr ZWDepthStencilState& setFrontFaceStencil(const ZWStencilOpDesc& value) { frontFaceStencil = value; return *this; }
        constexpr ZWDepthStencilState& setBackFaceStencil(const ZWStencilOpDesc& value) { backFaceStencil = value; return *this; }
        constexpr ZWDepthStencilState& setDynamicStencilRef(bool value) { dynamicStencilRef = value; return *this; }
    };

    struct ZWViewportState
    {
        // These are in pixels
        // note: you can only set each of these either in the PSO or per draw call in DrawArguments
        // it is not legal to have the same state set in both the PSO and DrawArguments
        // leaving these vectors empty means no state is set
        HCommon::StaticVector<ZWViewport, gMaxViewports> viewports;
        HCommon::StaticVector<ZWRect, gMaxViewports> scissorRects;

        ZWViewportState& addViewport(const ZWViewport& v) { viewports.push_back(v); return *this; }
        ZWViewportState& addScissorRect(const ZWRect& r) { scissorRects.push_back(r); return *this; }
        ZWViewportState& addViewportAndScissorRect(const ZWViewport& v) { return addViewport(v).addScissorRect(ZWRect(v)); }
    };

    struct ZWFramebufferDesc;

    // Describes the parameters of a framebuffer that can be used to determine if a given framebuffer
    // is compatible with a certain graphics or meshlet pipeline object. All fields of FramebufferInfo
    // must match between the framebuffer and the pipeline for them to be compatible.
    struct ZWFramebufferInfo
    {
        HCommon::StaticVector<EFormat, gMaxRenderTargets> colorFormats;
        EFormat depthFormat = EFormat::UNKNOWN;
        uint32_t sampleCount = 1;
        uint32_t sampleQuality = 0;

        ZWFramebufferInfo() = default;
        ZWFramebufferInfo(const ZWFramebufferDesc& desc);

        bool operator==(const ZWFramebufferInfo& other) const
        {
            return formatsEqual(colorFormats, other.colorFormats)
                && depthFormat == other.depthFormat
                && sampleCount == other.sampleCount
                && sampleQuality == other.sampleQuality;
        }
        bool operator!=(const ZWFramebufferInfo& other) const { return !(*this == other); }

        ZWFramebufferInfo& addColorFormat(EFormat format) { colorFormats.push_back(format); return *this; }
        ZWFramebufferInfo& setDepthFormat(EFormat format) { depthFormat = format; return *this; }
        ZWFramebufferInfo& setSampleCount(uint32_t count) { sampleCount = count; return *this; }
        ZWFramebufferInfo& setSampleQuality(uint32_t quality) { sampleQuality = quality; return *this; }

    private:
        static bool formatsEqual(const HCommon::StaticVector<EFormat, gMaxRenderTargets>& a, const HCommon::StaticVector<EFormat, gMaxRenderTargets>& b)
        {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); i++) if (a[i] != b[i]) return false;
            return true;
        }
    };

    // An extended version of FramebufferInfo that also contains the framebuffer dimensions.
    struct ZWFramebufferInfoEx : ZWFramebufferInfo
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t arraySize = 1;

        ZWFramebufferInfoEx() = default;
        ZWFramebufferInfoEx(const ZWFramebufferDesc& desc);

        ZWFramebufferInfoEx& setWidth(uint32_t value) { width = value; return *this; }
        ZWFramebufferInfoEx& setHeight(uint32_t value) { height = value; return *this; }
        ZWFramebufferInfoEx& setArraySize(uint32_t value) { arraySize = value; return *this; }

        [[nodiscard]] ZWViewport getViewport(float minZ = 0.f, float maxZ = 1.f) const
        {
            return ZWViewport(0.f, float(width), 0.f, float(height), minZ, maxZ);
        }
    };

    struct ZWBindingLayoutItem
    {
        uint32_t slot;

        EResourceType type : 8;
        uint8_t unused : 8;
        // Push constant byte size when (type == PushConstants)
        // Descriptor array size (1 or more) for all other resource types
        // Must be 1 for VolatileConstantBuffer
        uint16_t size : 16;

        bool operator ==(const ZWBindingLayoutItem& b) const
        {
            return slot == b.slot
                && type == b.type
                && size == b.size;
        }
        bool operator !=(const ZWBindingLayoutItem& b) const { return !(*this == b); }

        constexpr ZWBindingLayoutItem& setSlot(uint32_t value) { slot = value; return *this; }
        constexpr ZWBindingLayoutItem& setType(EResourceType value) { type = value; return *this; }
        constexpr ZWBindingLayoutItem& setSize(uint32_t value) { size = uint16_t(value); return *this; }

        uint32_t getArraySize() const { return (type == EResourceType::PushConstants) ? 1 : size; }

        // Helper functions for strongly typed initialization
#define HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(TYPE) \
        static ZWBindingLayoutItem TYPE(const uint32_t slot) { \
            ZWBindingLayoutItem result{}; \
            result.slot = slot; \
            result.type = EResourceType::TYPE; \
            result.size = 1; \
            return result; }

        HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(Texture_SRV)
        HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(Texture_UAV)
        HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(TypedBuffer_SRV)
        HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(TypedBuffer_UAV)
        HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(StructuredBuffer_SRV)
        HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(StructuredBuffer_UAV)
        HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(RawBuffer_SRV)
        HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(RawBuffer_UAV)
        HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(ConstantBuffer)
        HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(VolatileConstantBuffer)
        HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(Sampler)
        HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(RayTracingAccelStruct)
        HRHI_BINDING_LAYOUT_ITEM_INITIALIZER(SamplerFeedbackTexture_UAV)

        static ZWBindingLayoutItem PushConstants(const uint32_t slot, const size_t size)
        {
            ZWBindingLayoutItem result{};
            result.slot = slot;
            result.type = EResourceType::PushConstants;
            result.size = uint16_t(size);
            return result;
        }
#undef HRHI_BINDING_LAYOUT_ITEM_INITIALIZER
    };

    // verify the packing of BindingLayoutItem for good alignment
    static_assert(sizeof(ZWBindingLayoutItem) == 8, "sizeof(BindingLayoutItem) is supposed to be 8 bytes");

    // Describes compile-time settings for HLSL -> SPIR-V register allocation.
    struct ZWVulkanBindingOffsets
    {
        uint32_t shaderResource = 0;
        uint32_t sampler = 128;
        uint32_t constantBuffer = 256;
        uint32_t unorderedAccess = 384;

        constexpr ZWVulkanBindingOffsets& setShaderResourceOffset(uint32_t value) { shaderResource = value; return *this; }
        constexpr ZWVulkanBindingOffsets& setSamplerOffset(uint32_t value) { sampler = value; return *this; }
        constexpr ZWVulkanBindingOffsets& setConstantBufferOffset(uint32_t value) { constantBuffer = value; return *this; }
        constexpr ZWVulkanBindingOffsets& setUnorderedAccessViewOffset(uint32_t value) { unorderedAccess = value; return *this; }
    };

    struct ZWSinglePassStereoState
    {
        bool enabled = false;
        bool independentViewportMask = false;
        uint16_t renderTargetIndexOffset = 0;

        bool operator ==(const ZWSinglePassStereoState& b) const
        {
            return enabled == b.enabled
                && independentViewportMask == b.independentViewportMask
                && renderTargetIndexOffset == b.renderTargetIndexOffset;
        }

        bool operator !=(const ZWSinglePassStereoState& b) const { return !(*this == b); }

        constexpr ZWSinglePassStereoState& setEnabled(bool value) { enabled = value; return *this; }
        constexpr ZWSinglePassStereoState& setIndependentViewportMask(bool value) { independentViewportMask = value; return *this; }
        constexpr ZWSinglePassStereoState& setRenderTargetIndexOffset(uint16_t value) { renderTargetIndexOffset = value; return *this; }
    };

    struct ZWRenderState
    {
        ZWBlendState blendState;
        ZWDepthStencilState depthStencilState;
        ZWRasterState rasterState;
        ZWSinglePassStereoState singlePassStereo;

        constexpr ZWRenderState& setBlendState(const ZWBlendState& value) { blendState = value; return *this; }
        constexpr ZWRenderState& setDepthStencilState(const ZWDepthStencilState& value) { depthStencilState = value; return *this; }
        constexpr ZWRenderState& setRasterState(const ZWRasterState& value) { rasterState = value; return *this; }
        constexpr ZWRenderState& setSinglePassStereoState(const ZWSinglePassStereoState& value) { singlePassStereo = value; return *this; }
    };

    struct ZWVariableRateShadingState
    {
        bool enabled = false;
        EVariableShadingRate shadingRate = EVariableShadingRate::e1x1;
        EShadingRateCombiner pipelinePrimitiveCombiner = EShadingRateCombiner::Passthrough;
        EShadingRateCombiner imageCombiner = EShadingRateCombiner::Passthrough;

        bool operator ==(const ZWVariableRateShadingState& b) const
        {
            return enabled == b.enabled
                && shadingRate == b.shadingRate
                && pipelinePrimitiveCombiner == b.pipelinePrimitiveCombiner
                && imageCombiner == b.imageCombiner;
        }

        bool operator !=(const ZWVariableRateShadingState& b) const { return !(*this == b); }

        constexpr ZWVariableRateShadingState& setEnabled(bool value) { enabled = value; return *this; }
        constexpr ZWVariableRateShadingState& setShadingRate(EVariableShadingRate value) { shadingRate = value; return *this; }
        constexpr ZWVariableRateShadingState& setPipelinePrimitiveCombiner(EShadingRateCombiner value) { pipelinePrimitiveCombiner = value; return *this; }
        constexpr ZWVariableRateShadingState& setImageCombiner(EShadingRateCombiner value) { imageCombiner = value; return *this; }
    };

    class IBuffer;

    struct ZWVertexBufferBinding
    {
        IBuffer* buffer = nullptr;
        uint32_t slot;
        uint64_t offset;

        bool operator ==(const ZWVertexBufferBinding& b) const
        {
            return buffer == b.buffer
                && slot == b.slot
                && offset == b.offset;
        }
        bool operator !=(const ZWVertexBufferBinding& b) const { return !(*this == b); }

        ZWVertexBufferBinding& setBuffer(IBuffer* value);
        ZWVertexBufferBinding& setSlot(uint32_t value) { slot = value; return *this; }
        ZWVertexBufferBinding& setOffset(uint64_t value) { offset = value; return *this; }
    };

    struct ZWIndexBufferBinding
    {
        IBuffer* buffer = nullptr;
        EFormat format;
        uint32_t offset;

        bool operator ==(const ZWIndexBufferBinding& b) const
        {
            return buffer == b.buffer
                && format == b.format
                && offset == b.offset;
        }
        bool operator !=(const ZWIndexBufferBinding& b) const { return !(*this == b); }

        ZWIndexBufferBinding& setBuffer(IBuffer* value);
        ZWIndexBufferBinding& setFormat(EFormat value) { format = value; return *this; }
        ZWIndexBufferBinding& setOffset(uint32_t value) { offset = value; return *this; }
    };

    class IBindingSet;
    typedef HCommon::StaticVector<IBindingSet*, gMaxBindingLayouts> BindingSetVector;

    class IGraphicsPipeline;
    class IFramebuffer;
    struct ZWGraphicsState
    {
        IGraphicsPipeline* pipeline = nullptr;
        IFramebuffer* framebuffer = nullptr;
        ZWViewportState viewport;
        ZWVariableRateShadingState shadingRateState;
        ZWColor blendConstantColor{};
        uint8_t dynamicStencilRefValue = 0;

        BindingSetVector bindings;

        HCommon::StaticVector<ZWVertexBufferBinding, gMaxVertexAttributes> vertexBuffers;
        ZWIndexBufferBinding indexBuffer;

        IBuffer* indirectParams = nullptr;
        IBuffer* indirectCountBuffer = nullptr;

        ZWGraphicsState& setPipeline(IGraphicsPipeline* value);
        ZWGraphicsState& setFramebuffer(IFramebuffer* value);
        ZWGraphicsState& setViewport(const ZWViewportState& value) { viewport = value; return *this; }
        ZWGraphicsState& setShadingRateState(const ZWVariableRateShadingState& value) { shadingRateState = value; return *this; }
        ZWGraphicsState& setBlendColor(const ZWColor& value) { blendConstantColor = value; return *this; }
        ZWGraphicsState& setDynamicStencilRefValue(uint8_t value) { dynamicStencilRefValue = value; return *this; }
        ZWGraphicsState& addBindingSet(IBindingSet* value);
        ZWGraphicsState& addVertexBuffer(const ZWVertexBufferBinding& value) { vertexBuffers.push_back(value); return *this; }
        ZWGraphicsState& setIndexBuffer(const ZWIndexBufferBinding& value) { indexBuffer = value; return *this; }
        ZWGraphicsState& setIndirectParams(IBuffer* value);
        ZWGraphicsState& setIndirectCountBuffer(IBuffer* value);
    };

    struct ZWDrawArguments
    {
        uint32_t vertexCount = 0;
        uint32_t instanceCount = 1;
        uint32_t startIndexLocation = 0;
        uint32_t startVertexLocation = 0;
        uint32_t startInstanceLocation = 0;

        constexpr ZWDrawArguments& setVertexCount(uint32_t value) { vertexCount = value; return *this; }
        constexpr ZWDrawArguments& setInstanceCount(uint32_t value) { instanceCount = value; return *this; }
        constexpr ZWDrawArguments& setStartIndexLocation(uint32_t value) { startIndexLocation = value; return *this; }
        constexpr ZWDrawArguments& setStartVertexLocation(uint32_t value) { startVertexLocation = value; return *this; }
        constexpr ZWDrawArguments& setStartInstanceLocation(uint32_t value) { startInstanceLocation = value; return *this; }
    };

    struct ZWDrawIndirectArguments
    {
        uint32_t vertexCount = 0;
        uint32_t instanceCount = 1;
        uint32_t startVertexLocation = 0;
        uint32_t startInstanceLocation = 0;

        constexpr ZWDrawIndirectArguments& setVertexCount(uint32_t value) { vertexCount = value; return *this; }
        constexpr ZWDrawIndirectArguments& setInstanceCount(uint32_t value) { instanceCount = value; return *this; }
        constexpr ZWDrawIndirectArguments& setStartVertexLocation(uint32_t value) { startVertexLocation = value; return *this; }
        constexpr ZWDrawIndirectArguments& setStartInstanceLocation(uint32_t value) { startInstanceLocation = value; return *this; }
    };

    struct ZWDrawIndexedIndirectArguments
    {
        uint32_t indexCount = 0;
        uint32_t instanceCount = 1;
        uint32_t startIndexLocation = 0;
        int32_t  baseVertexLocation = 0;
        uint32_t startInstanceLocation = 0;

        constexpr ZWDrawIndexedIndirectArguments& setIndexCount(uint32_t value) { indexCount = value; return *this; }
        constexpr ZWDrawIndexedIndirectArguments& setInstanceCount(uint32_t value) { instanceCount = value; return *this; }
        constexpr ZWDrawIndexedIndirectArguments& setStartIndexLocation(uint32_t value) { startIndexLocation = value; return *this; }
        constexpr ZWDrawIndexedIndirectArguments& setBaseVertexLocation(int32_t value) { baseVertexLocation = value; return *this; }
        constexpr ZWDrawIndexedIndirectArguments& setStartInstanceLocation(uint32_t value) { startInstanceLocation = value; return *this; }
    };

    class IComputePipeline;
    struct ZWComputeState
    {
        IComputePipeline* pipeline = nullptr;

        BindingSetVector bindings;

        IBuffer* indirectParams = nullptr;

        ZWComputeState& setPipeline(IComputePipeline* value);
        ZWComputeState& addBindingSet(IBindingSet* value);
        ZWComputeState& setIndirectParams(IBuffer* value);
    };

    struct ZWDispatchIndirectArguments
    {
        uint32_t groupsX = 1;
        uint32_t groupsY = 1;
        uint32_t groupsZ = 1;

        constexpr ZWDispatchIndirectArguments& setGroupsX(uint32_t value) { groupsX = value; return *this; }
        constexpr ZWDispatchIndirectArguments& setGroupsY(uint32_t value) { groupsY = value; return *this; }
        constexpr ZWDispatchIndirectArguments& setGroupsZ(uint32_t value) { groupsZ = value; return *this; }
        constexpr ZWDispatchIndirectArguments& setGroups2D(uint32_t x, uint32_t y) { groupsX = x; groupsY = y; return *this; }
        constexpr ZWDispatchIndirectArguments& setGroups3D(uint32_t x, uint32_t y, uint32_t z) { groupsX = x; groupsY = y; groupsZ = z; return *this; }
    };

    class IMeshletPipeline;
    struct ZWMeshletState
    {
        IMeshletPipeline* pipeline = nullptr;
        IFramebuffer* framebuffer = nullptr;
        ZWViewportState viewport;
        ZWColor blendConstantColor{};
        uint8_t dynamicStencilRefValue = 0;

        BindingSetVector bindings;

        IBuffer* indirectParams = nullptr;

        ZWMeshletState& setPipeline(IMeshletPipeline* value);
        ZWMeshletState& setFramebuffer(IFramebuffer* value);
        ZWMeshletState& setViewport(const ZWViewportState& value) { viewport = value; return *this; }
        ZWMeshletState& setBlendColor(const ZWColor& value) { blendConstantColor = value; return *this; }
        ZWMeshletState& addBindingSet(IBindingSet* value);
        ZWMeshletState& setIndirectParams(IBuffer* value);
        ZWMeshletState& setDynamicStencilRefValue(uint8_t value) { dynamicStencilRefValue = value; return *this; }
    };

    struct ZWVariableRateShadingFeatureInfo
    {
        uint32_t shadingRateImageTileSize;
    };

    struct ZWWaveLaneCountMinMaxFeatureInfo
    {
        uint32_t minWaveLaneCount;
        uint32_t maxWaveLaneCount;
    };

    class IDevice;
    struct ZWCommandListParameters
    {
        // Two immediate command lists cannot be open at the same time, which is checked by the validation layer.
        bool enableImmediateExecution = true;

        // Minimum size of memory chunks created to upload data to the device on DX12.
        size_t uploadChunkSize = 64 * 1024;

        // Minimum size of memory chunks created for AS build scratch buffers.
        size_t scratchChunkSize = 64 * 1024;

        // Maximum total memory size used for all AS build scratch buffers owned by this command list.
        size_t scratchMaxMemory = 1024 * 1024 * 1024;

        // Type of the queue that this command list is to be executed on.
        // COPY and COMPUTE queues have limited subsets of methods available.
        ECommandQueue queueType = ECommandQueue::Graphics;

        ZWCommandListParameters& setEnableImmediateExecution(bool value) { enableImmediateExecution = value; return *this; }
        ZWCommandListParameters& setUploadChunkSize(size_t value) { uploadChunkSize = value; return *this; }
        ZWCommandListParameters& setScratchChunkSize(size_t value) { scratchChunkSize = value; return *this; }
        ZWCommandListParameters& setScratchMaxMemory(size_t value) { scratchMaxMemory = value; return *this; }
        ZWCommandListParameters& setQueueType(ECommandQueue value) { queueType = value; return *this; }
    };

    namespace HCoopVec
    {
        // Describes a combination of input and output data types for matrix multiplication with Cooperative Vectors.
        // - DX12: Maps from D3D12_COOPERATIVE_VECTOR_PROPERTIES_MUL.
        // - Vulkan: Maps from VkCooperativeVectorPropertiesNV.
        struct ZWMatMulFormatCombo
        {
            EDataType inputType;
            EDataType inputInterpretation;
            EDataType matrixInterpretation;
            EDataType biasInterpretation;
            EDataType outputType;
            bool transposeSupported;
        };

        struct ZWDeviceFeatures
        {
            // Format combinations supported by the device for matrix multiplication with Cooperative Vectors.
            std::vector<ZWMatMulFormatCombo> matMulFormats;

            // - DX12: True if FLOAT16 is supported as accumulation format for both outer product accumulation
            //         and vector accumulation.
            // - Vulkan: True if cooperativeVectorTrainingFloat16Accumulation is supported.
            bool trainingFloat16 = false;

            // - DX12: True if FLOAT32 is supported as accumulation format for both outer product accumulation
            //         and vector accumulation.
            // - Vulkan: True if cooperativeVectorTrainingFloat32Accumulation is supported.
            bool trainingFloat32 = false;
        };

        struct ZWMatrixLayoutDesc
        {
            // Buffer where the matrix is stored.
            IBuffer* buffer = nullptr;

            // Offset in bytes from the start of the buffer where the matrix starts.
            uint64_t offset = 0;

            // Data type of the matrix elements.
            EDataType type = EDataType::UInt8;

            // Layout of the matrix in memory.
            EMatrixLayout layout = EMatrixLayout::RowMajor;

            // Size in bytes of the matrix.
            size_t size = 0;

            // Stride in bytes between rows or columns, depending on the layout.
            // For RowMajor and ColumnMajor layouts, stride may be zero, in which case it is computed automatically.
            // For InferencingOptimal and TrainingOptimal layouts, stride does not matter and should be zero.
            size_t stride = 0;
        };

        // Describes a single matrix layout conversion operation.
        // Used by ICommandList::convertCoopVecMatrices(...)
        struct ZWConvertMatrixLayoutDesc
        {
            ZWMatrixLayoutDesc src;
            ZWMatrixLayoutDesc dst;

            uint32_t numRows = 0;
            uint32_t numColumns = 0;
        };

        // Returns the size in bytes of a given data type.
        size_t getDataTypeSize(EDataType type);

        // Returns the stride for a given matrix if it's stored in a RowMajor or ColumnMajor layout.
        // For other layouts, returns 0.
        size_t getOptimalMatrixStride(EDataType type, EMatrixLayout layout, uint32_t rows, uint32_t columns);
    }

    namespace Hrt
    {
        struct ZWOpacityMicromapUsageCount
        {
            // Number of OMMs with the specified subdivision level and format.
            uint32_t count;
            // Micro triangle count is 4^N, where N is the subdivision level.
            uint32_t subdivisionLevel;
            // OMM input sub format.
            EOpacityMicromapFormat format;
        };

        class IAccelStruct;

        typedef float AffineTransform[12];
        constexpr AffineTransform cIdentityTransform = {
            //  +----+----+---------  rotation and scaling
            //  v    v    v
                1.f, 0.f, 0.f, 0.f,
                0.f, 1.f, 0.f, 0.f,
                0.f, 0.f, 1.f, 0.f
                //                 ^
                //                 +----  translation
        };

        struct GeometryAABB
        {
            float minX;
            float minY;
            float minZ;
            float maxX;
            float maxY;
            float maxZ;
        };

        class IOpacityMicromap;

        struct ZWGeometryTriangles
        {
            IBuffer* indexBuffer = nullptr;   // make sure the first 2 fields in all Geometry 
            IBuffer* vertexBuffer = nullptr;  // structs are IBuffer* for easier debugging
            EFormat indexFormat = EFormat::UNKNOWN;
            EFormat vertexFormat = EFormat::UNKNOWN; // See D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC for accepted formats and how they are interpreted
            uint64_t indexOffset = 0;
            uint64_t vertexOffset = 0;
            uint32_t indexCount = 0;
            uint32_t vertexCount = 0;
            uint32_t vertexStride = 0;

            IOpacityMicromap* opacityMicromap = nullptr;
            IBuffer* ommIndexBuffer = nullptr;
            uint64_t ommIndexBufferOffset = 0;
            EFormat ommIndexFormat = EFormat::UNKNOWN;
            const ZWOpacityMicromapUsageCount* pOmmUsageCounts = nullptr;
            uint32_t numOmmUsageCounts = 0;

            ZWGeometryTriangles& setIndexBuffer(IBuffer* value);
            ZWGeometryTriangles& setVertexBuffer(IBuffer* value);
            ZWGeometryTriangles& setIndexFormat(EFormat value) { indexFormat = value; return *this; }
            ZWGeometryTriangles& setVertexFormat(EFormat value) { vertexFormat = value; return *this; }
            ZWGeometryTriangles& setIndexOffset(uint64_t value) { indexOffset = value; return *this; }
            ZWGeometryTriangles& setVertexOffset(uint64_t value) { vertexOffset = value; return *this; }
            ZWGeometryTriangles& setIndexCount(uint32_t value) { indexCount = value; return *this; }
            ZWGeometryTriangles& setVertexCount(uint32_t value) { vertexCount = value; return *this; }
            ZWGeometryTriangles& setVertexStride(uint32_t value) { vertexStride = value; return *this; }
            ZWGeometryTriangles& setOpacityMicromap(IOpacityMicromap* value);
            ZWGeometryTriangles& setOmmIndexBuffer(IBuffer* value);
            ZWGeometryTriangles& setOmmIndexBufferOffset(uint64_t value) { ommIndexBufferOffset = value; return *this; }
            ZWGeometryTriangles& setOmmIndexFormat(EFormat value) { ommIndexFormat = value; return *this; }
            ZWGeometryTriangles& setPOmmUsageCounts(const ZWOpacityMicromapUsageCount* value) { pOmmUsageCounts = value; return *this; }
            ZWGeometryTriangles& setNumOmmUsageCounts(uint32_t value) { numOmmUsageCounts = value; return *this; }
        };

        struct ZWGeometryAABBs
        {
            IBuffer* buffer = nullptr;
            IBuffer* unused = nullptr;
            uint64_t offset = 0;
            uint32_t count = 0;
            uint32_t stride = 0;

            ZWGeometryAABBs& setBuffer(IBuffer* value);
            ZWGeometryAABBs& setOffset(uint64_t value) { offset = value; return *this; }
            ZWGeometryAABBs& setCount(uint32_t value) { count = value; return *this; }
            ZWGeometryAABBs& setStride(uint32_t value) { stride = value; return *this; }
        };

        struct ZWGeometrySpheres
        {
            IBuffer* indexBuffer = nullptr;
            IBuffer* vertexBuffer = nullptr;
            EFormat indexFormat = EFormat::UNKNOWN;
            EFormat vertexPositionFormat = EFormat::UNKNOWN;
            EFormat vertexRadiusFormat = EFormat::UNKNOWN;
            uint64_t indexOffset = 0;
            uint64_t vertexPositionOffset = 0;
            uint64_t vertexRadiusOffset = 0;
            uint32_t indexCount = 0;
            uint32_t vertexCount = 0;
            uint32_t indexStride = 0;
            uint32_t vertexPositionStride = 0;
            uint32_t vertexRadiusStride = 0;

            ZWGeometrySpheres& setIndexBuffer(IBuffer* value);
            ZWGeometrySpheres& setVertexBuffer(IBuffer* value);
            ZWGeometrySpheres& setIndexFormat(EFormat value) { indexFormat = value; return *this; }
            ZWGeometrySpheres& setVertexPositionFormat(EFormat value) { vertexPositionFormat = value; return *this; }
            ZWGeometrySpheres& setVertexRadiusFormat(EFormat value) { vertexRadiusFormat = value; return *this; }
            ZWGeometrySpheres& setIndexOffset(uint64_t value) { indexOffset = value; return *this; }
            ZWGeometrySpheres& setVertexPositionOffset(uint64_t value) { vertexPositionOffset = value; return *this; }
            ZWGeometrySpheres& setVertexRadiusOffset(uint64_t value) { vertexRadiusOffset = value; return *this; }
            ZWGeometrySpheres& setIndexCount(uint32_t value) { indexCount = value; return *this; }
            ZWGeometrySpheres& setVertexCount(uint32_t value) { vertexCount = value; return *this; }
            ZWGeometrySpheres& setIndexStride(uint32_t value) { indexStride = value; return *this; }
            ZWGeometrySpheres& setVertexPositionStride(uint32_t value) { vertexPositionStride = value; return *this; }
            ZWGeometrySpheres& setVertexRadiusStride(uint32_t value) { vertexRadiusStride = value; return *this; }
        };

        struct ZWGeometryLss
        {
            IBuffer* indexBuffer = nullptr;
            IBuffer* vertexBuffer = nullptr;
            EFormat indexFormat = EFormat::UNKNOWN;
            EFormat vertexPositionFormat = EFormat::UNKNOWN;
            EFormat vertexRadiusFormat = EFormat::UNKNOWN;
            uint64_t indexOffset = 0;
            uint64_t vertexPositionOffset = 0;
            uint64_t vertexRadiusOffset = 0;
            uint32_t indexCount = 0;
            uint32_t primitiveCount = 0;
            uint32_t vertexCount = 0;
            uint32_t indexStride = 0;
            uint32_t vertexPositionStride = 0;
            uint32_t vertexRadiusStride = 0;
            EGeometryLssPrimitiveFormat primitiveFormat = EGeometryLssPrimitiveFormat::List;
            EGeometryLssEndcapMode endcapMode = EGeometryLssEndcapMode::None;

            ZWGeometryLss& setIndexBuffer(IBuffer* value);
            ZWGeometryLss& setVertexBuffer(IBuffer* value);
            ZWGeometryLss& setIndexFormat(EFormat value) { indexFormat = value; return *this; }
            ZWGeometryLss& setVertexPositionFormat(EFormat value) { vertexPositionFormat = value; return *this; }
            ZWGeometryLss& setVertexRadiusFormat(EFormat value) { vertexRadiusFormat = value; return *this; }
            ZWGeometryLss& setIndexOffset(uint64_t value) { indexOffset = value; return *this; }
            ZWGeometryLss& setVertexPositionOffset(uint64_t value) { vertexPositionOffset = value; return *this; }
            ZWGeometryLss& setVertexRadiusOffset(uint64_t value) { vertexRadiusOffset = value; return *this; }
            ZWGeometryLss& setIndexCount(uint32_t value) { indexCount = value; return *this; }
            ZWGeometryLss& setPrimitiveCount(uint32_t value) { primitiveCount = value; return *this; }
            ZWGeometryLss& setVertexCount(uint32_t value) { vertexCount = value; return *this; }
            ZWGeometryLss& setIndexStride(uint32_t value) { indexStride = value; return *this; }
            ZWGeometryLss& setVertexPositionStride(uint32_t value) { vertexPositionStride = value; return *this; }
            ZWGeometryLss& setVertexRadiusStride(uint32_t value) { vertexRadiusStride = value; return *this; }
            ZWGeometryLss& setPrimitiveFormat(EGeometryLssPrimitiveFormat value) { primitiveFormat = value; return *this; }
            ZWGeometryLss& setEndcapMode(EGeometryLssEndcapMode value) { endcapMode = value; return *this; }
        };

        struct ZWGeometryDesc
        {
            union GeomTypeUnion
            {
                ZWGeometryTriangles triangles;
                ZWGeometryAABBs aabbs;
                ZWGeometrySpheres spheres;
                ZWGeometryLss lss;
            } geometryData;

            bool useTransform = false;
            AffineTransform transform{};
            EGeometryFlags flags = EGeometryFlags::None;
            GeometryType geometryType = GeometryType::Triangles;

            ZWGeometryDesc() : geometryData{} {}

            ZWGeometryDesc& setTransform(const AffineTransform& value) { memcpy(&transform, &value, sizeof(AffineTransform)); useTransform = true; return *this; }
            ZWGeometryDesc& setFlags(EGeometryFlags value) { flags = value; return *this; }
            ZWGeometryDesc& setTriangles(const ZWGeometryTriangles& value) { geometryData.triangles = value; geometryType = GeometryType::Triangles; return *this; }
            ZWGeometryDesc& setAABBs(const ZWGeometryAABBs& value) { geometryData.aabbs = value; geometryType = GeometryType::AABBs; return *this; }
            ZWGeometryDesc& setSpheres(const ZWGeometrySpheres& value) { geometryData.spheres = value; geometryType = GeometryType::Spheres; return *this; }
            ZWGeometryDesc& setLss(const ZWGeometryLss& value) { geometryData.lss = value; geometryType = GeometryType::Lss; return *this; }
        };

        struct ZWInstanceDesc
        {
            AffineTransform transform;
            unsigned instanceID : 24;
            unsigned instanceMask : 8;
            unsigned instanceContributionToHitGroupIndex : 24;
            EInstanceFlags flags : 8;
            union
            {
                IAccelStruct* bottomLevelAS; // for buildTopLevelAccelStruct
                uint64_t blasDeviceAddress;  // for buildTopLevelAccelStructFromBuffer - use IAccelStruct::getDeviceAddress()
            };

            ZWInstanceDesc()
                : instanceID(0)
                , instanceMask(0)
                , instanceContributionToHitGroupIndex(0)
                , flags(EInstanceFlags::None)
                , bottomLevelAS(nullptr)
            {
                setTransform(cIdentityTransform);
            }

            ZWInstanceDesc& setInstanceID(uint32_t value) { instanceID = value; return *this; }
            ZWInstanceDesc& setInstanceContributionToHitGroupIndex(uint32_t value) { instanceContributionToHitGroupIndex = value; return *this; }
            ZWInstanceDesc& setInstanceMask(uint32_t value) { instanceMask = value; return *this; }
            ZWInstanceDesc& setTransform(const AffineTransform& value) { memcpy(&transform, &value, sizeof(AffineTransform)); return *this; }
            ZWInstanceDesc& setFlags(EInstanceFlags value) { flags = value; return *this; }
            ZWInstanceDesc& setBLAS(IAccelStruct* value);
        };

        // Shader friendly equivalent of HRHI::Hrt::InstanceDesc
        struct ZWIndirectInstanceDesc
        {
            float transform[12];
            uint32_t instanceID : 24;
            uint32_t instanceMask : 8;
            uint32_t instanceContributionToHitGroupIndex : 24;
            uint32_t flags : 8;
            GpuVirtualAddress blasDeviceAddress;
        };

        static_assert(sizeof(ZWInstanceDesc) == 64, "sizeof(InstanceDesc) is supposed to be 64 bytes");
        static_assert(sizeof(ZWIndirectInstanceDesc) == sizeof(ZWInstanceDesc));

        class IShaderTable;

        struct ZWState
        {
            IShaderTable* shaderTable = nullptr;

            BindingSetVector bindings;

            ZWState& setShaderTable(IShaderTable* value);
            ZWState& addBindingSet(IBindingSet* value);
        };

        struct ZWDispatchRaysArguments
        {
            uint32_t width = 1;
            uint32_t height = 1;
            uint32_t depth = 1;

            constexpr ZWDispatchRaysArguments& setWidth(uint32_t value) { width = value; return *this; }
            constexpr ZWDispatchRaysArguments& setHeight(uint32_t value) { height = value; return *this; }
            constexpr ZWDispatchRaysArguments& setDepth(uint32_t value) { depth = value; return *this; }
            constexpr ZWDispatchRaysArguments& setDimensions(uint32_t w, uint32_t h = 1, uint32_t d = 1) { width = w; height = h; depth = d; return *this; }
        };

        namespace HCluster
        {
            struct ZWOperationSizeInfo
            {
                uint64_t resultMaxSizeInBytes = 0;
                uint64_t scratchSizeInBytes = 0;
            };

            struct ZWOperationMoveParams
            {
                EOperationMoveType type;
                uint32_t maxBytes = 0;
            };

            struct ZWOperationClasBuildParams
            {
                // See D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC for accepted formats and how they are interpreted
                EFormat vertexFormat = EFormat::RGB32_FLOAT;

                // Index of the last geometry in a single CLAS
                uint32_t maxGeometryIndex = 0;

                // Maximum number of unique geometries in a single CLAS
                uint32_t maxUniqueGeometryCount = 1;

                // Maximum number of triangles in a single CLAS
                uint32_t maxTriangleCount = 0;

                // Maximum number of vertices in a single CLAS
                uint32_t maxVertexCount = 0;

                // Maximum number of triangles summed over all CLAS (in the current cluster operation)
                uint32_t maxTotalTriangleCount = 0;

                // Maximum number of vertices summed over all CLAS (in the current cluster operation)
                uint32_t maxTotalVertexCount = 0;

                // Minimum number of bits to be truncated in vertex positions across all CLAS (in the current cluster operation)
                uint32_t minPositionTruncateBitCount = 0;
            };

            struct ZWOperationBlasBuildParams
            {
                // Maximum number of CLAS references in a single BLAS
                uint32_t maxClasPerBlasCount = 0;

                // Maximum number of CLAS references summed over all BLAS (in the current cluster operation)
                uint32_t maxTotalClasCount = 0;
            };

            // Params that can be used to getClusterOperationSizeInfo on this shared struct before passing to executeMultiIndirectClusterOperation
            struct ZWOperationParams
            {
                // Maximum number of acceleration structures (or templates) to build/instantiate/move
                uint32_t maxArgCount = 0;

                EOperationType type;
                EOperationMode mode;
                EOperationFlags flags;

                ZWOperationMoveParams move;
                ZWOperationClasBuildParams clas;
                ZWOperationBlasBuildParams blas;
            };

            struct ZWOperationDesc
            {
                ZWOperationParams params;

                uint64_t scratchSizeInBytes = 0;                        // Size of scratch resource returned by getClusterOperationSizeInfo() scratchSizeInBytes 

                // Input Resources
                IBuffer* inIndirectArgCountBuffer = nullptr;            // Buffer containing the number of AS to build, instantiate, or move
                uint64_t inIndirectArgCountOffsetInBytes = 0;           // Offset (in bytes) to where the count is in the inIndirectArgCountBuffer 
                IBuffer* inIndirectArgsBuffer = nullptr;                // Buffer of descriptor array of format IndirectTriangleClasArgs, IndirectTriangleTemplateArgs, IndirectInstantiateTemplateArgs
                uint64_t inIndirectArgsOffsetInBytes = 0;               // Offset (in bytes) to where the descriptor array starts inIndirectArgsBuffer

                // In/Out Resources
                IBuffer* inOutAddressesBuffer = nullptr;                // Array of addresses of CLAS, CLAS Templates, or BLAS
                uint64_t inOutAddressesOffsetInBytes = 0;               // Offset (in bytes) to where the addresses array starts in inOutAddressesBuffer

                // Output Resources
                IBuffer* outSizesBuffer = nullptr;                      // Sizes (in bytes) of CLAS, CLAS Templates, or BLAS
                uint64_t outSizesOffsetInBytes = 0;                     // Offset (in bytes) to where the output sizes array starts in outSizesBuffer
                IBuffer* outAccelerationStructuresBuffer = nullptr;     // Destination buffer for CLAS, CLAS Template, or BLAS data. Size must be calculated with getOperationSizeInfo or with the outSizesBuffer result of OperationMode::GetSizes
                uint64_t outAccelerationStructuresOffsetInBytes = 0;    // Offset (in bytes) to where the output acceleration structures starts in outAccelerationStructuresBuffer
            };
        }
    }
}