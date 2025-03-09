#include "TransactionExecutorImpl.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"

bcos::transaction_executor::TransactionExecutorImpl::TransactionExecutorImpl(
    protocol::TransactionReceiptFactory const& receiptFactory, crypto::Hash::Ptr hashImpl,
    PrecompiledManager& precompiledManager)
  : m_receiptFactory(receiptFactory),
    m_hashImpl(std::move(hashImpl)),
    m_precompiledManager(precompiledManager)
{}

evmc_message bcos::transaction_executor::newEVMCMessage(protocol::BlockNumber blockNumber,
    protocol::Transaction const& transaction, int64_t gasLimit, const evmc_address& origin)
{
    auto recipientAddress = unhexAddress(transaction.to());
    evmc_message message = {.kind = transaction.to().empty() ? EVMC_CREATE : EVMC_CALL,
        .flags = 0,
        .depth = 0,
        .gas = gasLimit,
        .recipient = recipientAddress,
        .destination_ptr = nullptr,
        .destination_len = 0,
        .sender = origin,
        .sender_ptr = nullptr,
        .sender_len = 0,
        .input_data = transaction.input().data(),
        .input_size = transaction.input().size(),
        .value = toEvmC(u256(transaction.value())),
        .create2_salt = {},
        .code_address = recipientAddress};

    if (blockNumber == 0 && transaction.to() == precompiled::AUTH_COMMITTEE_ADDRESS)
    {
        message.kind = EVMC_CREATE;
    }

    return message;
}
