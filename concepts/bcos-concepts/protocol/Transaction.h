#pragma once

#include "../Basic.h"
#include "../Hash.h"
#include "../Serialize.h"
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-utilities/Ranges.h>
#include <concepts>
#include <type_traits>

namespace bcos::concepts::transaction
{
template <class TransactionDataType>
concept TransactionData = requires(TransactionDataType transactionData)
{
    requires std::integral<decltype(transactionData.version)>;
    transactionData.chainID;
    transactionData.groupID;
    requires std::integral<decltype(transactionData.blockLimit)>;
    transactionData.nonce;
    transactionData.to;
    transactionData.input;
    transactionData.abi;
};

template <class TransactionType>
concept Transaction = requires(TransactionType transaction)
{
    TransactionType{};
    requires TransactionData<decltype(transaction.data)>;
    transaction.dataHash;
    transaction.signature;
    transaction.sender;
    requires std::integral<decltype(transaction.importTime)>;
    requires std::integral<decltype(transaction.attribute)>;
};

// template <class TransactionType>
// concept Transaction = std::derived_from<TransactionType, bcos::protocol::Transaction>;
}  // namespace bcos::concepts::transaction