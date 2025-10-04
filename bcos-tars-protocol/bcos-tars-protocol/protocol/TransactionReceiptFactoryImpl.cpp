#include "TransactionReceiptFactoryImpl.h"

bcostars::protocol::TransactionReceiptImpl::Ptr
bcostars::protocol::TransactionReceiptFactoryImpl::createReceipt() const
{
    return std::make_shared<TransactionReceiptImpl>();
}
bcostars::protocol::TransactionReceiptImpl::Ptr
bcostars::protocol::TransactionReceiptFactoryImpl::createReceipt(
    bcos::protocol::TransactionReceipt& input) const
{
    auto tarsInput = dynamic_cast<TransactionReceiptImpl&>(input);
    return std::make_shared<TransactionReceiptImpl>(
        [m_inner = std::move(tarsInput.inner())]() mutable { return &m_inner; });
}


bcostars::protocol::TransactionReceiptImpl::Ptr
bcostars::protocol::TransactionReceiptFactoryImpl::createReceipt(
    bcos::bytesConstRef _receiptData) const
{
    auto transactionReceipt = std::make_shared<TransactionReceiptImpl>(
        [m_receipt = bcostars::TransactionReceipt()]() mutable { return &m_receipt; });

    transactionReceipt->decode(_receiptData);

    auto& data = transactionReceipt->inner();
    if (data.dataHash.empty())
    {
        // Update the hash field
        bcos::concepts::hash::calculate(data, m_hashImpl->hasher(), data.dataHash);

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
    auto& data = transactionReceipt->inner();
    data.data.version = 0;
    // inner.data.gasUsed = boost::lexical_cast<std::string>(gasUsed);
    data.data.gasUsed = gasUsed.backend().str({}, {});
    data.data.contractAddress = std::move(contractAddress);
    data.data.status = status;
    data.data.output.assign(output.begin(), output.end());
    transactionReceipt->setLogEntries(logEntries);
    data.data.blockNumber = blockNumber;

    bcos::concepts::hash::calculate(data, m_hashImpl->hasher(), data.dataHash);
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
    auto& data = transactionReceipt->inner();
    data.data.version = std::bit_cast<uint32_t>(version);
    data.data.gasUsed = boost::lexical_cast<std::string>(gasUsed);
    data.data.contractAddress = std::move(contractAddress);
    data.data.status = status;
    data.data.output.assign(output.begin(), output.end());
    transactionReceipt->setLogEntries(logEntries);
    data.data.blockNumber = blockNumber;
    data.data.effectiveGasPrice = std::move(effectiveGasPrice);

    // Update the hash field
    bcos::concepts::hash::calculate(data, m_hashImpl->hasher(), data.dataHash);
    return transactionReceipt;
}
bcostars::protocol::TransactionReceiptFactoryImpl::TransactionReceiptFactoryImpl(
    const bcos::crypto::CryptoSuite::Ptr& cryptoSuite)
  : m_hashImpl(cryptoSuite->hashImpl())
{}
