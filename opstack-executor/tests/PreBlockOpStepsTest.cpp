// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// PreBlockOpStepsTest — runtime coverage for preBlockOpSteps' block-shape decision surface
// (OpBlockExecute.h): the deposit-first rejects, the Jovian L1-attributes shape checks, and the
// DA footprint gas scalar extraction. Uses a hand-rolled FakeBlockHeader over the abstract
// protocol::BlockHeader (64 pure virtuals, mostly unused stubs — same pattern as
// OpTxConvertTest's FakeTransaction) so no concrete header implementation's link chain enters
// this binary. The pre-block system call runs against an EMPTY MemoryStorage, where
// system_call_block_start is a no-op (no code at the system-contract addresses), so every case
// reaches the shape checks.

#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <opstack-executor/Storage2State.h>

#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

using bcos::evm::engine::OpConsensusError;
using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;
namespace engine = bcos::evm::engine;
namespace op = bcos::evm::opstack;

namespace
{
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

/// Minimal BlockHeader: only the accessors detail::toBlockInfo reads carry knobs; every other
/// pure virtual is a stub (never called on the preBlockOpSteps path).
class FakeBlockHeader : public bcos::protocol::BlockHeader
{
public:
    // ---- knobs read by toBlockInfo ----
    bcos::protocol::BlockNumber m_number = 100;
    int64_t m_timestampMs = 1'700'000'000'000;  // FISCO stores milliseconds
    bcos::u256 m_gasLimit = 30'000'000;
    std::optional<bcos::u256> m_baseFee = bcos::u256{1000};
    bcos::Address m_coinbase{};
    bcos::h256 m_prevRandao{};
    std::optional<bcos::h256> m_parentBeaconBlockRoot = bcos::h256{};
    bcos::bytes m_extraData;
    std::optional<bcos::u256> m_blobGasUsed = bcos::u256{0};
    bcos::protocol::ParentInfo m_parentInfo{};

    bcos::protocol::BlockNumber number() const override { return m_number; }
    int64_t timestamp() const override { return m_timestampMs; }
    bcos::u256 gasLimit() const override { return m_gasLimit; }
    std::optional<bcos::u256> baseFee() const override { return m_baseFee; }
    bcos::Address coinbase() const override { return m_coinbase; }
    bcos::h256 prevRandao() const override { return m_prevRandao; }
    std::optional<bcos::h256> parentBeaconBlockRoot() const override
    {
        return m_parentBeaconBlockRoot;
    }
    bcos::bytesConstRef extraData() const override
    {
        return {m_extraData.data(), m_extraData.size()};
    }
    std::optional<bcos::u256> blobGasUsed() const override { return m_blobGasUsed; }
    bcos::protocol::ParentInfo parentInfo() const override { return m_parentInfo; }

    // ---- unused stubs ----
    void decode(bcos::bytesConstRef) override {}
    void encode(bcos::bytes&) const override {}
    bcos::crypto::HashType hash() const override { return {}; }
    void calculateHash(const bcos::crypto::Hash&) override {}
    void clear() override {}
    uint32_t version() const override { return 0; }
    bcos::protocol::EthBlockVersion ethBlockVersion() const override
    {
        return bcos::protocol::EthBlockVersion::PRAGUE;
    }
    void setEthBlockVersion(bcos::protocol::EthBlockVersion) override {}
    void setRLPHash(bcos::crypto::HashType) override {}
    int64_t sealer() const override { return 0; }
    gsl::span<const bcos::bytes> sealerList() const override { return {}; }
    gsl::span<const bcos::protocol::Signature> signatureList() const override { return {}; }
    gsl::span<const uint64_t> consensusWeights() const override { return {}; }
    void setVersion(uint32_t) override {}
    void setSealer(int64_t) override {}
    void setSealerList(gsl::span<const bcos::bytes> const&) override {}
    void setSealerList(std::vector<bcos::bytes>&&) override {}
    void setConsensusWeights(gsl::span<const uint64_t> const&) override {}
    void setConsensusWeights(std::vector<uint64_t>&&) override {}
    void setSignatureList(gsl::span<const bcos::protocol::Signature> const&) override {}
    void setSignatureList(bcos::protocol::SignatureList&&) override {}
    bcos::crypto::HashType txsRoot() const override { return {}; }
    bcos::crypto::HashType receiptsRoot() const override { return {}; }
    bcos::crypto::HashType stateRoot() const override { return {}; }
    bcos::u256 gasUsed() const override { return 0; }
    void setParentInfo(bcos::protocol::ParentInfo parentInfo) override
    {
        m_parentInfo = parentInfo;
    }
    void setTxsRoot(bcos::crypto::HashType) override {}
    void setReceiptsRoot(bcos::crypto::HashType) override {}
    void setStateRoot(bcos::crypto::HashType) override {}
    void setNumber(bcos::protocol::BlockNumber n) override { m_number = n; }
    void setGasUsed(bcos::u256) override {}
    void setTimestamp(int64_t t) override { m_timestampMs = t; }
    void setExtraData(bcos::bytes extraData) override { m_extraData = std::move(extraData); }
    size_t size() const override { return 0; }
    void setCoinbase(bcos::Address addr) override { m_coinbase = addr; }
    bcos::bytesConstRef logsBloom() const override { return {}; }
    void setLogsBloom(bcos::bytesConstRef) override {}
    void setGasLimit(bcos::u256 limit) override { m_gasLimit = limit; }
    void setPrevRandao(bcos::h256 digest) override { m_prevRandao = digest; }
    bcos::crypto::HashType uncleHash() const override { return {}; }
    void setUncleHash(bcos::crypto::HashType) override {}
    bcos::u256 difficulty() const override { return 0; }
    void setDifficulty(bcos::u256) override {}
    bcos::h64 nonce() const override { return {}; }
    void setNonce(bcos::h64) override {}
    void setBaseFee(bcos::u256 fee) override { m_baseFee = fee; }
    std::optional<bcos::h256> withdrawalsRoot() const override { return bcos::h256{}; }
    void setWithdrawalsRoot(bcos::h256) override {}
    void setBlobGasUsed(bcos::u256 val) override { m_blobGasUsed = val; }
    std::optional<bcos::u256> excessBlobGas() const override { return bcos::u256{0}; }
    void setExcessBlobGas(bcos::u256) override {}
    void setParentBeaconBlockRoot(bcos::h256 root) override { m_parentBeaconBlockRoot = root; }
    std::optional<bcos::h256> requestsHash() const override { return std::nullopt; }
    void setRequestsHash(bcos::h256) override {}
};

/// A minimal deposit: preBlockOpSteps only reads deposits[0].data (the L1-attributes content
/// check is a demoted WARNING, so from/to need not be the L1-attributes identity).
op::DepositTx depositWithData(evmc::bytes data)
{
    op::DepositTx dep{};
    dep.gas_limit = 1'000'000;
    dep.data = std::move(data);
    return dep;
}

/// L1-attributes calldata of `size` bytes, Jovian selector at the head, big-endian scalar
/// 0x1234 in the trailing [176:178] slot when present.
evmc::bytes l1AttributesData(size_t size)
{
    evmc::bytes data(size, uint8_t{0});
    if (size >= op::JovianL1AttributesLen)
    {
        std::memcpy(data.data(), op::JovianL1AttributesSelector.data(),
            op::JovianL1AttributesSelector.size());
        data[op::JovianL1AttributesLen - 2] = uint8_t{0x12};
        data[op::JovianL1AttributesLen - 1] = uint8_t{0x34};
    }
    return data;
}

struct Fixture
{
    MutableStorage storage;
    FakeBlockHeader header;
    bcos::executor_v1::opstack::OpstackExecutor executor{nullptr, nullptr};
    std::optional<engine::detail::RecentBlockHashes<MutableStorage>> hashes;
    std::optional<std::string> hashErr;
    std::optional<uint16_t> scalar;

    void run(const op::OpForkConfig& cfg, const std::vector<bcos::bytes>& rawTxBytes,
        const std::vector<op::DepositTx>& deposits)
    {
        engine::preBlockOpSteps(
            storage, header, cfg, rawTxBytes, deposits, executor, hashes, hashErr, scalar);
    }
};

const bcos::bytes kDepositEnvelope{bcos::byte{0x7e}, bcos::byte{0x01}};
const bcos::bytes kTypedEnvelope{bcos::byte{0x02}, bcos::byte{0x01}};
}  // namespace

BOOST_AUTO_TEST_SUITE(PreBlockOpStepsTest)

BOOST_AUTO_TEST_CASE(RejectsEmptyBlock)
{
    Fixture f;
    BOOST_CHECK_THROW(f.run(op::jovianConfig(), {}, {}), OpConsensusError);
}

BOOST_AUTO_TEST_CASE(RejectsEmptyFirstEnvelope)
{
    Fixture f;
    BOOST_CHECK_THROW(
        f.run(op::jovianConfig(), {bcos::bytes{}}, {depositWithData({})}), OpConsensusError);
}

BOOST_AUTO_TEST_CASE(RejectsNonDepositFirstEnvelope)
{
    Fixture f;
    BOOST_CHECK_THROW(
        f.run(op::jovianConfig(), {kTypedEnvelope}, {depositWithData({})}), OpConsensusError);
}

BOOST_AUTO_TEST_CASE(RejectsMissingDeposits)
{
    Fixture f;
    // The envelope says deposit, but the decoded deposit vector is empty.
    BOOST_CHECK_THROW(f.run(op::jovianConfig(), {kDepositEnvelope}, {}), OpConsensusError);
}

BOOST_AUTO_TEST_CASE(JovianActivationRejectsTrailingNonDeposit)
{
    Fixture f;
    // 176B attributes = the Jovian activation block, which must be deposits-only.
    auto dep = depositWithData(l1AttributesData(op::IsthmusL1AttributesLen));
    BOOST_CHECK_THROW(
        f.run(op::jovianConfig(), {kDepositEnvelope, kTypedEnvelope}, {dep, op::DepositTx{}}),
        OpConsensusError);
}

BOOST_AUTO_TEST_CASE(JovianRejectsDataShorterThan178)
{
    Fixture f;
    // 177B: past the 176B activation length but short of the Jovian selector layout.
    auto dep = depositWithData(l1AttributesData(op::IsthmusL1AttributesLen + 1));
    BOOST_CHECK_THROW(f.run(op::jovianConfig(), {kDepositEnvelope}, {dep}), OpConsensusError);
}

BOOST_AUTO_TEST_CASE(JovianRejectsBadSelector)
{
    Fixture f;
    auto data = l1AttributesData(op::JovianL1AttributesLen);
    data[0] ^= 0xff;  // corrupt the selector
    auto dep = depositWithData(std::move(data));
    BOOST_CHECK_THROW(f.run(op::jovianConfig(), {kDepositEnvelope}, {dep}), OpConsensusError);
}

BOOST_AUTO_TEST_CASE(IsthmusAcceptsAndLeavesScalarEmpty)
{
    Fixture f;
    // has_da_footprint == false: the Jovian shape checks and scalar extraction are skipped.
    auto dep = depositWithData(l1AttributesData(op::IsthmusL1AttributesLen));
    f.run(op::isthmusConfig(), {kDepositEnvelope}, {dep});
    BOOST_CHECK(f.hashes.has_value());
    BOOST_CHECK(!f.hashErr.has_value());
    BOOST_CHECK(!f.scalar.has_value());
}

BOOST_AUTO_TEST_CASE(JovianActivation176SetsScalarZero)
{
    Fixture f;
    auto dep = depositWithData(l1AttributesData(op::IsthmusL1AttributesLen));
    f.run(op::jovianConfig(), {kDepositEnvelope}, {dep});
    BOOST_REQUIRE(f.scalar.has_value());
    BOOST_CHECK_EQUAL(*f.scalar, 0u);
    BOOST_CHECK(!f.hashErr.has_value());
}

BOOST_AUTO_TEST_CASE(Jovian178ExtractsBigEndianScalar)
{
    Fixture f;
    auto dep = depositWithData(l1AttributesData(op::JovianL1AttributesLen));
    f.run(op::jovianConfig(), {kDepositEnvelope}, {dep});
    BOOST_REQUIRE(f.scalar.has_value());
    BOOST_CHECK_EQUAL(*f.scalar, 0x1234u);  // data[176:178] big-endian
    BOOST_CHECK(!f.hashErr.has_value());
}

BOOST_AUTO_TEST_CASE(PrePoisonedSharedSlotFailsAtSystemCallStep)
{
    Fixture f;
    // Poison the block-wide slot the way an earlier per-tx read fault would (a corrupt 4-byte
    // slot value trips the fetchStorage length check — same trigger as Storage2StatePoisonTest).
    auto sharedError = std::make_shared<bcos::evm::evmstate::SharedErrorSlot>();
    const evmc::address addr{};
    const std::string table = bcos::evm::evmstate::accountTableName(addr);
    bcos::storage::Entry e;
    e.set(std::string("abcd"));
    bcos::task::syncWait(bcos::storage2::writeOne(
        f.storage, StateKey{table, std::string(32, '\x01')}, std::move(e)));
    bcos::evm::evmstate::Storage2State<MutableStorage> prior(f.storage, sharedError);
    evmc::bytes32 slotKey{};
    std::memset(slotKey.bytes, 0x01, sizeof(slotKey.bytes));
    (void)prior.get_storage(addr, slotKey);
    BOOST_REQUIRE(prior.poisoned());

    // The executor shares the pre-poisoned slot: step (1) must surface the storage fault as
    // OpStorageError instead of executing on silently zero-valued reads.
    bcos::executor_v1::opstack::OpstackExecutor executor{
        nullptr, nullptr, op::jovianConfig(), sharedError};
    auto dep = depositWithData(l1AttributesData(op::JovianL1AttributesLen));
    const std::vector<bcos::bytes> rawTxs{kDepositEnvelope};
    const std::vector<op::DepositTx> deps{dep};
    BOOST_CHECK_THROW(engine::preBlockOpSteps(f.storage, f.header, op::jovianConfig(), rawTxs, deps,
                          executor, f.hashes, f.hashErr, f.scalar),
        engine::OpStorageError);
}

BOOST_AUTO_TEST_SUITE_END()
