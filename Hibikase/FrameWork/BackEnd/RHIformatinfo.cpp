#include <cassert>

#include <BackEnd/d3d12backend.h>

namespace HRHI
{
    // Format mapping table. The rows must be in the exactly same order as Format enum members are defined.
    static const ZWFormatInfo sFormatInfo[] = {
        //        format                   name             bytes blk         kind               red   green   blue  alpha  depth  stencl signed  srgb
            { EFormat::UNKNOWN,           "UNKNOWN",           0,   0, EFormatKind::Integer,      false, false, false, false, false, false, false, false },
            { EFormat::R8_UINT,           "R8_UINT",           1,   1, EFormatKind::Integer,      true,  false, false, false, false, false, false, false },
            { EFormat::R8_SINT,           "R8_SINT",           1,   1, EFormatKind::Integer,      true,  false, false, false, false, false, true,  false },
            { EFormat::R8_UNORM,          "R8_UNORM",          1,   1, EFormatKind::Normalized,   true,  false, false, false, false, false, false, false },
            { EFormat::R8_SNORM,          "R8_SNORM",          1,   1, EFormatKind::Normalized,   true,  false, false, false, false, false, true,  false },
            { EFormat::RG8_UINT,          "RG8_UINT",          2,   1, EFormatKind::Integer,      true,  true,  false, false, false, false, false, false },
            { EFormat::RG8_SINT,          "RG8_SINT",          2,   1, EFormatKind::Integer,      true,  true,  false, false, false, false, true,  false },
            { EFormat::RG8_UNORM,         "RG8_UNORM",         2,   1, EFormatKind::Normalized,   true,  true,  false, false, false, false, false, false },
            { EFormat::RG8_SNORM,         "RG8_SNORM",         2,   1, EFormatKind::Normalized,   true,  true,  false, false, false, false, true,  false },
            { EFormat::R16_UINT,          "R16_UINT",          2,   1, EFormatKind::Integer,      true,  false, false, false, false, false, false, false },
            { EFormat::R16_SINT,          "R16_SINT",          2,   1, EFormatKind::Integer,      true,  false, false, false, false, false, true,  false },
            { EFormat::R16_UNORM,         "R16_UNORM",         2,   1, EFormatKind::Normalized,   true,  false, false, false, false, false, false, false },
            { EFormat::R16_SNORM,         "R16_SNORM",         2,   1, EFormatKind::Normalized,   true,  false, false, false, false, false, true,  false },
            { EFormat::R16_FLOAT,         "R16_FLOAT",         2,   1, EFormatKind::Float,        true,  false, false, false, false, false, true,  false },
            { EFormat::BGRA4_UNORM,       "BGRA4_UNORM",       2,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
            { EFormat::B5G6R5_UNORM,      "B5G6R5_UNORM",      2,   1, EFormatKind::Normalized,   true,  true,  true,  false, false, false, false, false },
            { EFormat::B5G5R5A1_UNORM,    "B5G5R5A1_UNORM",    2,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
            { EFormat::RGBA8_UINT,        "RGBA8_UINT",        4,   1, EFormatKind::Integer,      true,  true,  true,  true,  false, false, false, false },
            { EFormat::RGBA8_SINT,        "RGBA8_SINT",        4,   1, EFormatKind::Integer,      true,  true,  true,  true,  false, false, true,  false },
            { EFormat::RGBA8_UNORM,       "RGBA8_UNORM",       4,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
            { EFormat::RGBA8_SNORM,       "RGBA8_SNORM",       4,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, true,  false },
            { EFormat::BGRA8_UNORM,       "BGRA8_UNORM",       4,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
            { EFormat::BGRX8_UNORM,       "BGRX8_UNORM",       4,   1, EFormatKind::Normalized,   true,  true,  true,  false, false, false, false, false },
            { EFormat::SRGBA8_UNORM,      "SRGBA8_UNORM",      4,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, true  },
            { EFormat::SBGRA8_UNORM,      "SBGRA8_UNORM",      4,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, true  },
            { EFormat::SBGRX8_UNORM,      "SBGRX8_UNORM",      4,   1, EFormatKind::Normalized,   true,  true,  true,  false, false, false, false, true  },
            { EFormat::R10G10B10A2_UNORM, "R10G10B10A2_UNORM", 4,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
            { EFormat::R11G11B10_FLOAT,   "R11G11B10_FLOAT",   4,   1, EFormatKind::Float,        true,  true,  true,  false, false, false, false, false },
            { EFormat::RG16_UINT,         "RG16_UINT",         4,   1, EFormatKind::Integer,      true,  true,  false, false, false, false, false, false },
            { EFormat::RG16_SINT,         "RG16_SINT",         4,   1, EFormatKind::Integer,      true,  true,  false, false, false, false, true,  false },
            { EFormat::RG16_UNORM,        "RG16_UNORM",        4,   1, EFormatKind::Normalized,   true,  true,  false, false, false, false, false, false },
            { EFormat::RG16_SNORM,        "RG16_SNORM",        4,   1, EFormatKind::Normalized,   true,  true,  false, false, false, false, true,  false },
            { EFormat::RG16_FLOAT,        "RG16_FLOAT",        4,   1, EFormatKind::Float,        true,  true,  false, false, false, false, true,  false },
            { EFormat::R32_UINT,          "R32_UINT",          4,   1, EFormatKind::Integer,      true,  false, false, false, false, false, false, false },
            { EFormat::R32_SINT,          "R32_SINT",          4,   1, EFormatKind::Integer,      true,  false, false, false, false, false, true,  false },
            { EFormat::R32_FLOAT,         "R32_FLOAT",         4,   1, EFormatKind::Float,        true,  false, false, false, false, false, true,  false },
            { EFormat::RGBA16_UINT,       "RGBA16_UINT",       8,   1, EFormatKind::Integer,      true,  true,  true,  true,  false, false, false, false },
            { EFormat::RGBA16_SINT,       "RGBA16_SINT",       8,   1, EFormatKind::Integer,      true,  true,  true,  true,  false, false, true,  false },
            { EFormat::RGBA16_FLOAT,      "RGBA16_FLOAT",      8,   1, EFormatKind::Float,        true,  true,  true,  true,  false, false, true,  false },
            { EFormat::RGBA16_UNORM,      "RGBA16_UNORM",      8,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
            { EFormat::RGBA16_SNORM,      "RGBA16_SNORM",      8,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, true,  false },
            { EFormat::RG32_UINT,         "RG32_UINT",         8,   1, EFormatKind::Integer,      true,  true,  false, false, false, false, false, false },
            { EFormat::RG32_SINT,         "RG32_SINT",         8,   1, EFormatKind::Integer,      true,  true,  false, false, false, false, true,  false },
            { EFormat::RG32_FLOAT,        "RG32_FLOAT",        8,   1, EFormatKind::Float,        true,  true,  false, false, false, false, true,  false },
            { EFormat::RGB32_UINT,        "RGB32_UINT",        12,  1, EFormatKind::Integer,      true,  true,  true,  false, false, false, false, false },
            { EFormat::RGB32_SINT,        "RGB32_SINT",        12,  1, EFormatKind::Integer,      true,  true,  true,  false, false, false, true,  false },
            { EFormat::RGB32_FLOAT,       "RGB32_FLOAT",       12,  1, EFormatKind::Float,        true,  true,  true,  false, false, false, true,  false },
            { EFormat::RGBA32_UINT,       "RGBA32_UINT",       16,  1, EFormatKind::Integer,      true,  true,  true,  true,  false, false, false, false },
            { EFormat::RGBA32_SINT,       "RGBA32_SINT",       16,  1, EFormatKind::Integer,      true,  true,  true,  true,  false, false, true,  false },
            { EFormat::RGBA32_FLOAT,      "RGBA32_FLOAT",      16,  1, EFormatKind::Float,        true,  true,  true,  true,  false, false, true,  false },
            { EFormat::D16,               "D16",               2,   1, EFormatKind::DepthStencil, false, false, false, false, true,  false, false, false },
            { EFormat::D24S8,             "D24S8",             4,   1, EFormatKind::DepthStencil, false, false, false, false, true,  true,  false, false },
            { EFormat::X24G8_UINT,        "X24G8_UINT",        4,   1, EFormatKind::Integer,      false, false, false, false, false, true,  false, false },
            { EFormat::D32,               "D32",               4,   1, EFormatKind::DepthStencil, false, false, false, false, true,  false, false, false },
            { EFormat::D32S8,             "D32S8",             8,   1, EFormatKind::DepthStencil, false, false, false, false, true,  true,  false, false },
            { EFormat::X32G8_UINT,        "X32G8_UINT",        8,   1, EFormatKind::Integer,      false, false, false, false, false, true,  false, false },
            { EFormat::BC1_UNORM,         "BC1_UNORM",         8,   4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
            { EFormat::BC1_UNORM_SRGB,    "BC1_UNORM_SRGB",    8,   4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, true  },
            { EFormat::BC2_UNORM,         "BC2_UNORM",         16,  4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
            { EFormat::BC2_UNORM_SRGB,    "BC2_UNORM_SRGB",    16,  4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, true  },
            { EFormat::BC3_UNORM,         "BC3_UNORM",         16,  4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
            { EFormat::BC3_UNORM_SRGB,    "BC3_UNORM_SRGB",    16,  4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, true  },
            { EFormat::BC4_UNORM,         "BC4_UNORM",         8,   4, EFormatKind::Normalized,   true,  false, false, false, false, false, false, false },
            { EFormat::BC4_SNORM,         "BC4_SNORM",         8,   4, EFormatKind::Normalized,   true,  false, false, false, false, false, true,  false },
            { EFormat::BC5_UNORM,         "BC5_UNORM",         16,  4, EFormatKind::Normalized,   true,  true,  false, false, false, false, false, false },
            { EFormat::BC5_SNORM,         "BC5_SNORM",         16,  4, EFormatKind::Normalized,   true,  true,  false, false, false, false, true,  false },
            { EFormat::BC6H_UFLOAT,       "BC6H_UFLOAT",       16,  4, EFormatKind::Float,        true,  true,  true,  false, false, false, false, false },
            { EFormat::BC6H_SFLOAT,       "BC6H_SFLOAT",       16,  4, EFormatKind::Float,        true,  true,  true,  false, false, false, true,  false },
            { EFormat::BC7_UNORM,         "BC7_UNORM",         16,  4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
            { EFormat::BC7_UNORM_SRGB,    "BC7_UNORM_SRGB",    16,  4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, true  },
    };

    const ZWFormatInfo& GetFormatInfo(EFormat format)
    {
        static_assert(sizeof(sFormatInfo) / sizeof(ZWFormatInfo) == size_t(EFormat::COUNT),
            "The format info table doesn't have the right number of elements");

        if (uint32_t(format) >= uint32_t(EFormat::COUNT))
            return sFormatInfo[0]; // UNKNOWN

        const ZWFormatInfo& info = sFormatInfo[uint32_t(format)];
        assert(info.format == format);
        return info;
    }

    // Format mapping table. The rows must be in the exactly same order as Format enum members are defined.
    static const HD3D12::ZWDxgiFormatMapping sFormatMappings[] = {
        { EFormat::UNKNOWN,              DXGI_FORMAT_UNKNOWN,                DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN                },

        { EFormat::R8_UINT,              DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_UINT,                  DXGI_FORMAT_R8_UINT                },
        { EFormat::R8_SINT,              DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_SINT,                  DXGI_FORMAT_R8_SINT                },
        { EFormat::R8_UNORM,             DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_UNORM,                 DXGI_FORMAT_R8_UNORM               },
        { EFormat::R8_SNORM,             DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_SNORM,                 DXGI_FORMAT_R8_SNORM               },
        { EFormat::RG8_UINT,             DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_UINT,                DXGI_FORMAT_R8G8_UINT              },
        { EFormat::RG8_SINT,             DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_SINT,                DXGI_FORMAT_R8G8_SINT              },
        { EFormat::RG8_UNORM,            DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_UNORM,               DXGI_FORMAT_R8G8_UNORM             },
        { EFormat::RG8_SNORM,            DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_SNORM,               DXGI_FORMAT_R8G8_SNORM             },
        { EFormat::R16_UINT,             DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_UINT,                 DXGI_FORMAT_R16_UINT               },
        { EFormat::R16_SINT,             DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_SINT,                 DXGI_FORMAT_R16_SINT               },
        { EFormat::R16_UNORM,            DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_UNORM,                DXGI_FORMAT_R16_UNORM              },
        { EFormat::R16_SNORM,            DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_SNORM,                DXGI_FORMAT_R16_SNORM              },
        { EFormat::R16_FLOAT,            DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_FLOAT,                DXGI_FORMAT_R16_FLOAT              },
        { EFormat::BGRA4_UNORM,          DXGI_FORMAT_B4G4R4A4_UNORM,         DXGI_FORMAT_B4G4R4A4_UNORM,           DXGI_FORMAT_B4G4R4A4_UNORM         },
        { EFormat::B5G6R5_UNORM,         DXGI_FORMAT_B5G6R5_UNORM,           DXGI_FORMAT_B5G6R5_UNORM,             DXGI_FORMAT_B5G6R5_UNORM           },
        { EFormat::B5G5R5A1_UNORM,       DXGI_FORMAT_B5G5R5A1_UNORM,         DXGI_FORMAT_B5G5R5A1_UNORM,           DXGI_FORMAT_B5G5R5A1_UNORM         },
        { EFormat::RGBA8_UINT,           DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_UINT,            DXGI_FORMAT_R8G8B8A8_UINT          },
        { EFormat::RGBA8_SINT,           DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_SINT,            DXGI_FORMAT_R8G8B8A8_SINT          },
        { EFormat::RGBA8_UNORM,          DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_UNORM,           DXGI_FORMAT_R8G8B8A8_UNORM         },
        { EFormat::RGBA8_SNORM,          DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_SNORM,           DXGI_FORMAT_R8G8B8A8_SNORM         },
        { EFormat::BGRA8_UNORM,          DXGI_FORMAT_B8G8R8A8_TYPELESS,      DXGI_FORMAT_B8G8R8A8_UNORM,           DXGI_FORMAT_B8G8R8A8_UNORM         },
        { EFormat::BGRX8_UNORM,          DXGI_FORMAT_B8G8R8X8_TYPELESS,      DXGI_FORMAT_B8G8R8X8_UNORM,           DXGI_FORMAT_B8G8R8X8_UNORM         },
        { EFormat::SRGBA8_UNORM,         DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,      DXGI_FORMAT_R8G8B8A8_UNORM_SRGB    },
        { EFormat::SBGRA8_UNORM,         DXGI_FORMAT_B8G8R8A8_TYPELESS,      DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,      DXGI_FORMAT_B8G8R8A8_UNORM_SRGB    },
        { EFormat::SBGRX8_UNORM,         DXGI_FORMAT_B8G8R8X8_TYPELESS,      DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,      DXGI_FORMAT_B8G8R8X8_UNORM_SRGB    },
        { EFormat::R10G10B10A2_UNORM,    DXGI_FORMAT_R10G10B10A2_TYPELESS,   DXGI_FORMAT_R10G10B10A2_UNORM,        DXGI_FORMAT_R10G10B10A2_UNORM      },
        { EFormat::R11G11B10_FLOAT,      DXGI_FORMAT_R11G11B10_FLOAT,        DXGI_FORMAT_R11G11B10_FLOAT,          DXGI_FORMAT_R11G11B10_FLOAT        },
        { EFormat::RG16_UINT,            DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_UINT,              DXGI_FORMAT_R16G16_UINT            },
        { EFormat::RG16_SINT,            DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_SINT,              DXGI_FORMAT_R16G16_SINT            },
        { EFormat::RG16_UNORM,           DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_UNORM,             DXGI_FORMAT_R16G16_UNORM           },
        { EFormat::RG16_SNORM,           DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_SNORM,             DXGI_FORMAT_R16G16_SNORM           },
        { EFormat::RG16_FLOAT,           DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_FLOAT,             DXGI_FORMAT_R16G16_FLOAT           },
        { EFormat::R32_UINT,             DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_UINT,                 DXGI_FORMAT_R32_UINT               },
        { EFormat::R32_SINT,             DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_SINT,                 DXGI_FORMAT_R32_SINT               },
        { EFormat::R32_FLOAT,            DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_FLOAT,                DXGI_FORMAT_R32_FLOAT              },
        { EFormat::RGBA16_UINT,          DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_UINT,        DXGI_FORMAT_R16G16B16A16_UINT      },
        { EFormat::RGBA16_SINT,          DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_SINT,        DXGI_FORMAT_R16G16B16A16_SINT      },
        { EFormat::RGBA16_FLOAT,         DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_FLOAT,       DXGI_FORMAT_R16G16B16A16_FLOAT     },
        { EFormat::RGBA16_UNORM,         DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_UNORM,       DXGI_FORMAT_R16G16B16A16_UNORM     },
        { EFormat::RGBA16_SNORM,         DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_SNORM,       DXGI_FORMAT_R16G16B16A16_SNORM     },
        { EFormat::RG32_UINT,            DXGI_FORMAT_R32G32_TYPELESS,        DXGI_FORMAT_R32G32_UINT,              DXGI_FORMAT_R32G32_UINT            },
        { EFormat::RG32_SINT,            DXGI_FORMAT_R32G32_TYPELESS,        DXGI_FORMAT_R32G32_SINT,              DXGI_FORMAT_R32G32_SINT            },
        { EFormat::RG32_FLOAT,           DXGI_FORMAT_R32G32_TYPELESS,        DXGI_FORMAT_R32G32_FLOAT,             DXGI_FORMAT_R32G32_FLOAT           },
        { EFormat::RGB32_UINT,           DXGI_FORMAT_R32G32B32_TYPELESS,     DXGI_FORMAT_R32G32B32_UINT,           DXGI_FORMAT_R32G32B32_UINT         },
        { EFormat::RGB32_SINT,           DXGI_FORMAT_R32G32B32_TYPELESS,     DXGI_FORMAT_R32G32B32_SINT,           DXGI_FORMAT_R32G32B32_SINT         },
        { EFormat::RGB32_FLOAT,          DXGI_FORMAT_R32G32B32_TYPELESS,     DXGI_FORMAT_R32G32B32_FLOAT,          DXGI_FORMAT_R32G32B32_FLOAT        },
        { EFormat::RGBA32_UINT,          DXGI_FORMAT_R32G32B32A32_TYPELESS,  DXGI_FORMAT_R32G32B32A32_UINT,        DXGI_FORMAT_R32G32B32A32_UINT      },
        { EFormat::RGBA32_SINT,          DXGI_FORMAT_R32G32B32A32_TYPELESS,  DXGI_FORMAT_R32G32B32A32_SINT,        DXGI_FORMAT_R32G32B32A32_SINT      },
        { EFormat::RGBA32_FLOAT,         DXGI_FORMAT_R32G32B32A32_TYPELESS,  DXGI_FORMAT_R32G32B32A32_FLOAT,       DXGI_FORMAT_R32G32B32A32_FLOAT     },

        { EFormat::D16,                  DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_UNORM,                DXGI_FORMAT_D16_UNORM              },
        { EFormat::D24S8,                DXGI_FORMAT_R24G8_TYPELESS,         DXGI_FORMAT_R24_UNORM_X8_TYPELESS,    DXGI_FORMAT_D24_UNORM_S8_UINT      },
        { EFormat::X24G8_UINT,           DXGI_FORMAT_R24G8_TYPELESS,         DXGI_FORMAT_X24_TYPELESS_G8_UINT,     DXGI_FORMAT_D24_UNORM_S8_UINT      },
        { EFormat::D32,                  DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_FLOAT,                DXGI_FORMAT_D32_FLOAT              },
        { EFormat::D32S8,                DXGI_FORMAT_R32G8X24_TYPELESS,      DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT   },
        { EFormat::X32G8_UINT,           DXGI_FORMAT_R32G8X24_TYPELESS,      DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,  DXGI_FORMAT_D32_FLOAT_S8X24_UINT   },

        { EFormat::BC1_UNORM,            DXGI_FORMAT_BC1_TYPELESS,           DXGI_FORMAT_BC1_UNORM,                DXGI_FORMAT_BC1_UNORM              },
        { EFormat::BC1_UNORM_SRGB,       DXGI_FORMAT_BC1_TYPELESS,           DXGI_FORMAT_BC1_UNORM_SRGB,           DXGI_FORMAT_BC1_UNORM_SRGB         },
        { EFormat::BC2_UNORM,            DXGI_FORMAT_BC2_TYPELESS,           DXGI_FORMAT_BC2_UNORM,                DXGI_FORMAT_BC2_UNORM              },
        { EFormat::BC2_UNORM_SRGB,       DXGI_FORMAT_BC2_TYPELESS,           DXGI_FORMAT_BC2_UNORM_SRGB,           DXGI_FORMAT_BC2_UNORM_SRGB         },
        { EFormat::BC3_UNORM,            DXGI_FORMAT_BC3_TYPELESS,           DXGI_FORMAT_BC3_UNORM,                DXGI_FORMAT_BC3_UNORM              },
        { EFormat::BC3_UNORM_SRGB,       DXGI_FORMAT_BC3_TYPELESS,           DXGI_FORMAT_BC3_UNORM_SRGB,           DXGI_FORMAT_BC3_UNORM_SRGB         },
        { EFormat::BC4_UNORM,            DXGI_FORMAT_BC4_TYPELESS,           DXGI_FORMAT_BC4_UNORM,                DXGI_FORMAT_BC4_UNORM              },
        { EFormat::BC4_SNORM,            DXGI_FORMAT_BC4_TYPELESS,           DXGI_FORMAT_BC4_SNORM,                DXGI_FORMAT_BC4_SNORM              },
        { EFormat::BC5_UNORM,            DXGI_FORMAT_BC5_TYPELESS,           DXGI_FORMAT_BC5_UNORM,                DXGI_FORMAT_BC5_UNORM              },
        { EFormat::BC5_SNORM,            DXGI_FORMAT_BC5_TYPELESS,           DXGI_FORMAT_BC5_SNORM,                DXGI_FORMAT_BC5_SNORM              },
        { EFormat::BC6H_UFLOAT,          DXGI_FORMAT_BC6H_TYPELESS,          DXGI_FORMAT_BC6H_UF16,                DXGI_FORMAT_BC6H_UF16              },
        { EFormat::BC6H_SFLOAT,          DXGI_FORMAT_BC6H_TYPELESS,          DXGI_FORMAT_BC6H_SF16,                DXGI_FORMAT_BC6H_SF16              },
        { EFormat::BC7_UNORM,            DXGI_FORMAT_BC7_TYPELESS,           DXGI_FORMAT_BC7_UNORM,                DXGI_FORMAT_BC7_UNORM              },
        { EFormat::BC7_UNORM_SRGB,       DXGI_FORMAT_BC7_TYPELESS,           DXGI_FORMAT_BC7_UNORM_SRGB,           DXGI_FORMAT_BC7_UNORM_SRGB         },
    };

    const HD3D12::ZWDxgiFormatMapping& HD3D12::GetDxgiFormatMapping(EFormat abstractFormat)
    {
        static_assert(sizeof(sFormatMappings) / sizeof(HD3D12::ZWDxgiFormatMapping) == size_t(EFormat::COUNT),
            "The format mapping table doesn't have the right number of elements");

        const HD3D12::ZWDxgiFormatMapping& mapping = sFormatMappings[uint32_t(abstractFormat)];
        assert(mapping.abstractFormat == abstractFormat);
        return mapping;
    }
}
