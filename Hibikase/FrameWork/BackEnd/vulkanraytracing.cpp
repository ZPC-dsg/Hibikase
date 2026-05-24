/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_map>

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

    static vk::DeviceOrHostAddressConstKHR GetBufferAddress(IBuffer* _buffer, uint64_t offset)
    {
        if (!_buffer)
            return vk::DeviceOrHostAddressConstKHR();

        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);

        return vk::DeviceOrHostAddressConstKHR().setDeviceAddress(buffer->deviceAddress + size_t(offset));
    }

    static vk::DeviceOrHostAddressKHR GetMutableBufferAddress(IBuffer* _buffer, uint64_t offset)
    {
        if (!_buffer)
            return vk::DeviceOrHostAddressKHR();

        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(_buffer);

        return vk::DeviceOrHostAddressKHR().setDeviceAddress(buffer->deviceAddress + size_t(offset));
    }

#if HRHI_VULKAN_HAS_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES
    using ZWVKSpheresGeometryData = vk::AccelerationStructureGeometrySpheresDataNV;
    using ZWVKLinearSweptSpheresGeometryData = vk::AccelerationStructureGeometryLinearSweptSpheresDataNV;
#else
    struct ZWVKSpheresGeometryData
    {
    };

    struct ZWVKLinearSweptSpheresGeometryData
    {
    };
#endif

    constexpr const char* cVulkanSpheresBuildUnsupportedMessage =
        "Spheres and line-swept spheres are not supported by this Vulkan backend build.";
    constexpr const char* cVulkanSpheresRuntimeUnavailableMessage =
        "Spheres and line-swept spheres are unavailable because the required VK_NV_ray_tracing_linear_swept_spheres feature is not enabled on this Vulkan device.";
    constexpr const char* cVulkanClusterBuildUnsupportedMessage =
        "Ray tracing cluster operations are not supported by this Vulkan backend build.";
    constexpr const char* cVulkanClusterRuntimeUnavailableMessage =
        "Ray tracing cluster operations are unavailable because VK_NV_cluster_acceleration_structure is not enabled on this Vulkan device.";

    static vk::BuildMicromapFlagBitsEXT GetAsVkBuildMicromapFlagBitsEXT(Hrt::EOpacityMicromapBuildFlags flags)
    {
        assert((flags & (Hrt::EOpacityMicromapBuildFlags::FastBuild | Hrt::EOpacityMicromapBuildFlags::FastTrace | Hrt::EOpacityMicromapBuildFlags::AllowCompaction)) == flags);
        static_assert((uint32_t)vk::BuildMicromapFlagBitsEXT::ePreferFastTrace == (uint32_t)Hrt::EOpacityMicromapBuildFlags::FastTrace);
        static_assert((uint32_t)vk::BuildMicromapFlagBitsEXT::ePreferFastBuild == (uint32_t)Hrt::EOpacityMicromapBuildFlags::FastBuild);
        static_assert((uint32_t)vk::BuildMicromapFlagBitsEXT::eAllowCompaction == (uint32_t)Hrt::EOpacityMicromapBuildFlags::AllowCompaction);
        return (vk::BuildMicromapFlagBitsEXT)flags;
    }

    static const vk::MicromapUsageEXT* GetAsVkOpacityMicromapUsageCounts(const Hrt::ZWOpacityMicromapUsageCount* counts) 
    {
        static_assert(sizeof(Hrt::ZWOpacityMicromapUsageCount) == sizeof(vk::MicromapUsageEXT));
        static_assert(offsetof(Hrt::ZWOpacityMicromapUsageCount, count) == offsetof(vk::MicromapUsageEXT, count));
        static_assert(sizeof(Hrt::ZWOpacityMicromapUsageCount::count) == sizeof(vk::MicromapUsageEXT::count));
        static_assert(offsetof(Hrt::ZWOpacityMicromapUsageCount, subdivisionLevel) == offsetof(vk::MicromapUsageEXT, subdivisionLevel));
        static_assert(sizeof(Hrt::ZWOpacityMicromapUsageCount::subdivisionLevel) == sizeof(vk::MicromapUsageEXT::subdivisionLevel));
        static_assert(offsetof(Hrt::ZWOpacityMicromapUsageCount, format) == offsetof(vk::MicromapUsageEXT, format));
        static_assert(sizeof(Hrt::ZWOpacityMicromapUsageCount::format) == sizeof(vk::MicromapUsageEXT::format));
        return (vk::MicromapUsageEXT*)counts;
    }

    static bool ConvertBottomLevelGeometry(
        const Hrt::ZWGeometryDesc& src,
        vk::AccelerationStructureGeometryKHR& dst,
        vk::AccelerationStructureTrianglesOpacityMicromapEXT& dstOmm,
        ZWVKSpheresGeometryData& dstSpheres,
        ZWVKLinearSweptSpheresGeometryData& dstLss,
        uint32_t& maxPrimitiveCount,
        vk::AccelerationStructureBuildRangeInfoKHR* pRange,
        const ZWVKContext& context,
        ZWVKUploadManager* uploadManager,
        uint64_t currentVersion)
    {
        auto convertIndexFormatToType = [&context](const EFormat indexFormat, const bool supportUint8) {
            switch (indexFormat)  // NOLINT(clang-diagnostic-switch-enum)
            {
            case EFormat::R8_UINT:
                if (supportUint8)
                {
                    return vk::IndexType::eUint8EXT;
                }
                else
                {
                    context.Error("UINT8 index type is not supported by the current ray tracing geometry configuration");
                    return vk::IndexType::eNoneKHR;
                }
            case EFormat::R16_UINT:
                return vk::IndexType::eUint16;
            case EFormat::R32_UINT:
                return vk::IndexType::eUint32;
            case EFormat::UNKNOWN:
                return vk::IndexType::eNoneKHR;
            default:
                context.Error("Unsupported ray tracing geometry index type");
                return vk::IndexType::eNoneKHR;
            }
        };

        switch (src.geometryType)
        {
        case Hrt::GeometryType::Triangles: {
            const Hrt::ZWGeometryTriangles& srct = src.geometryData.triangles;
            vk::AccelerationStructureGeometryTrianglesDataKHR dstt;

            dstt.setIndexType(convertIndexFormatToType(srct.indexFormat, true));
            dstt.setIndexData(GetBufferAddress(srct.indexBuffer, srct.indexOffset));

            dstt.setVertexFormat(vk::Format(ConvertFormat(srct.vertexFormat)));
            dstt.setVertexData(GetBufferAddress(srct.vertexBuffer, srct.vertexOffset));
            dstt.setVertexStride(srct.vertexStride);
            dstt.setMaxVertex(std::max(srct.vertexCount, 1u) - 1u);

            if (src.useTransform)
            {
                // The alignment of the transforms is supposed to be 16 bytes, as reported by the validation layer,
                // but there doesn't seem to be the appropriate constant or device property.
                constexpr size_t TransformAlignment = 16;

                if (uploadManager)
                {
                    // Suballocate a small piece of the upload buffer to copy the transform to the GPU.
                    ZWVKBuffer* uploadBuffer = nullptr;
                    uint64_t uploadOffset = 0;
                    void* uploadCpuVA = nullptr;

                    if (uploadManager->SuballocateBuffer(sizeof(vk::TransformMatrixKHR), &uploadBuffer, &uploadOffset,
                        &uploadCpuVA, currentVersion, uint32_t(TransformAlignment)))
                    {
                        static_assert(sizeof(vk::TransformMatrixKHR) == sizeof(Hrt::AffineTransform),
                            "The sizes of different transform types must match");
                        memcpy(uploadCpuVA, &src.transform, sizeof(vk::TransformMatrixKHR));
                        dstt.setTransformData(GetBufferAddress(uploadBuffer, uploadOffset));
                    }
                    else
                    {
                        context.Error("Couldn't suballocate an upload buffer for geometry transform.");
                        return false;
                    }
                }
                else
                {
                    // For build size queries, set a non-null dummy address for the transform.
                    // https://registry.khronos.org/vulkan/specs/latest/man/html/vkGetAccelerationStructureBuildSizesKHR.html
                    //
                    // >> The srcAccelerationStructure, dstAccelerationStructure, and mode members of pBuildInfo are
                    //    ignored. Any VkDeviceOrHostAddressKHR or VkDeviceOrHostAddressConstKHR members of pBuildInfo
                    //    are ignored by this command, except that the hostAddress member of
                    //    VkAccelerationStructureGeometryTrianglesDataKHR::transformData will be examined to check
                    //    if it is NULL.
                    dstt.setTransformData(vk::DeviceOrHostAddressConstKHR().setHostAddress((void*)TransformAlignment));
                }
            }

            if (srct.opacityMicromap)
            {
                ZWVKOpacityMicromap* om = static_cast<ZWVKOpacityMicromap*>(srct.opacityMicromap);

                dstOmm
                    .setIndexType(srct.ommIndexFormat == EFormat::R16_UINT ? vk::IndexType::eUint16 : vk::IndexType::eUint32)
                    .setIndexBuffer(GetMutableBufferAddress(srct.ommIndexBuffer, srct.ommIndexBufferOffset).deviceAddress)
                    .setIndexStride(srct.ommIndexFormat == EFormat::R16_UINT ? 2 : 4)
                    .setBaseTriangle(0)
                    .setPUsageCounts(GetAsVkOpacityMicromapUsageCounts(srct.pOmmUsageCounts))
                    .setUsageCountsCount(srct.numOmmUsageCounts)
                    .setMicromap(om->opacityMicromap.get());

                dstt.setPNext(&dstOmm);
            }

            maxPrimitiveCount = (srct.indexFormat == EFormat::UNKNOWN)
                ? (srct.vertexCount / 3)
                : (srct.indexCount / 3);

            dst.setGeometryType(vk::GeometryTypeKHR::eTriangles);
            dst.geometry.setTriangles(dstt);

            break;
        }
        case Hrt::GeometryType::AABBs: {
            const Hrt::ZWGeometryAABBs& srca = src.geometryData.aabbs;
            vk::AccelerationStructureGeometryAabbsDataKHR dsta;

            dsta.setData(GetBufferAddress(srca.buffer, srca.offset));
            dsta.setStride(srca.stride);

            maxPrimitiveCount = srca.count;

            dst.setGeometryType(vk::GeometryTypeKHR::eAabbs);
            dst.geometry.setAabbs(dsta);

            break;
        }
        case Hrt::GeometryType::Spheres: {
#if HRHI_VULKAN_HAS_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES
            if (!context.extensions.NV_ray_tracing_linear_swept_spheres || !context.linearSweptSpheresFeatures.spheres)
            {
                context.Error(cVulkanSpheresRuntimeUnavailableMessage);
                return false;
            }

            const Hrt::ZWGeometrySpheres& srcSpheres = src.geometryData.spheres;

            dstSpheres.setIndexType(convertIndexFormatToType(srcSpheres.indexFormat, false));
            dstSpheres.setIndexData(GetBufferAddress(srcSpheres.indexBuffer, srcSpheres.indexOffset));
            dstSpheres.setIndexStride(srcSpheres.indexStride);
            dstSpheres.setVertexFormat(vk::Format(ConvertFormat(srcSpheres.vertexPositionFormat)));
            dstSpheres.setVertexData(GetBufferAddress(srcSpheres.vertexBuffer, srcSpheres.vertexPositionOffset));
            dstSpheres.setVertexStride(srcSpheres.vertexPositionStride);
            dstSpheres.setRadiusFormat(vk::Format(ConvertFormat(srcSpheres.vertexRadiusFormat)));
            dstSpheres.setRadiusData(GetBufferAddress(srcSpheres.vertexBuffer, srcSpheres.vertexRadiusOffset));
            dstSpheres.setRadiusStride(srcSpheres.vertexRadiusStride);

            maxPrimitiveCount = srcSpheres.indexBuffer != nullptr ? srcSpheres.indexCount : srcSpheres.vertexCount;

            dst.setGeometryType(vk::GeometryTypeNV::eSpheresNV);
            dst.setPNext(&dstSpheres);
            break;
#else
            context.Error(cVulkanSpheresBuildUnsupportedMessage);
            return false;
#endif
        }
        case Hrt::GeometryType::Lss: {
#if HRHI_VULKAN_HAS_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES
            if (!context.extensions.NV_ray_tracing_linear_swept_spheres || !context.linearSweptSpheresFeatures.linearSweptSpheres)
            {
                context.Error(cVulkanSpheresRuntimeUnavailableMessage);
                return false;
            }

            const Hrt::ZWGeometryLss& srcLss = src.geometryData.lss;

            if (srcLss.indexBuffer)
            {
                dstLss.setIndexType(convertIndexFormatToType(srcLss.indexFormat, false));
                dstLss.setIndexData(GetBufferAddress(srcLss.indexBuffer, srcLss.indexOffset));
                dstLss.setIndexStride(srcLss.indexStride);

                switch (srcLss.primitiveFormat)
                {
                case Hrt::EGeometryLssPrimitiveFormat::List:
                    dstLss.setIndexingMode(vk::RayTracingLssIndexingModeNV::eList);
                    break;
                case Hrt::EGeometryLssPrimitiveFormat::SuccessiveImplicit:
                    dstLss.setIndexingMode(vk::RayTracingLssIndexingModeNV::eSuccessive);
                    break;
                default:
                    context.Error("Unsupported LSS primitive format type");
                    return false;
                }
            }
            else
            {
                // https://docs.vulkan.org/refpages/latest/refpages/source/VkAccelerationStructureGeometryLinearSweptSpheresDataNV.html#VUID-VkAccelerationStructureGeometryLinearSweptSpheresDataNV-indexingMode-10427
                if (srcLss.primitiveFormat != Hrt::EGeometryLssPrimitiveFormat::List)
                {
                    context.Error("Unsupported LSS primitive format type. If indexingMode is VK_RAY_TRACING_LSS_INDEXING_MODE_SUCCESSIVE_NV, indexData must NOT be NULL");
                    return false;
                }

                dstLss.setIndexType(vk::IndexType::eNoneKHR);
                dstLss.setIndexStride(0);
                dstLss.setIndexingMode(vk::RayTracingLssIndexingModeNV::eList);
            }

            dstLss.setVertexFormat(vk::Format(ConvertFormat(srcLss.vertexPositionFormat)));
            dstLss.setVertexData(GetBufferAddress(srcLss.vertexBuffer, srcLss.vertexPositionOffset));
            dstLss.setVertexStride(srcLss.vertexPositionStride);

            dstLss.setRadiusFormat(vk::Format(ConvertFormat(srcLss.vertexRadiusFormat)));
            dstLss.setRadiusData(GetBufferAddress(srcLss.vertexBuffer, srcLss.vertexRadiusOffset));
            dstLss.setRadiusStride(srcLss.vertexRadiusStride);

            vk::RayTracingLssPrimitiveEndCapsModeNV endcapMode = vk::RayTracingLssPrimitiveEndCapsModeNV::eNone;
            switch (srcLss.endcapMode)
            {
            case Hrt::EGeometryLssEndcapMode::None:
                endcapMode = vk::RayTracingLssPrimitiveEndCapsModeNV::eNone;
                break;
            case Hrt::EGeometryLssEndcapMode::Chained:
                endcapMode = vk::RayTracingLssPrimitiveEndCapsModeNV::eChained;
                break;
            default:
                context.Error("Unsupported LSS end cap mode type");
                return false;
            }
            dstLss.setEndCapsMode(endcapMode);

            maxPrimitiveCount = srcLss.primitiveCount;

            dst.setGeometryType(vk::GeometryTypeNV::eLinearSweptSpheresNV);
            dst.setPNext(&dstLss);

            break;
#else
            context.Error(cVulkanSpheresBuildUnsupportedMessage);
            return false;
#endif
        }
        default:
            context.Error("Encountered an unknown ray tracing geometry type.");
            return false;
        }

        if (pRange)
        {
            pRange->setPrimitiveCount(maxPrimitiveCount);
        }

        vk::GeometryFlagsKHR geometryFlags = vk::GeometryFlagBitsKHR(0);
        if ((src.flags & Hrt::EGeometryFlags::Opaque) != 0)
            geometryFlags |= vk::GeometryFlagBitsKHR::eOpaque;
        if ((src.flags & Hrt::EGeometryFlags::NoDuplicateAnyHitInvocation) != 0)
            geometryFlags |= vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation;
        dst.setFlags(geometryFlags);
        return true;
    }

    Hrt::ZWOpacityMicromapHandle ZWVKDevice::CreateOpacityMicromap(const Hrt::ZWOpacityMicromapDesc& desc)
    {
        auto buildSize = vk::MicromapBuildSizesInfoEXT();

        auto buildInfo = vk::MicromapBuildInfoEXT()
            .setType(vk::MicromapTypeEXT::eOpacityMicromap)
            .setFlags(GetAsVkBuildMicromapFlagBitsEXT(desc.flags))
            .setMode(vk::BuildMicromapModeEXT::eBuild)
            .setPUsageCounts(GetAsVkOpacityMicromapUsageCounts(desc.counts.data()))
            .setUsageCountsCount((uint32_t)desc.counts.size())
            ;

        mContext.device.getMicromapBuildSizesEXT(vk::AccelerationStructureBuildTypeKHR::eDevice, &buildInfo, &buildSize);

        ZWVKOpacityMicromap* om = new ZWVKOpacityMicromap();
        om->desc = desc;
        om->compacted = false;
        
        ZWBufferDesc bufferDesc;
        bufferDesc.canHaveUAVs = true;
        bufferDesc.byteSize = buildSize.micromapSize;
        bufferDesc.initialState = EResourceStates::AccelStructBuildBlas;
        bufferDesc.keepInitialState = true;
        bufferDesc.isAccelStructStorage = true;
        bufferDesc.debugName = desc.debugName;
        bufferDesc.isVirtual = false;
        om->dataBuffer = CreateBuffer(bufferDesc);

        ZWVKBuffer* buffer = static_cast<ZWVKBuffer*>(om->dataBuffer.Get());

        auto create = vk::MicromapCreateInfoEXT()
            .setType(vk::MicromapTypeEXT::eOpacityMicromap)
            .setBuffer(buffer->buffer)
            .setSize(buildSize.micromapSize)
            .setDeviceAddress(GetMutableBufferAddress(buffer, 0).deviceAddress);

        om->opacityMicromap = mContext.device.createMicromapEXTUnique(create, mContext.allocationCallbacks);
        return Hrt::ZWOpacityMicromapHandle::Create(om);
    }

    Hrt::ZWAccelStructHandle ZWVKDevice::CreateAccelStruct(const Hrt::ZWAccelStructDesc& desc)
    {
        ZWVKAccelStruct* as = new ZWVKAccelStruct(mContext);
        as->desc = desc;
        as->allowUpdate = (desc.buildFlags & Hrt::EAccelStructBuildFlags::AllowUpdate) != 0;

#if HRHI_WITH_RTXMU
        bool isManaged = desc.isTopLevel;
#else
        bool isManaged = true;
#endif

        if (isManaged)
        {
            std::vector<vk::AccelerationStructureGeometryKHR> geometries;
            std::vector<vk::AccelerationStructureTrianglesOpacityMicromapEXT> omms;
            std::vector<ZWVKSpheresGeometryData> spheres;
            std::vector<ZWVKLinearSweptSpheresGeometryData> lss;
            std::vector<uint32_t> maxPrimitiveCounts;

            auto buildInfo = vk::AccelerationStructureBuildGeometryInfoKHR();

            if (desc.isTopLevel)
            {
                geometries.push_back(vk::AccelerationStructureGeometryKHR()
                    .setGeometryType(vk::GeometryTypeKHR::eInstances));

                geometries[0].geometry.setInstances(vk::AccelerationStructureGeometryInstancesDataKHR());

                maxPrimitiveCounts.push_back(uint32_t(desc.topLevelMaxInstances));

                buildInfo.setType(vk::AccelerationStructureTypeKHR::eTopLevel);
            }
            else
            {
                geometries.resize(desc.bottomLevelGeometries.size());
                omms.resize(desc.bottomLevelGeometries.size());
                spheres.resize(desc.bottomLevelGeometries.size());
                lss.resize(desc.bottomLevelGeometries.size());
                maxPrimitiveCounts.resize(desc.bottomLevelGeometries.size());

                for (size_t i = 0; i < desc.bottomLevelGeometries.size(); i++)
                {
                    if (!ConvertBottomLevelGeometry(
                        desc.bottomLevelGeometries[i],
                        geometries[i],
                        omms[i],
                        spheres[i],
                        lss[i],
                        maxPrimitiveCounts[i],
                        nullptr,
                        mContext,
                        nullptr,
                        0))
                    {
                        delete as;
                        return nullptr;
                    }
                }

                buildInfo.setType(vk::AccelerationStructureTypeKHR::eBottomLevel);
            }

            buildInfo.setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
                .setGeometries(geometries)
                .setFlags(ConvertAccelStructBuildFlags(desc.buildFlags));

            auto buildSizes = mContext.device.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, maxPrimitiveCounts);

            ZWBufferDesc bufferDesc;
            bufferDesc.byteSize = buildSizes.accelerationStructureSize;
            bufferDesc.debugName = desc.debugName;
            bufferDesc.initialState = desc.isTopLevel ? EResourceStates::AccelStructRead : EResourceStates::AccelStructBuildBlas;
            bufferDesc.keepInitialState = true;
            bufferDesc.isAccelStructStorage = true;
            bufferDesc.isVirtual = desc.isVirtual;
            as->dataBuffer = CreateBuffer(bufferDesc);

            ZWVKBuffer* dataBuffer = static_cast<ZWVKBuffer*>(as->dataBuffer.Get());

            auto createInfo = vk::AccelerationStructureCreateInfoKHR()
                .setType(desc.isTopLevel ? vk::AccelerationStructureTypeKHR::eTopLevel : vk::AccelerationStructureTypeKHR::eBottomLevel)
                .setBuffer(dataBuffer->buffer)
                .setSize(buildSizes.accelerationStructureSize);

            as->accelStruct = mContext.device.createAccelerationStructureKHR(createInfo, mContext.allocationCallbacks);

            if (!desc.isVirtual)
            {
                auto addressInfo = vk::AccelerationStructureDeviceAddressInfoKHR()
                    .setAccelerationStructure(as->accelStruct);

                as->accelStructDeviceAddress = mContext.device.getAccelerationStructureAddressKHR(addressInfo);
            }
        }

        // Sanitize the geometry data to avoid dangling pointers, we don't need these buffers in the Desc
        for (auto& geometry : as->desc.bottomLevelGeometries)
        {
            static_assert(offsetof(Hrt::ZWGeometryTriangles, indexBuffer)
                == offsetof(Hrt::ZWGeometryAABBs, buffer));
            static_assert(offsetof(Hrt::ZWGeometryTriangles, vertexBuffer)
                == offsetof(Hrt::ZWGeometryAABBs, unused));

            static_assert(offsetof(Hrt::ZWGeometryTriangles, indexBuffer)
                == offsetof(Hrt::ZWGeometrySpheres, indexBuffer));
            static_assert(offsetof(Hrt::ZWGeometryTriangles, vertexBuffer)
                == offsetof(Hrt::ZWGeometrySpheres, vertexBuffer));

            static_assert(offsetof(Hrt::ZWGeometryTriangles, indexBuffer)
                == offsetof(Hrt::ZWGeometryLss, indexBuffer));
            static_assert(offsetof(Hrt::ZWGeometryTriangles, vertexBuffer)
                == offsetof(Hrt::ZWGeometryLss, vertexBuffer));

            // Clear only the triangles' data, because the other types' data is aliased to triangles (verified above)
            geometry.geometryData.triangles.indexBuffer = nullptr;
            geometry.geometryData.triangles.vertexBuffer = nullptr;
        }

        return Hrt::ZWAccelStructHandle::Create(as);
    }

    ZWMemoryRequirements ZWVKDevice::GetAccelStructMemoryRequirements(Hrt::IAccelStruct* _as)
    {
        ZWVKAccelStruct* as = static_cast<ZWVKAccelStruct*>(_as);

        if (as->dataBuffer)
            return GetBufferMemoryRequirements(as->dataBuffer);

        return ZWMemoryRequirements();
    }

#if HRHI_VULKAN_HAS_NV_CLUSTER_ACCELERATION_STRUCTURE
    static vk::ClusterAccelerationStructureTypeNV ConvertClusterAccelerationStructureType(Hrt::HCluster::EOperationMoveType type)
    {
        switch (type)
        {
            case Hrt::HCluster::EOperationMoveType::BottomLevel: return vk::ClusterAccelerationStructureTypeNV::eClustersBottomLevel;
            case Hrt::HCluster::EOperationMoveType::ClusterLevel: return vk::ClusterAccelerationStructureTypeNV::eTriangleCluster;
            case Hrt::HCluster::EOperationMoveType::Template: return vk::ClusterAccelerationStructureTypeNV::eTriangleClusterTemplate;
            default:
                assert(false);
                return vk::ClusterAccelerationStructureTypeNV::eClustersBottomLevel;
        }
    }

    static vk::ClusterAccelerationStructureOpTypeNV ConvertClusterOperationType(Hrt::HCluster::EOperationType type, const ZWVKContext& context)
    {
        switch (type)
        {
            case Hrt::HCluster::EOperationType::Move:
                return vk::ClusterAccelerationStructureOpTypeNV::eMoveObjects;
            case Hrt::HCluster::EOperationType::ClasBuild:
                return vk::ClusterAccelerationStructureOpTypeNV::eBuildTriangleCluster;
            case Hrt::HCluster::EOperationType::ClasBuildTemplates:
                return vk::ClusterAccelerationStructureOpTypeNV::eBuildTriangleClusterTemplate;
            case Hrt::HCluster::EOperationType::ClasInstantiateTemplates:
                return vk::ClusterAccelerationStructureOpTypeNV::eInstantiateTriangleCluster;
            case Hrt::HCluster::EOperationType::BlasBuild:
                return vk::ClusterAccelerationStructureOpTypeNV::eBuildClustersBottomLevel;
            default:
                context.Error("Invalid cluster operation type");
                return vk::ClusterAccelerationStructureOpTypeNV::eMoveObjects;
        }
    }

    static vk::ClusterAccelerationStructureOpModeNV ConvertClusterOperationMode(Hrt::HCluster::EOperationMode mode, const ZWVKContext& context)
    {
        switch (mode)
        {
            case Hrt::HCluster::EOperationMode::ImplicitDestinations:
                return vk::ClusterAccelerationStructureOpModeNV::eImplicitDestinations;
            case Hrt::HCluster::EOperationMode::ExplicitDestinations:
                return vk::ClusterAccelerationStructureOpModeNV::eExplicitDestinations;
            case Hrt::HCluster::EOperationMode::GetSizes:
                return vk::ClusterAccelerationStructureOpModeNV::eComputeSizes;
            default:
                context.Error("Invalid cluster operation mode");
                return vk::ClusterAccelerationStructureOpModeNV::eImplicitDestinations;
        }
    }

    static vk::BuildAccelerationStructureFlagsKHR ConvertClusterOperationFlags(Hrt::HCluster::EOperationFlags flags)
    {
        vk::BuildAccelerationStructureFlagsKHR operationFlags = {};

        bool fastTrace = (flags & Hrt::HCluster::EOperationFlags::FastTrace) != 0;
        bool fastBuild = (flags & Hrt::HCluster::EOperationFlags::FastBuild) != 0;
        
        if (fastTrace)
            operationFlags |= vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
        if (!fastTrace && fastBuild)
            operationFlags |= vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild;
        if ((flags & Hrt::HCluster::EOperationFlags::AllowOMM) != 0)
            operationFlags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowOpacityMicromapUpdateEXT;

        // (flags & Hrt::HCluster::EOperationFlags::NoOverlap)
        // is used to populate noMoveOverlap on vk::ClusterAccelerationStructureMoveObjectsInputNV
        
        return operationFlags;
    }

    static void PopulateClusterOperationInputInfo(
        const Hrt::HCluster::ZWOperationParams& params, 
        const ZWVKContext& context,
        vk::ClusterAccelerationStructureInputInfoNV& inputInfo,
        vk::ClusterAccelerationStructureMoveObjectsInputNV& moveInput,
        vk::ClusterAccelerationStructureTriangleClusterInputNV& clusterInput,
        vk::ClusterAccelerationStructureClustersBottomLevelInputNV& blasInput)
    {
        inputInfo.maxAccelerationStructureCount = params.maxArgCount;
        inputInfo.flags = ConvertClusterOperationFlags(params.flags);
        inputInfo.opType = ConvertClusterOperationType(params.type, context);
        inputInfo.opMode = ConvertClusterOperationMode(params.mode, context);

        // Set operation-specific parameters
        switch (params.type)
        {
            case Hrt::HCluster::EOperationType::Move:
            {
                moveInput.type = ConvertClusterAccelerationStructureType(params.move.type);
                moveInput.noMoveOverlap = (params.flags & Hrt::HCluster::EOperationFlags::NoOverlap) != 0;
                moveInput.maxMovedBytes = params.move.maxBytes;
                inputInfo.opInput.pMoveObjects = &moveInput;
                break;
            }

            case Hrt::HCluster::EOperationType::ClasBuild:
            case Hrt::HCluster::EOperationType::ClasBuildTemplates:
            case Hrt::HCluster::EOperationType::ClasInstantiateTemplates:
            {
                clusterInput.vertexFormat = vk::Format(ConvertFormat(params.clas.vertexFormat));
                clusterInput.maxGeometryIndexValue = params.clas.maxGeometryIndex;
                clusterInput.maxClusterUniqueGeometryCount = params.clas.maxUniqueGeometryCount;
                clusterInput.maxClusterTriangleCount = params.clas.maxTriangleCount;
                clusterInput.maxClusterVertexCount = params.clas.maxVertexCount;
                clusterInput.maxTotalTriangleCount = params.clas.maxTotalTriangleCount;
                clusterInput.maxTotalVertexCount = params.clas.maxTotalVertexCount;
                clusterInput.minPositionTruncateBitCount = params.clas.minPositionTruncateBitCount;
                inputInfo.opInput.pTriangleClusters = &clusterInput;
                break;
            }

            case Hrt::HCluster::EOperationType::BlasBuild:
            {
                blasInput.maxClusterCountPerAccelerationStructure = params.blas.maxClasPerBlasCount;
                blasInput.maxTotalClusterCount = params.blas.maxTotalClasCount;
                inputInfo.opInput.pClustersBottomLevel = &blasInput;
                break;
            }

            default:
                break;
        }
    }

    Hrt::HCluster::ZWOperationSizeInfo ZWVKDevice::GetClusterOperationSizeInfo(const Hrt::HCluster::ZWOperationParams& params)
    {
        if (!mContext.extensions.NV_cluster_acceleration_structure)
        {
            mContext.Error(cVulkanClusterRuntimeUnavailableMessage);
            return {};
        }

        Hrt::HCluster::ZWOperationSizeInfo info;

        // Create Vulkan operation parameters
        vk::ClusterAccelerationStructureInputInfoNV inputInfo = {};
        vk::ClusterAccelerationStructureMoveObjectsInputNV moveInput = {};
        vk::ClusterAccelerationStructureTriangleClusterInputNV clusterInput = {};
        vk::ClusterAccelerationStructureClustersBottomLevelInputNV blasInput = {};

        // Populate input info using helper function
        PopulateClusterOperationInputInfo(params, mContext, inputInfo, moveInput, clusterInput, blasInput);

        // Get size info from Vulkan
        auto vkSizeInfo = mContext.device.getClusterAccelerationStructureBuildSizesNV(inputInfo);

        // Convert Vulkan size info to the HRHI size structure
        info.resultMaxSizeInBytes = vkSizeInfo.accelerationStructureSize;
        info.scratchSizeInBytes = vkSizeInfo.buildScratchSize;

        return info;
    }

    bool ZWVKDevice::BindAccelStructMemory(Hrt::IAccelStruct* _as, IHeap* heap, uint64_t offset)
    {
        ZWVKAccelStruct* as = static_cast<ZWVKAccelStruct*>(_as);

        if (!as->dataBuffer)
            return false;

        const bool bound = BindBufferMemory(as->dataBuffer, heap, offset);

        if (bound)
        {
            auto addressInfo = vk::AccelerationStructureDeviceAddressInfoKHR()
                .setAccelerationStructure(as->accelStruct);

            as->accelStructDeviceAddress = mContext.device.getAccelerationStructureAddressKHR(addressInfo);
        }

        return bound;
    }

    void ZWVKCommandList::BuildOpacityMicromap(Hrt::IOpacityMicromap* pOpacityMicromap, const Hrt::ZWOpacityMicromapDesc& desc)
    {
        ZWVKOpacityMicromap* omm = static_cast<ZWVKOpacityMicromap*>(pOpacityMicromap);

        if (m_EnableAutomaticBarriers)
        {
            RequireBufferState(desc.inputBuffer, EResourceStates::OpacityMicromapBuildInput);
            RequireBufferState(desc.perOmmDescs, EResourceStates::OpacityMicromapBuildInput);

            RequireBufferState(omm->dataBuffer, EResourceStates::OpacityMicromapWrite);
            mBindingStatesDirty = true;
        }

        if (desc.trackLiveness)
        {
            mCurrentCmdBuf->referencedResources.push_back(desc.inputBuffer);
            mCurrentCmdBuf->referencedResources.push_back(desc.perOmmDescs);
            mCurrentCmdBuf->referencedResources.push_back(omm->dataBuffer);
        }

        CommitBarriers();

        auto buildInfo = vk::MicromapBuildInfoEXT()
            .setType(vk::MicromapTypeEXT::eOpacityMicromap)
            .setFlags(GetAsVkBuildMicromapFlagBitsEXT(desc.flags))
            .setMode(vk::BuildMicromapModeEXT::eBuild)
            .setDstMicromap(omm->opacityMicromap.get())
            .setPUsageCounts(GetAsVkOpacityMicromapUsageCounts(desc.counts.data()))
            .setUsageCountsCount((uint32_t)desc.counts.size())
            .setData(GetBufferAddress(desc.inputBuffer, desc.inputBufferOffset))
            .setTriangleArray(GetBufferAddress(desc.perOmmDescs, desc.perOmmDescsOffset))
            .setTriangleArrayStride((VkDeviceSize)sizeof(vk::MicromapTriangleEXT))
            ;

        vk::MicromapBuildSizesInfoEXT buildSize;
        mContext.device.getMicromapBuildSizesEXT(vk::AccelerationStructureBuildTypeKHR::eDevice, &buildInfo, &buildSize);

        if (buildSize.buildScratchSize != 0)
        {
            ZWVKBuffer* scratchBuffer = nullptr;
            uint64_t scratchOffset = 0;
            uint64_t currentVersion = MakeVersion(mCurrentCmdBuf->recordingID, mCommandListParameters.queueType, false);

            bool allocated = m_ScratchManager->SuballocateBuffer(buildSize.buildScratchSize, &scratchBuffer, &scratchOffset, nullptr,
                currentVersion, mContext.accelStructProperties.minAccelerationStructureScratchOffsetAlignment);

            if (!allocated)
            {
                std::stringstream ss;
                ss << "Couldn't suballocate a scratch buffer for OMM " << HApp::DebugNameToString(omm->desc.debugName) << " build. "
                    "The build requires " << buildSize.buildScratchSize << " bytes of scratch space.";

                mContext.Error(ss.str());
                return;
            }

            buildInfo.setScratchData(GetMutableBufferAddress(scratchBuffer, scratchOffset));
        }

        mCurrentCmdBuf->cmdBuf.buildMicromapsEXT(1, &buildInfo);
    }

    void ZWVKCommandList::BuildBottomLevelAccelStruct(Hrt::IAccelStruct* _as, const Hrt::ZWGeometryDesc* pGeometries, size_t numGeometries, Hrt::EAccelStructBuildFlags buildFlags)
    {
        ZWVKAccelStruct* as = static_cast<ZWVKAccelStruct*>(_as);

        const bool performUpdate = (buildFlags & Hrt::EAccelStructBuildFlags::PerformUpdate) != 0;
        if (performUpdate)
        {
            assert(as->allowUpdate);
        }

        std::vector<vk::AccelerationStructureGeometryKHR> geometries;
        std::vector<vk::AccelerationStructureTrianglesOpacityMicromapEXT> omms;
        std::vector<ZWVKSpheresGeometryData> spheres;
        std::vector<ZWVKLinearSweptSpheresGeometryData> lss;
        std::vector<vk::AccelerationStructureBuildRangeInfoKHR> buildRanges;
        std::vector<uint32_t> maxPrimitiveCounts;
        geometries.resize(numGeometries);
        omms.resize(numGeometries);
        spheres.resize(numGeometries);
        lss.resize(numGeometries);
        maxPrimitiveCounts.resize(numGeometries);
        buildRanges.resize(numGeometries);

        uint64_t currentVersion = MakeVersion(mCurrentCmdBuf->recordingID, mCommandListParameters.queueType, false);

        for (size_t i = 0; i < numGeometries; i++)
        {
            if (!ConvertBottomLevelGeometry(
                pGeometries[i],
                geometries[i],
                omms[i],
                spheres[i],
                lss[i],
                maxPrimitiveCounts[i],
                &buildRanges[i],
                mContext,
                m_UploadManager.get(),
                currentVersion))
            {
                return;
            }

            const Hrt::ZWGeometryDesc& src = pGeometries[i];

            switch (src.geometryType)
            {
            case Hrt::GeometryType::Triangles: {
                const Hrt::ZWGeometryTriangles& srct = src.geometryData.triangles;
                if (m_EnableAutomaticBarriers)
                {
                    if (srct.indexBuffer)
                        RequireBufferState(srct.indexBuffer, EResourceStates::AccelStructBuildInput);
                    if (srct.vertexBuffer)
                        RequireBufferState(srct.vertexBuffer, EResourceStates::AccelStructBuildInput);
                    if (ZWVKOpacityMicromap* om = static_cast<ZWVKOpacityMicromap*>(srct.opacityMicromap))
                        RequireBufferState(om->dataBuffer, EResourceStates::AccelStructBuildInput);
                }
                break;
            }
            case Hrt::GeometryType::AABBs: {
                const Hrt::ZWGeometryAABBs& srca = src.geometryData.aabbs;
                if (m_EnableAutomaticBarriers)
                {
                    if (srca.buffer)
                        RequireBufferState(srca.buffer, EResourceStates::AccelStructBuildInput);
                }
                break;
            }
            case Hrt::GeometryType::Lss: {
#if HRHI_VULKAN_HAS_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES
                const Hrt::ZWGeometryLss& srcLss = src.geometryData.lss;
                if (m_EnableAutomaticBarriers)
                {
                    if (srcLss.indexBuffer)
                        RequireBufferState(srcLss.indexBuffer, EResourceStates::AccelStructBuildInput);
                    if (srcLss.vertexBuffer)
                        RequireBufferState(srcLss.vertexBuffer, EResourceStates::AccelStructBuildInput);
                }
                break;
#else
                mContext.Error(cVulkanSpheresBuildUnsupportedMessage);
                return;
#endif
            }
            case Hrt::GeometryType::Spheres: {
#if HRHI_VULKAN_HAS_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES
                const Hrt::ZWGeometrySpheres& srcSpheres = src.geometryData.spheres;
                if (m_EnableAutomaticBarriers)
                {
                    if (srcSpheres.indexBuffer)
                        RequireBufferState(srcSpheres.indexBuffer, EResourceStates::AccelStructBuildInput);
                    if (srcSpheres.vertexBuffer)
                        RequireBufferState(srcSpheres.vertexBuffer, EResourceStates::AccelStructBuildInput);
                }
                break;
#else
                mContext.Error(cVulkanSpheresBuildUnsupportedMessage);
                return;
#endif
            }
            }
        }

        mBindingStatesDirty = true;

        auto buildInfo = vk::AccelerationStructureBuildGeometryInfoKHR()
            .setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
            .setMode(performUpdate ? vk::BuildAccelerationStructureModeKHR::eUpdate : vk::BuildAccelerationStructureModeKHR::eBuild)
            .setGeometries(geometries)
            .setFlags(ConvertAccelStructBuildFlags(buildFlags))
            .setDstAccelerationStructure(as->accelStruct);

        if (as->allowUpdate)
            buildInfo.flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;

        if (performUpdate)
            buildInfo.setSrcAccelerationStructure(as->accelStruct);
        
#if HRHI_WITH_RTXMU
        CommitBarriers();

        std::array<vk::AccelerationStructureBuildGeometryInfoKHR, 1> buildInfos = { buildInfo };
        std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> buildRangeArrays = { buildRanges.data() };
        std::array<const uint32_t*, 1> maxPrimArrays = { maxPrimitiveCounts.data() };

        if(as->rtxmuId == ~0ull)
        {
            std::vector<uint64_t> accelStructsToBuild;
            mContext.rtxMemUtil->PopulateBuildCommandList(mCurrentCmdBuf->cmdBuf,
                                                           buildInfos.data(),
                                                           buildRangeArrays.data(),
                                                           maxPrimArrays.data(),
                                                           (uint32_t)buildInfos.size(),
                                                           accelStructsToBuild);


            as->rtxmuId = accelStructsToBuild[0];
            
            as->rtxmuBuffer = mContext.rtxMemUtil->GetBuffer(as->rtxmuId);
            as->accelStruct = mContext.rtxMemUtil->GetAccelerationStruct(as->rtxmuId);
            as->accelStructDeviceAddress = mContext.rtxMemUtil->GetDeviceAddress(as->rtxmuId);

            mCurrentCmdBuf->rtxmuBuildIds.push_back(as->rtxmuId);
        }
        else
        {
            std::vector<uint64_t> buildsToUpdate(1, as->rtxmuId);

            mContext.rtxMemUtil->PopulateUpdateCommandList(mCurrentCmdBuf->cmdBuf,
                                                            buildInfos.data(),
                                                            buildRangeArrays.data(),
                                                            maxPrimArrays.data(),
                                                            (uint32_t)buildInfos.size(),
                                                            buildsToUpdate);
        }
#else

        if (m_EnableAutomaticBarriers)
        {
            RequireBufferState(as->dataBuffer, EResourceStates::AccelStructWrite);
        }
        CommitBarriers();

        auto buildSizes = mContext.device.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, maxPrimitiveCounts);

        if (buildSizes.accelerationStructureSize > as->dataBuffer->GetDesc().byteSize)
        {
            std::stringstream ss;
            ss << "BLAS " << HApp::DebugNameToString(as->desc.debugName) << " build requires at least "
                << buildSizes.accelerationStructureSize << " bytes in the data buffer, while the allocated buffer is only "
                << as->dataBuffer->GetDesc().byteSize << " bytes";

            mContext.Error(ss.str());
            return;
        }

        size_t scratchSize = performUpdate
            ? buildSizes.updateScratchSize
            : buildSizes.buildScratchSize;

        ZWVKBuffer* scratchBuffer = nullptr;
        uint64_t scratchOffset = 0;

        bool allocated = m_ScratchManager->SuballocateBuffer(scratchSize, &scratchBuffer, &scratchOffset, nullptr,
            currentVersion, mContext.accelStructProperties.minAccelerationStructureScratchOffsetAlignment);

        if (!allocated)
        {
            std::stringstream ss;
            ss << "Couldn't suballocate a scratch buffer for BLAS " << HApp::DebugNameToString(as->desc.debugName) << " build. "
                "The build requires " << scratchSize << " bytes of scratch space.";

            mContext.Error(ss.str());
            return;
        }
        
        assert(scratchBuffer->deviceAddress);
        buildInfo.setScratchData(scratchBuffer->deviceAddress + scratchOffset);

        std::array<vk::AccelerationStructureBuildGeometryInfoKHR, 1> buildInfos = { buildInfo };
        std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> buildRangeArrays = { buildRanges.data() };

        mCurrentCmdBuf->cmdBuf.buildAccelerationStructuresKHR(buildInfos, buildRangeArrays);
#endif
        if (as->desc.trackLiveness)
            mCurrentCmdBuf->referencedResources.push_back(as);
    }

    void ZWVKCommandList::CompactBottomLevelAccelStructs()
    {
#if HRHI_WITH_RTXMU

        if (!mContext.rtxMuResources->asBuildsCompleted.empty())
        {
            std::lock_guard lockGuard(mContext.rtxMuResources->asListMutex);

            if (!mContext.rtxMuResources->asBuildsCompleted.empty())
            {
                mContext.rtxMemUtil->PopulateCompactionCommandList(mCurrentCmdBuf->cmdBuf, mContext.rtxMuResources->asBuildsCompleted);

                mCurrentCmdBuf->rtxmuCompactionIds.insert(mCurrentCmdBuf->rtxmuCompactionIds.end(), mContext.rtxMuResources->asBuildsCompleted.begin(), mContext.rtxMuResources->asBuildsCompleted.end());

                mContext.rtxMuResources->asBuildsCompleted.clear();
            }
        }
#endif
    }

    void ZWVKCommandList::BuildTopLevelAccelStructInternal(ZWVKAccelStruct* as, VkDeviceAddress instanceData, size_t numInstances, Hrt::EAccelStructBuildFlags buildFlags, uint64_t currentVersion)
    {
        // Remove the internal flag
        buildFlags = buildFlags & ~Hrt::EAccelStructBuildFlags::AllowEmptyInstances;

        const bool performUpdate = (buildFlags & Hrt::EAccelStructBuildFlags::PerformUpdate) != 0;
        if (performUpdate)
        {
            assert(as->allowUpdate);
            assert(as->instances.size() == numInstances);
        }

        auto geometry = vk::AccelerationStructureGeometryKHR()
            .setGeometryType(vk::GeometryTypeKHR::eInstances);

        geometry.geometry.setInstances(vk::AccelerationStructureGeometryInstancesDataKHR()
            .setData(instanceData)
            .setArrayOfPointers(false));

        std::array<vk::AccelerationStructureGeometryKHR, 1> geometries = { geometry };
        std::array<vk::AccelerationStructureBuildRangeInfoKHR, 1> buildRanges = {
            vk::AccelerationStructureBuildRangeInfoKHR().setPrimitiveCount(uint32_t(numInstances)) };
        std::array<uint32_t, 1> maxPrimitiveCounts = { uint32_t(numInstances) };

        auto buildInfo = vk::AccelerationStructureBuildGeometryInfoKHR()
            .setType(vk::AccelerationStructureTypeKHR::eTopLevel)
            .setMode(performUpdate ? vk::BuildAccelerationStructureModeKHR::eUpdate : vk::BuildAccelerationStructureModeKHR::eBuild)
            .setGeometries(geometries)
            .setFlags(ConvertAccelStructBuildFlags(buildFlags))
            .setDstAccelerationStructure(as->accelStruct);

        if (as->allowUpdate)
            buildInfo.flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;

        if (performUpdate)
            buildInfo.setSrcAccelerationStructure(as->accelStruct);

        auto buildSizes = mContext.device.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, maxPrimitiveCounts);

        if (buildSizes.accelerationStructureSize > as->dataBuffer->GetDesc().byteSize)
        {
            std::stringstream ss;
            ss << "TLAS " << HApp::DebugNameToString(as->desc.debugName) << " build requires at least "
                << buildSizes.accelerationStructureSize << " bytes in the data buffer, while the allocated buffer is only "
                << as->dataBuffer->GetDesc().byteSize << " bytes";

            mContext.Error(ss.str());
            return;
        }

        size_t scratchSize = performUpdate
            ? buildSizes.updateScratchSize
            : buildSizes.buildScratchSize;

        ZWVKBuffer* scratchBuffer = nullptr;
        uint64_t scratchOffset = 0;

        bool allocated = m_ScratchManager->SuballocateBuffer(scratchSize, &scratchBuffer, &scratchOffset, nullptr,
            currentVersion, mContext.accelStructProperties.minAccelerationStructureScratchOffsetAlignment);

        if (!allocated)
        {
            std::stringstream ss;
            ss << "Couldn't suballocate a scratch buffer for TLAS " << HApp::DebugNameToString(as->desc.debugName) << " build. "
                "The build requires " << scratchSize << " bytes of scratch space.";

            mContext.Error(ss.str());
            return;
        }
        
        assert(scratchBuffer->deviceAddress);
        buildInfo.setScratchData(scratchBuffer->deviceAddress + scratchOffset);

        std::array<vk::AccelerationStructureBuildGeometryInfoKHR, 1> buildInfos = { buildInfo };
        std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> buildRangeArrays = { buildRanges.data() };

        mCurrentCmdBuf->cmdBuf.buildAccelerationStructuresKHR(buildInfos, buildRangeArrays);
    }

    void ZWVKCommandList::BuildTopLevelAccelStruct(Hrt::IAccelStruct* _as, const Hrt::ZWInstanceDesc* pInstances, size_t numInstances, Hrt::EAccelStructBuildFlags buildFlags)
    {
        ZWVKAccelStruct* as = static_cast<ZWVKAccelStruct*>(_as);

        as->instances.resize(numInstances);

        for (size_t i = 0; i < numInstances; i++)
        {
            const Hrt::ZWInstanceDesc& src = pInstances[i];
            vk::AccelerationStructureInstanceKHR& dst = as->instances[i];

            if (src.bottomLevelAS)
            {
                ZWVKAccelStruct* blas = static_cast<ZWVKAccelStruct*>(src.bottomLevelAS);
#if HRHI_WITH_RTXMU
                blas->rtxmuBuffer = mContext.rtxMemUtil->GetBuffer(blas->rtxmuId);
                blas->accelStruct = mContext.rtxMemUtil->GetAccelerationStruct(blas->rtxmuId);
                blas->accelStructDeviceAddress = mContext.rtxMemUtil->GetDeviceAddress(blas->rtxmuId);
                dst.setAccelerationStructureReference(blas->accelStructDeviceAddress);
#else
                dst.setAccelerationStructureReference(blas->accelStructDeviceAddress);

                if (m_EnableAutomaticBarriers)
                {
                    RequireBufferState(blas->dataBuffer, EResourceStates::AccelStructBuildBlas);
                }
#endif
            }
            else // !src.bottomLevelAS
            {
                dst.setAccelerationStructureReference(0);
            }

            dst.setInstanceCustomIndex(src.instanceID);
            dst.setInstanceShaderBindingTableRecordOffset(src.instanceContributionToHitGroupIndex);
            dst.setFlags(ConvertInstanceFlags(src.flags));
            dst.setMask(src.instanceMask);
            memcpy(dst.transform.matrix.data(), src.transform, sizeof(float) * 12);
        }

#if HRHI_WITH_RTXMU
        mContext.rtxMemUtil->PopulateUAVBarriersCommandList(mCurrentCmdBuf->cmdBuf, mCurrentCmdBuf->rtxmuBuildIds);
#endif

        uint64_t currentVersion = MakeVersion(mCurrentCmdBuf->recordingID, mCommandListParameters.queueType, false);

        ZWVKBuffer* uploadBuffer = nullptr;
        uint64_t uploadOffset = 0;
        void* uploadCpuVA = nullptr;
        m_UploadManager->SuballocateBuffer(as->instances.size() * sizeof(vk::AccelerationStructureInstanceKHR),
            &uploadBuffer, &uploadOffset, &uploadCpuVA, currentVersion);

        // Copy the instance data to GPU-visible memory.
        // The vk::AccelerationStructureInstanceKHR struct should be directly copyable, but ReSharper/clang thinks it's not,
        // so the inspection is disabled with a comment below.
        memcpy(uploadCpuVA, as->instances.data(), // NOLINT(bugprone-undefined-memory-manipulation)
            as->instances.size() * sizeof(vk::AccelerationStructureInstanceKHR));

        if (m_EnableAutomaticBarriers)
        {
            RequireBufferState(as->dataBuffer, EResourceStates::AccelStructWrite);
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        BuildTopLevelAccelStructInternal(as, uploadBuffer->deviceAddress + uploadOffset, numInstances, buildFlags, currentVersion);

        if (as->desc.trackLiveness)
            mCurrentCmdBuf->referencedResources.push_back(as);
    }

    void ZWVKCommandList::BuildTopLevelAccelStructFromBuffer(Hrt::IAccelStruct* _as, IBuffer* _instanceBuffer, uint64_t instanceBufferOffset, size_t numInstances, Hrt::EAccelStructBuildFlags buildFlags)
    {
        ZWVKAccelStruct* as = static_cast<ZWVKAccelStruct*>(_as);
        ZWVKBuffer* instanceBuffer = static_cast<ZWVKBuffer*>(_instanceBuffer);

        as->instances.clear();

        if (m_EnableAutomaticBarriers)
        {
            RequireBufferState(as->dataBuffer, EResourceStates::AccelStructWrite);
            RequireBufferState(instanceBuffer, EResourceStates::AccelStructBuildInput);
            mBindingStatesDirty = true;
        }
        CommitBarriers();

        uint64_t currentVersion = MakeVersion(mCurrentCmdBuf->recordingID, mCommandListParameters.queueType, false);
        
        BuildTopLevelAccelStructInternal(as, instanceBuffer->deviceAddress + instanceBufferOffset, numInstances, buildFlags, currentVersion);

        if (as->desc.trackLiveness)
            mCurrentCmdBuf->referencedResources.push_back(as);
    }

    void ZWVKCommandList::ExecuteMultiIndirectClusterOperation(const Hrt::HCluster::ZWOperationDesc& desc)
    {
        if (!mContext.extensions.NV_cluster_acceleration_structure)
        {
            mContext.Error(cVulkanClusterRuntimeUnavailableMessage);
            return;
        }

        if (desc.params.maxArgCount == 0)
        {
            return;
        }

        if (desc.inIndirectArgsBuffer == nullptr)
        {
            mContext.Error("ExecuteMultiIndirectClusterOperation requires a valid indirect arguments buffer.");
            return;
        }

        if (desc.scratchSizeInBytes == 0)
        {
            mContext.Error("ExecuteMultiIndirectClusterOperation requires a non-zero scratch size.");
            return;
        }

        if (desc.params.mode == Hrt::HCluster::EOperationMode::ImplicitDestinations)
        {
            if (desc.inOutAddressesBuffer == nullptr || desc.outAccelerationStructuresBuffer == nullptr)
            {
                mContext.Error("ImplicitDestinations cluster operations require both the addresses buffer and the destination acceleration-structure buffer.");
                return;
            }
        }
        else if (desc.params.mode == Hrt::HCluster::EOperationMode::ExplicitDestinations)
        {
            if (desc.inOutAddressesBuffer == nullptr)
            {
                mContext.Error("ExplicitDestinations cluster operations require a destination address buffer.");
                return;
            }
        }
        else if (desc.params.mode == Hrt::HCluster::EOperationMode::GetSizes)
        {
            if (desc.outSizesBuffer == nullptr)
            {
                mContext.Error("GetSizes cluster operations require a valid output size buffer.");
                return;
            }
        }

        // Create Vulkan operation info
        vk::ClusterAccelerationStructureInputInfoNV inputInfo = {};
        vk::ClusterAccelerationStructureMoveObjectsInputNV moveInput = {};
        vk::ClusterAccelerationStructureTriangleClusterInputNV clusterInput = {};
        vk::ClusterAccelerationStructureClustersBottomLevelInputNV blasInput = {};

        // Populate input info using helper function
        PopulateClusterOperationInputInfo(desc.params, mContext, inputInfo, moveInput, clusterInput, blasInput);

        // Set up buffer addresses
        ZWVKBuffer* indirectArgCountBuffer = static_cast<ZWVKBuffer*>(desc.inIndirectArgCountBuffer);
        ZWVKBuffer* indirectArgsBuffer = static_cast<ZWVKBuffer*>(desc.inIndirectArgsBuffer);
        ZWVKBuffer* inOutAddressesBuffer = static_cast<ZWVKBuffer*>(desc.inOutAddressesBuffer);
        ZWVKBuffer* outSizesBuffer = static_cast<ZWVKBuffer*>(desc.outSizesBuffer);
        ZWVKBuffer* outAccelerationStructuresBuffer = static_cast<ZWVKBuffer*>(desc.outAccelerationStructuresBuffer);

        // Set up resource states and barriers
        if (m_EnableAutomaticBarriers)
        {
            if (indirectArgsBuffer)
                RequireBufferState(indirectArgsBuffer, EResourceStates::ShaderResource);
            if (indirectArgCountBuffer)
                RequireBufferState(indirectArgCountBuffer, EResourceStates::ShaderResource);
            if (inOutAddressesBuffer)
                RequireBufferState(inOutAddressesBuffer, EResourceStates::UnorderedAccess);
            if (outSizesBuffer)
                RequireBufferState(outSizesBuffer, EResourceStates::UnorderedAccess);
            if (outAccelerationStructuresBuffer)
                RequireBufferState(outAccelerationStructuresBuffer, EResourceStates::AccelStructWrite);
            mBindingStatesDirty = true;
        }

        // Track resources for liveness
        if (indirectArgCountBuffer)
            mCurrentCmdBuf->referencedResources.push_back(indirectArgCountBuffer);
        if (indirectArgsBuffer)
            mCurrentCmdBuf->referencedResources.push_back(indirectArgsBuffer);
        if (inOutAddressesBuffer)
            mCurrentCmdBuf->referencedResources.push_back(inOutAddressesBuffer);
        if (outSizesBuffer)
            mCurrentCmdBuf->referencedResources.push_back(outSizesBuffer);
        if (outAccelerationStructuresBuffer)
            mCurrentCmdBuf->referencedResources.push_back(outAccelerationStructuresBuffer);

        CommitBarriers();

        // Allocate scratch buffer
        ZWVKBuffer* scratchBuffer = nullptr;
        uint64_t scratchOffset = 0;
        uint64_t currentVersion = MakeVersion(mCurrentCmdBuf->recordingID, mCommandListParameters.queueType, false);

        if (desc.scratchSizeInBytes > 0)
        {
            if (!m_ScratchManager->SuballocateBuffer(desc.scratchSizeInBytes, &scratchBuffer, &scratchOffset, nullptr,
                currentVersion, mContext.nvClusterAccelerationStructureProperties.clusterScratchByteAlignment))
            {
                std::stringstream ss;
                ss << "Couldn't suballocate a scratch buffer for cluster operation. "
                    "The operation requires " << desc.scratchSizeInBytes << " bytes of scratch space.";

                mContext.Error(ss.str());
                return;
            }
        }

        // Create commands info
        vk::ClusterAccelerationStructureCommandsInfoNV commandsInfo = {};
        commandsInfo.input = inputInfo;
        commandsInfo.scratchData = scratchBuffer ? scratchBuffer->deviceAddress + scratchOffset : 0;
        commandsInfo.dstImplicitData = outAccelerationStructuresBuffer ? outAccelerationStructuresBuffer->deviceAddress + desc.outAccelerationStructuresOffsetInBytes : 0;
        
        // Set up strided device address regions
        if (inOutAddressesBuffer)
        {
            commandsInfo.dstAddressesArray
                .setDeviceAddress(inOutAddressesBuffer->deviceAddress + desc.inOutAddressesOffsetInBytes)
                .setStride(inOutAddressesBuffer->GetDesc().structStride)
                .setSize(inOutAddressesBuffer->GetDesc().byteSize - desc.inOutAddressesOffsetInBytes);
        }
        
        if (outSizesBuffer)
        {
            commandsInfo.dstSizesArray
                .setDeviceAddress(outSizesBuffer->deviceAddress + desc.outSizesOffsetInBytes)
                .setStride(outSizesBuffer->GetDesc().structStride)
                .setSize(outSizesBuffer->GetDesc().byteSize - desc.outSizesOffsetInBytes);
        }
        
        if (indirectArgsBuffer)
        {
            commandsInfo.srcInfosArray
                .setDeviceAddress(indirectArgsBuffer->deviceAddress + desc.inIndirectArgsOffsetInBytes)
                .setStride(indirectArgsBuffer->GetDesc().structStride)
                .setSize(indirectArgsBuffer->GetDesc().byteSize - desc.inIndirectArgsOffsetInBytes);
        }
        
        commandsInfo.srcInfosCount = indirectArgCountBuffer ? indirectArgCountBuffer->deviceAddress + desc.inIndirectArgCountOffsetInBytes : 0;

        // vk::ClusterAccelerationStructureAddressResolutionFlagBitsNV is missing eNone bit
        // nvapi has this as NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE
        // so use 0 for now.
        commandsInfo.addressResolutionFlags = vk::ClusterAccelerationStructureAddressResolutionFlagBitsNV(0);

        // Execute the cluster operation
        mCurrentCmdBuf->cmdBuf.buildClusterAccelerationStructureIndirectNV(commandsInfo);
    }
#else
    Hrt::HCluster::ZWOperationSizeInfo ZWVKDevice::GetClusterOperationSizeInfo(const Hrt::HCluster::ZWOperationParams& params)
    {
        (void)params;
        mContext.Error(cVulkanClusterBuildUnsupportedMessage);
        return {};
    }

    void ZWVKCommandList::ExecuteMultiIndirectClusterOperation(const Hrt::HCluster::ZWOperationDesc& desc)
    {
        (void)desc;
        mContext.Error(cVulkanClusterBuildUnsupportedMessage);
    }
#endif

    ZWVKAccelStruct::~ZWVKAccelStruct()
    {
#if HRHI_WITH_RTXMU
        bool isManaged = desc.isTopLevel;
        if (!isManaged && rtxmuId != ~0ull)
        {
            std::vector<uint64_t> delAccel = { rtxmuId };
            mContext.rtxMemUtil->RemoveAccelerationStructures(delAccel);
            rtxmuId = ~0ull;
        }
#else
        bool isManaged = true;
#endif

        if (accelStruct && isManaged)
        {
            mContext.device.destroyAccelerationStructureKHR(accelStruct, mContext.allocationCallbacks);
            accelStruct = nullptr;
        }
    }

    HCommon::ZWObject ZWVKAccelStruct::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case HRHIObjectTypes::gVKBuffer:
        case HRHIObjectTypes::gVKDeviceMemory:
            if (dataBuffer)
                return dataBuffer->GetNativeObject(objectType);
            return nullptr;
        case HRHIObjectTypes::gVKAccelerationStructureKHR:
            return HCommon::ZWObject(accelStruct);
        default:
            return nullptr;
        }
    }

    uint64_t ZWVKAccelStruct::GetDeviceAddress() const
    {
#if HRHI_WITH_RTXMU
        if (!desc.isTopLevel)
            return mContext.rtxMemUtil->GetDeviceAddress(rtxmuId);
#endif
        return dataBuffer ? dataBuffer->GetGpuVirtualAddress() : 0;
    }

    ZWVKOpacityMicromap::~ZWVKOpacityMicromap()
    {
    }

    HCommon::ZWObject ZWVKOpacityMicromap::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case HRHIObjectTypes::gVKBuffer:
        case HRHIObjectTypes::gVKDeviceMemory:
            if (dataBuffer)
                return dataBuffer->GetNativeObject(objectType);
            return nullptr;
        case HRHIObjectTypes::gVKMicromap:
            return HCommon::ZWObject(opacityMicromap.get());
        default:
            return nullptr;
        }
    }

    uint64_t ZWVKOpacityMicromap::GetDeviceAddress() const
    {
        return dataBuffer ? dataBuffer->GetGpuVirtualAddress() : 0;
    }

    void ZWVKShaderTable::Bake(uint8_t* uploadCpuVA, vk::DeviceAddress uploadGpuVA, ZWVKShaderTableState& state)
    {
        const uint32_t shaderGroupHandleSize = mContext.rayTracingPipelineProperties.shaderGroupHandleSize;
        const uint32_t shaderGroupBaseAlignment = mContext.rayTracingPipelineProperties.shaderGroupBaseAlignment;

        // Copy the shader and group handles into the device SBT, record the pointers and the version.

        state.version = version;

        // ... RayGen

        uint32_t sbtIndex = 0;
        memcpy(uploadCpuVA + sbtIndex * shaderGroupBaseAlignment,
            pipeline->shaderGroupHandles.data() + shaderGroupHandleSize * rayGenerationShader,
            shaderGroupHandleSize);
        state.rayGen.setDeviceAddress(uploadGpuVA + sbtIndex * shaderGroupBaseAlignment);
        state.rayGen.setSize(shaderGroupBaseAlignment);
        state.rayGen.setStride(shaderGroupBaseAlignment);
        sbtIndex++;

        // ... Miss

        if (!missShaders.empty())
        {
            state.miss.setDeviceAddress(uploadGpuVA + sbtIndex * shaderGroupBaseAlignment);
            for (uint32_t shaderGroupIndex : missShaders)
            {
                memcpy(uploadCpuVA + sbtIndex * shaderGroupBaseAlignment,
                    pipeline->shaderGroupHandles.data() + shaderGroupHandleSize * shaderGroupIndex,
                    shaderGroupHandleSize);
                sbtIndex++;
            }
            state.miss.setSize(shaderGroupBaseAlignment * uint32_t(missShaders.size()));
            state.miss.setStride(shaderGroupBaseAlignment);
        }
        else
        {
            state.miss = vk::StridedDeviceAddressRegionKHR();
        }

        // ... Hit Groups

        if (!hitGroups.empty())
        {
            state.hitGroups.setDeviceAddress(uploadGpuVA + sbtIndex * shaderGroupBaseAlignment);
            for (uint32_t shaderGroupIndex : hitGroups)
            {
                memcpy(uploadCpuVA + sbtIndex * shaderGroupBaseAlignment,
                    pipeline->shaderGroupHandles.data() + shaderGroupHandleSize * shaderGroupIndex,
                    shaderGroupHandleSize);
                sbtIndex++;
            }
            state.hitGroups.setSize(shaderGroupBaseAlignment * uint32_t(hitGroups.size()));
            state.hitGroups.setStride(shaderGroupBaseAlignment);
        }
        else
        {
            state.hitGroups = vk::StridedDeviceAddressRegionKHR();
        }

        // ... Callable

        if (!callableShaders.empty())
        {
            state.callable.setDeviceAddress(uploadGpuVA + sbtIndex * shaderGroupBaseAlignment);
            for (uint32_t shaderGroupIndex : callableShaders)
            {
                memcpy(uploadCpuVA + sbtIndex * shaderGroupBaseAlignment,
                    pipeline->shaderGroupHandles.data() + shaderGroupHandleSize * shaderGroupIndex,
                    shaderGroupHandleSize);
                sbtIndex++;
            }
            state.callable.setSize(shaderGroupBaseAlignment * uint32_t(callableShaders.size()));
            state.callable.setStride(shaderGroupBaseAlignment);
        }
        else
        {
            state.callable = vk::StridedDeviceAddressRegionKHR();
        }
    }

    ZWVKShaderTableState& ZWVKCommandList::GetShaderTableState(Hrt::IShaderTable* _shaderTable)
    {
        ZWVKShaderTable* shaderTable = static_cast<ZWVKShaderTable*>(_shaderTable);
        if (shaderTable->GetDesc().isCached)
            return shaderTable->cacheState;

        auto it = m_UncachedShaderTableStates.find(shaderTable);

        if (it != m_UncachedShaderTableStates.end())
        {
            return *it->second;
        }

        std::unique_ptr<ZWVKShaderTableState> statePtr = std::make_unique<ZWVKShaderTableState>();

        ZWVKShaderTableState& state = *statePtr;
        m_UncachedShaderTableStates.insert(std::make_pair(shaderTable, std::move(statePtr)));

        return state;
    }

    void ZWVKCommandList::SetRayTracingState(const Hrt::ZWState& state)
    {
        if (!state.shaderTable)
            return;

        ZWVKShaderTable* shaderTable = static_cast<ZWVKShaderTable*>(state.shaderTable);
        ZWVKRayTracingPipeline* pso = shaderTable->pipeline;

        if (shaderTable->rayGenerationShader < 0)
        {
            mContext.Error("The STB does not have a valid RayGen shader set");
            return;
        }

        if (m_EnableAutomaticBarriers)
        {
            InsertRayTracingResourceBarriers(state);
        }

        if (mCurrentRayTracingState.shaderTable != state.shaderTable)
        {
            mCurrentCmdBuf->referencedResources.push_back(state.shaderTable);
        }

        if (!mCurrentRayTracingState.shaderTable || mCurrentRayTracingState.shaderTable->GetPipeline() != pso)
        {
            mCurrentCmdBuf->cmdBuf.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, pso->pipeline);
            mCurrentPipelineLayout = pso->pipelineLayout;
            mCurrentPushConstantsVisibility = pso->pushConstantVisibility;
        }

        if (ArraysAreDifferent(mCurrentRayTracingState.bindings, state.bindings) || mAnyVolatileBufferWrites)
        {
            BindBindingSets(vk::PipelineBindPoint::eRayTracingKHR, pso->pipelineLayout, state.bindings, pso->descriptorSetIdxToBindingIdx);
        }

        // Rebuild the SBT if it's uncached and we're using it for the first time in this command list,
        // or if it's been changed since the previous build.

        const bool shaderTableCached = shaderTable->GetDesc().isCached;
        ZWVKShaderTableState& shaderTableState = GetShaderTableState(shaderTable);
        bool const rebuildShaderTable = shaderTableState.version != shaderTable->version;

        if (rebuildShaderTable)
        {
            size_t const shaderTableSize = shaderTable->GetUploadSize();

            if (shaderTableCached && (!shaderTable->cache || shaderTableSize > shaderTable->cache->GetDesc().byteSize))
            {
                mContext.Error("Required shader table size is larger than the allocated cache. Increase ShaderTableDesc::maxEntries.");
                return;
            }

            // Allocate a piece of the upload buffer. That will be our SBT on the device.

            ZWVKBuffer* uploadBuffer = nullptr;
            uint64_t uploadOffset = 0;
            uint8_t* uploadCpuVA = nullptr;
            bool allocated = m_UploadManager->SuballocateBuffer(shaderTableSize, &uploadBuffer, &uploadOffset, (void**)&uploadCpuVA,
                MakeVersion(mCurrentCmdBuf->recordingID, mCommandListParameters.queueType, false),
                mContext.rayTracingPipelineProperties.shaderGroupBaseAlignment);

            if (!allocated)
            {
                mContext.Error("Failed to suballocate an upload buffer for the SBT");
                return;
            }

            assert(uploadCpuVA);
            assert(uploadBuffer);

            vk::DeviceAddress const effectiveGpuVA = shaderTableCached
                ? shaderTable->cache->GetGpuVirtualAddress()
                : uploadBuffer->GetGpuVirtualAddress() + uploadOffset;

            // Build the SBT in the upload buffer.

            shaderTable->Bake(uploadCpuVA, effectiveGpuVA, shaderTableState);

            // Copy the built SBT into the cache buffer, if it exists.

            if (shaderTableCached)
            {
                CopyBuffer(shaderTable->cache, 0, uploadBuffer, uploadOffset, shaderTableSize);
            }
        }

        if (shaderTableCached)
        {
            // Ensure that the cache buffer is in the right state.
            // It's not conditional on m_EnableAutomaticBarriers because the cache is an internal object,
            // completely invisible to the application, and so its state must be handled by HRHI.
            SetBufferState(shaderTable->cache, EResourceStates::ShaderResource);
        }

        if (shaderTableCached || rebuildShaderTable)
        {
            // If the shader table is not cached, then it's rebuilt at least once per CL, and we can AddRef it once then
            mCurrentCmdBuf->referencedResources.push_back(shaderTable);
        }

        CommitBarriers();

        mCurrentGraphicsState = ZWGraphicsState();
        mCurrentComputeState = ZWComputeState();
        mCurrentMeshletState = ZWMeshletState();
        mCurrentRayTracingState = state;
        mAnyVolatileBufferWrites = false;
    }

    void ZWVKCommandList::DispatchRays(const Hrt::ZWDispatchRaysArguments& args)
    {
        assert(mCurrentCmdBuf);

        UpdateRayTracingVolatileBuffers();

        ZWVKShaderTableState& shaderTableState = GetShaderTableState(mCurrentRayTracingState.shaderTable);

        mCurrentCmdBuf->cmdBuf.traceRaysKHR(
            &shaderTableState.rayGen,
            &shaderTableState.miss,
            &shaderTableState.hitGroups,
            &shaderTableState.callable,
            args.width, args.height, args.depth);
    }

    void ZWVKCommandList::UpdateRayTracingVolatileBuffers()
    {
        if (mAnyVolatileBufferWrites && mCurrentRayTracingState.shaderTable)
        {
            ZWVKRayTracingPipeline* pso = static_cast<ZWVKRayTracingPipeline*>(mCurrentRayTracingState.shaderTable->GetPipeline());

            BindBindingSets(vk::PipelineBindPoint::eRayTracingKHR, pso->pipelineLayout, mCurrentRayTracingState.bindings, pso->descriptorSetIdxToBindingIdx);

            mAnyVolatileBufferWrites = false;
        }
    }

    static void RegisterShaderModule(
        IShader* _shader,
        std::unordered_map<ZWVKShader*, uint32_t>& shaderStageIndices,
        size_t& numShaders,
        size_t& numShadersWithSpecializations,
        size_t& numSpecializationConstants)
    {
        if (!_shader)
            return;
        
        ZWVKShader* shader = static_cast<ZWVKShader*>(_shader);
        auto it = shaderStageIndices.find(shader);
        if (it == shaderStageIndices.end())
        {
            CountSpecializationConstants(shader, numShaders, numShadersWithSpecializations, numSpecializationConstants);
            shaderStageIndices[shader] = uint32_t(shaderStageIndices.size());
        }
    }

    Hrt::ZWPipelineHandle ZWVKDevice::CreateRayTracingPipeline(const Hrt::ZWPipelineDesc& desc)
    {
        ZWVKRayTracingPipeline* pso = new ZWVKRayTracingPipeline(mContext, this);
        pso->desc = desc;

        vk::Result res = CreatePipelineLayout(
            pso->pipelineLayout,
            pso->pipelineBindingLayouts,
            pso->pushConstantVisibility,
            pso->descriptorSetIdxToBindingIdx,
            mContext,
            desc.globalBindingLayouts);
        CHECK_VK_FAIL(res)

        // Count all shader modules with their specializations,
        // place them into a dictionary to remove duplicates.

        size_t numShaders = 0;
        size_t numShadersWithSpecializations = 0;
        size_t numSpecializationConstants = 0;

        std::unordered_map<ZWVKShader*, uint32_t> shaderStageIndices; // shader -> index

        for (const auto& shaderDesc : desc.shaders)
        {
            if (shaderDesc.bindingLayout)
            {
                mContext.Error("Local ray tracing binding layouts are not currently supported by the Vulkan backend.");
                return nullptr;
            }

            RegisterShaderModule(shaderDesc.shader, shaderStageIndices, numShaders, 
                numShadersWithSpecializations, numSpecializationConstants);
        }

        for (const auto& hitGroupDesc : desc.hitGroups)
        {
            if (hitGroupDesc.bindingLayout)
            {
                mContext.Error("Local ray tracing binding layouts are not currently supported by the Vulkan backend.");
                return nullptr;
            }

            RegisterShaderModule(hitGroupDesc.closestHitShader, shaderStageIndices, numShaders,
                numShadersWithSpecializations, numSpecializationConstants);

            RegisterShaderModule(hitGroupDesc.anyHitShader, shaderStageIndices, numShaders,
                numShadersWithSpecializations, numSpecializationConstants);

            RegisterShaderModule(hitGroupDesc.intersectionShader, shaderStageIndices, numShaders,
                numShadersWithSpecializations, numSpecializationConstants);
        }

        assert(numShaders == shaderStageIndices.size());

        // Populate the shader stages, shader groups, and specializations arrays.

        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
        std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shaderGroups;
        std::vector<vk::SpecializationInfo> specInfos;
        std::vector<vk::SpecializationMapEntry> specMapEntries;
        std::vector<uint32_t> specData;

        shaderStages.resize(numShaders);
        shaderGroups.reserve(desc.shaders.size() + desc.hitGroups.size());
        specInfos.reserve(numShadersWithSpecializations);
        specMapEntries.reserve(numSpecializationConstants);
        specData.reserve(numSpecializationConstants);

        // ... Individual shaders (RayGen, Miss, Callable)

        for (const auto& shaderDesc : desc.shaders)
        {
            std::string exportName = shaderDesc.exportName;

            auto shaderGroupCreateInfo = vk::RayTracingShaderGroupCreateInfoKHR()
                .setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
                .setClosestHitShader(VK_SHADER_UNUSED_KHR)
                .setAnyHitShader(VK_SHADER_UNUSED_KHR)
                .setIntersectionShader(VK_SHADER_UNUSED_KHR);

            if (shaderDesc.shader)
            {
                ZWVKShader* shader = static_cast<ZWVKShader*>(shaderDesc.shader.Get());
                uint32_t shaderStageIndex = shaderStageIndices[shader];
                shaderStages[shaderStageIndex] = MakeShaderStageCreateInfo(shader, specInfos, specMapEntries, specData);

                if (exportName.empty())
                    exportName = shader->desc.entryName;

                shaderGroupCreateInfo.setGeneralShader(shaderStageIndex);
            }

            if (!exportName.empty())
            {
                pso->shaderGroups[exportName] = uint32_t(shaderGroups.size());
                shaderGroups.push_back(shaderGroupCreateInfo);
            }
        }

        // ... Hit groups

        for (const auto& hitGroupDesc : desc.hitGroups)
        {
            auto shaderGroupCreateInfo = vk::RayTracingShaderGroupCreateInfoKHR()
                .setType(hitGroupDesc.isProceduralPrimitive 
                    ? vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup
                    : vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup)
                .setGeneralShader(VK_SHADER_UNUSED_KHR)
                .setClosestHitShader(VK_SHADER_UNUSED_KHR)
                .setAnyHitShader(VK_SHADER_UNUSED_KHR)
                .setIntersectionShader(VK_SHADER_UNUSED_KHR);

            if (hitGroupDesc.closestHitShader)
            {
                ZWVKShader* shader = static_cast<ZWVKShader*>(hitGroupDesc.closestHitShader.Get());
                uint32_t shaderStageIndex = shaderStageIndices[shader];
                shaderStages[shaderStageIndex] = MakeShaderStageCreateInfo(shader, specInfos, specMapEntries, specData);
                shaderGroupCreateInfo.setClosestHitShader(shaderStageIndex);
            }
            if (hitGroupDesc.anyHitShader)
            {
                ZWVKShader* shader = static_cast<ZWVKShader*>(hitGroupDesc.anyHitShader.Get());
                uint32_t shaderStageIndex = shaderStageIndices[shader];
                shaderStages[shaderStageIndex] = MakeShaderStageCreateInfo(shader, specInfos, specMapEntries, specData);
                shaderGroupCreateInfo.setAnyHitShader(shaderStageIndex);
            }
            if (hitGroupDesc.intersectionShader)
            {
                ZWVKShader* shader = static_cast<ZWVKShader*>(hitGroupDesc.intersectionShader.Get());
                uint32_t shaderStageIndex = shaderStageIndices[shader];
                shaderStages[shaderStageIndex] = MakeShaderStageCreateInfo(shader, specInfos, specMapEntries, specData);
                shaderGroupCreateInfo.setIntersectionShader(shaderStageIndex);
            }

            assert(!hitGroupDesc.exportName.empty());
            
            pso->shaderGroups[hitGroupDesc.exportName] = uint32_t(shaderGroups.size());
            shaderGroups.push_back(shaderGroupCreateInfo);
        }

        // Create the pipeline object

        auto libraryInfo = vk::PipelineLibraryCreateInfoKHR();

#if HRHI_VULKAN_HAS_NV_CLUSTER_ACCELERATION_STRUCTURE
        auto pipelineClusters = vk::RayTracingPipelineClusterAccelerationStructureCreateInfoNV()
            .setAllowClusterAccelerationStructure(true);
#endif

        auto pipelineFlags2 = vk::PipelineCreateFlags2CreateInfoKHR();
#if HRHI_VULKAN_HAS_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES
        const bool supportsSpheres = mContext.extensions.NV_ray_tracing_linear_swept_spheres && mContext.linearSweptSpheresFeatures.spheres;
        const bool supportsLinearSweptSpheres =
            mContext.extensions.NV_ray_tracing_linear_swept_spheres && mContext.linearSweptSpheresFeatures.linearSweptSpheres;
#else
        const bool supportsSpheres = false;
        const bool supportsLinearSweptSpheres = false;
#endif
        if (supportsSpheres || supportsLinearSweptSpheres)
        {
#if HRHI_VULKAN_HAS_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES
            pipelineFlags2.setFlags(vk::PipelineCreateFlagBits2::eRayTracingAllowSpheresAndLinearSweptSpheresNV);
#endif
        }

        auto pipelineInfo = vk::RayTracingPipelineCreateInfoKHR()
            .setStages(shaderStages)
            .setGroups(shaderGroups)
            .setLayout(pso->pipelineLayout)
            .setMaxPipelineRayRecursionDepth(desc.maxRecursionDepth)
            .setPLibraryInfo(&libraryInfo);

        if (supportsSpheres || supportsLinearSweptSpheres)
        {
            pipelineInfo.setPNext(&pipelineFlags2);
        }

        if (mContext.extensions.NV_cluster_acceleration_structure)
        {
#if HRHI_VULKAN_HAS_NV_CLUSTER_ACCELERATION_STRUCTURE
            if (supportsSpheres || supportsLinearSweptSpheres)
            {
                pipelineClusters.setPNext(&pipelineFlags2);
            }
            pipelineInfo.setPNext(&pipelineClusters);
#endif
        }

        res = mContext.device.createRayTracingPipelinesKHR(vk::DeferredOperationKHR(), mContext.pipelineCache,
            1, &pipelineInfo,
            mContext.allocationCallbacks,
            &pso->pipeline);

        CHECK_VK_FAIL(res)

        // Obtain the shader group handles to fill the SBT buffer later

        pso->shaderGroupHandles.resize(mContext.rayTracingPipelineProperties.shaderGroupHandleSize * shaderGroups.size());

        res = mContext.device.getRayTracingShaderGroupHandlesKHR(pso->pipeline, 0, 
            uint32_t(shaderGroups.size()), 
            pso->shaderGroupHandles.size(), pso->shaderGroupHandles.data());

        CHECK_VK_FAIL(res)

        return Hrt::ZWPipelineHandle::Create(pso);
    }

    ZWVKRayTracingPipeline::~ZWVKRayTracingPipeline()
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

    Hrt::ZWShaderTableHandle ZWVKRayTracingPipeline::CreateShaderTable(Hrt::ZWShaderTableDesc const& stDesc)
    {
        ZWBufferHandle cache;
        if (stDesc.isCached)
        {
            if (stDesc.maxEntries == 0)
            {
                mContext.Error("maxEntries must be nonzero for a cached ZWVKShaderTable");
                return nullptr;
            }
            
            ZWBufferDesc bufferDesc = ZWBufferDesc()
                .setDebugName(stDesc.debugName)
                .setByteSize(GetShaderTableEntrySize() * stDesc.maxEntries)
                .setIsShaderBindingTable(true)
                .enableAutomaticStateTracking(EResourceStates::ShaderResource);

            cache = mDevice->CreateBuffer(bufferDesc);
            if (!cache)
                return nullptr;
        }

        ZWVKShaderTable* shaderTable = new ZWVKShaderTable(mContext, this, stDesc);
        shaderTable->cache = cache;

        return Hrt::ZWShaderTableHandle::Create(shaderTable);
    }

    HCommon::ZWObject ZWVKRayTracingPipeline::GetNativeObject(ObjectType objectType)
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

    int32_t ZWVKRayTracingPipeline::FindShaderGroup(const std::string& name)
    {
        auto it = shaderGroups.find(name);
        if (it == shaderGroups.end())
            return -1;

        return int32_t(it->second);
    }

    bool ZWVKShaderTable::VerifyShaderGroupExists(const char* exportName, int32_t shaderGroupIndex) const
    {
        if (shaderGroupIndex >= 0)
            return true;

        std::stringstream ss;
        ss << "Cannot find a RT pipeline shader group for RayGen shader with name " << exportName;
        mContext.Error(ss.str());
        return false;
    }

    void ZWVKShaderTable::SetRayGenerationShader(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        if (bindings != nullptr)
            assert(false);

        const int32_t shaderGroupIndex = pipeline->FindShaderGroup(exportName);

        if (VerifyShaderGroupExists(exportName, shaderGroupIndex))
        {
            rayGenerationShader = shaderGroupIndex;
            ++version;
        }
    }

    int ZWVKShaderTable::AddMissShader(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        if (bindings != nullptr)
            assert(false);

        const int32_t shaderGroupIndex = pipeline->FindShaderGroup(exportName);

        if (VerifyShaderGroupExists(exportName, shaderGroupIndex))
        {
            missShaders.push_back(uint32_t(shaderGroupIndex));
            ++version;

            return int(missShaders.size()) - 1;
        }

        return -1;
    }

    int ZWVKShaderTable::AddHitGroup(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        if (bindings != nullptr)
            assert(false);

        const int32_t shaderGroupIndex = pipeline->FindShaderGroup(exportName);

        if (VerifyShaderGroupExists(exportName, shaderGroupIndex))
        {
            hitGroups.push_back(uint32_t(shaderGroupIndex));
            ++version;

            return int(hitGroups.size()) - 1;
        }

        return -1;
    }

    int ZWVKShaderTable::AddCallableShader(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        if (bindings != nullptr)
            assert(false);

        const int32_t shaderGroupIndex = pipeline->FindShaderGroup(exportName);

        if (VerifyShaderGroupExists(exportName, shaderGroupIndex))
        {
            callableShaders.push_back(uint32_t(shaderGroupIndex));
            ++version;

            return int(callableShaders.size()) - 1;
        }

        return -1;
    }

    void ZWVKShaderTable::ClearMissShaders()
    {
        missShaders.clear();
        ++version;
    }

    void ZWVKShaderTable::ClearHitShaders()
    {
        hitGroups.clear();
        ++version;
    }

    void ZWVKShaderTable::ClearCallableShaders()
    {
        callableShaders.clear();
        ++version;
    }
    
    uint32_t ZWVKShaderTable::GetNumEntries() const
    {
        return 1 + // rayGeneration
            uint32_t(missShaders.size()) +
            uint32_t(hitGroups.size()) +
            uint32_t(callableShaders.size());
    }
} // namespace HRHI
