/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file Try.h
 * @brief RLP_TRY: Rust-? style early-return macro for RlpResult-returning
 *        functions (collapses the take -> check -> propagate -> assign boilerplate).
 * @date 2026/9/13
 */
#pragma once

#define RLP_TRY_DETAIL_CONCAT_INNER(x, y) x##y
#define RLP_TRY_DETAIL_CONCAT(x, y) RLP_TRY_DETAIL_CONCAT_INNER(x, y)

// Rust-? style early return for RlpResult-returning functions:
//   RLP_TRY(msg.field, takeUint(items));         // assign to an existing field
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
