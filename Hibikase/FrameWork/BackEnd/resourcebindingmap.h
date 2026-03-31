#pragma once

#include <Common/resource.h>
#include <BackEnd/RHIinterface.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <tsl/robin_map.h>

namespace HRHI {

    // describes a texture binding --- used to manage SRV / VkImageView per texture
    struct ZWTextureBindingKey : public ZWTextureSubresourceSet
    {
        EFormat format;
        bool isReadOnlyDSV;

        ZWTextureBindingKey() {}

        ZWTextureBindingKey(const ZWTextureSubresourceSet& b, EFormat _format, bool _isReadOnlyDSV = false)
            : ZWTextureSubresourceSet(b)
            , format(_format)
            , isReadOnlyDSV(_isReadOnlyDSV)
        {
        }

        bool operator== (const ZWTextureBindingKey& other) const
        {
            return format == other.format &&
                static_cast<const ZWTextureSubresourceSet&>(*this) == static_cast<const ZWTextureSubresourceSet&>(other) &&
                isReadOnlyDSV == other.isReadOnlyDSV;
        }
    };

    template <typename T>
    using ZWTextureBindingKey_HashMap = tsl::robin_map<ZWTextureBindingKey, T>;

    struct ZWBufferBindingKey : public ZWBufferRange
    {
        EFormat format;
        EResourceType type;

        ZWBufferBindingKey() {}

        ZWBufferBindingKey(const ZWBufferRange& range, EFormat _format, EResourceType _type)
            : ZWBufferRange(range)
            , format(_format)
            , type(_type)
        {
        }

        bool operator== (const ZWBufferBindingKey& other) const
        {
            return format == other.format &&
                type == other.type &&
                static_cast<const ZWBufferRange&>(*this) == static_cast<const ZWBufferRange&>(other);
        }
    };

}

namespace std
{
    template<> struct hash<HRHI::ZWTextureBindingKey>
    {
        std::size_t operator()(HRHI::ZWTextureBindingKey const& s) const noexcept
        {
            return std::hash<HRHI::EFormat>()(s.format)
                ^ std::hash<HRHI::ZWTextureSubresourceSet>()(s)
                ^ std::hash<bool>()(s.isReadOnlyDSV);
        }
    };

    template<> struct hash<HRHI::ZWBufferBindingKey>
    {
        std::size_t operator()(HRHI::ZWBufferBindingKey const& s) const noexcept
        {
            return std::hash<HRHI::EFormat>()(s.format)
                ^ std::hash<HRHI::ZWBufferRange>()(s);
        }
    };
}
