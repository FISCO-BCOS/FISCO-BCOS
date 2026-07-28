#pragma once
#include "../storage/Entry.h"
#include "bcos-task/Trait.h"
#include <bcos-utilities/FixedBytes.h>
#include <evmc/evmc.h>
#include <algorithm>
#include <iterator>

namespace bcos::ledger::account
{

/// The Account interface speaks evmc_bytes32 while storage and crypto speak h256, so every
/// implementation converts between the two. Shared here rather than re-privatised per class:
/// bcos-executor has its own toEvmC/fromEvmC (executor-private Common.h, and fromEvmC yields
/// u256 rather than h256), which layers below the executor cannot include.
inline h256 toH256(const evmc_bytes32& value)
{
    return h256{bytesConstRef{value.bytes, sizeof(value.bytes)}};
}

inline evmc_bytes32 toEvmcBytes32(h256 const& value)
{
    evmc_bytes32 out{};
    std::copy(value.begin(), value.end(), std::begin(out.bytes));
    return out;
}
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