#include "BlockFactoryImpl.h"
#include "BlockImpl.h"

bcostars::protocol::BlockFactoryImpl::BlockFactoryImpl(bcos::crypto::CryptoSuite::Ptr cryptoSuite,
    bcos::protocol::BlockHeaderFactory::Ptr blockHeaderFactory,
    bcos::protocol::TransactionFactory::Ptr transactionFactory,
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory)
  : m_cryptoSuite(std::move(cryptoSuite)),
    m_blockHeaderFactory(std::move(blockHeaderFactory)),
    m_transactionFactory(std::move(transactionFactory)),
    m_receiptFactory(std::move(receiptFactory)) {};
bcos::protocol::Block::Ptr bcostars::protocol::BlockFactoryImpl::createBlock()
{
    return std::make_shared<BlockImpl>();
}
bcos::protocol::Block::Ptr bcostars::protocol::BlockFactoryImpl::createBlock(
    bcos::bytesConstRef _data, bool _calculateHash, bool _checkSig)
{
    auto block = std::make_shared<BlockImpl>();
    block->decode(_data, _calculateHash, _checkSig);

    if (block->inner().blockHeader.dataHash.empty())
    {
        block->blockHeader()->calculateHash(*m_cryptoSuite->hashImpl());
    }

    return block;
}
bcos::crypto::CryptoSuite::Ptr bcostars::protocol::BlockFactoryImpl::cryptoSuite()
{
    return m_cryptoSuite;
}
bcos::protocol::BlockHeaderFactory::Ptr bcostars::protocol::BlockFactoryImpl::blockHeaderFactory()
{
    return m_blockHeaderFactory;
}
bcos::protocol::TransactionFactory::Ptr bcostars::protocol::BlockFactoryImpl::transactionFactory()
{
    return m_transactionFactory;
}
bcos::protocol::TransactionReceiptFactory::Ptr
bcostars::protocol::BlockFactoryImpl::receiptFactory()
{
    return m_receiptFactory;
}
bcos::protocol::TransactionMetaData::Ptr
bcostars::protocol::BlockFactoryImpl::createTransactionMetaData()
{
    return std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
        [inner = bcostars::TransactionMetaData()]() mutable { return &inner; });
}
bcos::protocol::TransactionMetaData::Ptr
bcostars::protocol::BlockFactoryImpl::createTransactionMetaData(
    bcos::crypto::HashType _hash, std::string _to)
{
    auto txMetaData = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>();
    txMetaData->setHash(_hash);
    txMetaData->setTo(std::move(_to));

    return txMetaData;
}
