#pragma once
#include <bcos-framework/protocol/BlockHeader.h>

namespace bcos::test
{
class MockBlockHeader : public bcos::protocol::BlockHeader
{
public:
    MockBlockHeader(protocol::BlockNumber _number) : BlockHeader({}), m_blockNumber(_number) {}
    virtual ~MockBlockHeader() {}

    void decode(bytesConstRef _data) override {}
    void encode(bytes& _encodeData) const override {}
    void clear() override {}
    uint32_t version() const override { return 0; }
    gsl::span<const protocol::ParentInfo> parentInfo() const override
    {
        return gsl::span<const protocol::ParentInfo>();
    }
    crypto::HashType txsRoot() const override { return bcos::crypto::HashType(); }
    crypto::HashType receiptsRoot() const override { return bcos::crypto::HashType(); }
    crypto::HashType stateRoot() const override { return bcos::crypto::HashType(); }
    protocol::BlockNumber number() const override { return m_blockNumber; }
    u256 gasUsed() const override { return bcos::u256(); }
    int64_t timestamp() const override { return 0; }
    int64_t sealer() const override { return 0; }
    gsl::span<const bytes> sealerList() const override { return gsl::span<const bytes>(); }
    bytesConstRef extraData() const override { return bcos::bytesConstRef(); }
    gsl::span<const protocol::Signature> signatureList() const override
    {
        return gsl::span<const protocol::Signature>();
    }
    gsl::span<const uint64_t> consensusWeights() const override
    {
        return gsl::span<const uint64_t>();
    }
    void setVersion(uint32_t _version) override {}
    void setParentInfo(const gsl::span<const protocol::ParentInfo>& _parentInfo) override {}
    void setParentInfo(protocol::ParentInfoList&& _parentInfo) override {}
    void setTxsRoot(bcos::crypto::HashType _txsRoot) override {}
    void setReceiptsRoot(bcos::crypto::HashType _receiptsRoot) override {}
    void setStateRoot(bcos::crypto::HashType _stateRoot) override {}
    void setNumber(protocol::BlockNumber _blockNumber) override { m_blockNumber = _blockNumber; }
    void setGasUsed(u256 _gasUsed) override {}
    void setTimestamp(int64_t _timestamp) override {}
    void setSealer(int64_t _sealerId) override {}
    void setSealerList(const gsl::span<const bytes>& _sealerList) override {}
    void setSealerList(std::vector<bytes>&& _sealerList) override {}
    void setConsensusWeights(const gsl::span<const uint64_t>& _weightList) override {}
    void setConsensusWeights(std::vector<uint64_t>&& _weightList) override {}
    void setExtraData(const bytes& _extraData) override {}
    void setExtraData(bytes&& _extraData) override {}
    void setSignatureList(const gsl::span<const protocol::Signature>& _signatureList) override {}
    void setSignatureList(protocol::SignatureList&& _signatureList) override {}

private:
    protocol::BlockNumber m_blockNumber;
};
}  // namespace bcos::test
