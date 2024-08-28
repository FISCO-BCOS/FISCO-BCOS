#pragma once

#ifdef __APPLE__
#include "Common.h"
#include <string_view>
namespace std
{
inline strong_ordering operator<=>(const string_view& lhs, const string_view& rhs)
{
    return (lhs.compare(rhs)) <=> 0;
}
inline strong_ordering operator<=>(const bcos::bytes& lhs, const bcos::bytes& rhs)
{
    string_view lhsView(reinterpret_cast<const char*>(lhs.data()), lhs.size());
    string_view rhsView(reinterpret_cast<const char*>(rhs.data()), rhs.size());
    return (lhsView.compare(rhsView)) <=> 0;
}
}  // namespace std
#endif
