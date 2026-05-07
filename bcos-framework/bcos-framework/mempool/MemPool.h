#pragma once

#include "bcos-framework/protocol/Transaction.h"
#include <concepts>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <vector>

namespace bcos::mempool
{

template <class TransactionsType>
concept InputTransactions =
    ::ranges::input_range<TransactionsType> &&
    std::same_as<::ranges::range_value_t<TransactionsType>, protocol::Transaction::Ptr>;

template <class InputHashesType>
concept InputHashes =
    ::ranges::input_range<InputHashesType> &&
    std::same_as<::ranges::range_value_t<InputHashesType>, bcos::crypto::HashType>;

template <class SenderNonceTuple>
concept SenderNonce = requires(SenderNonceTuple senderNonce) {
    { std::get<0>(senderNonce) } -> std::convertible_to<std::string_view>;
    { std::get<1>(senderNonce) } -> std::convertible_to<int64_t>;
};

template <class SenderNoncesType>
concept SenderNonces = ::ranges::input_range<SenderNoncesType> &&
                       SenderNonce<::ranges::range_value_t<SenderNoncesType>>;

template <class Web3TransactionsType, class StateStorage>
concept MemPool = requires(Web3TransactionsType& transactions,
    std::vector<protocol::Transaction::Ptr> inputTransactions,
    std::vector<bcos::crypto::HashType> hashes, StateStorage& state,
    std::back_insert_iterator<std::vector<protocol::Transaction::Ptr>> out, int64_t limit) {
    { transactions.add(inputTransactions) } -> std::same_as<void>;

    { transactions.seal(limit, state, out) } -> std::same_as<void>;

    { transactions.remove(state) } -> std::same_as<void>;

    { transactions.remove(hashes) } -> std::same_as<void>;

    { transactions.get(hashes) } -> std::same_as<std::vector<protocol::Transaction::Ptr>>;
};

}  // namespace bcos::mempool