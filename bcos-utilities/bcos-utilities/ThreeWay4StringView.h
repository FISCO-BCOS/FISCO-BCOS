#pragma once

#ifdef __APPLE__
#include <string_view>

namespace std
{
inline strong_ordering operator<=>(const string_view& lhs, const string_view& rhs)
{
    return (lhs.compare(rhs)) <=> 0;
}
}  // namespace std

#endif