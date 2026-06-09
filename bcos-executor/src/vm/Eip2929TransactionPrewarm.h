/*
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @brief EIP-2929 transaction-entry warm set (W1 + CREATE + W2) shared by TE and executor.
 * @file Eip2929TransactionPrewarm.h
 */

#pragma once

#include "../CallParameters.h"
#include "Eip2929AccessState.h"
#include "Eip2929Util.h"
#include <evmc/evmc.h>
#include <optional>

namespace bcos::executor
{

/// Inputs for warming accessed_addresses at transaction entry (before EVM execution).
struct Eip2929TxPrewarmInput
{
    evmc_revision revision = EVMC_FRONTIER;
    evmc_address origin{};
    /// Callee for CALL; omitted for CREATE/CREATE2 (EIP-2929 W1).
    std::optional<evmc_address> callee;
    /// CREATE/CREATE2 contract address warmed at tx entry (even if init fails).
    std::optional<evmc_address> createCodeAddress;
    /// EIP-3651: block coinbase when revision >= Shanghai (optional until wired).
    std::optional<evmc_address> coinbase;
    uint8_t web3TypedTxKind = 0;
    /// Non-owning pointer to typed-tx access list (W2); may be empty vector.
    Eip2930AccessList const* accessList = nullptr;
};

/// Apply W1 (origin, callee, precompiles), optional coinbase/CREATE address, and W2 access list.
template <typename AddrConverter>
void warmEip2929AtTransactionEntry(
    Eip2929AccessState& state, Eip2929TxPrewarmInput const& input, AddrConverter&& toAddr)
{
    state.warmUpInitialTxSet(input.origin, input.callee, input.revision);

    if (input.coinbase.has_value())
    {
        (void)state.warmUpAddressNoJournal(*input.coinbase);
    }

    if (input.createCodeAddress.has_value())
    {
        (void)state.warmUpAddressNoJournal(*input.createCodeAddress);
    }

    if (input.web3TypedTxKind != 0 && input.accessList != nullptr && !input.accessList->empty())
    {
        state.warmUpAccessList(*input.accessList, std::forward<AddrConverter>(toAddr));
    }
}

/// Warm only EIP-2930/1559/4844 access-list entries (W2).
template <typename AddrConverter>
void warmEip2930AccessListOnly(Eip2929AccessState& state, uint8_t web3TypedTxKind,
    Eip2930AccessList const& accessList, AddrConverter&& toAddr)
{
    if (web3TypedTxKind == 0 || accessList.empty())
    {
        return;
    }
    state.warmUpAccessList(accessList, std::forward<AddrConverter>(toAddr));
}

}  // namespace bcos::executor
