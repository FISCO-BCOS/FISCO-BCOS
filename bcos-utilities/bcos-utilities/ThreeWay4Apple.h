#pragma once

#ifdef __APPLE__
#include "Common.h"
#include <string_view>
namespace std
{
constexpr strong_ordering operator<=>(const string_view& lhs, const string_view& rhs)
{
    return (lhs.compare(rhs)) <=> 0;
}
constexpr strong_ordering operator<=>(const bcos::bytes& lhs, const bcos::bytes& rhs)
{
    string_view lhsView((const char*)lhs.data(), lhs.size());
    string_view rhsView((const char*)rhs.data(), rhs.size());
    return lhsView <=> rhsView;
}
}  // namespace std
#endif
