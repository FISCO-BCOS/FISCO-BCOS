#pragma once

#include "Basic.h"
#include "Serialize.h"
#include "Hash.h"
#include <concepts>
#include <ranges>
#include <type_traits>

namespace bcos::concepts::transaction
{
template <class TransactionDataType>
concept TransactionData = requires(TransactionDataType transactionData)
{
    std::integral<decltype(transactionData.version)>;
    transactionData.chainID;
    transactionData.groupID;
    std::integral<decltype(transactionData.blockLimit)>;
    transactionData.nonce;
    transactionData.to;
    transactionData.input;
    transactionData.abi;
};

template <class TransactionType>
concept Transaction = requires(TransactionType transaction)
{
    bcos::concepts::hash::Hashable<TransactionType>;
    bcos::concepts::serialize::Serializable<TransactionType>;
    TransactionType{};
    TransactionData<decltype(transaction.data)>;
    transaction.dataHash;
    transaction.signature;
    transaction.sender;
    std::integral<decltype(transaction.importTime)>;
    std::integral<decltype(transaction.attribute)>;
    transaction.source;
};
}  // namespace bcos::concepts::transaction