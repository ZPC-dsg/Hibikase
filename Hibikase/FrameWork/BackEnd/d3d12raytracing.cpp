#include <BackEnd/d3d12backend.h>
#include <Utils/stringtranslatehelper.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_map>

namespace HRHI::HD3D12
{
    namespace
    {
        template <typename TValue, typename TAlignment>
        constexpr TValue AlignValue(TValue value, TAlignment alignment)
        {
            const TValue alignmentValue = static_cast<TValue>(alignment);
            return (value + alignmentValue - TValue(1)) & ~(alignmentValue - TValue(1));
        }

        template <typename TContainer>
        uint32_t ArrayDifferenceMask(const TContainer& left, const TContainer& right)
        {
            const size_t maxSize = std::max(left.size(), right.size());
            uint32_t differenceMask = 0;

            for (size_t index = 0; index < maxSize; ++index)
            {
                const bool isSame = index < left.size()
                    && index < right.size()
                    && left[index] == right[index];

                if (!isSame)
                {
                    differenceMask |= (1u << index);
                }
            }

            return differenceMask;
        }

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS ConvertAccelStructBuildFlags(Hrt::EAccelStructBuildFlags buildFlags)
        {
            buildFlags = buildFlags & ~Hrt::EAccelStructBuildFlags::AllowEmptyInstances;
            return static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS>(buildFlags);
        }

        std::wstring ToWideString(const std::string& value)
        {
            return std::wstring(value.begin(), value.end());
        }

        uint32_t GetBindingLayoutRootParameterCount(IBindingLayout* bindingLayout)
        {
            if (bindingLayout == nullptr)
            {
                return 0;
            }

            if (bindingLayout->GetDesc() != nullptr)
            {
                ZWD3D12BindingLayout* d3d12BindingLayout = static_cast<ZWD3D12BindingLayout*>(bindingLayout);
                return static_cast<uint32_t>(d3d12BindingLayout->rootParameters.size());
            }

            const ZWBindlessLayoutDesc* bindlessDesc = bindingLayout->GetBindlessDesc();
            if (bindlessDesc == nullptr)
            {
                return 0;
            }

            return bindlessDesc->layoutType == ZWBindlessLayoutDesc::ELayoutType::Immutable ? 1u : 0u;
        }

        bool ValidateBoundAccelStructStorage(const ZWD3D12AccelStruct* accelStruct, const ZWD3D12Context& context, const char* operationName)
        {
            if (accelStruct == nullptr)
            {
                context.Error(std::string(operationName) + " failed because the acceleration structure handle is null.");
                return false;
            }

            if (accelStruct->dataBuffer == nullptr || accelStruct->dataBuffer->resource == nullptr)
            {
                std::stringstream messageBuilder;
                messageBuilder << operationName << " requires acceleration-structure memory to be bound for "
                    << HApp::DebugNameToString(accelStruct->desc.debugName) << ".";
                context.Error(messageBuilder.str());
                return false;
            }

            return true;
        }

        class ZWD3D12RaytracingGeometryDesc
        {
            struct ZWRaytracingGeometryDesc
            {
#if HRHI_WITH_NVAPI_OPACITY_MICROMAP || HRHI_WITH_NVAPI_LSS
                NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_EX type;
#else
                D3D12_RAYTRACING_GEOMETRY_TYPE type;
#endif
                D3D12_RAYTRACING_GEOMETRY_FLAGS flags;
                union
                {
                    D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC triangles;
                    D3D12_RAYTRACING_GEOMETRY_AABBS_DESC aabbs;
#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
                    D3D12_RAYTRACING_GEOMETRY_OMM_TRIANGLES_DESC ommTriangles;
#endif
#if HRHI_WITH_NVAPI_OPACITY_MICROMAP
                    NVAPI_D3D12_RAYTRACING_GEOMETRY_OMM_TRIANGLES_DESC ommTriangles;
#endif
#if HRHI_WITH_NVAPI_DISPLACEMENT_MICROMAP
                    NVAPI_D3D12_RAYTRACING_GEOMETRY_DMM_TRIANGLES_DESC dmmTriangles;
#endif
#if HRHI_WITH_NVAPI_LSS
                    NVAPI_D3D12_RAYTRACING_GEOMETRY_SPHERES_DESC spheres;
                    NVAPI_D3D12_RAYTRACING_GEOMETRY_LSS_DESC lss;
#endif
                };
            } mData = {};

        public:
            void SetFlags(D3D12_RAYTRACING_GEOMETRY_FLAGS flags) { mData.flags = flags; }

            void SetTriangles(const D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& triangles)
            {
#if HRHI_WITH_NVAPI_OPACITY_MICROMAP || HRHI_WITH_NVAPI_LSS
                mData.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES_EX;
#else
                mData.type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
#endif
                mData.triangles = triangles;
            }

            void SetAABBs(const D3D12_RAYTRACING_GEOMETRY_AABBS_DESC& aabbs)
            {
#if HRHI_WITH_NVAPI_OPACITY_MICROMAP || HRHI_WITH_NVAPI_LSS
                mData.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS_EX;
#else
                mData.type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
#endif
                mData.aabbs = aabbs;
            }

#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
            void SetOMMTriangles(ZWD3D12RaytracingGeometryDesc& triangles, D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC* linkage)
            {
                mData.type = D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES;
                mData.ommTriangles.pTriangles = &triangles.mData.triangles;
                mData.ommTriangles.pOmmLinkage = linkage;
            }
#endif

#if HRHI_WITH_NVAPI_OPACITY_MICROMAP
            void SetOMMTriangles(const NVAPI_D3D12_RAYTRACING_GEOMETRY_OMM_TRIANGLES_DESC& ommTriangles)
            {
                mData.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES_EX;
                mData.ommTriangles = ommTriangles;
            }
#endif

#if HRHI_WITH_NVAPI_LSS
            void SetSpheres(const NVAPI_D3D12_RAYTRACING_GEOMETRY_SPHERES_DESC& spheres)
            {
                mData.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_SPHERES_EX;
                mData.spheres = spheres;
            }

            void SetLss(const NVAPI_D3D12_RAYTRACING_GEOMETRY_LSS_DESC& lss)
            {
                mData.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_LSS_EX;
                mData.lss = lss;
            }
#endif
        };

        class ZWD3D12BuildRaytracingAccelerationStructureInputs
        {
            struct BuildRaytracingAccelerationStructure
            {
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE Type;
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS Flags;
                UINT NumDescs;
                D3D12_ELEMENTS_LAYOUT DescsLayout;

                union
                {
                    D3D12_GPU_VIRTUAL_ADDRESS InstanceDescs;
                    const ZWD3D12RaytracingGeometryDesc* const* ppGeometryDescs;
                };
            } mDesc = {};

            std::vector<ZWD3D12RaytracingGeometryDesc> mGeometryDescs;
            std::vector<ZWD3D12RaytracingGeometryDesc*> mGeometryDescPointers;
#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
            std::vector<ZWD3D12RaytracingGeometryDesc> mOmmGeometryDescs;
            std::vector<ZWD3D12RaytracingGeometryDesc*> mOmmGeometryDescPointers;
            std::vector<D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC> mOmmLinkageDescs;
#endif

        public:
            void SetGeometryDescCount(uint32_t numDescs);
#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
            void SetOMMDescCount(uint32_t numDescs);
            D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC& GetOMMLinkageDesc(uint32_t index);
#endif
            void SetType(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE type) { mDesc.Type = type; }
            void SetFlags(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags) { mDesc.Flags = flags; }
            void SetInstanceDescs(D3D12_GPU_VIRTUAL_ADDRESS instanceDescs, UINT numDescs);
            ZWD3D12RaytracingGeometryDesc& GetGeometryDesc(uint32_t index) { return mGeometryDescs[index]; }

            template <class T>
            const T GetAs();
        };

        void ZWD3D12BuildRaytracingAccelerationStructureInputs::SetGeometryDescCount(uint32_t numDescs)
        {
            mGeometryDescs.resize(numDescs);
            mGeometryDescPointers.resize(numDescs);

            for (uint32_t index = 0; index < numDescs; ++index)
            {
                mGeometryDescPointers[index] = mGeometryDescs.data() + index;
            }

            mDesc.ppGeometryDescs = mGeometryDescPointers.data();
            mDesc.NumDescs = numDescs;
            mDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS;
        }

#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
        void ZWD3D12BuildRaytracingAccelerationStructureInputs::SetOMMDescCount(uint32_t numDescs)
        {
            mOmmGeometryDescs.resize(numDescs);
            mOmmGeometryDescPointers.resize(numDescs);
            mOmmLinkageDescs.resize(numDescs);

            for (uint32_t index = 0; index < numDescs; ++index)
            {
                mOmmGeometryDescs[index].SetOMMTriangles(mGeometryDescs[index], &mOmmLinkageDescs[index]);
                mOmmGeometryDescPointers[index] = &mOmmGeometryDescs[index];
            }

            mDesc.ppGeometryDescs = mOmmGeometryDescPointers.data();
            mDesc.NumDescs = numDescs;
            mDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS;
        }

        D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC& ZWD3D12BuildRaytracingAccelerationStructureInputs::GetOMMLinkageDesc(uint32_t index)
        {
            return mOmmLinkageDescs[index];
        }
#endif

        void ZWD3D12BuildRaytracingAccelerationStructureInputs::SetInstanceDescs(D3D12_GPU_VIRTUAL_ADDRESS instanceDescs, UINT numDescs)
        {
            mDesc.InstanceDescs = instanceDescs;
            mDesc.NumDescs = numDescs;
            mDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        }

#if HRHI_WITH_NVAPI_OPACITY_MICROMAP || HRHI_WITH_NVAPI_LSS
        template <>
        const NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX ZWD3D12BuildRaytracingAccelerationStructureInputs::GetAs<NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX>()
        {
            NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX inputs = {};
            inputs.type = mDesc.Type;
            inputs.flags = static_cast<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS_EX>(mDesc.Flags);
            inputs.numDescs = mDesc.NumDescs;
            inputs.geometryDescStrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX);
            inputs.descsLayout = mDesc.DescsLayout;
            inputs.instanceDescs = mDesc.InstanceDescs;
            static_assert(sizeof(BuildRaytracingAccelerationStructure::ppGeometryDescs) == sizeof(BuildRaytracingAccelerationStructure::InstanceDescs));
            static_assert(sizeof(NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX::ppGeometryDescs) == sizeof(NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX::instanceDescs));
            return inputs;
        }
#endif

        template <>
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ZWD3D12BuildRaytracingAccelerationStructureInputs::GetAs<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS>()
        {
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
            inputs.Type = mDesc.Type;
            inputs.Flags = mDesc.Flags;
            inputs.NumDescs = mDesc.NumDescs;
            inputs.DescsLayout = mDesc.DescsLayout;
            inputs.InstanceDescs = mDesc.InstanceDescs;
            static_assert(sizeof(BuildRaytracingAccelerationStructure::ppGeometryDescs) == sizeof(BuildRaytracingAccelerationStructure::InstanceDescs));
            static_assert(sizeof(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS::ppGeometryDescs) == sizeof(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS::InstanceDescs));
#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
            static_assert(sizeof(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS::pOpacityMicromapArrayDesc) == sizeof(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS::InstanceDescs));
#endif
            return inputs;
        }

#if HRHI_WITH_NVAPI_OPACITY_MICROMAP
        const NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT* CastToUsageCount(const Hrt::ZWOpacityMicromapUsageCount* desc)
        {
            static_assert(sizeof(Hrt::ZWOpacityMicromapUsageCount) == sizeof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT));
            static_assert(offsetof(Hrt::ZWOpacityMicromapUsageCount, count) == offsetof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT, count));
            static_assert(offsetof(Hrt::ZWOpacityMicromapUsageCount, subdivisionLevel) == offsetof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT, subdivisionLevel));
            static_assert(offsetof(Hrt::ZWOpacityMicromapUsageCount, format) == offsetof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT, format));
            return reinterpret_cast<const NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT*>(desc);
        }

        void FillD3D12OpacityMicromapDesc(
            NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_INPUTS& outD3DDesc,
            const Hrt::ZWOpacityMicromapDesc& desc)
        {
            outD3DDesc.flags = static_cast<NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_BUILD_FLAGS>(desc.flags);
            outD3DDesc.numOMMUsageCounts = static_cast<NvU32>(desc.counts.size());
            outD3DDesc.pOMMUsageCounts = CastToUsageCount(desc.counts.data());
            outD3DDesc.inputBuffer = static_cast<ZWD3D12Buffer*>(desc.inputBuffer)->gpuVA + desc.inputBufferOffset;
            outD3DDesc.perOMMDescs =
            {
                static_cast<ZWD3D12Buffer*>(desc.perOmmDescs)->gpuVA + desc.perOmmDescsOffset,
                sizeof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_DESC)
            };
        }

        void FillOmmAttachmentDesc(NVAPI_D3D12_RAYTRACING_GEOMETRY_OMM_ATTACHMENT_DESC& ommAttachment, const Hrt::ZWGeometryDesc& geometryDesc)
        {
            const Hrt::ZWGeometryTriangles& triangles = geometryDesc.geometryData.triangles;
            ommAttachment.opacityMicromapArray = triangles.opacityMicromap == nullptr
                ? 128
                : static_cast<ZWD3D12OpacityMicromap*>(triangles.opacityMicromap)->GetDeviceAddress();
            ommAttachment.opacityMicromapBaseLocation = 0;
            ommAttachment.opacityMicromapIndexBuffer.StartAddress = triangles.ommIndexBuffer == nullptr
                ? 0
                : static_cast<ZWD3D12Buffer*>(triangles.ommIndexBuffer)->gpuVA + triangles.ommIndexBufferOffset;
            ommAttachment.opacityMicromapIndexBuffer.StrideInBytes = triangles.ommIndexFormat == EFormat::R32_UINT ? 4 : 2;
            ommAttachment.opacityMicromapIndexFormat = GetDxgiFormatMapping(triangles.ommIndexFormat).srvFormat;

            if (triangles.pOmmUsageCounts != nullptr)
            {
                ommAttachment.pOMMUsageCounts = CastToUsageCount(triangles.pOmmUsageCounts);
                ommAttachment.numOMMUsageCounts = triangles.numOmmUsageCounts;
            }
            else
            {
                ommAttachment.pOMMUsageCounts = nullptr;
                ommAttachment.numOMMUsageCounts = 0;
            }
        }
#endif

#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
        const D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY* CastToHistogram(const Hrt::ZWOpacityMicromapUsageCount* desc)
        {
            static_assert(sizeof(Hrt::ZWOpacityMicromapUsageCount) == sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY));
            static_assert(offsetof(Hrt::ZWOpacityMicromapUsageCount, count) == offsetof(D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY, Count));
            static_assert(offsetof(Hrt::ZWOpacityMicromapUsageCount, subdivisionLevel) == offsetof(D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY, SubdivisionLevel));
            static_assert(offsetof(Hrt::ZWOpacityMicromapUsageCount, format) == offsetof(D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY, Format));
            return reinterpret_cast<const D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY*>(desc);
        }

        void FillD3D12OpacityMicromapDesc(
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& outD3DDesc,
            D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC& ommDesc,
            const Hrt::ZWOpacityMicromapDesc& desc)
        {
            outD3DDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY;
            outD3DDesc.Flags = {};
            if ((desc.flags & Hrt::EOpacityMicromapBuildFlags::FastTrace) != 0) outD3DDesc.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            if ((desc.flags & Hrt::EOpacityMicromapBuildFlags::FastBuild) != 0) outD3DDesc.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
            if ((desc.flags & Hrt::EOpacityMicromapBuildFlags::AllowCompaction) != 0) outD3DDesc.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
            outD3DDesc.NumDescs = 1;
            outD3DDesc.pOpacityMicromapArrayDesc = &ommDesc;

            ommDesc.InputBuffer = static_cast<ZWD3D12Buffer*>(desc.inputBuffer)->gpuVA + desc.inputBufferOffset;
            ommDesc.PerOmmDescs.StartAddress = static_cast<ZWD3D12Buffer*>(desc.perOmmDescs)->gpuVA + desc.perOmmDescsOffset;
            ommDesc.PerOmmDescs.StrideInBytes = sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC);
            ommDesc.NumOmmHistogramEntries = static_cast<UINT>(desc.counts.size());
            ommDesc.pOmmHistogram = CastToHistogram(desc.counts.data());
        }

        void FillD3D12GeometryOMMLinkageDesc(D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC& outLinkage, const Hrt::ZWGeometryDesc& geometryDesc)
        {
            const Hrt::ZWGeometryTriangles& triangles = geometryDesc.geometryData.triangles;
            outLinkage.OpacityMicromapArray = triangles.opacityMicromap->GetDeviceAddress();
            outLinkage.OpacityMicromapBaseLocation = 0;
            outLinkage.OpacityMicromapIndexBuffer.StartAddress = triangles.ommIndexBuffer == nullptr
                ? 0
                : static_cast<ZWD3D12Buffer*>(triangles.ommIndexBuffer)->gpuVA + triangles.ommIndexBufferOffset;
            outLinkage.OpacityMicromapIndexBuffer.StrideInBytes = triangles.ommIndexFormat == EFormat::R32_UINT ? 4 : 2;
            outLinkage.OpacityMicromapIndexFormat = GetDxgiFormatMapping(triangles.ommIndexFormat).srvFormat;
        }
#endif

        void FillD3D12GeometryTrianglesDesc(
            D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& outTriangles,
            const Hrt::ZWGeometryDesc& geometryDesc,
            D3D12_GPU_VIRTUAL_ADDRESS transform4x4)
        {
            const Hrt::ZWGeometryTriangles& triangles = geometryDesc.geometryData.triangles;

            outTriangles.IndexBuffer = triangles.indexBuffer != nullptr
                ? static_cast<ZWD3D12Buffer*>(triangles.indexBuffer)->gpuVA + triangles.indexOffset
                : 0;
            outTriangles.IndexFormat = GetDxgiFormatMapping(triangles.indexFormat).srvFormat;
            outTriangles.IndexCount = triangles.indexCount;

            outTriangles.VertexBuffer.StartAddress = triangles.vertexBuffer != nullptr
                ? static_cast<ZWD3D12Buffer*>(triangles.vertexBuffer)->gpuVA + triangles.vertexOffset
                : 0;
            outTriangles.VertexBuffer.StrideInBytes = triangles.vertexStride;
            outTriangles.VertexFormat = GetDxgiFormatMapping(triangles.vertexFormat).srvFormat;
            outTriangles.VertexCount = triangles.vertexCount;
            outTriangles.Transform3x4 = transform4x4;
        }

        void FillD3D12AABBDesc(D3D12_RAYTRACING_GEOMETRY_AABBS_DESC& outAABBs, const Hrt::ZWGeometryDesc& geometryDesc)
        {
            const Hrt::ZWGeometryAABBs& aabbs = geometryDesc.geometryData.aabbs;

            outAABBs.AABBs.StartAddress = aabbs.buffer != nullptr
                ? static_cast<ZWD3D12Buffer*>(aabbs.buffer)->gpuVA + aabbs.offset
                : 0;
            outAABBs.AABBs.StrideInBytes = aabbs.stride;
            outAABBs.AABBCount = aabbs.count;
        }

#if HRHI_WITH_NVAPI_LSS
        void FillD3D12SpheresDesc(NVAPI_D3D12_RAYTRACING_GEOMETRY_SPHERES_DESC& outSpheres, const Hrt::ZWGeometryDesc& geometryDesc)
        {
            const Hrt::ZWGeometrySpheres& spheres = geometryDesc.geometryData.spheres;
            outSpheres.indexBuffer.StartAddress = spheres.indexBuffer != nullptr
                ? static_cast<ZWD3D12Buffer*>(spheres.indexBuffer)->gpuVA + spheres.indexOffset
                : 0;

            if (spheres.vertexBuffer != nullptr)
            {
                outSpheres.vertexPositionBuffer.StartAddress = static_cast<ZWD3D12Buffer*>(spheres.vertexBuffer)->gpuVA + spheres.vertexPositionOffset;
                outSpheres.vertexRadiusBuffer.StartAddress = static_cast<ZWD3D12Buffer*>(spheres.vertexBuffer)->gpuVA + spheres.vertexRadiusOffset;
            }
            else
            {
                outSpheres.vertexPositionBuffer.StartAddress = 0;
                outSpheres.vertexRadiusBuffer.StartAddress = 0;
            }

            outSpheres.indexBuffer.StrideInBytes = spheres.indexStride;
            outSpheres.vertexPositionBuffer.StrideInBytes = spheres.vertexPositionStride;
            outSpheres.vertexRadiusBuffer.StrideInBytes = spheres.vertexRadiusStride;
            outSpheres.indexFormat = GetDxgiFormatMapping(spheres.indexFormat).srvFormat;
            outSpheres.vertexPositionFormat = GetDxgiFormatMapping(spheres.vertexPositionFormat).srvFormat;
            outSpheres.vertexRadiusFormat = GetDxgiFormatMapping(spheres.vertexRadiusFormat).srvFormat;
            outSpheres.indexCount = spheres.indexCount;
            outSpheres.vertexCount = spheres.vertexCount;
        }

        void FillD3D12LssDesc(NVAPI_D3D12_RAYTRACING_GEOMETRY_LSS_DESC& outLss, const Hrt::ZWGeometryDesc& geometryDesc)
        {
            const Hrt::ZWGeometryLss& lss = geometryDesc.geometryData.lss;
            outLss.indexBuffer.StartAddress = lss.indexBuffer != nullptr
                ? static_cast<ZWD3D12Buffer*>(lss.indexBuffer)->gpuVA + lss.indexOffset
                : 0;

            if (lss.vertexBuffer != nullptr)
            {
                outLss.vertexPositionBuffer.StartAddress = static_cast<ZWD3D12Buffer*>(lss.vertexBuffer)->gpuVA + lss.vertexPositionOffset;
                outLss.vertexRadiusBuffer.StartAddress = static_cast<ZWD3D12Buffer*>(lss.vertexBuffer)->gpuVA + lss.vertexRadiusOffset;
            }
            else
            {
                outLss.vertexPositionBuffer.StartAddress = 0;
                outLss.vertexRadiusBuffer.StartAddress = 0;
            }

            outLss.indexBuffer.StrideInBytes = lss.indexStride;
            outLss.vertexPositionBuffer.StrideInBytes = lss.vertexPositionStride;
            outLss.vertexRadiusBuffer.StrideInBytes = lss.vertexRadiusStride;
            outLss.indexFormat = GetDxgiFormatMapping(lss.indexFormat).srvFormat;
            outLss.vertexPositionFormat = GetDxgiFormatMapping(lss.vertexPositionFormat).srvFormat;
            outLss.vertexRadiusFormat = GetDxgiFormatMapping(lss.vertexRadiusFormat).srvFormat;
            outLss.indexCount = lss.indexCount;
            outLss.primitiveCount = lss.primitiveCount;
            outLss.vertexCount = lss.vertexCount;
            outLss.primitiveFormat = lss.primitiveFormat == Hrt::EGeometryLssPrimitiveFormat::List
                ? NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT_LIST
                : NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT_SUCCESSIVE_IMPLICIT;
            outLss.endcapMode = lss.endcapMode == Hrt::EGeometryLssEndcapMode::None
                ? NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_NONE
                : NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_CHAINED;
        }
#endif

        bool FillD3D12GeometryDesc(
            ZWD3D12RaytracingGeometryDesc& outGeometryDesc,
            const Hrt::ZWGeometryDesc& geometryDesc,
            D3D12_GPU_VIRTUAL_ADDRESS transform4x4,
            const ZWD3D12Context& context)
        {
            outGeometryDesc.SetFlags(static_cast<D3D12_RAYTRACING_GEOMETRY_FLAGS>(geometryDesc.flags));

            if (geometryDesc.geometryType == Hrt::GeometryType::Triangles)
            {
                const Hrt::ZWGeometryTriangles& triangles = geometryDesc.geometryData.triangles;
                if (triangles.opacityMicromap != nullptr || triangles.ommIndexBuffer != nullptr)
                {
#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
                    D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC trianglesDesc = {};
                    FillD3D12GeometryTrianglesDesc(trianglesDesc, geometryDesc, transform4x4);
                    outGeometryDesc.SetTriangles(trianglesDesc);
#elif HRHI_WITH_NVAPI_OPACITY_MICROMAP
                    NVAPI_D3D12_RAYTRACING_GEOMETRY_OMM_TRIANGLES_DESC ommTriangles = {};
                    FillD3D12GeometryTrianglesDesc(ommTriangles.triangles, geometryDesc, transform4x4);
                    FillOmmAttachmentDesc(ommTriangles.ommAttachment, geometryDesc);
                    outGeometryDesc.SetOMMTriangles(ommTriangles);
#else
                    context.Error("Opacity micromaps are not supported by this D3D12 backend build.");
                    return false;
#endif
                }
                else
                {
                    D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC trianglesDesc = {};
                    FillD3D12GeometryTrianglesDesc(trianglesDesc, geometryDesc, transform4x4);
                    outGeometryDesc.SetTriangles(trianglesDesc);
                }
                return true;
            }

#if HRHI_WITH_NVAPI_LSS
            if (geometryDesc.geometryType == Hrt::GeometryType::Spheres)
            {
                NVAPI_D3D12_RAYTRACING_GEOMETRY_SPHERES_DESC spheresDesc = {};
                FillD3D12SpheresDesc(spheresDesc, geometryDesc);
                outGeometryDesc.SetSpheres(spheresDesc);
                return true;
            }

            if (geometryDesc.geometryType == Hrt::GeometryType::Lss)
            {
                NVAPI_D3D12_RAYTRACING_GEOMETRY_LSS_DESC lssDesc = {};
                FillD3D12LssDesc(lssDesc, geometryDesc);
                outGeometryDesc.SetLss(lssDesc);
                return true;
            }
#else
            if (geometryDesc.geometryType == Hrt::GeometryType::Spheres || geometryDesc.geometryType == Hrt::GeometryType::Lss)
            {
                context.Error("Spheres and line-swept spheres are not supported by this D3D12 backend build.");
                return false;
#endif
            }

            if (geometryDesc.geometryType == Hrt::GeometryType::AABBs)
            {
                D3D12_RAYTRACING_GEOMETRY_AABBS_DESC aabbDesc = {};
                FillD3D12AABBDesc(aabbDesc, geometryDesc);
                outGeometryDesc.SetAABBs(aabbDesc);
                return true;
            }

            context.Error("Encountered an unknown ray tracing geometry type.");
            return false;
        }

        bool FillAsInputDescForPreBuildInfo(
            ZWD3D12BuildRaytracingAccelerationStructureInputs& outInputs,
            const Hrt::ZWAccelStructDesc& desc,
            const ZWD3D12Context& context)
        {
            if (desc.isTopLevel)
            {
                outInputs.SetType(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL);
                outInputs.SetFlags(ConvertAccelStructBuildFlags(desc.buildFlags));
                outInputs.SetInstanceDescs(0, static_cast<uint32_t>(desc.topLevelMaxInstances));
                return true;
            }

            outInputs.SetType(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL);
            outInputs.SetFlags(ConvertAccelStructBuildFlags(desc.buildFlags));
            outInputs.SetGeometryDescCount(static_cast<uint32_t>(desc.bottomLevelGeometries.size()));

            bool hasOMM = false;

            for (uint32_t index = 0; index < static_cast<uint32_t>(desc.bottomLevelGeometries.size()); ++index)
            {
                const Hrt::ZWGeometryDesc& geometryDesc = desc.bottomLevelGeometries[index];
                const D3D12_GPU_VIRTUAL_ADDRESS transform4x4 = geometryDesc.useTransform ? 16 : 0;

                if (!FillD3D12GeometryDesc(outInputs.GetGeometryDesc(index), geometryDesc, transform4x4, context))
                {
                    return false;
                }

                if (geometryDesc.geometryType == Hrt::GeometryType::Triangles
                    && geometryDesc.geometryData.triangles.opacityMicromap != nullptr)
                {
                    hasOMM = true;
                }
            }

#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
            if (hasOMM)
            {
                outInputs.SetOMMDescCount(static_cast<uint32_t>(desc.bottomLevelGeometries.size()));

                for (uint32_t index = 0; index < static_cast<uint32_t>(desc.bottomLevelGeometries.size()); ++index)
                {
                    FillD3D12GeometryOMMLinkageDesc(outInputs.GetOMMLinkageDesc(index), desc.bottomLevelGeometries[index]);
                }
            }
#endif

            return true;
        }
    }

    HCommon::ZWObject ZWD3D12OpacityMicromap::GetNativeObject(ObjectType objectType)
    {
        if (dataBuffer != nullptr)
        {
            return dataBuffer->GetNativeObject(objectType);
        }

        return nullptr;
    }

    uint64_t ZWD3D12OpacityMicromap::GetDeviceAddress() const
    {
        return dataBuffer != nullptr ? dataBuffer->gpuVA : 0;
    }

    ZWD3D12AccelStruct::~ZWD3D12AccelStruct()
    {
#if HRHI_WITH_RTXMU
        if (!desc.isTopLevel && rtxmuId != ~0ull && mContext.rtxMemUtil != nullptr)
        {
            std::vector<uint64_t> accelStructsToRemove = { static_cast<uint64_t>(rtxmuId) };
            mContext.rtxMemUtil->RemoveAccelerationStructures(accelStructsToRemove);
            rtxmuId = ~0ull;
        }
#endif
    }

    HCommon::ZWObject ZWD3D12AccelStruct::GetNativeObject(ObjectType objectType)
    {
        if (dataBuffer != nullptr)
        {
            return dataBuffer->GetNativeObject(objectType);
        }

        return nullptr;
    }

    uint64_t ZWD3D12AccelStruct::GetDeviceAddress() const
    {
#if HRHI_WITH_RTXMU
        if (!desc.isTopLevel && rtxmuId != ~0ull && mContext.rtxMemUtil != nullptr)
        {
            return mContext.rtxMemUtil->GetAccelStructGPUVA(static_cast<uint64_t>(rtxmuId));
        }
#endif
        return dataBuffer != nullptr ? dataBuffer->gpuVA : 0;
    }

    void ZWD3D12AccelStruct::CreateSRV(size_t descriptor) const
    {
        if (dataBuffer == nullptr)
        {
            return;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.RaytracingAccelerationStructure.Location = GetDeviceAddress();

        mContext.device->CreateShaderResourceView(nullptr, &srvDesc, { descriptor });
    }

    uint32_t ZWD3D12ShaderTable::GetNumEntries() const
    {
        return 1u
            + static_cast<uint32_t>(missShaders.size())
            + static_cast<uint32_t>(hitGroups.size())
            + static_cast<uint32_t>(callableShaders.size());
    }

    bool ZWD3D12ShaderTable::VerifyExport(const ZWD3D12RayTracingPipeline::ZWD3D12ExportTableEntry* pExport, IBindingSet* bindings) const
    {
        if (pExport == nullptr)
        {
            mContext.Error("Couldn't find a DXR pipeline export with the requested name.");
            return false;
        }

        if (pExport->bindingLayout != nullptr && bindings == nullptr)
        {
            mContext.Error("A shader table entry is missing the required local bindings.");
            return false;
        }

        if (pExport->bindingLayout == nullptr && bindings != nullptr)
        {
            mContext.Error("A shader table entry provides local bindings even though none are required.");
            return false;
        }

        if (bindings != nullptr)
        {
            ZWD3D12BindingSet* bindingSet = static_cast<ZWD3D12BindingSet*>(bindings);
            if (bindingSet == nullptr || bindingSet->layout.Get() != pExport->bindingLayout)
            {
                mContext.Error("A shader table entry provides local bindings that do not match the pipeline export layout.");
                return false;
            }
        }

        return true;
    }

    void ZWD3D12ShaderTable::SetRayGenerationShader(const char* exportName, IBindingSet* bindings)
    {
        const ZWD3D12RayTracingPipeline::ZWD3D12ExportTableEntry* pipelineExport = pipeline != nullptr
            ? pipeline->GetExport(exportName)
            : nullptr;

        if (!VerifyExport(pipelineExport, bindings))
        {
            return;
        }

        rayGenerationShader.pShaderIdentifier = pipelineExport->pShaderIdentifier;
        rayGenerationShader.localBindings = bindings;
        ++version;
    }

    int ZWD3D12ShaderTable::AddMissShader(const char* exportName, IBindingSet* bindings)
    {
        const ZWD3D12RayTracingPipeline::ZWD3D12ExportTableEntry* pipelineExport = pipeline != nullptr
            ? pipeline->GetExport(exportName)
            : nullptr;

        if (!VerifyExport(pipelineExport, bindings))
        {
            return -1;
        }

        ZWD3D12Entry entry = {};
        entry.pShaderIdentifier = pipelineExport->pShaderIdentifier;
        entry.localBindings = bindings;
        missShaders.push_back(entry);
        ++version;
        return static_cast<int>(missShaders.size()) - 1;
    }

    int ZWD3D12ShaderTable::AddHitGroup(const char* exportName, IBindingSet* bindings)
    {
        const ZWD3D12RayTracingPipeline::ZWD3D12ExportTableEntry* pipelineExport = pipeline != nullptr
            ? pipeline->GetExport(exportName)
            : nullptr;

        if (!VerifyExport(pipelineExport, bindings))
        {
            return -1;
        }

        ZWD3D12Entry entry = {};
        entry.pShaderIdentifier = pipelineExport->pShaderIdentifier;
        entry.localBindings = bindings;
        hitGroups.push_back(entry);
        ++version;
        return static_cast<int>(hitGroups.size()) - 1;
    }

    int ZWD3D12ShaderTable::AddCallableShader(const char* exportName, IBindingSet* bindings)
    {
        const ZWD3D12RayTracingPipeline::ZWD3D12ExportTableEntry* pipelineExport = pipeline != nullptr
            ? pipeline->GetExport(exportName)
            : nullptr;

        if (!VerifyExport(pipelineExport, bindings))
        {
            return -1;
        }

        ZWD3D12Entry entry = {};
        entry.pShaderIdentifier = pipelineExport->pShaderIdentifier;
        entry.localBindings = bindings;
        callableShaders.push_back(entry);
        ++version;
        return static_cast<int>(callableShaders.size()) - 1;
    }

    void ZWD3D12ShaderTable::ClearMissShaders()
    {
        missShaders.clear();
        ++version;
    }

    void ZWD3D12ShaderTable::ClearHitShaders()
    {
        hitGroups.clear();
        ++version;
    }

    void ZWD3D12ShaderTable::ClearCallableShaders()
    {
        callableShaders.clear();
        ++version;
    }

    const ZWD3D12RayTracingPipeline::ZWD3D12ExportTableEntry* ZWD3D12RayTracingPipeline::GetExport(const char* name)
    {
        if (name == nullptr)
        {
            return nullptr;
        }

        const auto exportIt = exports.find(name);
        if (exportIt == exports.end())
        {
            return nullptr;
        }

        return &exportIt->second;
    }

    Hrt::ZWShaderTableHandle ZWD3D12RayTracingPipeline::CreateShaderTable(Hrt::ZWShaderTableDesc const& stDesc)
    {
        ZWBufferHandle cache;

        if (stDesc.isCached)
        {
            if (stDesc.maxEntries == 0)
            {
                mContext.Error("maxEntries must be non-zero when creating a cached shader table.");
                return nullptr;
            }

            ZWBufferDesc bufferDesc;
            bufferDesc.debugName = stDesc.debugName;
            bufferDesc.byteSize = GetShaderTableEntrySize() * stDesc.maxEntries;
            bufferDesc.isShaderBindingTable = true;
            bufferDesc.enableAutomaticStateTracking(EResourceStates::ShaderResource);

            cache = mDevice->CreateBuffer(bufferDesc);
            if (!cache)
            {
                return nullptr;
            }
        }

        ZWD3D12ShaderTable* shaderTable = new ZWD3D12ShaderTable(mContext, this, stDesc);
        shaderTable->cache = cache;
        return Hrt::ZWShaderTableHandle::Create(shaderTable);
    }

    uint32_t ZWD3D12RayTracingPipeline::GetShaderTableEntrySize() const
    {
        const uint32_t requiredSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(uint64_t) * maxLocalRootParameters;
        return AlignValue(requiredSize, static_cast<uint32_t>(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT));
    }

    Hrt::ZWOpacityMicromapHandle ZWD3D12Device::CreateOpacityMicromap(const Hrt::ZWOpacityMicromapDesc& desc)
    {
#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
        if (!mOpacityMicromapSupported || mContext.device8 == nullptr)
        {
            mContext.Error("Opacity micromaps are not supported by this D3D12 backend build.");
            return nullptr;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ommInputs = {};
        D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC ommArrayDesc = {};
        FillD3D12OpacityMicromapDesc(ommInputs, ommArrayDesc, desc);

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuildInfo = {};
        mContext.device8->GetRaytracingAccelerationStructurePrebuildInfo(&ommInputs, &preBuildInfo);

        ZWD3D12OpacityMicromap* opacityMicromap = new ZWD3D12OpacityMicromap();
        opacityMicromap->desc = desc;
        opacityMicromap->compacted = false;

        ZWBufferDesc bufferDesc;
        bufferDesc.canHaveUAVs = true;
        bufferDesc.byteSize = preBuildInfo.ResultDataMaxSizeInBytes;
        bufferDesc.initialState = EResourceStates::OpacityMicromapWrite;
        bufferDesc.keepInitialState = true;
        bufferDesc.isAccelStructStorage = true;
        bufferDesc.debugName = desc.debugName;
        bufferDesc.isVirtual = false;
        opacityMicromap->dataBuffer = static_cast<ZWD3D12Buffer*>(CreateBuffer(bufferDesc).Get());

        return Hrt::ZWOpacityMicromapHandle::Create(opacityMicromap);
#elif HRHI_WITH_NVAPI_OPACITY_MICROMAP
        if (!mOpacityMicromapSupported || mContext.device5 == nullptr)
        {
            mContext.Error("Opacity micromaps are not supported by this D3D12 backend build.");
            return nullptr;
        }

        NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_INPUTS inputs = {};
        FillD3D12OpacityMicromapDesc(inputs, desc);

        NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO preBuildInfo = {};
        NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS params = {};
        params.version = NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS_VER;
        params.pDesc = &inputs;
        params.pInfo = &preBuildInfo;

        const NvAPI_Status status = NvAPI_D3D12_GetRaytracingOpacityMicromapArrayPrebuildInfo(mContext.device5.Get(), &params);
        if (status != NVAPI_OK)
        {
            mContext.Error("NvAPI_D3D12_GetRaytracingOpacityMicromapArrayPrebuildInfo failed.");
            return nullptr;
        }

        ZWD3D12OpacityMicromap* opacityMicromap = new ZWD3D12OpacityMicromap();
        opacityMicromap->desc = desc;
        opacityMicromap->compacted = false;

        ZWBufferDesc bufferDesc;
        bufferDesc.canHaveUAVs = true;
        bufferDesc.byteSize = preBuildInfo.resultDataMaxSizeInBytes;
        bufferDesc.initialState = EResourceStates::OpacityMicromapWrite;
        bufferDesc.keepInitialState = true;
        bufferDesc.isAccelStructStorage = true;
        bufferDesc.debugName = desc.debugName;
        bufferDesc.isVirtual = false;
        opacityMicromap->dataBuffer = static_cast<ZWD3D12Buffer*>(CreateBuffer(bufferDesc).Get());

        return Hrt::ZWOpacityMicromapHandle::Create(opacityMicromap);
#else
        (void)desc;
        mContext.Error("Opacity micromaps are not supported by this D3D12 backend build.");
        return nullptr;
#endif
    }

    bool ZWD3D12Device::GetAccelStructPreBuildInfo(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& outPreBuildInfo, const Hrt::ZWAccelStructDesc& desc) const
    {
        if (mContext.device5 == nullptr)
        {
            mContext.Error("Ray tracing acceleration structure prebuild info is unavailable because ID3D12Device5 is not present.");
            return false;
        }

        ZWD3D12BuildRaytracingAccelerationStructureInputs inputs;
        if (!FillAsInputDescForPreBuildInfo(inputs, desc, mContext))
        {
            return false;
        }

#if HRHI_WITH_NVAPI_OPACITY_MICROMAP || HRHI_WITH_NVAPI_LSS
        bool requiresOpacityMicromap = false;
        bool requiresSpheres = false;
        bool requiresLss = false;

        if (!desc.isTopLevel)
        {
            for (const Hrt::ZWGeometryDesc& geometryDesc : desc.bottomLevelGeometries)
            {
                if (geometryDesc.geometryType == Hrt::GeometryType::Triangles
                    && geometryDesc.geometryData.triangles.opacityMicromap != nullptr)
                {
                    requiresOpacityMicromap = true;
                }

                if (geometryDesc.geometryType == Hrt::GeometryType::Spheres)
                {
                    requiresSpheres = true;
                }

                if (geometryDesc.geometryType == Hrt::GeometryType::Lss)
                {
                    requiresLss = true;
                }
            }
        }

        if (requiresOpacityMicromap && !mOpacityMicromapSupported)
        {
            mContext.Error("This device does not support opacity micromap geometry in D3D12 acceleration structures.");
            return false;
        }

        if (requiresSpheres && !mSpheresSupported)
        {
            mContext.Error("This device does not support sphere geometry in D3D12 acceleration structures.");
            return false;
        }

        if (requiresLss && !mLinearSweptSpheresSupported)
        {
            mContext.Error("This device does not support line-swept sphere geometry in D3D12 acceleration structures.");
            return false;
        }

        if (requiresOpacityMicromap || requiresSpheres || requiresLss)
        {
            const NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX d3d12Inputs =
                inputs.GetAs<NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX>();

            NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS params = {};
            params.version = NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS_VER;
            params.pDesc = &d3d12Inputs;
            params.pInfo = &outPreBuildInfo;

            const NvAPI_Status status = NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx(mContext.device5.Get(), &params);
            if (status != NVAPI_OK)
            {
                mContext.Error("NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx failed.");
                return false;
            }

            return true;
        }
#endif

        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS d3d12Inputs =
            inputs.GetAs<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS>();
        mContext.device5->GetRaytracingAccelerationStructurePrebuildInfo(&d3d12Inputs, &outPreBuildInfo);
        return true;
    }

    Hrt::ZWAccelStructHandle ZWD3D12Device::CreateAccelStruct(const Hrt::ZWAccelStructDesc& desc)
    {
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuildInfo = {};
        if (!GetAccelStructPreBuildInfo(preBuildInfo, desc))
        {
            return nullptr;
        }

        ZWD3D12AccelStruct* accelStruct = new ZWD3D12AccelStruct(mContext);
        accelStruct->desc = desc;
        accelStruct->allowUpdate = (desc.buildFlags & Hrt::EAccelStructBuildFlags::AllowUpdate) != 0;

        ZWBufferDesc bufferDesc;
        bufferDesc.canHaveUAVs = true;
        bufferDesc.byteSize = preBuildInfo.ResultDataMaxSizeInBytes;
        bufferDesc.initialState = desc.isTopLevel ? EResourceStates::AccelStructRead : EResourceStates::AccelStructBuildBlas;
        bufferDesc.keepInitialState = true;
        bufferDesc.isAccelStructStorage = true;
        bufferDesc.debugName = desc.debugName;
        bufferDesc.isVirtual = desc.isVirtual;

        ZWBufferHandle buffer = CreateBuffer(bufferDesc);
        accelStruct->dataBuffer = static_cast<ZWD3D12Buffer*>(buffer.Get());

        for (Hrt::ZWGeometryDesc& geometry : accelStruct->desc.bottomLevelGeometries)
        {
            static_assert(offsetof(Hrt::ZWGeometryTriangles, indexBuffer) == offsetof(Hrt::ZWGeometryAABBs, buffer));
            static_assert(offsetof(Hrt::ZWGeometryTriangles, vertexBuffer) == offsetof(Hrt::ZWGeometryAABBs, unused));
            static_assert(offsetof(Hrt::ZWGeometryTriangles, indexBuffer) == offsetof(Hrt::ZWGeometrySpheres, indexBuffer));
            static_assert(offsetof(Hrt::ZWGeometryTriangles, vertexBuffer) == offsetof(Hrt::ZWGeometrySpheres, vertexBuffer));
            static_assert(offsetof(Hrt::ZWGeometryTriangles, indexBuffer) == offsetof(Hrt::ZWGeometryLss, indexBuffer));
            static_assert(offsetof(Hrt::ZWGeometryTriangles, vertexBuffer) == offsetof(Hrt::ZWGeometryLss, vertexBuffer));

            geometry.geometryData.triangles.indexBuffer = nullptr;
            geometry.geometryData.triangles.vertexBuffer = nullptr;
        }

        return Hrt::ZWAccelStructHandle::Create(accelStruct);
    }

    ZWMemoryRequirements ZWD3D12Device::GetAccelStructMemoryRequirements(Hrt::IAccelStruct* accelStruct)
    {
        ZWD3D12AccelStruct* d3d12AccelStruct = static_cast<ZWD3D12AccelStruct*>(accelStruct);
        if (d3d12AccelStruct != nullptr && d3d12AccelStruct->dataBuffer != nullptr)
        {
            return GetBufferMemoryRequirements(d3d12AccelStruct->dataBuffer.Get());
        }

        return {};
    }

    bool ZWD3D12Device::BindAccelStructMemory(Hrt::IAccelStruct* accelStruct, IHeap* heap, uint64_t offset)
    {
        ZWD3D12AccelStruct* d3d12AccelStruct = static_cast<ZWD3D12AccelStruct*>(accelStruct);
        if (d3d12AccelStruct != nullptr && d3d12AccelStruct->dataBuffer != nullptr)
        {
            return BindBufferMemory(d3d12AccelStruct->dataBuffer.Get(), heap, offset);
        }

        return false;
    }

#if HRHI_WITH_NVAPI_CLUSTERS
    namespace
    {
        DEFINE_ENUM_FLAG_OPERATORS(NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAGS);

        NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAGS TranslateClusterOperationFlags(const Hrt::HCluster::EOperationFlags& flags)
        {
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAGS result =
                NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE;

            const bool fastTrace = (flags & Hrt::HCluster::EOperationFlags::FastTrace) != 0;
            const bool fastBuild = (flags & Hrt::HCluster::EOperationFlags::FastBuild) != 0;

            if (fastTrace)
            {
                result |= NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_TRACE;
            }

            if (!fastTrace && fastBuild)
            {
                result |= NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_BUILD;
            }

            if ((flags & Hrt::HCluster::EOperationFlags::AllowOMM) != 0)
            {
                result |= NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_ALLOW_OMM;
            }

            if ((flags & Hrt::HCluster::EOperationFlags::NoOverlap) != 0)
            {
                result |= NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NO_OVERLAP;
            }

            return result;
        }

        NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE TranslateClusterOperationMode(const Hrt::HCluster::EOperationMode& mode)
        {
            switch (mode)
            {
            case Hrt::HCluster::EOperationMode::ImplicitDestinations:
                return NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS;

            case Hrt::HCluster::EOperationMode::ExplicitDestinations:
                return NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_EXPLICIT_DESTINATIONS;

            case Hrt::HCluster::EOperationMode::GetSizes:
                return NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_GET_SIZES;

            default:
                return NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS;
            }
        }

        DXGI_FORMAT TranslateClasBuildOperationVertexFormat(const Hrt::HCluster::ZWOperationParams& params)
        {
            const ZWDxgiFormatMapping& formatMapping = GetDxgiFormatMapping(params.clas.vertexFormat);
            return formatMapping.srvFormat;
        }

        void TranslateClusterTriangleDesc(
            const Hrt::HCluster::ZWOperationParams& params,
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUT_TRIANGLES_DESC& triangleDesc)
        {
            triangleDesc.vertexFormat = TranslateClasBuildOperationVertexFormat(params);
            triangleDesc.maxGeometryIndexValue = params.clas.maxGeometryIndex;
            triangleDesc.maxUniqueGeometryCountPerArg = params.clas.maxUniqueGeometryCount;
            triangleDesc.maxTriangleCountPerArg = params.clas.maxTriangleCount;
            triangleDesc.maxVertexCountPerArg = params.clas.maxVertexCount;
            triangleDesc.maxTotalTriangleCount = params.clas.maxTotalTriangleCount;
            triangleDesc.maxTotalVertexCount = params.clas.maxTotalVertexCount;
            triangleDesc.minPositionTruncateBitCount = params.clas.minPositionTruncateBitCount;
        }

        NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS TranslateClusterOperation(const Hrt::HCluster::ZWOperationParams& params)
        {
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS inputs = {};
            inputs.maxArgCount = params.maxArgCount;
            inputs.mode = TranslateClusterOperationMode(params.mode);
            inputs.flags = TranslateClusterOperationFlags(params.flags);

            switch (params.type)
            {
            case Hrt::HCluster::EOperationType::Move:
                inputs.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_MOVE_CLUSTER_OBJECT;
                inputs.movesDesc.maxBytesMoved = params.move.maxBytes;

                switch (params.move.type)
                {
                case Hrt::HCluster::EOperationMoveType::BottomLevel:
                    inputs.movesDesc.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE_BOTTOM_LEVEL_ACCELERATION_STRUCTURE;
                    break;

                case Hrt::HCluster::EOperationMoveType::ClusterLevel:
                    inputs.movesDesc.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE_CLUSTER_LEVEL_ACCELERATION_STRUCTURE;
                    break;

                case Hrt::HCluster::EOperationMoveType::Template:
                    inputs.movesDesc.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE_TEMPLATE;
                    break;
                }
                break;

            case Hrt::HCluster::EOperationType::ClasBuild:
                inputs.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLAS_FROM_TRIANGLES;
                TranslateClusterTriangleDesc(params, inputs.trianglesDesc);
                break;

            case Hrt::HCluster::EOperationType::ClasBuildTemplates:
                inputs.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLUSTER_TEMPLATES_FROM_TRIANGLES;
                TranslateClusterTriangleDesc(params, inputs.trianglesDesc);
                break;

            case Hrt::HCluster::EOperationType::ClasInstantiateTemplates:
                inputs.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_INSTANTIATE_CLUSTER_TEMPLATES;
                TranslateClusterTriangleDesc(params, inputs.trianglesDesc);
                break;

            case Hrt::HCluster::EOperationType::BlasBuild:
                inputs.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS;
                inputs.clasDesc.maxTotalClasCount = params.blas.maxTotalClasCount;
                inputs.clasDesc.maxClasCountPerArg = params.blas.maxClasPerBlasCount;
                break;
            }

            return inputs;
        }
    }
#endif

    Hrt::HCluster::ZWOperationSizeInfo ZWD3D12Device::GetClusterOperationSizeInfo(const Hrt::HCluster::ZWOperationParams& params)
    {
#if HRHI_WITH_NVAPI_CLUSTERS
        if (!mRayTracingClustersSupported || mContext.device5 == nullptr)
        {
            mContext.Error("Ray tracing cluster operations are not supported by this D3D12 backend build.");
            return {};
        }

        NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS inputs = TranslateClusterOperation(params);
        NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO info = {};

        NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS d3d12Params = {};
        d3d12Params.version = NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS_VER;
        d3d12Params.pInput = &inputs;
        d3d12Params.pInfo = &info;

        const NvAPI_Status result = NvAPI_D3D12_GetRaytracingMultiIndirectClusterOperationRequirementsInfo(mContext.device5.Get(), &d3d12Params);
        if (result != NVAPI_OK)
        {
            mContext.Error("NvAPI_D3D12_GetRaytracingMultiIndirectClusterOperationRequirementsInfo failed.");
            return {};
        }

        Hrt::HCluster::ZWOperationSizeInfo sizeInfo = {};
        sizeInfo.resultMaxSizeInBytes = AlignValue(info.resultDataMaxSizeInBytes, uint64_t(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT));
        sizeInfo.scratchSizeInBytes = AlignValue(info.scratchDataSizeInBytes, uint64_t(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT));
        return sizeInfo;
#else
        (void)params;
        mContext.Error("Ray tracing cluster operations are not supported by this D3D12 backend build.");
        return {};
#endif
    }

    bool ZWD3D12Device::SetHlslExtensionsUAV(uint32_t slot)
    {
#if HRHI_D3D12_WITH_NVAPI
        if (GetNvapiIsInitialized())
        {
            const NvAPI_Status status = NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread(mContext.device.Get(), slot, 0);
            if (status != NVAPI_OK)
            {
                mContext.Error("Failed to set the NVAPI HLSL extensions UAV slot.");
                return false;
            }

            return true;
        }

        mContext.Error("HLSL extensions require an NVIDIA graphics device with NVAPI support.");
        return false;
#else
        (void)slot;
        mContext.Error("NVAPI shader extensions are not supported by this D3D12 backend build.");
        return false;
#endif
    }

    Hrt::ZWPipelineHandle ZWD3D12Device::CreateRayTracingPipeline(const Hrt::ZWPipelineDesc& desc)
    {
        if (mContext.device5 == nullptr)
        {
            mContext.Error("Ray tracing pipeline creation requires ID3D12Device5.");
            return nullptr;
        }

        if (desc.hlslExtensionsUAV >= 0 && !SetHlslExtensionsUAV(static_cast<uint32_t>(desc.hlslExtensionsUAV)))
        {
            return nullptr;
        }

        std::unique_ptr<ZWD3D12RayTracingPipeline> pipeline = std::make_unique<ZWD3D12RayTracingPipeline>(mContext, this);
        pipeline->desc = desc;
        pipeline->maxLocalRootParameters = 0;

        struct Library
        {
            const void* pBlob = nullptr;
            size_t blobSize = 0;
            std::vector<std::pair<std::wstring, std::wstring>> exports;
            std::vector<D3D12_EXPORT_DESC> d3dExports;
        };

        std::unordered_map<const void*, Library> dxilLibraries;

        for (const Hrt::ZWPipelineShaderDesc& shaderDesc : desc.shaders)
        {
            if (shaderDesc.shader == nullptr)
            {
                mContext.Error("Failed to create a ray tracing pipeline because one of the shader exports is null.");
                return nullptr;
            }

            const void* blob = nullptr;
            size_t blobSize = 0;
            shaderDesc.shader->GetBytecode(&blob, &blobSize);

            if (blob == nullptr || blobSize == 0)
            {
                mContext.Error("Failed to create a ray tracing pipeline because one of the shader bytecodes is unavailable.");
                return nullptr;
            }

            Library& library = dxilLibraries[blob];
            library.pBlob = blob;
            library.blobSize = blobSize;

            const std::string originalShaderName = shaderDesc.shader->GetDesc().entryName;
            const std::string newShaderName = shaderDesc.exportName.empty() ? originalShaderName : shaderDesc.exportName;
            library.exports.emplace_back(ToWideString(originalShaderName), ToWideString(newShaderName));

            if (shaderDesc.bindingLayout != nullptr)
            {
                RootSignatureHandle& localRootSignature = pipeline->localRootSignatures[shaderDesc.bindingLayout.Get()];
                if (!localRootSignature)
                {
                    HCommon::StaticVector<ZWBindingLayoutHandle, gMaxBindingLayouts> localLayouts;
                    localLayouts.push_back(shaderDesc.bindingLayout);
                    localRootSignature = BuildRootSignature(localLayouts, false, true);
                    if (!localRootSignature)
                    {
                        return nullptr;
                    }

                    pipeline->maxLocalRootParameters = std::max(
                        pipeline->maxLocalRootParameters,
                        GetBindingLayoutRootParameterCount(shaderDesc.bindingLayout.Get()));
                }
            }
        }

        std::vector<D3D12_HIT_GROUP_DESC> d3dHitGroups;
        std::unordered_map<IShader*, std::wstring> hitGroupShaderNames;
        std::vector<std::wstring> hitGroupExportNames;
        hitGroupExportNames.reserve(desc.hitGroups.size());

        for (const Hrt::ZWPipelineHitGroupDesc& hitGroupDesc : desc.hitGroups)
        {
            const ZWShaderHandle shaders[] =
            {
                hitGroupDesc.closestHitShader,
                hitGroupDesc.anyHitShader,
                hitGroupDesc.intersectionShader
            };

            for (const ZWShaderHandle& shader : shaders)
            {
                if (!shader)
                {
                    continue;
                }

                std::wstring& newShaderName = hitGroupShaderNames[shader.Get()];
                if (!newShaderName.empty())
                {
                    continue;
                }

                const void* blob = nullptr;
                size_t blobSize = 0;
                shader->GetBytecode(&blob, &blobSize);

                if (blob == nullptr || blobSize == 0)
                {
                    mContext.Error("Failed to create a ray tracing pipeline because one of the hit-group shader bytecodes is unavailable.");
                    return nullptr;
                }

                Library& library = dxilLibraries[blob];
                library.pBlob = blob;
                library.blobSize = blobSize;

                const std::string originalShaderName = shader->GetDesc().entryName;
                const std::string uniqueShaderName = originalShaderName + std::to_string(hitGroupShaderNames.size());
                library.exports.emplace_back(ToWideString(originalShaderName), ToWideString(uniqueShaderName));
                newShaderName = ToWideString(uniqueShaderName);
            }

            if (hitGroupDesc.bindingLayout != nullptr)
            {
                RootSignatureHandle& localRootSignature = pipeline->localRootSignatures[hitGroupDesc.bindingLayout.Get()];
                if (!localRootSignature)
                {
                    HCommon::StaticVector<ZWBindingLayoutHandle, gMaxBindingLayouts> localLayouts;
                    localLayouts.push_back(hitGroupDesc.bindingLayout);
                    localRootSignature = BuildRootSignature(localLayouts, false, true);
                    if (!localRootSignature)
                    {
                        return nullptr;
                    }

                    pipeline->maxLocalRootParameters = std::max(
                        pipeline->maxLocalRootParameters,
                        GetBindingLayoutRootParameterCount(hitGroupDesc.bindingLayout.Get()));
                }
            }

            D3D12_HIT_GROUP_DESC d3dHitGroupDesc = {};
            if (hitGroupDesc.anyHitShader)
            {
                d3dHitGroupDesc.AnyHitShaderImport = hitGroupShaderNames[hitGroupDesc.anyHitShader.Get()].c_str();
            }
            if (hitGroupDesc.closestHitShader)
            {
                d3dHitGroupDesc.ClosestHitShaderImport = hitGroupShaderNames[hitGroupDesc.closestHitShader.Get()].c_str();
            }
            if (hitGroupDesc.intersectionShader)
            {
                d3dHitGroupDesc.IntersectionShaderImport = hitGroupShaderNames[hitGroupDesc.intersectionShader.Get()].c_str();
            }

            d3dHitGroupDesc.Type = hitGroupDesc.isProceduralPrimitive
                ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE
                : D3D12_HIT_GROUP_TYPE_TRIANGLES;

            hitGroupExportNames.push_back(ToWideString(hitGroupDesc.exportName));
            d3dHitGroupDesc.HitGroupExport = hitGroupExportNames.back().c_str();
            d3dHitGroups.push_back(d3dHitGroupDesc);
        }

        std::vector<D3D12_DXIL_LIBRARY_DESC> d3dLibraries;
        d3dLibraries.reserve(dxilLibraries.size());

        for (auto& libraryIt : dxilLibraries)
        {
            Library& library = libraryIt.second;

            for (const std::pair<std::wstring, std::wstring>& exportNames : library.exports)
            {
                D3D12_EXPORT_DESC& exportDesc = library.d3dExports.emplace_back();
                exportDesc.ExportToRename = exportNames.first.c_str();
                exportDesc.Name = exportNames.second.c_str();
                exportDesc.Flags = D3D12_EXPORT_FLAG_NONE;
            }

            D3D12_DXIL_LIBRARY_DESC& d3dLibraryDesc = d3dLibraries.emplace_back();
            d3dLibraryDesc.DXILLibrary.pShaderBytecode = library.pBlob;
            d3dLibraryDesc.DXILLibrary.BytecodeLength = library.blobSize;
            d3dLibraryDesc.NumExports = static_cast<UINT>(library.d3dExports.size());
            d3dLibraryDesc.pExports = library.d3dExports.data();
        }

        std::vector<D3D12_STATE_SUBOBJECT> d3dSubobjects;
        D3D12_STATE_SUBOBJECT d3dSubobject = {};

        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
        shaderConfig.MaxAttributeSizeInBytes = desc.maxAttributeSize;
        shaderConfig.MaxPayloadSizeInBytes = desc.maxPayloadSize;
        d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        d3dSubobject.pDesc = &shaderConfig;
        d3dSubobjects.push_back(d3dSubobject);

        D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
        pipelineConfig.MaxTraceRecursionDepth = desc.maxRecursionDepth;
        d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        d3dSubobject.pDesc = &pipelineConfig;
        d3dSubobjects.push_back(d3dSubobject);

        for (const D3D12_DXIL_LIBRARY_DESC& libraryDesc : d3dLibraries)
        {
            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            d3dSubobject.pDesc = &libraryDesc;
            d3dSubobjects.push_back(d3dSubobject);
        }

        for (const D3D12_HIT_GROUP_DESC& hitGroupDesc : d3dHitGroups)
        {
            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            d3dSubobject.pDesc = &hitGroupDesc;
            d3dSubobjects.push_back(d3dSubobject);
        }

        {
            RootSignatureHandle globalRootSignature = BuildRootSignature(desc.globalBindingLayouts, false, false);
            if (!globalRootSignature)
            {
                return nullptr;
            }

            pipeline->globalRootSignature = static_cast<ZWD3D12RootSignature*>(globalRootSignature.Get());

            D3D12_GLOBAL_ROOT_SIGNATURE globalRootSignatureDesc = {};
            globalRootSignatureDesc.pGlobalRootSignature = pipeline->globalRootSignature->handle.Get();
            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
            d3dSubobject.pDesc = &globalRootSignatureDesc;
            d3dSubobjects.push_back(d3dSubobject);
        }

        std::vector<D3D12_LOCAL_ROOT_SIGNATURE> localRootSignatures;
        std::vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> associations;
        localRootSignatures.reserve(pipeline->localRootSignatures.size());
        associations.reserve(pipeline->localRootSignatures.size());
        d3dSubobjects.reserve(d3dSubobjects.size() + pipeline->localRootSignatures.size() * 2);

        const size_t numAssociations = desc.shaders.size() + desc.hitGroups.size();
        std::vector<std::wstring> associationExportStorage;
        std::vector<LPCWSTR> associationExports;
        associationExportStorage.reserve(numAssociations);
        associationExports.reserve(numAssociations);

        for (const auto& localRootSignatureIt : pipeline->localRootSignatures)
        {
            D3D12_LOCAL_ROOT_SIGNATURE& localRootSignatureDesc = localRootSignatures.emplace_back();
            localRootSignatureDesc.pLocalRootSignature = localRootSignatureIt.second != nullptr
                ? static_cast<ZWD3D12RootSignature*>(localRootSignatureIt.second.Get())->handle.Get()
                : nullptr;

            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
            d3dSubobject.pDesc = &localRootSignatureDesc;
            d3dSubobjects.push_back(d3dSubobject);

            D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION& association = associations.emplace_back();
            association.pSubobjectToAssociate = &d3dSubobjects.back();
            association.NumExports = 0;
            const size_t firstExportIndex = associationExports.size();

            for (const Hrt::ZWPipelineShaderDesc& shaderDesc : desc.shaders)
            {
                if (shaderDesc.bindingLayout.Get() == localRootSignatureIt.first)
                {
                    const std::string exportName = shaderDesc.exportName.empty()
                        ? shaderDesc.shader->GetDesc().entryName
                        : shaderDesc.exportName;

                    associationExportStorage.push_back(ToWideString(exportName));
                    associationExports.push_back(associationExportStorage.back().c_str());
                    ++association.NumExports;
                }
            }

            for (const Hrt::ZWPipelineHitGroupDesc& hitGroupDesc : desc.hitGroups)
            {
                if (hitGroupDesc.bindingLayout.Get() == localRootSignatureIt.first)
                {
                    associationExportStorage.push_back(ToWideString(hitGroupDesc.exportName));
                    associationExports.push_back(associationExportStorage.back().c_str());
                    ++association.NumExports;
                }
            }

            association.pExports = association.NumExports != 0 ? &associationExports[firstExportIndex] : nullptr;

            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
            d3dSubobject.pDesc = &association;
            d3dSubobjects.push_back(d3dSubobject);
        }

        D3D12_STATE_OBJECT_DESC pipelineDesc = {};
        pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        pipelineDesc.NumSubobjects = static_cast<UINT>(d3dSubobjects.size());
        pipelineDesc.pSubobjects = d3dSubobjects.data();

        const HRESULT createResult = mContext.device5->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(pipeline->pipelineState.ReleaseAndGetAddressOf()));

        if (desc.hlslExtensionsUAV >= 0)
        {
            SetHlslExtensionsUAV(0xFFFFFFFFu);
        }

        if (FAILED(createResult))
        {
            std::stringstream messageBuilder;
            messageBuilder << "Failed to create a D3D12 ray tracing state object, HRESULT = 0x" << std::hex << createResult;
            mContext.Error(messageBuilder.str());
            return nullptr;
        }

        const HRESULT queryResult = pipeline->pipelineState->QueryInterface(IID_PPV_ARGS(pipeline->pipelineInfo.ReleaseAndGetAddressOf()));
        if (FAILED(queryResult))
        {
            std::stringstream messageBuilder;
            messageBuilder << "Failed to query ID3D12StateObjectProperties from a D3D12 ray tracing pipeline, HRESULT = 0x" << std::hex << queryResult;
            mContext.Error(messageBuilder.str());
            return nullptr;
        }

        for (const Hrt::ZWPipelineShaderDesc& shaderDesc : desc.shaders)
        {
            const std::string exportName = shaderDesc.exportName.empty()
                ? shaderDesc.shader->GetDesc().entryName
                : shaderDesc.exportName;

            const std::wstring exportNameWide = ToWideString(exportName);
            const void* shaderIdentifier = pipeline->pipelineInfo->GetShaderIdentifier(exportNameWide.c_str());
            if (shaderIdentifier == nullptr)
            {
                mContext.Error("Failed to retrieve a shader identifier from a freshly created D3D12 ray tracing pipeline.");
                return nullptr;
            }

            pipeline->exports[exportName] = ZWD3D12RayTracingPipeline::ZWD3D12ExportTableEntry
            {
                shaderDesc.bindingLayout.Get(),
                shaderIdentifier
            };
        }

        for (const Hrt::ZWPipelineHitGroupDesc& hitGroupDesc : desc.hitGroups)
        {
            const std::wstring exportNameWide = ToWideString(hitGroupDesc.exportName);
            const void* shaderIdentifier = pipeline->pipelineInfo->GetShaderIdentifier(exportNameWide.c_str());
            if (shaderIdentifier == nullptr)
            {
                mContext.Error("Failed to retrieve a hit-group identifier from a freshly created D3D12 ray tracing pipeline.");
                return nullptr;
            }

            pipeline->exports[hitGroupDesc.exportName] = ZWD3D12RayTracingPipeline::ZWD3D12ExportTableEntry
            {
                hitGroupDesc.bindingLayout.Get(),
                shaderIdentifier
            };
        }

        return Hrt::ZWPipelineHandle::Create(pipeline.release());
    }

    void ZWD3D12ShaderTable::Bake(
        uint8_t* cpuVA,
        D3D12_GPU_VIRTUAL_ADDRESS gpuVA,
        ZWD3D12DeviceResources& resources,
        ZWD3D12ShaderTableState& state)
    {
        const uint32_t entrySize = pipeline->GetShaderTableEntrySize();

        auto writeEntry = [this, &resources, entrySize, &cpuVA, &gpuVA](const ZWD3D12Entry& entry)
        {
            std::memset(cpuVA, 0, entrySize);

            if (entry.pShaderIdentifier != nullptr)
            {
                std::memcpy(cpuVA, entry.pShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            }

            if (entry.localBindings != nullptr)
            {
                ZWD3D12BindingSet* bindingSet = static_cast<ZWD3D12BindingSet*>(entry.localBindings.Get());
                ZWD3D12BindingLayout* layout = bindingSet != nullptr ? bindingSet->layout.Get() : nullptr;

                if (layout != nullptr)
                {
                    if (layout->descriptorTableSizeSamplers > 0)
                    {
                        D3D12_GPU_DESCRIPTOR_HANDLE* descriptorTable = reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(
                            cpuVA + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + layout->rootParameterSamplers * sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
                        *descriptorTable = resources.samplerHeap.GetGpuHandle(bindingSet->descriptorTableSamplers);
                    }

                    if (layout->descriptorTableSizeSRVetc > 0)
                    {
                        D3D12_GPU_DESCRIPTOR_HANDLE* descriptorTable = reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(
                            cpuVA + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + layout->rootParameterSRVetc * sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
                        *descriptorTable = resources.shaderResourceViewHeap.GetGpuHandle(bindingSet->descriptorTableSRVetc);
                    }

                    if (!layout->rootParametersVolatileCB.empty())
                    {
                        mContext.Error("Volatile constant buffers cannot be used in local shader-table bindings.");
                    }
                }
            }

            cpuVA += entrySize;
            gpuVA += entrySize;
        };

        D3D12_DISPATCH_RAYS_DESC& dispatchDesc = state.dispatchRaysTemplate;
        std::memset(&dispatchDesc, 0, sizeof(dispatchDesc));

        dispatchDesc.RayGenerationShaderRecord.StartAddress = gpuVA;
        dispatchDesc.RayGenerationShaderRecord.SizeInBytes = entrySize;
        writeEntry(rayGenerationShader);

        if (!missShaders.empty())
        {
            dispatchDesc.MissShaderTable.StartAddress = gpuVA;
            dispatchDesc.MissShaderTable.StrideInBytes = missShaders.size() == 1 ? 0u : entrySize;
            dispatchDesc.MissShaderTable.SizeInBytes = static_cast<uint32_t>(missShaders.size()) * entrySize;

            for (const ZWD3D12Entry& entry : missShaders)
            {
                writeEntry(entry);
            }
        }

        if (!hitGroups.empty())
        {
            dispatchDesc.HitGroupTable.StartAddress = gpuVA;
            dispatchDesc.HitGroupTable.StrideInBytes = hitGroups.size() == 1 ? 0u : entrySize;
            dispatchDesc.HitGroupTable.SizeInBytes = static_cast<uint32_t>(hitGroups.size()) * entrySize;

            for (const ZWD3D12Entry& entry : hitGroups)
            {
                writeEntry(entry);
            }
        }

        if (!callableShaders.empty())
        {
            dispatchDesc.CallableShaderTable.StartAddress = gpuVA;
            dispatchDesc.CallableShaderTable.StrideInBytes = callableShaders.size() == 1 ? 0u : entrySize;
            dispatchDesc.CallableShaderTable.SizeInBytes = static_cast<uint32_t>(callableShaders.size()) * entrySize;

            for (const ZWD3D12Entry& entry : callableShaders)
            {
                writeEntry(entry);
            }
        }

        state.committedVersion = version;
        if (pipeline != nullptr && pipeline->HasLocalResources())
        {
            state.descriptorHeapSRV = resources.shaderResourceViewHeap.GetShaderVisibleHeap();
            state.descriptorHeapSamplers = resources.samplerHeap.GetShaderVisibleHeap();
        }
        else
        {
            state.descriptorHeapSRV = nullptr;
            state.descriptorHeapSamplers = nullptr;
        }
    }

    bool ZWD3D12ShaderTable::IsStateValid(ZWD3D12ShaderTableState const& state, ZWD3D12DeviceResources const& resources) const
    {
        if (pipeline != nullptr && pipeline->HasLocalResources())
        {
            return state.committedVersion == version
                && state.descriptorHeapSRV == resources.shaderResourceViewHeap.GetShaderVisibleHeap()
                && state.descriptorHeapSamplers == resources.samplerHeap.GetShaderVisibleHeap();
        }

        return state.committedVersion == version;
    }

    ZWD3D12ShaderTableState& ZWD3D12CommandList::GetShaderTableState(Hrt::IShaderTable* shaderTable)
    {
        ZWD3D12ShaderTable* d3d12ShaderTable = static_cast<ZWD3D12ShaderTable*>(shaderTable);
        if (d3d12ShaderTable->GetDesc().isCached)
        {
            return d3d12ShaderTable->cacheState;
        }

        auto shaderTableStateIt = mUncachedShaderTableStates.find(shaderTable);
        if (shaderTableStateIt != mUncachedShaderTableStates.end())
        {
            return *shaderTableStateIt->second;
        }

        std::unique_ptr<ZWD3D12ShaderTableState> state = std::make_unique<ZWD3D12ShaderTableState>();
        ZWD3D12ShaderTableState& stateRef = *state;
        mUncachedShaderTableStates.insert(std::make_pair(shaderTable, std::move(state)));
        return stateRef;
    }

    void ZWD3D12CommandList::SetRayTracingState(const Hrt::ZWState& state)
    {
        if (mActiveCommandList == nullptr || mActiveCommandList->commandList4 == nullptr)
        {
            m_Context.Error("Ray tracing is not supported because ID3D12GraphicsCommandList4 is unavailable.");
            return;
        }

        ZWD3D12ShaderTable* shaderTable = static_cast<ZWD3D12ShaderTable*>(state.shaderTable);
        if (shaderTable == nullptr)
        {
            mCurrentRayTracingStateValid = false;
            mCurrentRayTracingState = state;
            return;
        }

        ZWD3D12RayTracingPipeline* pipeline = shaderTable->pipeline.Get();
        if (pipeline == nullptr || pipeline->pipelineState == nullptr || pipeline->globalRootSignature == nullptr)
        {
            m_Context.Error("The D3D12 ray tracing pipeline is incomplete and cannot be bound.");
            return;
        }

        if (shaderTable->rayGenerationShader.pShaderIdentifier == nullptr)
        {
            m_Context.Error("The shader table does not have a ray generation shader assigned.");
            return;
        }

        const bool shaderTableCached = shaderTable->GetDesc().isCached;
        ZWD3D12ShaderTableState& shaderTableState = GetShaderTableState(shaderTable);
        const bool rebuildShaderTable = !shaderTable->IsStateValid(shaderTableState, m_Resources);

        if (rebuildShaderTable)
        {
            const size_t shaderTableSize = shaderTable->GetUploadSize();
            ZWD3D12Buffer* cacheBuffer = static_cast<ZWD3D12Buffer*>(shaderTable->cache.Get());

            if (shaderTableCached && (cacheBuffer == nullptr || shaderTableSize > cacheBuffer->desc.byteSize))
            {
                m_Context.Error("The baked shader table is larger than the allocated cache buffer. Increase ShaderTableDesc::maxEntries.");
                return;
            }

            ID3D12Resource* uploadBuffer = nullptr;
            size_t uploadOffset = 0;
            uint8_t* uploadCpuVA = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS uploadGpuVA = 0;

            if (!mUploadManager.SuballocateBuffer(
                shaderTableSize,
                nullptr,
                &uploadBuffer,
                &uploadOffset,
                reinterpret_cast<void**>(&uploadCpuVA),
                &uploadGpuVA,
                mRecordingVersion,
                D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT))
            {
                m_Context.Error("Couldn't suballocate an upload buffer for building the shader table.");
                return;
            }

            const D3D12_GPU_VIRTUAL_ADDRESS effectiveGpuVA = shaderTableCached ? cacheBuffer->gpuVA : uploadGpuVA;
            shaderTable->Bake(uploadCpuVA, effectiveGpuVA, m_Resources, shaderTableState);

            if (shaderTableCached)
            {
                SetBufferState(cacheBuffer, EResourceStates::CopyDest);
                CommitBarriers();

                mActiveCommandList->commandList->CopyBufferRegion(
                    cacheBuffer->resource.Get(),
                    0,
                    uploadBuffer,
                    uploadOffset,
                    shaderTableSize);
            }
        }

        if (shaderTableCached)
        {
            SetBufferState(shaderTable->cache.Get(), EResourceStates::ShaderResource);
        }

        if (shaderTableCached || rebuildShaderTable)
        {
            mInstance->referencedResources.push_back(shaderTable);
        }

        const ZWD3D12ShaderTable* currentShaderTable = mCurrentRayTracingStateValid
            ? static_cast<ZWD3D12ShaderTable*>(mCurrentRayTracingState.shaderTable)
            : nullptr;

        const bool updateRootSignature = !mCurrentRayTracingStateValid
            || currentShaderTable == nullptr
            || currentShaderTable->pipeline.Get() == nullptr
            || currentShaderTable->pipeline->globalRootSignature.Get() != pipeline->globalRootSignature.Get();
        const bool updatePipeline = !mCurrentRayTracingStateValid
            || currentShaderTable == nullptr
            || currentShaderTable->pipeline.Get() != pipeline;

        uint32_t bindingUpdateMask = 0;
        if (!mCurrentRayTracingStateValid || updateRootSignature)
        {
            bindingUpdateMask = ~0u;
        }

        if (CommitDescriptorHeaps())
        {
            bindingUpdateMask = ~0u;
        }

        if (bindingUpdateMask == 0)
        {
            bindingUpdateMask = ArrayDifferenceMask(mCurrentRayTracingState.bindings, state.bindings);
        }

        if (updateRootSignature)
        {
            mActiveCommandList->commandList->SetComputeRootSignature(pipeline->globalRootSignature->handle.Get());
        }

        if (updatePipeline)
        {
            mActiveCommandList->commandList4->SetPipelineState1(pipeline->pipelineState.Get());
            mInstance->referencedResources.push_back(pipeline);
        }

        SetComputeBindings(state.bindings, bindingUpdateMask, nullptr, false, pipeline->globalRootSignature.Get());
        UnbindShadingRateState();

        mCurrentGraphicsStateValid = false;
        mCurrentComputeStateValid = false;
        mCurrentMeshletStateValid = false;
        mCurrentRayTracingStateValid = true;
        mCurrentRayTracingState = state;
        mBindingStatesDirty = false;

        CommitBarriers();
    }

    void ZWD3D12CommandList::DispatchRays(const Hrt::ZWDispatchRaysArguments& args)
    {
        UpdateComputeVolatileBuffers();

        if (mActiveCommandList == nullptr || mActiveCommandList->commandList4 == nullptr)
        {
            m_Context.Error("Ray tracing is not supported because ID3D12GraphicsCommandList4 is unavailable.");
            return;
        }

        if (!mCurrentRayTracingStateValid || mCurrentRayTracingState.shaderTable == nullptr)
        {
            m_Context.Error("SetRayTracingState must be called before DispatchRays.");
            return;
        }

        ZWD3D12ShaderTableState& shaderTableState = GetShaderTableState(mCurrentRayTracingState.shaderTable);
        const D3D12_DISPATCH_RAYS_DESC& bakedTemplate = shaderTableState.dispatchRaysTemplate;
        if (bakedTemplate.RayGenerationShaderRecord.StartAddress == 0)
        {
            m_Context.Error("The D3D12 shader table has not been baked yet.");
            return;
        }

        D3D12_DISPATCH_RAYS_DESC dispatchDesc = bakedTemplate;
        dispatchDesc.Width = args.width;
        dispatchDesc.Height = args.height;
        dispatchDesc.Depth = args.depth;

        mActiveCommandList->commandList4->DispatchRays(&dispatchDesc);
    }

    void ZWD3D12CommandList::BuildOpacityMicromap(Hrt::IOpacityMicromap* omm, const Hrt::ZWOpacityMicromapDesc& desc)
    {
#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP || HRHI_WITH_NVAPI_OPACITY_MICROMAP
        ZWD3D12OpacityMicromap* d3d12OpacityMicromap = static_cast<ZWD3D12OpacityMicromap*>(omm);
        if (d3d12OpacityMicromap == nullptr || d3d12OpacityMicromap->dataBuffer == nullptr)
        {
            m_Context.Error("BuildOpacityMicromap requires a valid opacity micromap target.");
            return;
        }

        if (mEnableAutomaticBarriers)
        {
            RequireBufferState(desc.inputBuffer, EResourceStates::OpacityMicromapBuildInput);
            RequireBufferState(desc.perOmmDescs, EResourceStates::OpacityMicromapBuildInput);
            RequireBufferState(d3d12OpacityMicromap->dataBuffer.Get(), EResourceStates::OpacityMicromapWrite);
            mBindingStatesDirty = true;
        }

        if (desc.trackLiveness)
        {
            mInstance->referencedResources.push_back(desc.inputBuffer);
            mInstance->referencedResources.push_back(desc.perOmmDescs);
            mInstance->referencedResources.push_back(d3d12OpacityMicromap->dataBuffer);
        }

        CommitBarriers();
#endif

#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ommInputs = {};
        D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC ommArrayDesc = {};
        FillD3D12OpacityMicromapDesc(ommInputs, ommArrayDesc, desc);

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuildInfo = {};
        m_Context.device8->GetRaytracingAccelerationStructurePrebuildInfo(&ommInputs, &preBuildInfo);

        D3D12_GPU_VIRTUAL_ADDRESS scratchGpuVA = 0;
        if (preBuildInfo.ScratchDataSizeInBytes > 0)
        {
            if (!mDxrScratchManager.SuballocateBuffer(
                preBuildInfo.ScratchDataSizeInBytes,
                mActiveCommandList->commandList.Get(),
                nullptr,
                nullptr,
                nullptr,
                &scratchGpuVA,
                mRecordingVersion,
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT))
            {
                m_Context.Error("Couldn't suballocate a scratch buffer for opacity micromap build.");
                return;
            }
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        buildDesc.Inputs = ommInputs;
        buildDesc.ScratchAccelerationStructureData = scratchGpuVA;
        buildDesc.DestAccelerationStructureData = d3d12OpacityMicromap->GetDeviceAddress();
        mActiveCommandList->commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
#elif HRHI_WITH_NVAPI_OPACITY_MICROMAP
        NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_INPUTS inputs = {};
        FillD3D12OpacityMicromapDesc(inputs, desc);

        NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO preBuildInfo = {};
        NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS prebuildParams = {};
        prebuildParams.version = NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS_VER;
        prebuildParams.pDesc = &inputs;
        prebuildParams.pInfo = &preBuildInfo;

        const NvAPI_Status prebuildStatus = NvAPI_D3D12_GetRaytracingOpacityMicromapArrayPrebuildInfo(m_Context.device5.Get(), &prebuildParams);
        if (prebuildStatus != NVAPI_OK)
        {
            m_Context.Error("NvAPI_D3D12_GetRaytracingOpacityMicromapArrayPrebuildInfo failed.");
            return;
        }

        D3D12_GPU_VIRTUAL_ADDRESS scratchGpuVA = 0;
        if (preBuildInfo.scratchDataSizeInBytes != 0)
        {
            if (!mDxrScratchManager.SuballocateBuffer(
                preBuildInfo.scratchDataSizeInBytes,
                mActiveCommandList->commandList.Get(),
                nullptr,
                nullptr,
                nullptr,
                &scratchGpuVA,
                mRecordingVersion,
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT))
            {
                m_Context.Error("Couldn't suballocate a scratch buffer for opacity micromap build.");
                return;
            }
        }

        NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC nativeDesc = {};
        nativeDesc.destOpacityMicromapArrayData = d3d12OpacityMicromap->GetDeviceAddress();
        nativeDesc.inputs = inputs;
        nativeDesc.scratchOpacityMicromapArrayData = scratchGpuVA;

        NVAPI_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_PARAMS params = {};
        params.version = NVAPI_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_PARAMS_VER;
        params.pDesc = &nativeDesc;
        params.numPostbuildInfoDescs = 0;
        params.pPostbuildInfoDescs = nullptr;

        const NvAPI_Status buildStatus = NvAPI_D3D12_BuildRaytracingOpacityMicromapArray(mActiveCommandList->commandList4.Get(), &params);
        if (buildStatus != NVAPI_OK)
        {
            m_Context.Error("NvAPI_D3D12_BuildRaytracingOpacityMicromapArray failed.");
        }
#else
        (void)omm;
        (void)desc;
        m_Context.Error("Opacity micromaps are not supported by this D3D12 backend build.");
#endif
    }

    void ZWD3D12CommandList::BuildBottomLevelAccelStruct(
        Hrt::IAccelStruct* accelStruct,
        const Hrt::ZWGeometryDesc* pGeometries,
        size_t numGeometries,
        Hrt::EAccelStructBuildFlags buildFlags)
    {
        if (mActiveCommandList == nullptr || mActiveCommandList->commandList4 == nullptr)
        {
            m_Context.Error("Ray tracing is not supported because ID3D12GraphicsCommandList4 is unavailable.");
            return;
        }

        if (numGeometries != 0 && pGeometries == nullptr)
        {
            m_Context.Error("BuildBottomLevelAccelStruct requires a valid geometry array when numGeometries is non-zero.");
            return;
        }

        ZWD3D12AccelStruct* d3d12AccelStruct = static_cast<ZWD3D12AccelStruct*>(accelStruct);
        if (!ValidateBoundAccelStructStorage(d3d12AccelStruct, m_Context, "BLAS build"))
        {
            return;
        }

        const bool performUpdate = (buildFlags & Hrt::EAccelStructBuildFlags::PerformUpdate) != 0;
        if (performUpdate && !d3d12AccelStruct->allowUpdate)
        {
            m_Context.Error("The BLAS was not created with AllowUpdate, but PerformUpdate was requested.");
            return;
        }

        bool hasOpacityMicromap = false;
        bool hasSpheres = false;
        bool hasLss = false;

        for (size_t geometryIndex = 0; geometryIndex < numGeometries; ++geometryIndex)
        {
            const Hrt::ZWGeometryDesc& geometryDesc = pGeometries[geometryIndex];

            switch (geometryDesc.geometryType)
            {
            case Hrt::GeometryType::Triangles:
            {
                const Hrt::ZWGeometryTriangles& triangles = geometryDesc.geometryData.triangles;
                ZWD3D12OpacityMicromap* opacityMicromap = static_cast<ZWD3D12OpacityMicromap*>(triangles.opacityMicromap);

                if (triangles.opacityMicromap != nullptr || triangles.ommIndexBuffer != nullptr)
                {
#if !(HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP || HRHI_WITH_NVAPI_OPACITY_MICROMAP)
                    m_Context.Error("Opacity micromaps are not supported by this D3D12 backend build.");
                    return;
#endif
                    hasOpacityMicromap = true;
                }

                if (mEnableAutomaticBarriers)
                {
                    if (triangles.indexBuffer != nullptr)
                    {
                        RequireBufferState(triangles.indexBuffer, EResourceStates::AccelStructBuildInput);
                    }
                    if (triangles.vertexBuffer != nullptr)
                    {
                        RequireBufferState(triangles.vertexBuffer, EResourceStates::AccelStructBuildInput);
                    }
                    if (opacityMicromap != nullptr && opacityMicromap->dataBuffer != nullptr)
                    {
                        RequireBufferState(opacityMicromap->dataBuffer.Get(), EResourceStates::AccelStructBuildInput);
                    }
                    if (triangles.ommIndexBuffer != nullptr)
                    {
                        RequireBufferState(triangles.ommIndexBuffer, EResourceStates::AccelStructBuildInput);
                    }
                }

                if (triangles.indexBuffer != nullptr)
                {
                    mInstance->referencedResources.push_back(triangles.indexBuffer);
                }
                if (triangles.vertexBuffer != nullptr)
                {
                    mInstance->referencedResources.push_back(triangles.vertexBuffer);
                }
                if (opacityMicromap != nullptr && opacityMicromap->desc.trackLiveness)
                {
                    mInstance->referencedResources.push_back(opacityMicromap);
                }
                if (triangles.ommIndexBuffer != nullptr)
                {
                    mInstance->referencedResources.push_back(triangles.ommIndexBuffer);
                }
                break;
            }

            case Hrt::GeometryType::AABBs:
            {
                const Hrt::ZWGeometryAABBs& aabbs = geometryDesc.geometryData.aabbs;
                if (mEnableAutomaticBarriers && aabbs.buffer != nullptr)
                {
                    RequireBufferState(aabbs.buffer, EResourceStates::AccelStructBuildInput);
                }

                if (aabbs.buffer != nullptr)
                {
                    mInstance->referencedResources.push_back(aabbs.buffer);
                }
                break;
            }

            case Hrt::GeometryType::Spheres:
            {
#if HRHI_WITH_NVAPI_LSS
                const Hrt::ZWGeometrySpheres& spheres = geometryDesc.geometryData.spheres;
                hasSpheres = true;

                if (mEnableAutomaticBarriers)
                {
                    if (spheres.indexBuffer != nullptr)
                    {
                        RequireBufferState(spheres.indexBuffer, EResourceStates::AccelStructBuildInput);
                    }
                    if (spheres.vertexBuffer != nullptr)
                    {
                        RequireBufferState(spheres.vertexBuffer, EResourceStates::AccelStructBuildInput);
                    }
                }

                if (spheres.indexBuffer != nullptr)
                {
                    mInstance->referencedResources.push_back(spheres.indexBuffer);
                }
                if (spheres.vertexBuffer != nullptr)
                {
                    mInstance->referencedResources.push_back(spheres.vertexBuffer);
                }
                break;
#else
                m_Context.Error("Spheres and line-swept spheres are not supported by this D3D12 backend build.");
                return;
#endif
            }

            case Hrt::GeometryType::Lss:
            {
#if HRHI_WITH_NVAPI_LSS
                const Hrt::ZWGeometryLss& lss = geometryDesc.geometryData.lss;
                hasLss = true;

                if (mEnableAutomaticBarriers)
                {
                    if (lss.indexBuffer != nullptr)
                    {
                        RequireBufferState(lss.indexBuffer, EResourceStates::AccelStructBuildInput);
                    }
                    if (lss.vertexBuffer != nullptr)
                    {
                        RequireBufferState(lss.vertexBuffer, EResourceStates::AccelStructBuildInput);
                    }
                }

                if (lss.indexBuffer != nullptr)
                {
                    mInstance->referencedResources.push_back(lss.indexBuffer);
                }
                if (lss.vertexBuffer != nullptr)
                {
                    mInstance->referencedResources.push_back(lss.vertexBuffer);
                }
                break;
#else
                m_Context.Error("Spheres and line-swept spheres are not supported by this D3D12 backend build.");
                return;
#endif
            }

            default:
                m_Context.Error("Encountered an unknown geometry type while building a BLAS.");
                return;
            }
        }

        ZWD3D12Device* d3d12Device = static_cast<ZWD3D12Device*>(mDevice);
        if (d3d12Device == nullptr)
        {
            m_Context.Error("The active D3D12 command list is not attached to a valid D3D12 device.");
            return;
        }

        if (hasOpacityMicromap && !d3d12Device->GetOpacityMicromapSupported())
        {
            m_Context.Error("This device does not support opacity micromap geometry in D3D12 acceleration structures.");
            return;
        }

        if (hasSpheres && !d3d12Device->GetSpheresSupported())
        {
            m_Context.Error("This device does not support sphere geometry in D3D12 acceleration structures.");
            return;
        }

        if (hasLss && !d3d12Device->GetLinearSweptSpheresSupported())
        {
            m_Context.Error("This device does not support line-swept sphere geometry in D3D12 acceleration structures.");
            return;
        }

        const bool requiresNvapiExBuild = hasOpacityMicromap || hasSpheres || hasLss;

        if (mEnableAutomaticBarriers)
        {
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        ZWD3D12BuildRaytracingAccelerationStructureInputs inputs;
        inputs.SetType(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL);

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS d3d12BuildFlags = ConvertAccelStructBuildFlags(buildFlags);
        if (d3d12AccelStruct->allowUpdate)
        {
            d3d12BuildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        }

        inputs.SetFlags(d3d12BuildFlags);
        inputs.SetGeometryDescCount(static_cast<uint32_t>(numGeometries));

        bool hasDxr12OMM = false;
        for (uint32_t geometryIndex = 0; geometryIndex < static_cast<uint32_t>(numGeometries); ++geometryIndex)
        {
            const Hrt::ZWGeometryDesc& geometryDesc = pGeometries[geometryIndex];

            D3D12_GPU_VIRTUAL_ADDRESS transformGpuVA = 0;
            if (geometryDesc.useTransform)
            {
                void* uploadCpuVA = nullptr;
                if (!mUploadManager.SuballocateBuffer(
                    sizeof(Hrt::AffineTransform),
                    nullptr,
                    nullptr,
                    nullptr,
                    &uploadCpuVA,
                    &transformGpuVA,
                    mRecordingVersion,
                    D3D12_RAYTRACING_TRANSFORM3X4_BYTE_ALIGNMENT))
                {
                    m_Context.Error("Couldn't suballocate an upload buffer for a BLAS transform.");
                    return;
                }

                std::memcpy(uploadCpuVA, &geometryDesc.transform, sizeof(Hrt::AffineTransform));
            }

            if (!FillD3D12GeometryDesc(inputs.GetGeometryDesc(geometryIndex), geometryDesc, transformGpuVA, m_Context))
            {
                return;
            }

            if (geometryDesc.geometryType == Hrt::GeometryType::Triangles
                && geometryDesc.geometryData.triangles.opacityMicromap != nullptr)
            {
                hasDxr12OMM = true;
            }
        }

#if HRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP
        if (hasDxr12OMM)
        {
            inputs.SetOMMDescCount(static_cast<uint32_t>(numGeometries));

            for (uint32_t geometryIndex = 0; geometryIndex < static_cast<uint32_t>(numGeometries); ++geometryIndex)
            {
                FillD3D12GeometryOMMLinkageDesc(inputs.GetOMMLinkageDesc(geometryIndex), pGeometries[geometryIndex]);
            }
        }
#endif

#if HRHI_WITH_RTXMU
        const bool useRtxmu = m_Context.rtxMemUtil != nullptr && !requiresNvapiExBuild;
        if (requiresNvapiExBuild && d3d12AccelStruct->rtxmuId != ~0ull)
        {
            m_Context.Error("This BLAS is currently managed by RTXMU and cannot be rebuilt with opacity micromaps or sphere-based geometry.");
            return;
        }

        if (useRtxmu)
        {
            std::vector<uint64_t> accelStructsToBuild;
            std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS> buildInputs;
            buildInputs.push_back(inputs.GetAs<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS>());

            if (d3d12AccelStruct->rtxmuId == ~0ull)
            {
                m_Context.rtxMemUtil->PopulateBuildCommandList(
                    mActiveCommandList->commandList4.Get(),
                    buildInputs.data(),
                    buildInputs.size(),
                    accelStructsToBuild);

                if (accelStructsToBuild.empty())
                {
                    m_Context.Error("RTXMU did not return a BLAS handle after PopulateBuildCommandList.");
                    return;
                }

                d3d12AccelStruct->rtxmuId = static_cast<size_t>(accelStructsToBuild[0]);
                mInstance->rtxmuBuildIds.push_back(accelStructsToBuild[0]);
            }
            else
            {
                std::vector<uint64_t> accelStructsToUpdate = { static_cast<uint64_t>(d3d12AccelStruct->rtxmuId) };
                m_Context.rtxMemUtil->PopulateUpdateCommandList(
                    mActiveCommandList->commandList4.Get(),
                    buildInputs.data(),
                    static_cast<uint32_t>(buildInputs.size()),
                    accelStructsToUpdate);
            }

            if (d3d12AccelStruct->desc.trackLiveness)
            {
                mInstance->referencedResources.push_back(d3d12AccelStruct);
            }
            return;
        }
#endif

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuildInfo = {};
#if HRHI_WITH_NVAPI_OPACITY_MICROMAP || HRHI_WITH_NVAPI_LSS
        if (requiresNvapiExBuild)
        {
            const NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX d3d12Inputs =
                inputs.GetAs<NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX>();

            NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS params = {};
            params.version = NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS_VER;
            params.pDesc = &d3d12Inputs;
            params.pInfo = &preBuildInfo;

            const NvAPI_Status status = NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx(m_Context.device5.Get(), &params);
            if (status != NVAPI_OK)
            {
                m_Context.Error("NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx failed.");
                return;
            }
        }
        else
#endif
        {
            const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS d3d12Inputs =
                inputs.GetAs<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS>();
            m_Context.device5->GetRaytracingAccelerationStructurePrebuildInfo(&d3d12Inputs, &preBuildInfo);
        }

        if (preBuildInfo.ResultDataMaxSizeInBytes > d3d12AccelStruct->dataBuffer->desc.byteSize)
        {
            std::stringstream messageBuilder;
            messageBuilder << "BLAS " << HApp::DebugNameToString(d3d12AccelStruct->desc.debugName)
                << " build requires at least " << preBuildInfo.ResultDataMaxSizeInBytes
                << " bytes, but only " << d3d12AccelStruct->dataBuffer->desc.byteSize << " bytes were allocated.";
            m_Context.Error(messageBuilder.str());
            return;
        }

        const uint64_t scratchSize = performUpdate
            ? preBuildInfo.UpdateScratchDataSizeInBytes
            : preBuildInfo.ScratchDataSizeInBytes;

        D3D12_GPU_VIRTUAL_ADDRESS scratchGpuVA = 0;
        if (scratchSize > 0)
        {
            if (!mDxrScratchManager.SuballocateBuffer(
                scratchSize,
                mActiveCommandList->commandList.Get(),
                nullptr,
                nullptr,
                nullptr,
                &scratchGpuVA,
                mRecordingVersion,
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT))
            {
                std::stringstream messageBuilder;
                messageBuilder << "Couldn't suballocate a scratch buffer for BLAS "
                    << HApp::DebugNameToString(d3d12AccelStruct->desc.debugName)
                    << ". The build requires " << scratchSize << " bytes of scratch space.";
                m_Context.Error(messageBuilder.str());
                return;
            }
        }

        if (mEnableAutomaticBarriers)
        {
            RequireBufferState(d3d12AccelStruct->dataBuffer.Get(), EResourceStates::AccelStructWrite);
            mBindingStatesDirty = true;
        }
        CommitBarriers();

#if HRHI_WITH_NVAPI_OPACITY_MICROMAP || HRHI_WITH_NVAPI_LSS
        if (requiresNvapiExBuild)
        {
            NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC_EX buildDesc = {};
            buildDesc.inputs = inputs.GetAs<NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX>();
            buildDesc.scratchAccelerationStructureData = scratchGpuVA;
            buildDesc.destAccelerationStructureData = d3d12AccelStruct->dataBuffer->gpuVA;
            buildDesc.sourceAccelerationStructureData = performUpdate ? d3d12AccelStruct->dataBuffer->gpuVA : 0;

            NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS params = {};
            params.version = NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS_VER;
            params.pDesc = &buildDesc;
            params.numPostbuildInfoDescs = 0;
            params.pPostbuildInfoDescs = nullptr;

            const NvAPI_Status status = NvAPI_D3D12_BuildRaytracingAccelerationStructureEx(mActiveCommandList->commandList4.Get(), &params);
            if (status != NVAPI_OK)
            {
                m_Context.Error("NvAPI_D3D12_BuildRaytracingAccelerationStructureEx failed.");
                return;
            }
        }
        else
#endif
        {
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
            buildDesc.Inputs = inputs.GetAs<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS>();
            buildDesc.ScratchAccelerationStructureData = scratchGpuVA;
            buildDesc.DestAccelerationStructureData = d3d12AccelStruct->dataBuffer->gpuVA;
            buildDesc.SourceAccelerationStructureData = performUpdate ? d3d12AccelStruct->dataBuffer->gpuVA : 0;

            mActiveCommandList->commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
        }

        if (d3d12AccelStruct->desc.trackLiveness)
        {
            mInstance->referencedResources.push_back(d3d12AccelStruct);
        }
    }

    void ZWD3D12CommandList::CompactBottomLevelAccelStructs()
    {
#if HRHI_WITH_RTXMU
        if (!m_Resources.asBuildsCompleted.empty())
        {
            std::lock_guard<std::mutex> lockGuard(m_Resources.asListMutex);

            if (!m_Resources.asBuildsCompleted.empty())
            {
                m_Context.rtxMemUtil->PopulateCompactionCommandList(
                    mActiveCommandList->commandList4.Get(),
                    m_Resources.asBuildsCompleted);

                mInstance->rtxmuCompactionIds.insert(
                    mInstance->rtxmuCompactionIds.end(),
                    m_Resources.asBuildsCompleted.begin(),
                    m_Resources.asBuildsCompleted.end());

                m_Resources.asBuildsCompleted.clear();
            }
        }
#endif
    }

    void ZWD3D12CommandList::BuildTopLevelAccelStructInternal(
        ZWD3D12AccelStruct* accelStruct,
        D3D12_GPU_VIRTUAL_ADDRESS instanceData,
        size_t numInstances,
        Hrt::EAccelStructBuildFlags buildFlags)
    {
        if (mActiveCommandList == nullptr || mActiveCommandList->commandList4 == nullptr)
        {
            m_Context.Error("Ray tracing is not supported because ID3D12GraphicsCommandList4 is unavailable.");
            return;
        }

        if (!ValidateBoundAccelStructStorage(accelStruct, m_Context, "TLAS build"))
        {
            return;
        }

        buildFlags = buildFlags & ~Hrt::EAccelStructBuildFlags::AllowEmptyInstances;

        const bool performUpdate = (buildFlags & Hrt::EAccelStructBuildFlags::PerformUpdate) != 0;
        if (performUpdate && !accelStruct->allowUpdate)
        {
            m_Context.Error("The TLAS was not created with AllowUpdate, but PerformUpdate was requested.");
            return;
        }

        if (performUpdate && accelStruct->dxrInstances.size() != numInstances)
        {
            m_Context.Error("A TLAS update must use the same instance count as the previous build.");
            return;
        }

        if (m_Context.device5 == nullptr)
        {
            m_Context.Error("TLAS building requires ID3D12Device5.");
            return;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.InstanceDescs = instanceData;
        inputs.NumDescs = static_cast<UINT>(numInstances);
        inputs.Flags = ConvertAccelStructBuildFlags(buildFlags);

        if (accelStruct->allowUpdate)
        {
            inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        }

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuildInfo = {};
        m_Context.device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &preBuildInfo);

        if (preBuildInfo.ResultDataMaxSizeInBytes > accelStruct->dataBuffer->desc.byteSize)
        {
            std::stringstream messageBuilder;
            messageBuilder << "TLAS " << HApp::DebugNameToString(accelStruct->desc.debugName)
                << " build requires at least " << preBuildInfo.ResultDataMaxSizeInBytes
                << " bytes, but only " << accelStruct->dataBuffer->desc.byteSize << " bytes were allocated.";
            m_Context.Error(messageBuilder.str());
            return;
        }

        const uint64_t scratchSize = performUpdate
            ? preBuildInfo.UpdateScratchDataSizeInBytes
            : preBuildInfo.ScratchDataSizeInBytes;

        D3D12_GPU_VIRTUAL_ADDRESS scratchGpuVA = 0;
        if (scratchSize > 0)
        {
            if (!mDxrScratchManager.SuballocateBuffer(
                scratchSize,
                mActiveCommandList->commandList.Get(),
                nullptr,
                nullptr,
                nullptr,
                &scratchGpuVA,
                mRecordingVersion,
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT))
            {
                std::stringstream messageBuilder;
                messageBuilder << "Couldn't suballocate a scratch buffer for TLAS "
                    << HApp::DebugNameToString(accelStruct->desc.debugName)
                    << ". The build requires " << scratchSize << " bytes of scratch space.";
                m_Context.Error(messageBuilder.str());
                return;
            }
        }

#if HRHI_WITH_RTXMU
        if (m_Context.rtxMemUtil != nullptr && !mInstance->rtxmuBuildIds.empty())
        {
            m_Context.rtxMemUtil->PopulateUAVBarriersCommandList(mActiveCommandList->commandList4.Get(), mInstance->rtxmuBuildIds);
        }
#endif

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        buildDesc.Inputs = inputs;
        buildDesc.ScratchAccelerationStructureData = scratchGpuVA;
        buildDesc.DestAccelerationStructureData = accelStruct->dataBuffer->gpuVA;
        buildDesc.SourceAccelerationStructureData = performUpdate ? accelStruct->dataBuffer->gpuVA : 0;

        mActiveCommandList->commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    }

    void ZWD3D12CommandList::BuildTopLevelAccelStruct(
        Hrt::IAccelStruct* accelStruct,
        const Hrt::ZWInstanceDesc* pInstances,
        size_t numInstances,
        Hrt::EAccelStructBuildFlags buildFlags)
    {
        if (numInstances != 0 && pInstances == nullptr)
        {
            m_Context.Error("BuildTopLevelAccelStruct requires a valid instance array when numInstances is non-zero.");
            return;
        }

        ZWD3D12AccelStruct* d3d12AccelStruct = static_cast<ZWD3D12AccelStruct*>(accelStruct);
        if (!ValidateBoundAccelStructStorage(d3d12AccelStruct, m_Context, "TLAS build"))
        {
            return;
        }

        d3d12AccelStruct->bottomLevelASes.clear();
        d3d12AccelStruct->dxrInstances.resize(numInstances);

        static_assert(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) == sizeof(Hrt::ZWInstanceDesc));

        for (size_t instanceIndex = 0; instanceIndex < numInstances; ++instanceIndex)
        {
            const Hrt::ZWInstanceDesc& instanceDesc = pInstances[instanceIndex];
            D3D12_RAYTRACING_INSTANCE_DESC& d3d12InstanceDesc = d3d12AccelStruct->dxrInstances[instanceIndex];

            if (instanceDesc.bottomLevelAS != nullptr)
            {
                ZWD3D12AccelStruct* bottomLevelAS = static_cast<ZWD3D12AccelStruct*>(instanceDesc.bottomLevelAS);
                if (!ValidateBoundAccelStructStorage(bottomLevelAS, m_Context, "TLAS build"))
                {
                    return;
                }

                if (bottomLevelAS->desc.trackLiveness)
                {
                    d3d12AccelStruct->bottomLevelASes.push_back(bottomLevelAS);
                }

                std::memcpy(&d3d12InstanceDesc, &instanceDesc, sizeof(instanceDesc));
                d3d12InstanceDesc.AccelerationStructure = bottomLevelAS->GetDeviceAddress();

                if (mEnableAutomaticBarriers)
                {
                    RequireBufferState(bottomLevelAS->dataBuffer.Get(), EResourceStates::AccelStructBuildBlas);
                }
            }
            else
            {
                std::memcpy(&d3d12InstanceDesc, &instanceDesc, sizeof(instanceDesc));
                d3d12InstanceDesc.AccelerationStructure = 0;
            }
        }

        D3D12_RAYTRACING_INSTANCE_DESC* uploadCpuVA = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS uploadGpuVA = 0;
        const size_t uploadSize = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * d3d12AccelStruct->dxrInstances.size();

        if (uploadSize > 0)
        {
            if (!mUploadManager.SuballocateBuffer(
                uploadSize,
                nullptr,
                nullptr,
                nullptr,
                reinterpret_cast<void**>(&uploadCpuVA),
                &uploadGpuVA,
                mRecordingVersion,
                D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
            {
                m_Context.Error("Couldn't suballocate an upload buffer for TLAS instances.");
                return;
            }

            std::memcpy(uploadCpuVA, d3d12AccelStruct->dxrInstances.data(), uploadSize);
        }

        if (mEnableAutomaticBarriers)
        {
            RequireBufferState(d3d12AccelStruct->dataBuffer.Get(), EResourceStates::AccelStructWrite);
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        BuildTopLevelAccelStructInternal(d3d12AccelStruct, uploadGpuVA, numInstances, buildFlags);

        if (d3d12AccelStruct->desc.trackLiveness)
        {
            mInstance->referencedResources.push_back(d3d12AccelStruct);
        }
    }

    void ZWD3D12CommandList::BuildTopLevelAccelStructFromBuffer(
        Hrt::IAccelStruct* accelStruct,
        HRHI::IBuffer* instanceBuffer,
        uint64_t instanceBufferOffset,
        size_t numInstances,
        Hrt::EAccelStructBuildFlags buildFlags)
    {
        if (numInstances != 0 && instanceBuffer == nullptr)
        {
            m_Context.Error("BuildTopLevelAccelStructFromBuffer requires a valid instance buffer when numInstances is non-zero.");
            return;
        }

        ZWD3D12AccelStruct* d3d12AccelStruct = static_cast<ZWD3D12AccelStruct*>(accelStruct);
        if (!ValidateBoundAccelStructStorage(d3d12AccelStruct, m_Context, "TLAS build"))
        {
            return;
        }

        d3d12AccelStruct->bottomLevelASes.clear();
        d3d12AccelStruct->dxrInstances.clear();

        if (mEnableAutomaticBarriers)
        {
            RequireBufferState(d3d12AccelStruct->dataBuffer.Get(), EResourceStates::AccelStructWrite);
            if (instanceBuffer != nullptr)
            {
                RequireBufferState(instanceBuffer, EResourceStates::AccelStructBuildInput);
            }
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        if (instanceBuffer != nullptr)
        {
            mInstance->referencedResources.push_back(instanceBuffer);
        }

        const D3D12_GPU_VIRTUAL_ADDRESS instanceData = instanceBuffer != nullptr
            ? GetBufferGpuVA(instanceBuffer) + instanceBufferOffset
            : 0;

        BuildTopLevelAccelStructInternal(d3d12AccelStruct, instanceData, numInstances, buildFlags);

        if (d3d12AccelStruct->desc.trackLiveness)
        {
            mInstance->referencedResources.push_back(d3d12AccelStruct);
        }
    }

    void ZWD3D12CommandList::ExecuteMultiIndirectClusterOperation(const Hrt::HCluster::ZWOperationDesc& desc)
    {
#if HRHI_WITH_NVAPI_CLUSTERS
        if (desc.params.maxArgCount == 0)
        {
            return;
        }

        if (desc.inIndirectArgsBuffer == nullptr)
        {
            m_Context.Error("ExecuteMultiIndirectClusterOperation requires a valid indirect arguments buffer.");
            return;
        }

        if (desc.scratchSizeInBytes == 0)
        {
            m_Context.Error("ExecuteMultiIndirectClusterOperation requires a non-zero scratch size.");
            return;
        }

        if (desc.params.mode == Hrt::HCluster::EOperationMode::ImplicitDestinations)
        {
            if (desc.inOutAddressesBuffer == nullptr || desc.outAccelerationStructuresBuffer == nullptr)
            {
                m_Context.Error("ImplicitDestinations cluster operations require both the addresses buffer and the destination acceleration-structure buffer.");
                return;
            }
        }
        else if (desc.params.mode == Hrt::HCluster::EOperationMode::ExplicitDestinations)
        {
            if (desc.inOutAddressesBuffer == nullptr)
            {
                m_Context.Error("ExplicitDestinations cluster operations require a destination address buffer.");
                return;
            }
        }
        else if (desc.params.mode == Hrt::HCluster::EOperationMode::GetSizes)
        {
            if (desc.outSizesBuffer == nullptr)
            {
                m_Context.Error("GetSizes cluster operations require a valid output size buffer.");
                return;
            }
        }

        ZWD3D12Buffer* inIndirectArgCountBuffer = static_cast<ZWD3D12Buffer*>(desc.inIndirectArgCountBuffer);
        ZWD3D12Buffer* inIndirectArgsBuffer = static_cast<ZWD3D12Buffer*>(desc.inIndirectArgsBuffer);
        ZWD3D12Buffer* inOutAddressesBuffer = static_cast<ZWD3D12Buffer*>(desc.inOutAddressesBuffer);
        ZWD3D12Buffer* outAccelerationStructuresBuffer = static_cast<ZWD3D12Buffer*>(desc.outAccelerationStructuresBuffer);
        ZWD3D12Buffer* outSizesBuffer = static_cast<ZWD3D12Buffer*>(desc.outSizesBuffer);

        D3D12_GPU_VIRTUAL_ADDRESS scratchGpuVA = 0;
        if (!mDxrScratchManager.SuballocateBuffer(
            desc.scratchSizeInBytes,
            mActiveCommandList->commandList.Get(),
            nullptr,
            nullptr,
            nullptr,
            &scratchGpuVA,
            mRecordingVersion,
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT))
        {
            const char* operationType = "Unknown";
            switch (desc.params.type)
            {
            case Hrt::HCluster::EOperationType::Move:
                operationType = "Move";
                break;
            case Hrt::HCluster::EOperationType::ClasBuild:
                operationType = "ClasBuild";
                break;
            case Hrt::HCluster::EOperationType::ClasBuildTemplates:
                operationType = "ClasBuildTemplates";
                break;
            case Hrt::HCluster::EOperationType::ClasInstantiateTemplates:
                operationType = "ClasInstantiateTemplates";
                break;
            case Hrt::HCluster::EOperationType::BlasBuild:
                operationType = "BlasBuild";
                break;
            }

            std::stringstream messageBuilder;
            messageBuilder << "Couldn't suballocate a scratch buffer for cluster operation " << operationType
                << ". The operation requires " << desc.scratchSizeInBytes << " bytes of scratch space.";
            m_Context.Error(messageBuilder.str());
            return;
        }

        if (mEnableAutomaticBarriers)
        {
            RequireBufferState(inIndirectArgsBuffer, EResourceStates::ShaderResource);
            if (inIndirectArgCountBuffer != nullptr)
            {
                RequireBufferState(inIndirectArgCountBuffer, EResourceStates::ShaderResource);
            }
            if (inOutAddressesBuffer != nullptr)
            {
                RequireBufferState(inOutAddressesBuffer, EResourceStates::UnorderedAccess);
            }
            if (outAccelerationStructuresBuffer != nullptr)
            {
                RequireBufferState(outAccelerationStructuresBuffer, EResourceStates::AccelStructWrite);
            }
            if (outSizesBuffer != nullptr)
            {
                RequireBufferState(outSizesBuffer, EResourceStates::UnorderedAccess);
            }
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        if (inIndirectArgCountBuffer != nullptr)
        {
            mInstance->referencedResources.push_back(inIndirectArgCountBuffer);
        }
        mInstance->referencedResources.push_back(inIndirectArgsBuffer);
        if (inOutAddressesBuffer != nullptr)
        {
            mInstance->referencedResources.push_back(inOutAddressesBuffer);
        }
        if (outAccelerationStructuresBuffer != nullptr)
        {
            mInstance->referencedResources.push_back(outAccelerationStructuresBuffer);
        }
        if (outSizesBuffer != nullptr)
        {
            mInstance->referencedResources.push_back(outSizesBuffer);
        }

        NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS inputs = TranslateClusterOperation(desc.params);

        NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC nativeDesc = {};
        nativeDesc.inputs = inputs;
        nativeDesc.addressResolutionFlags = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE;

        if (inIndirectArgCountBuffer != nullptr)
        {
            nativeDesc.indirectArgCount = inIndirectArgCountBuffer->gpuVA + desc.inIndirectArgCountOffsetInBytes;
        }

        nativeDesc.indirectArgArray.StartAddress = inIndirectArgsBuffer->gpuVA + desc.inIndirectArgsOffsetInBytes;
        nativeDesc.indirectArgArray.StrideInBytes = inIndirectArgsBuffer->desc.structStride;
        nativeDesc.batchScratchData = scratchGpuVA;

        if (inOutAddressesBuffer != nullptr)
        {
            nativeDesc.destinationAddressArray.StartAddress = inOutAddressesBuffer->gpuVA + desc.inOutAddressesOffsetInBytes;
            nativeDesc.destinationAddressArray.StrideInBytes = inOutAddressesBuffer->desc.structStride;
        }

        if (outAccelerationStructuresBuffer != nullptr)
        {
            nativeDesc.batchResultData = outAccelerationStructuresBuffer->gpuVA + desc.outAccelerationStructuresOffsetInBytes;
        }

        if (outSizesBuffer != nullptr)
        {
            nativeDesc.resultSizeArray.StartAddress = outSizesBuffer->gpuVA + desc.outSizesOffsetInBytes;
            nativeDesc.resultSizeArray.StrideInBytes = outSizesBuffer->desc.structStride;
        }

        NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS params = {};
        params.version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER;
        params.pDesc = &nativeDesc;

        const NvAPI_Status result = NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(mActiveCommandList->commandList4.Get(), &params);
        if (result != NVAPI_OK)
        {
            m_Context.Error("NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation failed.");
        }
#else
        (void)desc;
        m_Context.Error("Ray tracing cluster operations are not supported by this D3D12 backend build.");
#endif
    }
}
