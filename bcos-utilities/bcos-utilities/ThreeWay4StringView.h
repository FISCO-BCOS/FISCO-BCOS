#pragma once

#ifdef __APPLE__
#include "Ranges.h"
#include <compare>
#include <string_view>

namespace std
{
inline strong_ordering operator<=>(const string_view& lhs, const string_view& rhs)
{
    if (lhs.size() == rhs.size())
    {
        if (RANGES::lexicographical_compare(lhs, rhs))
        {
            return strong_ordering::greater;
        }
        return strong_ordering::less;
    }
    return lhs.size() <=> rhs.size();
}
}  // namespace std

#endif