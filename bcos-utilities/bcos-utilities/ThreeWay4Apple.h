#pragma once

#ifdef __APPLE__
#include "Common.h"
#include <bit>
#include <string_view>
namespace std
{
constexpr strong_ordering operator<=>(const string_view& lhs, const string_view& rhs)
{
    return (lhs.compare(rhs)) <=> 0;
}
constexpr strong_ordering operator<=>(const bcos::bytes& lhs, const bcos::bytes& rhs)
{
    string_view lhsView(std::bit_cast<const char*>(lhs.data()), lhs.size());
    string_view rhsView(std::bit_cast<const char*>(rhs.data()), rhs.size());
    return lhsView <=> rhsView;
}
}  // namespace std
#endif
