#include "TransactionReceiptFactoryImpl.h"

bcostars::protocol::TransactionReceiptImpl::Ptr
bcostars::protocol::TransactionReceiptFactoryImpl::createReceipt() const
{
    return std::make_shared<TransactionReceiptImpl>();
}

bcostars::protocol::TransactionReceiptImpl::Ptr
bcostars::protocol::TransactionReceiptFactoryImpl::createReceipt(
    bcos::bytesConstRef _receiptData) const
{
    auto transactionReceipt = std::make_shared<TransactionReceiptImpl>(
        [m_receipt = bcostars::TransactionReceipt()]() mutable { return &m_receipt; });

    transactionReceipt->decode(_receiptData);

    auto& inner = transactionReceipt->mutableInner();
    if (inner.dataHash.empty())
    {
        // Update the hash field
        bcos::concepts::hash::calculate(inner, m_hashImpl->hasher(), inner.dataHash);

        BCOS_LOG(TRACE) << LOG_BADGE("createReceipt") << LOG_DESC("recalculate receipt dataHash");
    }

    return transactionReceipt;
}
bcostars::protocol::TransactionReceiptImpl::Ptr
bcostars::protocol::TransactionReceiptFactoryImpl::createReceipt(
    bcos::bytes const& _receiptData) const
{
    return createReceipt(bcos::ref(_receiptData));
}
bcostars::protocol::TransactionReceiptImpl::Ptr
bcostars::protocol::TransactionReceiptFactoryImpl::createReceipt(bcos::u256 const& gasUsed,
    std::string contractAddress, const std::vector<bcos::protocol::LogEntry>& logEntries,
    int32_t status, bcos::bytesConstRef output, bcos::protocol::BlockNumber blockNumber) const
{
    auto transactionReceipt = std::make_shared<TransactionReceiptImpl>(
        [m_receipt = bcostars::TransactionReceipt()]() mutable { return &m_receipt; });
    auto& inner = transactionReceipt->mutableInner();
    inner.data.version = 0;
    // inner.data.gasUsed = boost::lexical_cast<std::string>(gasUsed);
    inner.data.gasUsed = gasUsed.backend().str({}, {});
    inner.data.contractAddress = std::move(contractAddress);
    inner.data.status = status;
    inner.data.output.assign(output.begin(), output.end());
    transactionReceipt->setLogEntries(logEntries);
    inner.data.blockNumber = blockNumber;

    bcos::concepts::hash::calculate(inner, m_hashImpl->hasher(), inner.dataHash);
    return transactionReceipt;
}
bcostars::protocol::TransactionReceiptImpl::Ptr
bcostars::protocol::TransactionReceiptFactoryImpl::createReceipt2(bcos::u256 const& gasUsed,
    std::string contractAddress, const std::vector<bcos::protocol::LogEntry>& logEntries,
    int32_t status, bcos::bytesConstRef output, bcos::protocol::BlockNumber blockNumber,
    std::string effectiveGasPrice, bcos::protocol::TransactionVersion version, bool withHash) const
{
    auto transactionReceipt = std::make_shared<TransactionReceiptImpl>(
        [m_receipt = bcostars::TransactionReceipt()]() mutable { return &m_receipt; });
    auto& inner = transactionReceipt->mutableInner();
    inner.data.version = static_cast<uint32_t>(version);
    inner.data.gasUsed = boost::lexical_cast<std::string>(gasUsed);
    inner.data.contractAddress = std::move(contractAddress);
    inner.data.status = status;
    inner.data.output.assign(output.begin(), output.end());
    transactionReceipt->setLogEntries(logEntries);
    inner.data.blockNumber = blockNumber;
    inner.data.effectiveGasPrice = std::move(effectiveGasPrice);

    // Update the hash field
    bcos::concepts::hash::calculate(inner, m_hashImpl->hasher(), inner.dataHash);
    return transactionReceipt;
}
