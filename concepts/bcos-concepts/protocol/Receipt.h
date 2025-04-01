#pragma once
#include "../Basic.h"
#include "../Hash.h"
#include "../Serialize.h"
#include <range/v3/range.hpp>

namespace bcos::concepts::receipt
{
template <class LogEntryType>
concept LogEntry = requires(LogEntryType logEntry)
{
    logEntry.address;
    requires ::ranges::range<decltype(logEntry.topic)>;
    logEntry.data;
};

template <class TransactionReceiptDataType>
concept TransactionReceiptData = requires(TransactionReceiptDataType transactionReceiptData)
{
    requires std::integral<decltype(transactionReceiptData.version)>;
    transactionReceiptData.gasUsed;
    transactionReceiptData.contractAddress;
    requires std::integral<decltype(transactionReceiptData.status)>;
    transactionReceiptData.output;
    requires ::ranges::range<decltype(transactionReceiptData.logEntries)> &&
        LogEntry<::ranges::range_value_t<decltype(transactionReceiptData.logEntries)>>;
    requires std::integral<decltype(transactionReceiptData.blockNumber)>;
};

template <class TransactionReceiptType>
concept TransactionReceipt = requires(TransactionReceiptType transactionReceipt)
{
    requires TransactionReceiptData<decltype(transactionReceipt.data)>;
    transactionReceipt.dataHash;
    transactionReceipt.message;
};
}  // namespace bcos::concepts::receipt