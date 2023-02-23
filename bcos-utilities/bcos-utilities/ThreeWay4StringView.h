#pragma once

#ifdef __APPLE__
#include "Ranges.h"
#include <compare>
#include <string_view>

namespace std
{
inline weak_ordering operator<=>(const string_view& lhs, const string_view& rhs)
{
    auto sizeComp = lhs.size() <=> rhs.size();
    if (sizeComp != weak_ordering::equivalent)
    {
        return sizeComp;
    }

    if (RANGES::lexicographical_compare(lhs, rhs))
    {
        return weak_ordering::greater;
    }
    return weak_ordering::less;
}
}  // namespace std

#endif