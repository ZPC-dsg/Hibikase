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

#include <unordered_map>

#include <BackEnd/vulkanbackend.h>

namespace HRHI
{

    ZWShaderHandle ZWVKDevice::CreateShader(const ZWShaderDesc& desc, const void* binary, const size_t binarySize)
    {
        ZWVKShader* shader = new ZWVKShader(mContext);

        shader->desc = desc;
        shader->stageFlagBits = ConvertShaderTypeToShaderStageFlagBits(desc.shaderType);

        auto shaderInfo = vk::ShaderModuleCreateInfo()
            .setCodeSize(binarySize)
            .setPCode((const uint32_t*)binary);

        const vk::Result res = mContext.device.createShaderModule(&shaderInfo, mContext.allocationCallbacks, &shader->shaderModule);
        CHECK_VK_FAIL(res)

        const std::string debugName = desc.debugName + ":" + desc.entryName;
        mContext.NameVKObject(VkShaderModule(shader->shaderModule), vk::ObjectType::eShaderModule, vk::DebugReportObjectTypeEXT::eShaderModule, debugName.c_str());

        return ZWShaderHandle::Create(shader);
    }

    ZWShaderLibraryHandle ZWVKDevice::CreateShaderLibrary(const void* binary, const size_t binarySize)
    {
        ZWVKShaderLibrary* library = new ZWVKShaderLibrary(mContext);
        
        auto shaderInfo = vk::ShaderModuleCreateInfo()
            .setCodeSize(binarySize)
            .setPCode((const uint32_t*)binary);

        const vk::Result res = mContext.device.createShaderModule(&shaderInfo, mContext.allocationCallbacks, &library->shaderModule);
        CHECK_VK_FAIL(res)

        return ZWShaderLibraryHandle::Create(library);
    }

    ZWShaderHandle ZWVKDevice::CreateShaderSpecialization(IShader* _baseShader, const ZWShaderSpecialization* constants, const uint32_t numConstants)
    {
        ZWVKShader* baseShader = static_cast<ZWVKShader*>(_baseShader);
        assert(constants);
        assert(numConstants != 0);

        ZWVKShader* newShader = new ZWVKShader(mContext);

        // Hold a strong reference to the parent object
        newShader->baseShader = (baseShader->baseShader) ? baseShader->baseShader : baseShader;
        newShader->desc = baseShader->desc;
        newShader->shaderModule = baseShader->shaderModule;
        newShader->stageFlagBits = baseShader->stageFlagBits;
        newShader->specializationConstants.assign(constants, constants + numConstants);

        return ZWShaderHandle::Create(newShader);
    }


    ZWVKShader::~ZWVKShader()
    {
        if (shaderModule && !baseShader) // do not destroy the module if this is a derived specialization shader or a library entry
        {
            mContext.device.destroyShaderModule(shaderModule, mContext.allocationCallbacks);
            shaderModule = vk::ShaderModule();
        }
    }

    void ZWVKShader::GetBytecode(const void** ppBytecode, size_t* pSize) const
    {
        // we don't save these for vulkan
        if (ppBytecode) *ppBytecode = nullptr;
        if (pSize) *pSize = 0;
    }

    HCommon::ZWObject ZWVKShader::GetNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case HRHIObjectTypes::gVKShaderModule:
            return HCommon::ZWObject(shaderModule);
        default:
            return nullptr;
        }
    }

    ZWVKShaderLibrary::~ZWVKShaderLibrary()
    {
        if (shaderModule)
        {
            mContext.device.destroyShaderModule(shaderModule, mContext.allocationCallbacks);
            shaderModule = vk::ShaderModule();
        }
    }

    void ZWVKShaderLibrary::GetBytecode(const void** ppBytecode, size_t* pSize) const
    {
        if (ppBytecode) *ppBytecode = nullptr;
        if (pSize) *pSize = 0;
    }

    ZWShaderHandle ZWVKShaderLibrary::GetShader(const char* entryName, EShaderType shaderType)
    {
        ZWVKShader* newShader = new ZWVKShader(mContext);
        newShader->desc.entryName = entryName;
        newShader->desc.shaderType = shaderType;
        newShader->shaderModule = shaderModule;
        newShader->baseShader = this;
        newShader->stageFlagBits = ConvertShaderTypeToShaderStageFlagBits(shaderType);

        return ZWShaderHandle::Create(newShader);
    }

    ZWInputLayoutHandle ZWVKDevice::CreateInputLayout(const ZWVertexAttributeDesc* attributeDesc, uint32_t attributeCount, IShader* vertexShader)
    {
        (void)vertexShader;

        ZWVKInputLayout* layout = new ZWVKInputLayout();

        int totalAttributeArraySize = 0;

        // collect all buffer bindings
        std::unordered_map<uint32_t, vk::VertexInputBindingDescription> bindingMap;
        for (uint32_t i = 0; i < attributeCount; i++)
        {
            const ZWVertexAttributeDesc& desc = attributeDesc[i];

            assert(desc.arraySize > 0);

            totalAttributeArraySize += desc.arraySize;

            if (bindingMap.find(desc.bufferIndex) == bindingMap.end())
            {
                bindingMap[desc.bufferIndex] = vk::VertexInputBindingDescription()
                    .setBinding(desc.bufferIndex)
                    .setStride(desc.elementStride)
                    .setInputRate(desc.isInstanced ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex);
            }
            else {
                assert(bindingMap[desc.bufferIndex].stride == desc.elementStride);
                assert(bindingMap[desc.bufferIndex].inputRate == (desc.isInstanced ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex));
            }
        }

        for (const auto& b : bindingMap)
        {
            layout->bindingDesc.push_back(b.second);
        }

        // build attribute descriptions
        layout->inputDesc.resize(attributeCount);
        layout->attributeDesc.resize(totalAttributeArraySize);

        uint32_t attributeLocation = 0;
        for (uint32_t i = 0; i < attributeCount; i++)
        {
            const ZWVertexAttributeDesc& in = attributeDesc[i];
            layout->inputDesc[i] = in;

            uint32_t elementSizeBytes = GetFormatInfo(in.format).bytesPerBlock;

            uint32_t bufferOffset = 0;

            for (uint32_t slot = 0; slot < in.arraySize; ++slot)
            {
                auto& outAttrib = layout->attributeDesc[attributeLocation];

                outAttrib.location = attributeLocation;
                outAttrib.binding = in.bufferIndex;
                outAttrib.format = vk::Format(ConvertFormat(in.format));
                outAttrib.offset = bufferOffset + in.offset;
                bufferOffset += elementSizeBytes;

                ++attributeLocation;
            }
        }

        return ZWInputLayoutHandle::Create(layout);
    }

    uint32_t ZWVKInputLayout::GetNumAttributes() const 
    { 
        return uint32_t(inputDesc.size());
    }

    const ZWVertexAttributeDesc* ZWVKInputLayout::GetAttributeDesc(uint32_t index) const 
    {
        if (index < uint32_t(inputDesc.size())) 
            return &inputDesc[index]; 
        else 
            return nullptr;
    }

} // namespace HRHI
