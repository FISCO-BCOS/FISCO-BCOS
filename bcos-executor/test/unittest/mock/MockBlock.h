#pragma once
#include "MockBlockHeader.h"
#include <bcos-framework/protocol/Block.h>

namespace bcos::test
{
class MockBlock : public bcos::protocol::Block
{
public:
    MockBlock() : bcos::protocol::Block(nullptr, nullptr) {}
    ~MockBlock() override {}

    void setBlockHeader(protocol::BlockHeader::Ptr blockHeader) override
    {
        m_blockHeader = blockHeader;
    }
    void decode(bytesConstRef _data, bool _calculateHash, bool _checkSig) override {}
    void encode(bytes& _encodeData) const override {}
    crypto::HashType calculateTransactionRoot() const override { return bcos::crypto::HashType(); }
    crypto::HashType calculateReceiptRoot() const override { return bcos::crypto::HashType(); }
    int32_t version() const override { return m_blockHeader->version(); }
    void setVersion(int32_t _version) override { m_blockHeader->setVersion(_version); }
    protocol::BlockType blockType() const override { return protocol::WithTransactionsHash; }
    protocol::BlockHeader::ConstPtr blockHeaderConst() const override { return m_blockHeader; }
    protocol::BlockHeader::Ptr blockHeader() override { return m_blockHeader; }
    protocol::Transaction::ConstPtr transaction(uint64_t _index) const override { return {}; }
    protocol::TransactionReceipt::ConstPtr receipt(uint64_t _index) const override { return {}; }
    protocol::TransactionMetaData::ConstPtr transactionMetaData(uint64_t _index) const override
    {
        return {};
    }
    crypto::HashType transactionHash(uint64_t _index) const override
    {
        return Block::transactionHash(_index);
    }
    void setBlockType(protocol::BlockType _blockType) override {}
    void setTransaction(uint64_t _index, protocol::Transaction::Ptr _transaction) override {}
    void appendTransaction(protocol::Transaction::Ptr _transaction) override {}
    void setReceipt(uint64_t _index, protocol::TransactionReceipt::Ptr _receipt) override {}
    void appendReceipt(protocol::TransactionReceipt::Ptr _receipt) override {}
    void appendTransactionMetaData(protocol::TransactionMetaData::Ptr _txMetaData) override {}
    protocol::NonceListPtr nonces() const override { return {}; }
    uint64_t transactionsSize() const override { return 0; }
    uint64_t transactionsMetaDataSize() const override { return 0; }
    uint64_t transactionsHashSize() const override { return Block::transactionsHashSize(); }
    uint64_t receiptsSize() const override { return 0; }
    void setNonceList(const protocol::NonceList& _nonceList) override {}
    void setNonceList(protocol::NonceList&& _nonceList) override {}
    const protocol::NonceList& nonceList() const override { return m_nodelist; }

private:
    protocol::BlockHeader::Ptr m_blockHeader = std::make_shared<MockBlockHeader>(1);
    protocol::NonceList m_nodelist;
};
}  // namespace bcos::test
