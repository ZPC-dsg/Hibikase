#pragma once

#include <BackEnd/RHItypedefs.h>

#include <atomic>
#include <concepts>
#include <array>
#include <cassert>

namespace HCommon
{
struct ZWObject
{
    union {
        uint64_t integer;
        void* pointer;
    };

    ZWObject(uint64_t i) : integer(i) {}
    ZWObject(void* p) : pointer(p) {} 

    template<typename T> operator T* () const { return static_cast<T*>(pointer); }
};

class IResource
{
public:
    IResource() = default;
    IResource(const IResource&) = delete;
    IResource(IResource&&) = delete;
    IResource& operator=(const IResource&) = delete;
    IResource& operator=(IResource&&) = delete;

    virtual unsigned long AddRef() = 0;
    virtual unsigned long Release() = 0;
    virtual unsigned long GetRefCount() = 0;

    // Returns a native object or interface, for example ID3D11Device*, or nullptr if the requested interface is unavailable.
    // Does *not* AddRef the returned interface.
    virtual ZWObject GetNativeObject(HRHI::ObjectType objectType) { (void)objectType; return nullptr; }

protected:
    virtual ~IResource() = default;
};

template <typename T>
concept RefCountedObject = requires(T* object)
{
    { object->AddRef() } -> std::convertible_to<unsigned long>;
    { object->Release() } -> std::convertible_to<unsigned long>;
};

template <typename T>
concept InspectableRefCountedObject = RefCountedObject<T> && requires(T* object)
{
    { object->GetRefCount() } -> std::convertible_to<unsigned long>;
};

template <typename T>
concept ResourceInterface = std::derived_from<T, IResource>;

template <ResourceInterface T>
class RefCounter : public T
{
public:
    RefCounter() noexcept = default;
    RefCounter(const RefCounter&) = delete;
    RefCounter(RefCounter&&) = delete;
    RefCounter& operator=(const RefCounter&) = delete;
    RefCounter& operator=(RefCounter&&) = delete;

    unsigned long AddRef() override
    {
        return ++mRefCount;
    }

    unsigned long Release() override
    {
        const unsigned long refCount = --mRefCount;
        if (refCount == 0)
        {
            delete this;
        }

        return refCount;
    }

    unsigned long GetRefCount() override
    {
        return mRefCount.load(std::memory_order_acquire);
    }

protected:
    ~RefCounter() override = default;

private:
    std::atomic<unsigned long> mRefCount{ 1 };
};

template <typename T, uint32_t _max_elements>
struct StaticVector : private std::array<T, _max_elements>
{
    typedef std::array<T, _max_elements> base;
    enum { max_elements = _max_elements };

    using typename base::value_type;
    using typename base::size_type;
    using typename base::difference_type;
    using typename base::reference;
    using typename base::const_reference;
    using typename base::pointer;
    using typename base::const_pointer;
    using typename base::iterator;
    using typename base::const_iterator;
    // xxxnsubtil: reverse iterators not implemented

    StaticVector()
        : base()
        , current_size(0)
    {
    }

    StaticVector(size_t size)
        : base()
        , current_size(size)
    {
        assert(size <= max_elements);
    }

    StaticVector(std::initializer_list<T> il)
        : current_size(0)
    {
        for (auto i : il)
            push_back(i);
    }

    using base::at;

    reference operator[] (size_type pos)
    {
        assert(pos < current_size);
        return base::operator[](pos);
    }

    const_reference operator[] (size_type pos) const
    {
        assert(pos < current_size);
        return base::operator[](pos);
    }

    using base::front;

    reference back() noexcept { auto tmp = end(); --tmp; return *tmp; }
    const_reference back() const noexcept { auto tmp = cend(); --tmp; return *tmp; }

    using base::data;
    using base::begin;
    using base::cbegin;

    iterator end() noexcept { return iterator(begin()) + current_size; }
    const_iterator end() const noexcept { return cend(); }
    const_iterator cend() const noexcept { return const_iterator(cbegin()) + current_size; }

    bool empty() const noexcept { return current_size == 0; }
    size_t size() const noexcept { return current_size; }
    constexpr size_t max_size() const noexcept { return max_elements; }

    void fill(const T& value) noexcept
    {
        base::fill(value);
        current_size = max_elements;
    }

    void swap(StaticVector& other) noexcept
    {
        base::swap(*this);
        std::swap(current_size, other.current_size);
    }

    void push_back(const T& value) noexcept
    {
        assert(current_size < max_elements);
        *(data() + current_size) = value;
        current_size++;
    }

    void push_back(T&& value) noexcept
    {
        assert(current_size < max_elements);
        *(data() + current_size) = std::move(value);
        current_size++;
    }

    void pop_back() noexcept
    {
        assert(current_size > 0);
        current_size--;
    }

    void resize(size_type new_size) noexcept
    {
        assert(new_size <= max_elements);

        if (current_size > new_size)
        {
            for (size_type i = new_size; i < current_size; i++)
                *(data() + i) = T{};
        }
        else
        {
            for (size_type i = current_size; i < new_size; i++)
                *(data() + i) = T{};
        }

        current_size = new_size;
    }

    reference emplace_back() noexcept
    {
        assert(current_size < max_elements);
        ++current_size;
        back() = T{};
        return back();
    }

private:
    size_type current_size = 0;
};

}
