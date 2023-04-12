#pragma once

#ifdef __APPLE__
#include "Ranges.h"
#include <compare>
#include <string_view>

namespace std
{
inline strong_ordering operator<=>(const string_view& lhs, const string_view& rhs)
{
    std::string lhsString(lhs);
    std::string rhsString(rhs);
    return lhsString <=> rhsString;
}
}  // namespace std

#endif