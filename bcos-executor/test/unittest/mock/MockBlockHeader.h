#pragma once
#include "bcos-framework/protocol/BlockHeader.h"

namespace bcos::test
{
class MockBlockHeader : public bcos::protocol::BlockHeader
{
public:
    MockBlockHeader(const MockBlockHeader&) = default;
    MockBlockHeader(MockBlockHeader&&) = default;
    MockBlockHeader& operator=(const MockBlockHeader&) = default;
    MockBlockHeader& operator=(MockBlockHeader&&) = default;
    MockBlockHeader(protocol::BlockNumber _number) : m_blockNumber(_number) {}
    ~MockBlockHeader() override = default;

    bcos::crypto::HashType hash() const override { return {}; }
    void calculateHash(const crypto::Hash& hashImpl) override {}

    void decode(bytesConstRef _data) override {}
    void encode(bytes& _encodeData) const override {}
    void clear() override {}
    uint32_t version() const override { return 0; }
    bcos::protocol::ParentInfo parentInfo() const override
    {
        return bcos::protocol::ParentInfo{};
    }
    crypto::HashType txsRoot() const override { return {}; }
    crypto::HashType receiptsRoot() const override { return {}; }
    crypto::HashType stateRoot() const override { return {}; }
    protocol::BlockNumber number() const override { return m_blockNumber; }
    u256 gasUsed() const override { return {}; }
    int64_t timestamp() const override { return 0; }
    int64_t sealer() const override { return 0; }
    gsl::span<const bytes> sealerList() const override { return {}; }
    bytesConstRef extraData() const override { return {}; }
    gsl::span<const protocol::Signature> signatureList() const override { return {}; }
    gsl::span<const uint64_t> consensusWeights() const override { return {}; }
    bytes coinbase() const override { return {}; }

    void setVersion(uint32_t _version) override {}
    void setParentInfo(bcos::protocol::ParentInfo parentInfo) override {}
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
    void setExtraData(bytes _extraData) override {}
    void setSignatureList(const gsl::span<const protocol::Signature>& _signatureList) override {}
    void setSignatureList(protocol::SignatureList&& _signatureList) override {}
    size_t size() const override { return 0; }

    // ---- Ethereum-specific field accessors ----
    bcos::Address coinbase() const override { return {}; }
    void setCoinbase(bcos::Address _addr) override {}
    bcos::bytesConstRef logsBloom() const override { return {}; }
    void setLogsBloom(bcos::bytesConstRef _bloom) override {}
    u256 gasLimit() const override { return {}; }
    void setGasLimit(u256 _limit) override {}
    bcos::h256 prevRandao() const override { return {}; }
    void setPrevRandao(bcos::h256 _digest) override {}
    std::optional<u256> baseFee() const override { return std::nullopt; }
    void setBaseFee(u256 _fee) override {}
    std::optional<bcos::h256> withdrawalsRoot() const override { return std::nullopt; }
    void setWithdrawalsRoot(bcos::h256 _hash) override {}
    std::optional<u256> blobGasUsed() const override { return std::nullopt; }
    void setBlobGasUsed(u256 _val) override {}
    std::optional<u256> excessBlobGas() const override { return std::nullopt; }
    void setExcessBlobGas(u256 _val) override {}
    std::optional<bcos::h256> parentBeaconBlockRoot() const override { return std::nullopt; }
    void setParentBeaconBlockRoot(bcos::h256 _root) override {}
    std::optional<bcos::h256> requestsHash() const override { return std::nullopt; }
    void setRequestsHash(bcos::h256 _hash) override {}
    std::optional<bcos::h256> blockAccessListHash() const override { return std::nullopt; }
    void setBlockAccessListHash(bcos::h256 _hash) override {}
    std::optional<uint64_t> slotNumber() const override { return std::nullopt; }
    void setSlotNumber(uint64_t _val) override {}

private:
    protocol::BlockNumber m_blockNumber;
};
}  // namespace bcos::test
