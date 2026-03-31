#include <BackEnd/d3d12backend.h>

#include <cassert>
#include <cstring>

namespace HRHI::HD3D12
{
    namespace
    {
#if HRHI_D3D12_WITH_NVAPI
        bool ConvertCustomSemantics(uint32_t numSemantics, const CustomSemantic* semantics, std::vector<NV_CUSTOM_SEMANTIC>& output)
        {
            output.resize(numSemantics);

            for (uint32_t semanticIndex = 0; semanticIndex < numSemantics; ++semanticIndex)
            {
                const CustomSemantic& sourceSemantic = semantics[semanticIndex];
                NV_CUSTOM_SEMANTIC& destinationSemantic = output[semanticIndex];

                destinationSemantic.version = NV_CUSTOM_SEMANTIC_VERSION;
                destinationSemantic.RegisterMask = 0;
                destinationSemantic.RegisterNum = 0;
                destinationSemantic.RegisterSpecified = FALSE;
                destinationSemantic.Reserved = 0;

                strncpy_s(destinationSemantic.NVCustomSemanticNameString, sourceSemantic.name.c_str(), sourceSemantic.name.size());

                switch (sourceSemantic.type)
                {
                case CustomSemantic::XRight:
                    destinationSemantic.NVCustomSemanticType = NV_X_RIGHT_SEMANTIC;
                    break;

                case CustomSemantic::ViewportMask:
                    destinationSemantic.NVCustomSemanticType = NV_VIEWPORT_MASK_SEMANTIC;
                    break;

                case CustomSemantic::Undefined:
                default:
                    return false;
                }
            }

            return true;
        }
#endif
    }

    ZWShaderHandle ZWD3D12Device::CreateShader(const ZWShaderDesc& desc, const void* binary, size_t binarySize)
    {
        if (binary == nullptr || binarySize == 0)
        {
            return nullptr;
        }

        ZWD3D12Shader* shader = new ZWD3D12Shader();
        shader->desc = desc;
        shader->bytecode.resize(binarySize);
        std::memcpy(shader->bytecode.data(), binary, binarySize);

#if HRHI_D3D12_WITH_NVAPI
        if (desc.numCustomSemantics != 0 && desc.pCustomSemantics != nullptr)
        {
            if (!ConvertCustomSemantics(desc.numCustomSemantics, desc.pCustomSemantics, shader->customSemantics))
            {
                delete shader;
                mContext.Error("Failed to convert custom NVAPI semantics for a D3D12 shader.");
                return nullptr;
            }
        }

        if (desc.pCoordinateSwizzling != nullptr)
        {
            constexpr uint32_t kNumSwizzles = 16;
            shader->coordinateSwizzling.resize(kNumSwizzles);
            std::memcpy(shader->coordinateSwizzling.data(), desc.pCoordinateSwizzling, sizeof(uint32_t) * kNumSwizzles);
        }

        if (desc.hlslExtensionsUAV >= 0)
        {
            auto* extensionDesc = new NVAPI_D3D12_PSO_SET_SHADER_EXTENSION_SLOT_DESC();
            std::memset(extensionDesc, 0, sizeof(*extensionDesc));
            extensionDesc->baseVersion = NV_PSO_EXTENSION_DESC_VER;
            extensionDesc->psoExtension = NV_PSO_SET_SHADER_EXTNENSION_SLOT_AND_SPACE;
            extensionDesc->version = NV_SET_SHADER_EXTENSION_SLOT_DESC_VER;
            extensionDesc->uavSlot = desc.hlslExtensionsUAV;
            extensionDesc->registerSpace = 0;
            shader->extensions.push_back(reinterpret_cast<NVAPI_D3D12_PSO_EXTENSION_DESC*>(extensionDesc));
        }

        switch (desc.shaderType)
        {
        case EShaderType::Vertex:
            if (desc.numCustomSemantics != 0)
            {
                auto* extensionDesc = new NVAPI_D3D12_PSO_VERTEX_SHADER_DESC();
                std::memset(extensionDesc, 0, sizeof(*extensionDesc));
                extensionDesc->baseVersion = NV_PSO_EXTENSION_DESC_VER;
                extensionDesc->psoExtension = NV_PSO_VERTEX_SHADER_EXTENSION;
                extensionDesc->version = NV_VERTEX_SHADER_PSO_EXTENSION_DESC_VER;
                extensionDesc->NumCustomSemantics = desc.numCustomSemantics;
                extensionDesc->pCustomSemantics = shader->customSemantics.data();
                extensionDesc->UseSpecificShaderExt = desc.useSpecificShaderExt;
                shader->extensions.push_back(reinterpret_cast<NVAPI_D3D12_PSO_EXTENSION_DESC*>(extensionDesc));
            }
            break;

        case EShaderType::Hull:
            if (desc.numCustomSemantics != 0)
            {
                auto* extensionDesc = new NVAPI_D3D12_PSO_HULL_SHADER_DESC();
                std::memset(extensionDesc, 0, sizeof(*extensionDesc));
                extensionDesc->baseVersion = NV_PSO_EXTENSION_DESC_VER;
                extensionDesc->psoExtension = NV_PSO_VERTEX_SHADER_EXTENSION;
                extensionDesc->version = NV_HULL_SHADER_PSO_EXTENSION_DESC_VER;
                extensionDesc->NumCustomSemantics = desc.numCustomSemantics;
                extensionDesc->pCustomSemantics = shader->customSemantics.data();
                extensionDesc->UseSpecificShaderExt = desc.useSpecificShaderExt;
                shader->extensions.push_back(reinterpret_cast<NVAPI_D3D12_PSO_EXTENSION_DESC*>(extensionDesc));
            }
            break;

        case EShaderType::Domain:
            if (desc.numCustomSemantics != 0)
            {
                auto* extensionDesc = new NVAPI_D3D12_PSO_DOMAIN_SHADER_DESC();
                std::memset(extensionDesc, 0, sizeof(*extensionDesc));
                extensionDesc->baseVersion = NV_PSO_EXTENSION_DESC_VER;
                extensionDesc->psoExtension = NV_PSO_VERTEX_SHADER_EXTENSION;
                extensionDesc->version = NV_DOMAIN_SHADER_PSO_EXTENSION_DESC_VER;
                extensionDesc->NumCustomSemantics = desc.numCustomSemantics;
                extensionDesc->pCustomSemantics = shader->customSemantics.data();
                extensionDesc->UseSpecificShaderExt = desc.useSpecificShaderExt;
                shader->extensions.push_back(reinterpret_cast<NVAPI_D3D12_PSO_EXTENSION_DESC*>(extensionDesc));
            }
            break;

        case EShaderType::Geometry:
            if ((desc.fastGSFlags & EFastGeometryShaderFlags::ForceFastGS) != 0
                || desc.numCustomSemantics != 0
                || desc.pCoordinateSwizzling != nullptr)
            {
                auto* extensionDesc = new NVAPI_D3D12_PSO_GEOMETRY_SHADER_DESC();
                std::memset(extensionDesc, 0, sizeof(*extensionDesc));
                extensionDesc->baseVersion = NV_PSO_EXTENSION_DESC_VER;
                extensionDesc->psoExtension = NV_PSO_GEOMETRY_SHADER_EXTENSION;
                extensionDesc->version = NV_GEOMETRY_SHADER_PSO_EXTENSION_DESC_VER;
                extensionDesc->NumCustomSemantics = desc.numCustomSemantics;
                extensionDesc->pCustomSemantics = desc.numCustomSemantics != 0 ? shader->customSemantics.data() : nullptr;
                extensionDesc->UseCoordinateSwizzle = desc.pCoordinateSwizzling != nullptr;
                extensionDesc->pCoordinateSwizzling = desc.pCoordinateSwizzling != nullptr ? shader->coordinateSwizzling.data() : nullptr;
                extensionDesc->ForceFastGS = (desc.fastGSFlags & EFastGeometryShaderFlags::ForceFastGS) != 0;
                extensionDesc->UseViewportMask = (desc.fastGSFlags & EFastGeometryShaderFlags::UseViewportMask) != 0;
                extensionDesc->OffsetRtIndexByVpIndex = (desc.fastGSFlags & EFastGeometryShaderFlags::OffsetTargetIndexByViewportIndex) != 0;
                extensionDesc->DontUseViewportOrder = (desc.fastGSFlags & EFastGeometryShaderFlags::StrictApiOrder) != 0;
                extensionDesc->UseSpecificShaderExt = desc.useSpecificShaderExt;
                extensionDesc->UseAttributeSkipMask = false;
                shader->extensions.push_back(reinterpret_cast<NVAPI_D3D12_PSO_EXTENSION_DESC*>(extensionDesc));
            }
            break;

        case EShaderType::Compute:
        case EShaderType::Pixel:
        case EShaderType::Amplification:
        case EShaderType::Mesh:
        case EShaderType::AllGraphics:
        case EShaderType::RayGeneration:
        case EShaderType::Miss:
        case EShaderType::ClosestHit:
        case EShaderType::AnyHit:
        case EShaderType::Intersection:
            if (desc.numCustomSemantics != 0)
            {
                delete shader;
                mContext.Error("Custom NVAPI semantics are not supported for this D3D12 shader stage.");
                return nullptr;
            }
            break;

        case EShaderType::None:
        case EShaderType::AllRayTracing:
        case EShaderType::All:
        default:
            delete shader;
            mContext.Error("Encountered an invalid shader stage while creating a D3D12 shader.");
            return nullptr;
        }
#else
        if (desc.numCustomSemantics != 0
            || desc.pCoordinateSwizzling != nullptr
            || desc.fastGSFlags != EFastGeometryShaderFlags(0)
            || desc.hlslExtensionsUAV >= 0)
        {
            mContext.Error("NVAPI shader extensions are not supported by this D3D12 backend build.");
            delete shader;
            return nullptr;
        }
#endif

        return ZWShaderHandle::Create(shader);
    }

    ZWShaderHandle ZWD3D12Device::CreateShaderSpecialization(IShader* baseShader, const ZWShaderSpecialization* constants, uint32_t numConstants)
    {
        (void)baseShader;
        (void)constants;
        (void)numConstants;

        mContext.Error("Shader specialization is not supported by the D3D12 backend.");
        return nullptr;
    }

    ZWShaderLibraryHandle ZWD3D12Device::CreateShaderLibrary(const void* binary, size_t binarySize)
    {
        ZWD3D12ShaderLibrary* shaderLibrary = new ZWD3D12ShaderLibrary();
        shaderLibrary->bytecode.resize(binarySize);

        if (binary != nullptr && binarySize != 0)
        {
            std::memcpy(shaderLibrary->bytecode.data(), binary, binarySize);
        }

        return ZWShaderLibraryHandle::Create(shaderLibrary);
    }

    ZWInputLayoutHandle ZWD3D12Device::CreateInputLayout(const ZWVertexAttributeDesc* attributes, uint32_t attributeCount, IShader* vertexShader)
    {
        (void)vertexShader;

        if (attributeCount != 0 && attributes == nullptr)
        {
            mContext.Error("Failed to create a D3D12 input layout because the attribute description array is null.");
            return nullptr;
        }

        ZWD3D12InputLayout* inputLayout = new ZWD3D12InputLayout();
        inputLayout->attributes.resize(attributeCount);

        for (uint32_t attributeIndex = 0; attributeIndex < attributeCount; ++attributeIndex)
        {
            ZWVertexAttributeDesc& attribute = inputLayout->attributes[attributeIndex];
            attribute = attributes[attributeIndex];

            assert(attribute.arraySize > 0);

            const ZWDxgiFormatMapping& formatMapping = GetDxgiFormatMapping(attribute.format);
            const ZWFormatInfo& formatInfo = GetFormatInfo(attribute.format);

            for (uint32_t semanticIndex = 0; semanticIndex < attribute.arraySize; ++semanticIndex)
            {
                D3D12_INPUT_ELEMENT_DESC inputElement = {};
                inputElement.SemanticName = attribute.name.c_str();
                inputElement.SemanticIndex = semanticIndex;
                inputElement.Format = formatMapping.srvFormat;
                inputElement.InputSlot = attribute.bufferIndex;
                inputElement.AlignedByteOffset = attribute.offset + semanticIndex * formatInfo.bytesPerBlock;

                if (attribute.isInstanced)
                {
                    inputElement.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
                    inputElement.InstanceDataStepRate = 1;
                }
                else
                {
                    inputElement.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                    inputElement.InstanceDataStepRate = 0;
                }

                inputLayout->inputElements.push_back(inputElement);
            }

            const auto strideIt = inputLayout->elementStrides.find(attribute.bufferIndex);
            if (strideIt == inputLayout->elementStrides.end())
            {
                inputLayout->elementStrides[attribute.bufferIndex] = attribute.elementStride;
            }
            else
            {
                assert(strideIt->second == attribute.elementStride);
            }
        }

        return ZWInputLayoutHandle::Create(inputLayout);
    }

    uint32_t ZWD3D12InputLayout::GetNumAttributes() const
    {
        return static_cast<uint32_t>(attributes.size());
    }

    const ZWVertexAttributeDesc* ZWD3D12InputLayout::GetAttributeDesc(uint32_t index) const
    {
        if (index >= static_cast<uint32_t>(attributes.size()))
        {
            return nullptr;
        }

        return &attributes[index];
    }

    void ZWD3D12Shader::GetBytecode(const void** ppBytecode, size_t* pSize) const
    {
        if (ppBytecode != nullptr)
        {
            *ppBytecode = bytecode.empty() ? nullptr : bytecode.data();
        }

        if (pSize != nullptr)
        {
            *pSize = bytecode.size();
        }
    }

    void ZWD3D12ShaderLibraryEntry::GetBytecode(const void** ppBytecode, size_t* pSize) const
    {
        library->GetBytecode(ppBytecode, pSize);
    }

    void ZWD3D12ShaderLibrary::GetBytecode(const void** ppBytecode, size_t* pSize) const
    {
        if (ppBytecode != nullptr)
        {
            *ppBytecode = bytecode.empty() ? nullptr : bytecode.data();
        }

        if (pSize != nullptr)
        {
            *pSize = bytecode.size();
        }
    }

    ZWShaderHandle ZWD3D12ShaderLibrary::GetShader(const char* entryName, EShaderType shaderType)
    {
        return ZWShaderHandle::Create(new ZWD3D12ShaderLibraryEntry(this, entryName, shaderType));
    }
}
