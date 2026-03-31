#include <BackEnd/d3d12backend.h>

#include <algorithm>

namespace HRHI::HD3D12
{
    namespace
    {
        uint32_t NextPowerOfTwo(uint32_t value)
        {
            value--;
            value |= value >> 1;
            value |= value >> 2;
            value |= value >> 4;
            value |= value >> 8;
            value |= value >> 16;
            value++;
            return value;
        }
    }

    ZWD3D12StaticDescriptorHeap::ZWD3D12StaticDescriptorHeap(const ZWD3D12Context& context)
        : m_Context(context)
    {
    }

    HRESULT ZWD3D12StaticDescriptorHeap::AllocateResources(D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t numDescriptors, bool shaderVisible)
    {
        m_Heap = nullptr;
        m_ShaderVisibleHeap = nullptr;

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = heapType;
        heapDesc.NumDescriptors = numDescriptors;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        HRESULT result = m_Context.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_Heap.ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            return result;
        }

        if (shaderVisible)
        {
            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            result = m_Context.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_ShaderVisibleHeap.ReleaseAndGetAddressOf()));
            if (FAILED(result))
            {
                return result;
            }

            m_StartCpuHandleShaderVisible = m_ShaderVisibleHeap->GetCPUDescriptorHandleForHeapStart();
            m_StartGpuHandleShaderVisible = m_ShaderVisibleHeap->GetGPUDescriptorHandleForHeapStart();
        }

        m_NumDescriptors = numDescriptors;
        m_HeapType = heapType;
        m_StartCpuHandle = m_Heap->GetCPUDescriptorHandleForHeapStart();
        m_Stride = m_Context.device->GetDescriptorHandleIncrementSize(heapType);
        m_AllocatedDescriptors.resize(m_NumDescriptors);

        return S_OK;
    }

    HRESULT ZWD3D12StaticDescriptorHeap::Grow(uint32_t minRequiredSize)
    {
        const uint32_t oldSize = m_NumDescriptors;
        uint32_t newSize = NextPowerOfTwo(minRequiredSize);
        const bool shaderVisible = m_ShaderVisibleHeap != nullptr;

        if (shaderVisible)
        {
            const uint32_t maxSize = m_HeapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                ? D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1
                : D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;

            newSize = std::min(newSize, maxSize);
            if (newSize < minRequiredSize)
            {
                return E_OUTOFMEMORY;
            }
        }

        HCommon::RefCountPtr<ID3D12DescriptorHeap> oldHeap = m_Heap;

        const HRESULT result = AllocateResources(m_HeapType, newSize, shaderVisible);
        if (FAILED(result))
        {
            return result;
        }

        m_Context.device->CopyDescriptorsSimple(oldSize, m_StartCpuHandle, oldHeap->GetCPUDescriptorHandleForHeapStart(), m_HeapType);

        if (m_ShaderVisibleHeap != nullptr)
        {
            m_Context.device->CopyDescriptorsSimple(oldSize, m_StartCpuHandleShaderVisible, oldHeap->GetCPUDescriptorHandleForHeapStart(), m_HeapType);
        }

        return S_OK;
    }

    void ZWD3D12StaticDescriptorHeap::CopyToShaderVisibleHeap(DescriptorIndex index, uint32_t count)
    {
        m_Context.device->CopyDescriptorsSimple(count, GetCpuHandleShaderVisible(index), GetCpuHandle(index), m_HeapType);
    }

    DescriptorIndex ZWD3D12StaticDescriptorHeap::AllocateDescriptors(uint32_t count)
    {
        std::lock_guard<std::mutex> lockGuard(m_Mutex);

        DescriptorIndex foundIndex = 0;
        uint32_t freeCount = 0;
        bool found = false;

        for (DescriptorIndex index = m_SearchStart; index < m_NumDescriptors; ++index)
        {
            if (m_AllocatedDescriptors[index])
            {
                freeCount = 0;
            }
            else
            {
                freeCount += 1;
            }

            if (freeCount >= count)
            {
                foundIndex = index - count + 1;
                found = true;
                break;
            }
        }

        if (!found)
        {
            foundIndex = m_NumDescriptors;
            if (FAILED(Grow(m_NumDescriptors + count)))
            {
                m_Context.Error("Failed to grow a descriptor heap.");
                return cInvalidDescriptorIndex;
            }
        }

        for (DescriptorIndex index = foundIndex; index < foundIndex + count; ++index)
        {
            m_AllocatedDescriptors[index] = true;
        }

        m_NumAllocatedDescriptors += count;
        m_SearchStart = foundIndex + count;
        return foundIndex;
    }

    DescriptorIndex ZWD3D12StaticDescriptorHeap::AllocateDescriptor()
    {
        return AllocateDescriptors(1);
    }

    void ZWD3D12StaticDescriptorHeap::ReleaseDescriptors(DescriptorIndex baseIndex, uint32_t count)
    {
        std::lock_guard<std::mutex> lockGuard(m_Mutex);

        if (count == 0)
        {
            return;
        }

        for (DescriptorIndex index = baseIndex; index < baseIndex + count; ++index)
        {
            m_AllocatedDescriptors[index] = false;
        }

        m_NumAllocatedDescriptors -= count;

        if (m_SearchStart > baseIndex)
        {
            m_SearchStart = baseIndex;
        }
    }

    void ZWD3D12StaticDescriptorHeap::ReleaseDescriptor(DescriptorIndex index)
    {
        ReleaseDescriptors(index, 1);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE ZWD3D12StaticDescriptorHeap::GetCpuHandle(DescriptorIndex index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_StartCpuHandle;
        handle.ptr += SIZE_T(index) * SIZE_T(m_Stride);
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE ZWD3D12StaticDescriptorHeap::GetCpuHandleShaderVisible(DescriptorIndex index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_StartCpuHandleShaderVisible;
        handle.ptr += SIZE_T(index) * SIZE_T(m_Stride);
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ZWD3D12StaticDescriptorHeap::GetGpuHandle(DescriptorIndex index)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = m_StartGpuHandleShaderVisible;
        handle.ptr += UINT64(index) * UINT64(m_Stride);
        return handle;
    }

    ID3D12DescriptorHeap* ZWD3D12StaticDescriptorHeap::GetHeap() const
    {
        return m_Heap.Get();
    }

    ID3D12DescriptorHeap* ZWD3D12StaticDescriptorHeap::GetShaderVisibleHeap() const
    {
        return m_ShaderVisibleHeap.Get();
    }
}
