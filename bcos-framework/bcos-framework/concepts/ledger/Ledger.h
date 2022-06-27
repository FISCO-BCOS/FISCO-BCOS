#pragma once
#include <bcos-framework/concepts/Block.h>
#include <bcos-framework/concepts/storage/Storage.h>
#include <boost/type_traits.hpp>
#include <boost/type_traits/function_traits.hpp>

namespace bcos::concepts::ledger
{

template <class ArgType>
concept TransactionOrReceipt = bcos::concepts::transaction::Transaction<ArgType> ||
    bcos::concepts::receipt::TransactionReceipt<ArgType>;

template <class LedgerType>
concept Ledger = requires(LedgerType ledger,
    typename boost::function_traits<decltype(&LedgerType::getBlock)>::arg1_type getBlockArg1,
    typename boost::function_traits<decltype(&LedgerType::setBlock)>::arg1_type setBlockArg1,
    typename boost::function_traits<decltype(&LedgerType::setBlock)>::arg2_type setBlockArg2,
    typename boost::function_traits<decltype(&LedgerType::getTransactionsOrReceipts)>::arg1_type
        getTransactionsOrReceiptsArg1,
    typename boost::function_traits<decltype(&LedgerType::setTransactionsOrReceipts)>::arg1_type
        setTransactionsOrReceiptsArg1,
    typename boost::function_traits<decltype(&LedgerType::setTransactionOrReceiptBuffers)>::
        arg1_type setTransactionOrReceiptBuffersArg1,
    typename boost::function_traits<decltype(&LedgerType::setTransactionOrReceiptBuffers)>::
        arg2_type setTransactionOrReceiptBuffersArg2)
{
    // clang-format off
    { getBlockArg1 } -> std::integral;
    { ledger.getBlock(getBlockArg1) } -> bcos::concepts::block::Block;
    
    { setBlockArg1 } -> bcos::concepts::storage::Storage;
    { setBlockArg2 } -> bcos::concepts::block::Block;
    ledger.setBlock(setBlockArg1, setBlockArg2);

    { getTransactionsOrReceiptsArg1 } -> std::ranges::range;
    { ledger.getTransactionsOrReceipts()} ->TransactionOrReceipt;

    ledger.getTotalTransactionCount();

    { setTransactionsOrReceiptsArg1 } -> std::ranges::range;
    ledger.setTransactionsOrReceipts(setTransactionsOrReceiptsArg1);

    { setTransactionOrReceiptBuffersArg1 } -> std::ranges::range;
    { setTransactionOrReceiptBuffersArg2 } -> std::ranges::range;
    ledger.setTransactionOrReceiptBuffers(setTransactionOrReceiptBuffersArg1, setTransactionOrReceiptBuffersArg2);
    //clang-format on
};

}  // namespace bcos::concepts::ledger