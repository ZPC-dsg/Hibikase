#pragma once

#include <Common\resource.h>

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace HCommon
{

template <RefCountedObject T>
class RefCountPtr
{
public:
    using ElementType = T;

    RefCountPtr() noexcept = default;

    RefCountPtr(std::nullptr_t) noexcept
        : mPtr(nullptr)
    {
    }

    RefCountPtr(T* other) noexcept
        : mPtr(other)
    {
        InternalAddRef();
    }

    RefCountPtr(const RefCountPtr& other) noexcept
        : mPtr(other.mPtr)
    {
        InternalAddRef();
    }

    template <RefCountedObject U>
        requires (!std::same_as<U, T> && std::convertible_to<U*, T*>)
    RefCountPtr(U* other) noexcept
        : mPtr(other)
    {
        InternalAddRef();
    }

    template <RefCountedObject U>
        requires (!std::same_as<U, T> && std::convertible_to<U*, T*>)
    RefCountPtr(const RefCountPtr<U>& other) noexcept
        : mPtr(other.Get())
    {
        InternalAddRef();
    }

    RefCountPtr(RefCountPtr&& other) noexcept
        : mPtr(other.Detach())
    {
    }

    template <RefCountedObject U>
        requires (!std::same_as<U, T> && std::convertible_to<U*, T*>)
    RefCountPtr(RefCountPtr<U>&& other) noexcept
        : mPtr(other.Detach())
    {
    }

    ~RefCountPtr()
    {
        Reset();
    }

    RefCountPtr& operator=(std::nullptr_t) noexcept
    {
        Reset();
        return *this;
    }

    RefCountPtr& operator=(T* other) noexcept
    {
        RefCountPtr(other).Swap(*this);
        return *this;
    }

    RefCountPtr& operator=(const RefCountPtr& other) noexcept
    {
        if (this != std::addressof(other))
        {
            RefCountPtr(other).Swap(*this);
        }

        return *this;
    }

    RefCountPtr& operator=(RefCountPtr&& other) noexcept
    {
        if (this != std::addressof(other))
        {
            Reset();
            mPtr = other.Detach();
        }

        return *this;
    }

    template <RefCountedObject U>
        requires (!std::same_as<U, T> && std::convertible_to<U*, T*>)
    RefCountPtr& operator=(U* other) noexcept
    {
        RefCountPtr(other).Swap(*this);
        return *this;
    }

    template <RefCountedObject U>
        requires (!std::same_as<U, T> && std::convertible_to<U*, T*>)
    RefCountPtr& operator=(const RefCountPtr<U>& other) noexcept
    {
        RefCountPtr(other).Swap(*this);
        return *this;
    }

    template <RefCountedObject U>
        requires (!std::same_as<U, T> && std::convertible_to<U*, T*>)
    RefCountPtr& operator=(RefCountPtr<U>&& other) noexcept
    {
        Reset();
        mPtr = other.Detach();
        return *this;
    }

    [[nodiscard]] T* Get() const noexcept
    {
        return mPtr;
    }

    [[nodiscard]] T** GetAddressOf() noexcept
    {
        return &mPtr;
    }

    [[nodiscard]] T* const* GetAddressOf() const noexcept
    {
        return &mPtr;
    }

    [[nodiscard]] T** ReleaseAndGetAddressOf() noexcept
    {
        Reset();
        return &mPtr;
    }

    unsigned long Reset() noexcept
    {
        T* other = mPtr;
        if (other == nullptr)
        {
            return 0;
        }

        mPtr = nullptr;
        return other->Release();
    }

    template <RefCountedObject U>
        requires std::convertible_to<U*, T*>
    void Attach(U* other) noexcept
    {
        T* next = other;
        if (mPtr == next)
        {
            return;
        }

        Reset();
        mPtr = next;
    }

    [[nodiscard]] T* Detach() noexcept
    {
        T* other = mPtr;
        mPtr = nullptr;
        return other;
    }

    void Swap(RefCountPtr& other) noexcept
    {
        std::swap(mPtr, other.mPtr);
    }

    [[nodiscard]] T* operator->() const noexcept
    {
        return mPtr;
    }

    operator T*() const noexcept
    {
        return mPtr;
    }

    explicit operator bool() const noexcept
    {
        return mPtr != nullptr;
    }

    [[nodiscard]] T** operator&() noexcept
    {
        assert(mPtr == nullptr && "Use ReleaseAndGetAddressOf before overwriting a non-empty RefCountPtr.");
        return &mPtr;
    }

    template <RefCountedObject U>
        requires std::convertible_to<U*, T*>
    [[nodiscard]] static RefCountPtr Create(U* other) noexcept
    {
        RefCountPtr result;
        result.Attach(other);
        return result;
    }

private:
    void InternalAddRef() noexcept
    {
        if (mPtr != nullptr)
        {
            mPtr->AddRef();
        }
    }

    T* mPtr{ nullptr };
};

template <RefCountedObject T>
void swap(RefCountPtr<T>& left, RefCountPtr<T>& right) noexcept
{
    left.Swap(right);
}

typedef RefCountPtr<IResource> ZWResourceHandle;

}

namespace std
{

template <typename T>
struct hash<HCommon::RefCountPtr<T>>
{
    size_t operator()(const HCommon::RefCountPtr<T>& value) const noexcept
    {
        return hash<T*>{}(value.Get());
    }
};

}
