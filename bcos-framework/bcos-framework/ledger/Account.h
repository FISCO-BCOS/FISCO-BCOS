#pragma once
#include "../storage/Entry.h"
#include "bcos-task/Trait.h"
#include <evmc/evmc.h>

namespace bcos::ledger::account
{
template <class AccountType>
concept Account = requires(AccountType account) {
    { account.exists() } -> task::IsAwaitableReturnValue<bool>;
    { account.create() } -> task::IsAwaitableReturnValue<void>;
    { account.code() } -> task::IsAwaitableReturnValue<std::optional<storage::Entry>>;
    {
        account.setCode(
            std::declval<bytes>(), std::declval<std::string>(), std::declval<crypto::HashType>())
    } -> task::IsAwaitableReturnValue<void>;
    { account.codeHash() } -> task::IsAwaitableReturnValue<h256>;
    { account.abi() } -> task::IsAwaitableReturnValue<std::optional<storage::Entry>>;
    { account.balance() } -> task::IsAwaitableReturnValue<u256>;
    { account.setBalance(std::declval<u256>()) } -> task::IsAwaitableReturnValue<void>;
    { account.nonce() } -> task::IsAwaitableReturnValue<std::optional<std::string>>;
    { account.setNonce(std::declval<std::string>()) } -> task::IsAwaitableReturnValue<void>;
    { account.storage(std::declval<evmc_bytes32>()) } -> task::IsAwaitableReturnValue<evmc_bytes32>;
    {
        account.setStorage(std::declval<evmc_bytes32>(), std::declval<evmc_bytes32>())
    } -> task::IsAwaitableReturnValue<void>;
    { account.path() } -> task::IsAwaitableReturnValue<std::string_view>;
    { account.address() } -> std::same_as<std::string_view>;
};
}  // namespace bcos::ledger::account