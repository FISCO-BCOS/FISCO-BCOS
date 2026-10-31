// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
// @file Try.h
// @brief RLP_TRY: Rust-? style early-return macro for RlpResult-returning
//        functions (collapses the take -> check -> propagate -> assign boilerplate).
#pragma once

#define RLP_TRY_DETAIL_CONCAT_INNER(x, y) x##y
#define RLP_TRY_DETAIL_CONCAT(x, y) RLP_TRY_DETAIL_CONCAT_INNER(x, y)

// Rust-? style early return for RlpResult-returning functions:
//   RLP_TRY(msg.field, take<uint64_t>(items));    // assign to an existing field
//   RLP_TRY(auto payload, takeListPayload(view)); // or declare a new variable
// Evaluates expr (an RlpResult<T>); on error returns std::unexpected(error) from the
// enclosing function, otherwise moves the value into `target`. Only for functions that
// themselves return RlpResult. One RLP_TRY per line (__LINE__ generates the temp name).
#define RLP_TRY(target, expr)                                                       \
    auto&& RLP_TRY_DETAIL_CONCAT(_rlp_try_, __LINE__) = (expr);                     \
    if (!RLP_TRY_DETAIL_CONCAT(_rlp_try_, __LINE__)) [[unlikely]]                   \
    {                                                                               \
        return std::unexpected(RLP_TRY_DETAIL_CONCAT(_rlp_try_, __LINE__).error()); \
    }                                                                               \
    target = std::move(*RLP_TRY_DETAIL_CONCAT(_rlp_try_, __LINE__))
