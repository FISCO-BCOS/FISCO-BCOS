#pragma once

#include <range/v3/view/any_view.hpp>
#include <ranges>

namespace std::ranges
{
template <typename Ref, ::ranges::category Cat, typename Enable>
inline constexpr bool enable_view<::ranges::any_view<Ref, Cat, Enable>> = true;
}
