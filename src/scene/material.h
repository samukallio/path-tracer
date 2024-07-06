#pragma once

#include "core/common.h"

enum texture_type
{
    TEXTURE_TYPE_RAW                    = 0,
    TEXTURE_TYPE_REFLECTANCE_WITH_ALPHA = 1,
    TEXTURE_TYPE_RADIANCE               = 2,
    TEXTURE_TYPE__COUNT                 = 3,
};

enum texture_flag : uint
{
    TEXTURE_FLAG_FILTER_NEAREST         = 1 << 0,
};

enum material_type
{
    MATERIAL_TYPE_OPENPBR               = 0,
};

inline char const* TextureTypeName(texture_type Type)
{
    switch (Type) {
    case TEXTURE_TYPE_RAW:                  return "Raw";
    case TEXTURE_TYPE_REFLECTANCE_WITH_ALPHA: return "Reflectance (with alpha)";
    case TEXTURE_TYPE_RADIANCE:             return "Radiance";
    }
    assert(false);
    return nullptr;
}

struct texture
{
    std::string                     Name                    = "New Texture";
    texture_type                    Type                    = TEXTURE_TYPE_RAW;
    bool                            EnableNearestFiltering  = false;

    uint32_t                        Width                   = 0;
    uint32_t                        Height                  = 0;
    glm::vec4 const*                Pixels                  = nullptr;

    uint32_t                        PackedTextureIndex      = 0;
};

struct material
{
    material_type                   Type                    = {};
    std::string                     Name                    = "New Material";
    uint32_t                        Flags                   = 0;

    float                           Opacity                 = 1.0f;

    uint32_t                        PackedMaterialIndex     = 0;

    virtual ~material() {}
};
