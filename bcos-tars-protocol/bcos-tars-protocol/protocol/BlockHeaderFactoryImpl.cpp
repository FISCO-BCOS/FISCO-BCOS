#include "BlockHeaderFactoryImpl.h"
#include "../impl/TarsHashable.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"

bcostars::protocol::BlockHeaderFactoryImpl::BlockHeaderFactoryImpl(
    bcos::crypto::CryptoSuite::Ptr cryptoSuite)
  : m_cryptoSuite(std::move(cryptoSuite)), m_hashImpl(m_cryptoSuite->hashImpl())
{}

bcos::protocol::BlockHeader::Ptr bcostars::protocol::BlockHeaderFactoryImpl::createBlockHeader()
{
    return std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_header = bcostars::BlockHeader()]() mutable { return &m_header; });
}

bcos::protocol::BlockHeader::Ptr bcostars::protocol::BlockHeaderFactoryImpl::createBlockHeader(
    bcos::bytes const& _data)
{
    return createBlockHeader(bcos::ref(_data));
}

bcos::protocol::BlockHeader::Ptr bcostars::protocol::BlockHeaderFactoryImpl::createBlockHeader(
    bcos::bytesConstRef _data)
{
    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_header = bcostars::BlockHeader()]() mutable { return &m_header; });
    blockHeader->decode(_data);

    auto& inner = blockHeader->mutableInner();
    if (inner.dataHash.empty())
    {
        // Update the hash field
        bcos::concepts::hash::calculate(inner, m_hashImpl->hasher(), inner.dataHash);

        BCOS_LOG(TRACE) << LOG_BADGE("createBlockHeader")
                        << LOG_DESC("recalculate blockHeader dataHash");
    }

    return blockHeader;
}

bcos::protocol::BlockHeader::Ptr bcostars::protocol::BlockHeaderFactoryImpl::createBlockHeader(
    bcos::protocol::BlockNumber _number)
{
    auto blockHeader = createBlockHeader();
    blockHeader->setNumber(_number);

    return blockHeader;
}
