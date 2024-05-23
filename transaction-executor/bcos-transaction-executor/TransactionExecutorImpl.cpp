#include "TransactionExecutorImpl.h"

bcos::transaction_executor::TransactionExecutorImpl::TransactionExecutorImpl(
    protocol::TransactionReceiptFactory const& receiptFactory, crypto::Hash::Ptr hashImpl)
  : m_receiptFactory(receiptFactory),
    m_hashImpl(std::move(hashImpl)),
    m_precompiledManager(m_hashImpl)
{}

evmc_message bcos::transaction_executor::TransactionExecutorImpl::newEVMCMessage(
    protocol::Transaction const& transaction, int64_t gasLimit)
{
    auto toAddress = unhexAddress(transaction.to());
    evmc_message message = {.kind = transaction.to().empty() ? EVMC_CREATE : EVMC_CALL,
        .flags = 0,
        .depth = 0,
        .gas = gasLimit,
        .recipient = toAddress,
        .destination_ptr = nullptr,
        .destination_len = 0,
        .sender =
            (!transaction.sender().empty() && transaction.sender().size() == sizeof(evmc_address)) ?
                *(evmc_address*)transaction.sender().data() :
                evmc_address{},
        .sender_ptr = nullptr,
        .sender_len = 0,
        .input_data = transaction.input().data(),
        .input_size = transaction.input().size(),
        .value = {},
        .create2_salt = {},
        .code_address = toAddress};

    return message;
}
