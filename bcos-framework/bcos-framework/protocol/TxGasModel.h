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
 * @file TxGasModel.h
 * @brief Transaction cost model shared by execution and admission.
 * @date 2026/8/25
 */

// The intrinsic-gas formula and the per-transaction gas cap must be computed identically by the
// executor and by whatever admits a transaction into a pool. When the two drift, admission lets
// through transactions that execution then fails with OutOfGasLimit -- a block full of
// deterministically failing transactions. EIP-7623's min_cost and
// AUTHORIZATION_EMPTY_ACCOUNT_COST both move with hard forks, so a second copy would drift.
//
// Lives in bcos-framework (not in the executor) because the admission layer must not link
// ethereum-executor: EthereumTransition.h drags in EthereumHost.h / EthereumState.h / evmone,
// and neither txpool nor rpc can reach evmone today. What this header adds on top of what
// bcos-framework already carries is the evmc header alone, which is header-only -- no evmone,
// no intx.

#pragma once

#include "bcos-framework/protocol/Transaction.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <algorithm>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <optional>
#include <span>

namespace bcos::protocol
{

/// EIP-7825: the per-transaction gas cap enforced from Osaka onwards.
inline constexpr int64_t MAX_TX_GAS_LIMIT = 0x1000000;  // 2**24

/// EIP-7702: intrinsic cost charged per authorization-list entry.
inline constexpr int64_t AUTHORIZATION_EMPTY_ACCOUNT_COST = 25000;

/// Resolve the recipient of a bcos Transaction (Ethereum addresses are big-endian and
/// right-aligned). std::nullopt means contract creation.
///
/// A malformed `to` -- one that is neither empty nor a well-formed 20-byte address -- also
/// returns nullopt and is therefore priced and executed as a CREATE, where geth and evmone
/// reject it at decode. That collapse is the executor's long-standing behaviour and is left
/// unchanged here, because changing it would change consensus. What stands in front of it is
/// txpool::isValidToField, on both pool entry points: TxValidator.cpp for submissions and
/// MemoryStorage.cpp for the proposal path (issue #5318). Note it stands down entirely under
/// g_BCOSConfig.isWasm(), so this is a guard on the EVM chain modes, not an invariant.
///
/// Do not add a caller that reads this nullopt as "definitely empty".
inline std::optional<evmc::address> ethToAddress(Transaction const& tx)
{
    auto const& tb = tx.to();
    if (tb.empty())
        return std::nullopt;

    const bool has0x = tb.size() >= 2 && tb[0] == '0' && (tb[1] == 'x' || tb[1] == 'X');
    const bool is40Hex =
        tb.size() == sizeof(evmc_address) * 2 && std::all_of(tb.begin(), tb.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        });
    if (has0x || is40Hex)
    {
        // Hex-string form. Only a well-formed 20-byte address decodes to a valid recipient.
        if (auto decoded = safeFromHex(tb); decoded && decoded->size() == sizeof(evmc_address))
        {
            evmc::address a{};
            std::copy(decoded->begin(), decoded->end(), a.bytes);
            return a;
        }
        return std::nullopt;
    }
    if (tb.size() == sizeof(evmc_address))
    {
        // Defensive fallback for raw 20-byte addresses.
        evmc::address a{};
        std::copy_n(tb.begin(), sizeof(evmc_address), a.bytes);
        return a;
    }
    // Anything else (short raw bytes, malformed hex) is contract creation.
    return std::nullopt;
}

/// The cost model proper. Nested one level deeper than ethToAddress and the constants because
/// `num_words` / `TransactionCost` are names a caller would plausibly define itself, and
/// bcos::protocol is opened wholesale by `using namespace` across the tree.
namespace gas
{

constexpr int64_t num_words(size_t size_in_bytes) noexcept
{
    return static_cast<int64_t>((size_in_bytes + 31) / 32);
}

inline size_t compute_tx_data_tokens(evmc_revision rev, std::span<const uint8_t> data) noexcept
{
    const auto num_zero_bytes = static_cast<size_t>(std::ranges::count(data, 0));
    const auto num_nonzero_bytes = data.size() - num_zero_bytes;

    const size_t nonzero_byte_multiplier = rev >= EVMC_ISTANBUL ? 4 : 17;
    return (nonzero_byte_multiplier * num_nonzero_bytes) + num_zero_bytes;
}

inline int64_t compute_access_list_cost(const Web3AccessList& access_list) noexcept
{
    static constexpr auto ADDRESS_COST = 2400;
    static constexpr auto STORAGE_KEY_COST = 1900;

    int64_t cost = 0;
    for (const auto& entry : access_list)
        cost += ADDRESS_COST + static_cast<int64_t>(entry.storageKeys.size()) * STORAGE_KEY_COST;
    return cost;
}

struct TransactionCost
{
    int64_t intrinsic = 0;
    int64_t min = 0;
};

/// Compute the transaction intrinsic gas g0 (Yellow Paper, 6.2) and minimal gas (EIP-7623).
/// Ported from evmone state.cpp.
///
/// NOT noexcept, unlike the three helpers above: tx.authorizationList() is fail-loud by design
/// (TransactionImpl.cpp throws BadHexCharacter / InvalidAddress on a malformed entry rather than
/// skipping it, because EIP-7702 entries are consensus-critical). The tars authorizationList is
/// an unauthenticated mirror -- the signature binds only extraTransactionBytes -- so a peer can
/// choose that value, and declaring this function noexcept would turn a rejectable transaction
/// into std::terminate.
inline TransactionCost compute_tx_intrinsic_cost(evmc_revision rev, Transaction const& tx)
{
    static constexpr auto TX_BASE_COST = 21000;
    static constexpr auto TX_CREATE_COST = 32000;
    static constexpr auto DATA_TOKEN_COST = 4;
    static constexpr auto INITCODE_WORD_COST = 2;
    static constexpr auto TOTAL_COST_FLOOR_PER_TOKEN = 10;

    const auto is_create = !ethToAddress(tx).has_value();

    const auto create_cost = (is_create && rev >= EVMC_HOMESTEAD) ? TX_CREATE_COST : 0;

    const auto data = tx.input();
    const auto num_tokens = static_cast<int64_t>(
        compute_tx_data_tokens(rev, std::span<const uint8_t>{data.data(), data.size()}));
    const auto data_cost = num_tokens * DATA_TOKEN_COST;

    const auto access_list_cost = compute_access_list_cost(tx.web3AccessList());

    // evmone charges this with no gate, and can: its authorization_list is a decoded field that
    // only the type-4 RLP form populates, so non-empty implies set-code. That guarantee does not
    // exist here. authorizationList() reads the tars mirror, which the schema documents as
    // unauthenticated -- the signature binds only extraTransactionBytes -- and nothing clears it
    // before pricing, because validateTransaction inspects the list only under `case 4`. Ungated,
    // a peer hangs a list on a legacy or 1559 transaction and the node prices, and can reject
    // with IntrinsicGasTooLow, something geth charges nothing extra for.
    //
    // So restate evmone's precondition here instead of trusting a caller to have established it:
    // the same mirror the type rules already switch on, plus the revision that makes set-code
    // transactions exist. Both pipelines do reject a type-4 transaction before Prague on their
    // own -- EthereumTransition in the `case 4` arm, admission through Check::TypeByRevision,
    // which its evaluation order puts ahead of Check::IntrinsicGas -- but a header shared by two
    // pipelines should not depend on either one's ordering. Short-circuiting also keeps the
    // list from being materialised (and every r/s hex-parsed) for the transactions that cannot
    // carry one.
    static constexpr uint8_t SET_CODE_TX_KIND = 4;
    const auto auth_list_cost =
        (rev >= EVMC_PRAGUE && tx.web3TypedTxKind() == SET_CODE_TX_KIND) ?
            static_cast<int64_t>(tx.authorizationList().size()) * AUTHORIZATION_EMPTY_ACCOUNT_COST :
            0;

    const auto initcode_cost =
        (is_create && rev >= EVMC_SHANGHAI) ? INITCODE_WORD_COST * num_words(data.size()) : 0;

    const auto intrinsic_cost =
        TX_BASE_COST + create_cost + data_cost + access_list_cost + auth_list_cost + initcode_cost;

    // EIP-7623: Compute the minimum cost for the transaction. If disabled, just use 0.
    const auto min_cost =
        rev >= EVMC_PRAGUE ? TX_BASE_COST + num_tokens * TOTAL_COST_FLOOR_PER_TOKEN : 0;

    return {intrinsic_cost, min_cost};
}

}  // namespace gas
}  // namespace bcos::protocol
