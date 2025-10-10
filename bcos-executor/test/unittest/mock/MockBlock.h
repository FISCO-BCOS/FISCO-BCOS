#pragma once
#include "MockBlockHeader.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-utilities/AnyHolder.h"
#include <bcos-framework/protocol/Block.h>

namespace bcos::test
{

class MockBlock : public bcos::protocol::Block
{
public:
    MockBlock() {}
    ~MockBlock() override {}

    void setBlockHeader(protocol::BlockHeader::Ptr blockHeader) override
    {
        m_blockHeader = blockHeader;
    }
    void decode(bytesConstRef _data, bool _calculateHash, bool _checkSig) override {}
    void encode(bytes& _encodeData) const override {}
    crypto::HashType calculateTransactionRoot(const crypto::Hash& hashImpl) const override
    {
        return {};
    }
    crypto::HashType calculateReceiptRoot(const crypto::Hash& hashImpl) const override
    {
        return {};
    }
    int32_t version() const override { return m_blockHeader->version(); }
    void setVersion(int32_t _version) override { m_blockHeader->setVersion(_version); }
    protocol::BlockType blockType() const override { return protocol::WithTransactionsHash; }
    protocol::AnyBlockHeader blockHeader() const override
    {
        return {bcos::InPlace<MockBlockHeader>{}, m_blockHeader->number()};
    }
    protocol::BlockHeader::Ptr blockHeader() override { return m_blockHeader; }
    crypto::HashType transactionHash(uint64_t _index) const override
    {
        return transactionMetaDatas()[_index]->hash();
    }
    void setBlockType(protocol::BlockType _blockType) override {}
    void setTransaction(uint64_t _index, protocol::Transaction::Ptr _transaction) override {}
    void appendTransaction(protocol::Transaction::Ptr _transaction) override {}
    void setReceipt(uint64_t _index, protocol::TransactionReceipt::Ptr _receipt) override {}
    void appendReceipt(protocol::TransactionReceipt::Ptr _receipt) override {}
    void appendTransactionMetaData(protocol::TransactionMetaData::Ptr _txMetaData) override {}
    uint64_t transactionsSize() const override { return 0; }
    uint64_t transactionsMetaDataSize() const override { return 0; }
    uint64_t transactionsHashSize() const override { return Block::transactionsHashSize(); }
    uint64_t receiptsSize() const override { return 0; }
    void setNonceList(RANGES::any_view<protocol::NonceType> nonces) override {}
    RANGES::any_view<protocol::NonceType> nonceList() const override { return m_nodelist; }
    size_t size() const override { return 0; }

    protocol::ViewResult<crypto::HashType> transactionHashes() const override { return {}; }
    protocol::ViewResult<protocol::AnyTransactionMetaData> transactionMetaDatas() const override
    {
        return {};
    }
    protocol::ViewResult<protocol::AnyTransaction> transactions() const override { return {}; }
    protocol::ViewResult<protocol::AnyTransactionReceipt> receipts() const override { return {}; }
    bcos::bytesConstRef logsBloom() const override { return {}; }
    void setLogsBloom(bcos::bytesConstRef logsBloom) override {}

private:
    protocol::BlockHeader::Ptr m_blockHeader = std::make_shared<MockBlockHeader>(1);
    protocol::NonceList m_nodelist;
};
}  // namespace bcos::test
