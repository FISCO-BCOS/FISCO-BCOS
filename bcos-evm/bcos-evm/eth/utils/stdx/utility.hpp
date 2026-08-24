// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2023 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <type_traits>

namespace stdx
{
template <typename EnumT>
constexpr auto to_underlying(EnumT e) noexcept
{
    return static_cast<std::underlying_type_t<EnumT>>(e);
}
}  // namespace stdx
