#pragma once
#include "Basic.h"
#include "Hash.h"
#include "Serialize.h"

namespace bcos::concepts::receipt
{
template <class LogEntryType>
concept LogEntry = requires(LogEntryType logEntry) {
                       logEntry.address;
                       std::ranges::range<decltype(logEntry.topic)>;
                       logEntry.data;
                   };

template <class TransactionReceiptDataType>
concept TransactionReceiptData =
    requires(TransactionReceiptDataType transactionReceiptData) {
        std::integral<decltype(transactionReceiptData.version)>;
        transactionReceiptData.gasUsed;
        transactionReceiptData.contractAddress;
        std::integral<decltype(transactionReceiptData.status)>;
        transactionReceiptData.output;
        requires std::ranges::range<decltype(transactionReceiptData.logEntries)> &&
                     LogEntry<
                         std::ranges::range_value_t<decltype(transactionReceiptData.logEntries)>>;
        std::integral<decltype(transactionReceiptData.blockNumber)>;
    };

template <class TransactionReceiptType>
concept TransactionReceipt = requires(TransactionReceiptType transactionReceipt) {
                                 bcos::concepts::hash::Hashable<TransactionReceiptType>;
                                 bcos::concepts::serialize::Serializable<TransactionReceiptType>;
                                 TransactionReceiptData<decltype(transactionReceipt.data)>;
                                 transactionReceipt.dataHash;
                                 transactionReceipt.message;
                             };
}  // namespace bcos::concepts::receipt