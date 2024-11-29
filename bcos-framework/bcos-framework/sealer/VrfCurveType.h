#pragma once

#include <magic_enum.hpp>

namespace bcos::sealer
{
enum class VrfCurveType : uint8_t
{
    CURVE25519 = 0,
    SECKP256K1 = 1
};

}
