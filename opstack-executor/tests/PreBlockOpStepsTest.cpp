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

#include <bcos-evm/test/opstack/OpTestReceiptFactory.h>

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpDepositEncode.h>
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <opstack-executor/Storage2State.h>

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using bcos::evm::OpConsensusError;
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
    op::OpForkSchedule schedule{op::OpForkSchedule::legacy(false)};

    void run(const op::OpForkConfig& cfg, const std::vector<bcos::bytes>& rawTxBytes,
        const std::vector<op::DepositTx>& deposits)
    {
        engine::preBlockOpSteps(storage, header, cfg, rawTxBytes, deposits, executor, hashes,
            hashErr, scalar, &schedule, /*parentTsSec=*/0);
    }
};

const bcos::bytes kDepositEnvelope{bcos::byte{0x7e}, bcos::byte{0x01}};
const bcos::bytes kTypedEnvelope{bcos::byte{0x02}, bcos::byte{0x01}};
}  // namespace

BOOST_AUTO_TEST_SUITE(PreBlockOpStepsTest)

BOOST_AUTO_TEST_CASE(RejectsNullSchedule)
{
    Fixture f;
    auto dep = depositWithData(l1AttributesData(op::IsthmusL1AttributesLen));
    BOOST_CHECK_EXCEPTION(
        engine::preBlockOpSteps(f.storage, f.header, op::isthmusConfig(),
            std::vector<bcos::bytes>{kDepositEnvelope}, std::vector<op::DepositTx>{dep}, f.executor,
            f.hashes, f.hashErr, f.scalar, nullptr, /*parentTsSec=*/0),
        std::invalid_argument, [](std::invalid_argument const& e) {
            return std::string_view{e.what()} == "preBlockOpSteps: OpForkSchedule is required";
        });
}

BOOST_AUTO_TEST_CASE(ProcessOpBlockRejectsNullSchedule)
{
    MutableStorage storage;
    bcos::evm::evmstate::Storage2State<MutableStorage> view(storage);
    evmone::state::BlockInfo block;
    block.gas_limit = 30'000'000;
    bcos::executor_v1::opstack::NullBlockHashes hashes;
    auto vm = evmc::VM{evmc_create_evmone()};
    op::DepositTx dep{};
    dep.gas_limit = 1'000'000;
    op::OpBlockTx depTx;
    depTx.tx = dep;
    std::vector<op::OpBlockTx> const txs{depTx};
    BOOST_CHECK_EXCEPTION((void)op::processOpBlock(
                              view, block, hashes, txs, op::isthmusConfig(), vm,
                              /*chainId=*/10, bcos::evm::opstack::testutil::kOpTestReceiptFactory,
                              [](const evmone::state::StateDiff&) {}, nullptr, /*parentTsSec=*/0),
        std::invalid_argument, [](std::invalid_argument const& e) {
            return std::string_view{e.what()} == "processOpBlock: OpForkSchedule is required";
        });
}

BOOST_AUTO_TEST_CASE(RejectsEmptyBlock)
{
    Fixture f;
    BOOST_CHECK_EXCEPTION(
        f.run(op::jovianConfig(), {}, {}), OpConsensusError, [](OpConsensusError const& e) {
            return std::string(e.what()).find("missing L1 attributes deposit (empty block)") !=
                   std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(RejectsEmptyFirstEnvelope)
{
    Fixture f;
    BOOST_CHECK_EXCEPTION(f.run(op::jovianConfig(), {bcos::bytes{}}, {depositWithData({})}),
        OpConsensusError, [](OpConsensusError const& e) {
            return std::string(e.what()).find("no deposit transaction to seed the block") !=
                   std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(RejectsNonDepositFirstEnvelope)
{
    Fixture f;
    BOOST_CHECK_EXCEPTION(f.run(op::jovianConfig(), {kTypedEnvelope}, {depositWithData({})}),
        OpConsensusError, [](OpConsensusError const& e) {
            return std::string(e.what()).find("no deposit transaction to seed the block") !=
                   std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(RejectsMissingDeposits)
{
    Fixture f;
    // The envelope says deposit, but the decoded deposit vector is empty.
    BOOST_CHECK_EXCEPTION(f.run(op::jovianConfig(), {kDepositEnvelope}, {}), OpConsensusError,
        [](OpConsensusError const& e) {
            return std::string(e.what()).find("no deposit transaction to seed the block") !=
                   std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(JovianRejectsUserTxOnIsthmusLenAttrs)
{
    Fixture f;
    // Fixture schedule is Isthmus-only (no Q5 window), so the timestamp rule cannot fire:
    // the 176B attributes length alone makes this a deposits-only block in op-geth
    // (CalcDAFootprint), and the trailing typed envelope is the last transaction.
    auto dep = depositWithData(l1AttributesData(op::IsthmusL1AttributesLen));
    BOOST_CHECK_EXCEPTION(
        f.run(op::jovianConfig(), {kDepositEnvelope, kTypedEnvelope}, {dep, op::DepositTx{}}),
        OpConsensusError, [](OpConsensusError const& e) {
            return std::string(e.what()).find("unexpected non-deposit transactions") !=
                   std::string::npos;
        });
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

BOOST_AUTO_TEST_CASE(AcceptsFirstDepositThatIsNotL1Attributes)
{
    // First tx must be a deposit; it does not have to be the L1-attributes deposit.
    Fixture f;
    auto dep = depositWithData(l1AttributesData(op::IsthmusL1AttributesLen));
    dep.from = evmc::address{};
    dep.to = evmc::address{};
    BOOST_REQUIRE(!op::isL1AttributesTx(dep));
    BOOST_CHECK_NO_THROW(f.run(op::isthmusConfig(), {kDepositEnvelope}, {dep}));
    BOOST_CHECK(f.hashes.has_value());
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
                          executor, f.hashes, f.hashErr, f.scalar, &f.schedule, /*parentTsSec=*/0),
        engine::OpStorageError);
}

// processOpBlock's four applyDiff call sites must normalize a storage write-back
// failure to OpStorageError, exactly like the per-tx path (m_finish / executeDeposit /
// finalizeBlock). Storage2State::applyDiff poisons AND rethrows raw, so without the block-path
// wrapper a bare std::runtime_error would escape processOpBlock and break the documented
// "OpStorageError (-32603), never a bare runtime_error" contract. The first applyDiff (the
// pre-block system call) runs before the empty-block reject, so an empty tx span is enough to
// reach the wrapper.
BOOST_AUTO_TEST_CASE(ProcessOpBlockNormalizesWritebackFailure)
{
    MutableStorage storage;
    bcos::evm::evmstate::Storage2State<MutableStorage> view(storage);
    evmone::state::BlockInfo block;
    block.gas_limit = 30'000'000;
    bcos::executor_v1::opstack::NullBlockHashes hashes;
    auto vm = evmc::VM{evmc_create_evmone()};

    auto const schedule = op::OpForkSchedule::legacy(false);
    BOOST_CHECK_EXCEPTION(op::processOpBlock(
                              view, block, hashes, /*txs=*/{}, op::jovianConfig(), vm,
                              /*chainId=*/10, bcos::evm::opstack::testutil::kOpTestReceiptFactory,
                              [](const evmone::state::StateDiff&) {
                                  throw std::runtime_error(
                                      "storage fault injected for the write-back test");
                              },
                              &schedule, /*parentTsSec=*/0),
        bcos::evm::engine::OpStorageError, [](bcos::evm::engine::OpStorageError const& e) {
            return std::string(e.what()).find("storage write-back failed") != std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(ProcessOpBlockCapacityFaultIsNotAnEvictableCulprit)
{
    // The block path must classify a full remaining-gas pool exactly like the per-tx
    // path does (m_prepare -> OpBlockGasPoolFull): the tx is VALID but does not fit
    // this candidate. The thrown OpConsensusError carries capacity (never evict — the
    // tx must stay pooled) AND the culprit's txHash, which names whose bytes the wired
    // build loop trims from THIS candidate (plus its sender's nonce tail). Without the
    // tag the loop's only skip path is unreachable and a full gas pool would answer
    // -32603 on every retry; capacity alone decides evict-vs-skip,
    // so the tagged hash does not make the tx pool-evictable.
    MutableStorage storage;
    bcos::evm::evmstate::Storage2State<MutableStorage> view(storage);
    evmone::state::BlockInfo block;
    block.gas_limit = 30'000'000;
    bcos::executor_v1::opstack::NullBlockHashes hashes;
    auto vm = evmc::VM{evmc_create_evmone()};

    op::DepositTx dep{};
    dep.gas_limit = 1'000'000;
    dep.data = evmc::bytes(op::IsthmusL1AttributesLen, uint8_t{0});

    // A normal EIP-1559 tx whose gasLimit exceeds the block's remaining gas; the
    // mirror fields must match the signed envelope (the block path cross-checks them).
    evmone::state::Transaction tx;
    tx.type = evmone::state::Transaction::Type::eip1559;
    tx.nonce = 0;
    tx.gas_limit = block.gas_limit + 1;
    tx.to = evmc::address{};
    std::memset(tx.to->bytes, 0x11, sizeof(tx.to->bytes));
    tx.value = intx::uint256{0};
    tx.max_gas_price = 30'000'000'000;
    tx.max_priority_gas_price = 30'000'000'000;
    tx.sender = evmc::address{};
    std::memset(tx.sender.bytes, 0xaa, sizeof(tx.sender.bytes));

    namespace rlp = bcos::codec::rlp;
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    append(intItem(10));  // chainId
    append(intItem(0));   // nonce
    append(intItem(30000000000));
    append(intItem(30000000000));
    append(intItem(static_cast<uint64_t>(tx.gas_limit)));
    bcos::bytes toBytes(std::begin(tx.to->bytes), std::end(tx.to->bytes));
    bcos::bytes toItem;
    rlp::encode(toItem, bcos::bytesConstRef{toBytes.data(), toBytes.size()});
    append(toItem);
    append(intItem(0));       // value
    payload.push_back(0x80);  // empty data (bare byte)
    payload.push_back(0xc0);  // empty accessList
    bcos::bytes listHeader;
    rlp::encodeHeader(listHeader, {.isList = true, .payloadLength = payload.size()});
    evmc::bytes envelope;
    envelope.push_back(0x02);
    envelope.insert(envelope.end(), listHeader.begin(), listHeader.end());
    envelope.insert(envelope.end(), payload.begin(), payload.end());

    try
    {
        op::OpBlockTx depTx;
        depTx.tx = dep;
        auto const depEnvelope = bcos::evm::opstack::encodeDepositEnvelope(dep);
        depTx.signedEnvelope.assign(depEnvelope.begin(), depEnvelope.end());
        op::OpBlockTx normalTx;
        normalTx.tx = tx;
        normalTx.signedEnvelope = envelope;
        std::vector<op::OpBlockTx> const txs{depTx, normalTx};
        auto const schedule = op::OpForkSchedule::legacy(false);
        (void)op::processOpBlock(
            view, block, hashes, txs, op::isthmusConfig(), vm, /*chainId=*/10,
            bcos::evm::opstack::testutil::kOpTestReceiptFactory,
            [](const evmone::state::StateDiff&) {}, &schedule, /*parentTsSec=*/0);
        BOOST_FAIL("a tx over the remaining block gas must void the block");
    }
    catch (bcos::evm::OpConsensusError const& e)
    {
        BOOST_CHECK(e.capacity);
        BOOST_REQUIRE(e.txHash.has_value());
        BOOST_CHECK_EQUAL(e.txHash->hex(),
            bcos::crypto::keccak256Hash(bcos::bytesConstRef{envelope.data(), envelope.size()})
                .hex());
        BOOST_CHECK_MESSAGE(
            std::string(e.what()).find("does not fit the remaining block gas") != std::string::npos,
            "unexpected reject: " << e.what());
    }
}

namespace
{
/// Wraps the shared test factory and throws a bare std::runtime_error from the Nth
/// createReceipt call (1-based), so the block path's runtime_error -> tagged
/// OpConsensusError normalization can be exercised without a real storage fault.
class ThrowingReceiptFactory : public bcos::protocol::TransactionReceiptFactory
{
public:
    ThrowingReceiptFactory(bcos::protocol::TransactionReceiptFactory::Ptr inner, unsigned throwOn)
      : m_inner(std::move(inner)), m_throwOn(throwOn)
    {}

    bcos::protocol::TransactionReceipt::Ptr createReceipt() const override
    {
        maybeThrow();
        return m_inner->createReceipt();
    }
    bcos::protocol::TransactionReceipt::Ptr createReceipt(
        bcos::protocol::TransactionReceipt& input) const override
    {
        maybeThrow();
        return m_inner->createReceipt(input);
    }
    bcos::protocol::TransactionReceipt::Ptr createReceipt(
        bcos::bytesConstRef receiptData) const override
    {
        maybeThrow();
        return m_inner->createReceipt(receiptData);
    }
    bcos::protocol::TransactionReceipt::Ptr createReceipt(
        bcos::bytes const& receiptData) const override
    {
        maybeThrow();
        return m_inner->createReceipt(receiptData);
    }
    bcos::protocol::TransactionReceipt::Ptr createReceipt(bcos::u256 const& gasUsed,
        std::string contractAddress, const std::vector<bcos::protocol::LogEntry>& logEntries,
        int32_t status, bcos::bytesConstRef output,
        bcos::protocol::BlockNumber blockNumber) const override
    {
        maybeThrow();
        return m_inner->createReceipt(
            gasUsed, std::move(contractAddress), logEntries, status, output, blockNumber);
    }
    bcos::protocol::TransactionReceipt::Ptr createReceipt2(bcos::u256 const& gasUsed,
        std::string contractAddress, const std::vector<bcos::protocol::LogEntry>& logEntries,
        int32_t status, bcos::bytesConstRef output, bcos::protocol::BlockNumber blockNumber,
        std::string effectiveGasPrice, bcos::protocol::TransactionVersion version,
        bool withHash) const override
    {
        maybeThrow();
        return m_inner->createReceipt2(gasUsed, std::move(contractAddress), logEntries, status,
            output, blockNumber, std::move(effectiveGasPrice), version, withHash);
    }

private:
    void maybeThrow() const
    {
        if (++m_calls == m_throwOn)
        {
            throw std::runtime_error("receipt fault injected for the culprit-tag test");
        }
    }
    bcos::protocol::TransactionReceiptFactory::Ptr m_inner;
    unsigned m_throwOn;
    mutable unsigned m_calls = 0;
};
}  // namespace

// The block path must tag a bare std::runtime_error escaping opTransition with the signed
// envelope's hash: the build loop keys culprit eviction on OpConsensusError::txHash, so an
// untagged throw leaves the tx pooled and the next forkchoiceUpdated selects it again.
// (The per-tx path needs no tag because it has no eviction loop.)
BOOST_AUTO_TEST_CASE(ProcessOpBlockTagsRuntimeErrorWithCulpritHash)
{
    MutableStorage storage;
    bcos::evm::evmstate::Storage2State<MutableStorage> view(storage);
    evmone::state::BlockInfo block;
    block.gas_limit = 30'000'000;
    bcos::executor_v1::opstack::NullBlockHashes hashes;
    auto vm = evmc::VM{evmc_create_evmone()};

    op::DepositTx dep{};
    dep.gas_limit = 1'000'000;
    dep.data = evmc::bytes(op::IsthmusL1AttributesLen, uint8_t{0});

    evmone::state::Transaction tx;
    tx.type = evmone::state::Transaction::Type::eip1559;
    tx.nonce = 0;
    tx.gas_limit = 21000;
    tx.to = evmc::address{};
    std::memset(tx.to->bytes, 0x11, sizeof(tx.to->bytes));
    tx.value = intx::uint256{0};
    // Zero fees: the sender has no balance in the empty fixture storage, and a zero cap
    // passes the funds check while still reaching opTransition (which creates the receipt).
    tx.max_gas_price = 0;
    tx.max_priority_gas_price = 0;
    tx.sender = evmc::address{};
    std::memset(tx.sender.bytes, 0xaa, sizeof(tx.sender.bytes));

    namespace rlp = bcos::codec::rlp;
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    append(intItem(10));  // chainId
    append(intItem(0));   // nonce
    append(intItem(0));   // maxPriorityFeePerGas
    append(intItem(0));   // maxFeePerGas
    append(intItem(static_cast<uint64_t>(tx.gas_limit)));
    bcos::bytes toBytes(std::begin(tx.to->bytes), std::end(tx.to->bytes));
    bcos::bytes toItem;
    rlp::encode(toItem, bcos::bytesConstRef{toBytes.data(), toBytes.size()});
    append(toItem);
    append(intItem(0));       // value
    payload.push_back(0x80);  // empty data (bare byte)
    payload.push_back(0xc0);  // empty accessList
    bcos::bytes listHeader;
    rlp::encodeHeader(listHeader, {.isList = true, .payloadLength = payload.size()});
    evmc::bytes envelope;
    envelope.push_back(0x02);
    envelope.insert(envelope.end(), listHeader.begin(), listHeader.end());
    envelope.insert(envelope.end(), payload.begin(), payload.end());

    op::OpBlockTx depTx;
    depTx.tx = dep;
    auto const depEnvelope = bcos::evm::opstack::encodeDepositEnvelope(dep);
    depTx.signedEnvelope.assign(depEnvelope.begin(), depEnvelope.end());
    op::OpBlockTx normalTx;
    normalTx.tx = tx;
    normalTx.signedEnvelope = envelope;
    std::vector<op::OpBlockTx> const txs{depTx, normalTx};

    // Deposit receipt = call #1 (succeeds); the normal tx's receipt = call #2 (throws).
    auto factory = std::make_shared<ThrowingReceiptFactory>(
        bcos::evm::opstack::testutil::kOpTestReceiptFactory, 2);
    try
    {
        auto const schedule = op::OpForkSchedule::legacy(false);
        (void)op::processOpBlock(
            view, block, hashes, txs, op::isthmusConfig(), vm, /*chainId=*/10, factory,
            [](const evmone::state::StateDiff&) {}, &schedule, /*parentTsSec=*/0);
        BOOST_FAIL("an injected receipt fault must void the block");
    }
    catch (bcos::evm::OpConsensusError const& e)
    {
        BOOST_REQUIRE(e.txHash.has_value());
        BOOST_CHECK_EQUAL(e.txHash->hex(),
            bcos::crypto::keccak256Hash(bcos::bytesConstRef{envelope.data(), envelope.size()})
                .hex());
        BOOST_CHECK_MESSAGE(
            std::string(e.what()).find("transaction execution failed") != std::string::npos,
            "unexpected reject: " << e.what());
    }
}

BOOST_AUTO_TEST_CASE(ProcessOpBlockKarstActivationRejectsUserTx)
{
    // 178B Jovian attrs: the 176B heuristic cannot be what rejects. Q5 must.
    MutableStorage storage;
    bcos::evm::evmstate::Storage2State<MutableStorage> view(storage);
    evmone::state::BlockInfo block;
    block.gas_limit = 30'000'000;
    block.timestamp = 100;
    bcos::executor_v1::opstack::NullBlockHashes hashes;
    auto vm = evmc::VM{evmc_create_evmone()};
    auto schedule = op::OpForkSchedule::parse("0:jovian,100:karst");

    evmc::bytes data(op::JovianL1AttributesLen, uint8_t{0});
    std::memcpy(
        data.data(), op::JovianL1AttributesSelector.data(), op::JovianL1AttributesSelector.size());
    op::DepositTx dep{};
    dep.gas_limit = 1'000'000;
    dep.data = std::move(data);

    evmone::state::Transaction user{};
    user.type = evmone::state::Transaction::Type::eip1559;
    user.gas_limit = 21'000;

    op::OpBlockTx depTx;
    depTx.tx = dep;
    auto const depEnvelope = bcos::evm::opstack::encodeDepositEnvelope(dep);
    depTx.signedEnvelope.assign(depEnvelope.begin(), depEnvelope.end());
    op::OpBlockTx userTx;
    userTx.tx = user;
    userTx.signedEnvelope = evmc::bytes{uint8_t{0x02}, uint8_t{0x01}};
    std::vector<op::OpBlockTx> const txs{depTx, userTx};

    BOOST_CHECK_EXCEPTION(
        (void)op::processOpBlock(
            view, block, hashes, txs, op::karstConfig(), vm, /*chainId=*/10,
            bcos::evm::opstack::testutil::kOpTestReceiptFactory,
            [](const evmone::state::StateDiff&) {}, &schedule, /*parentTsSec=*/99),
        OpConsensusError, [](OpConsensusError const& e) {
            return std::string_view{e.what()}.find("unexpected non-deposit") !=
                   std::string_view::npos;
        });
}

BOOST_AUTO_TEST_CASE(HasNonDepositTxUsesEnvelopeWhenPresent)
{
    op::OpBlockTx depositEmpty;
    depositEmpty.tx = op::DepositTx{};
    std::vector<op::OpBlockTx> onlyEmpty{depositEmpty};
    BOOST_CHECK(!op::hasNonDepositTx(onlyEmpty));

    op::OpBlockTx depositTyped;
    depositTyped.tx = op::DepositTx{};
    depositTyped.signedEnvelope = evmc::bytes{uint8_t{0x02}, uint8_t{0x01}};
    std::vector<op::OpBlockTx> typedOnDeposit{depositTyped};
    BOOST_CHECK(op::hasNonDepositTx(typedOnDeposit));

    op::OpBlockTx deposit7e;
    deposit7e.tx = op::DepositTx{};
    deposit7e.signedEnvelope = evmc::bytes{uint8_t{0x7e}, uint8_t{0x01}};
    std::vector<op::OpBlockTx> depositEnvelope{deposit7e};
    BOOST_CHECK(!op::hasNonDepositTx(depositEnvelope));

    op::OpBlockTx user7e;
    user7e.tx = evmone::state::Transaction{};
    user7e.signedEnvelope = evmc::bytes{uint8_t{0x7e}, uint8_t{0x01}};
    std::vector<op::OpBlockTx> typedVariant{user7e};
    BOOST_CHECK(op::hasNonDepositTx(typedVariant));
}

BOOST_AUTO_TEST_CASE(ProcessOpBlockQ5RejectsDepositVariantWithTypedEnvelope)
{
    MutableStorage storage;
    bcos::evm::evmstate::Storage2State<MutableStorage> view(storage);
    evmone::state::BlockInfo block;
    block.gas_limit = 30'000'000;
    block.timestamp = 100;
    bcos::executor_v1::opstack::NullBlockHashes hashes;
    auto vm = evmc::VM{evmc_create_evmone()};
    auto schedule = op::OpForkSchedule::parse("0:jovian,100:karst");

    evmc::bytes data(op::JovianL1AttributesLen, uint8_t{0});
    std::memcpy(
        data.data(), op::JovianL1AttributesSelector.data(), op::JovianL1AttributesSelector.size());
    op::DepositTx dep{};
    dep.gas_limit = 1'000'000;
    dep.data = std::move(data);

    op::OpBlockTx depTx;
    depTx.tx = dep;
    auto const depEnvelope = bcos::evm::opstack::encodeDepositEnvelope(dep);
    depTx.signedEnvelope.assign(depEnvelope.begin(), depEnvelope.end());

    op::OpBlockTx mismatched;
    mismatched.tx = op::DepositTx{};
    mismatched.signedEnvelope = evmc::bytes{uint8_t{0x02}, uint8_t{0x01}};
    std::vector<op::OpBlockTx> const txs{depTx, mismatched};

    BOOST_CHECK_EXCEPTION(
        (void)op::processOpBlock(
            view, block, hashes, txs, op::karstConfig(), vm, /*chainId=*/10,
            bcos::evm::opstack::testutil::kOpTestReceiptFactory,
            [](const evmone::state::StateDiff&) {}, &schedule, /*parentTsSec=*/99),
        OpConsensusError, [](OpConsensusError const& e) {
            return std::string_view{e.what()}.find("unexpected non-deposit") !=
                   std::string_view::npos;
        });
}

// ---- create2Deployer at the Canyon activation timestamp (op-geth EnsureCreate2Deployer) ----
//
// op-geth (consensus/misc/create2deployer.go:34-40) writes the create2Deployer code ONLY when
// the block timestamp equals CanyonTime (`*c.CanyonTime != timestamp` returns), and does so
// unconditionally. The three cells below pin that gate: the activation block deploys, a
// pre-activation block does not, and a post-activation block observes the code as carried
// state while not re-firing the gate itself. The code is observed through a fresh Storage2State
// bridge over the same MemoryStorage the hook wrote into.

namespace
{
const op::OpForkSchedule kCanyonAt1000 = op::OpForkSchedule::parse("0:regolith,1000:canyon");

evmc::bytes create2DeployerCodeIn(MutableStorage& storage)
{
    bcos::evm::evmstate::Storage2State<MutableStorage> view(storage);
    return view.get_account_code(engine::create2DeployerAddress());
}
}  // namespace

// (a) Exactly at the Canyon activation timestamp the code is written, with the op-geth codeHash.
BOOST_AUTO_TEST_CASE(CanyonActivationBlockDeploysCreate2Deployer)
{
    Fixture f;
    f.schedule = kCanyonAt1000;
    f.header.m_timestampMs = 1'000'000;  // blockTsSec == 1'000'000/1000 == 1000 (activation)
    auto dep = depositWithData(l1AttributesData(op::IsthmusL1AttributesLen));
    f.run(op::canyonConfig(), {kDepositEnvelope}, {dep});

    bcos::evm::evmstate::Storage2State<MutableStorage> view(f.storage);
    auto code = view.get_account_code(engine::create2DeployerAddress());
    BOOST_REQUIRE(!view.poisoned());
    BOOST_CHECK_EQUAL(code.size(), engine::c_create2DeployerCode.size());
    BOOST_CHECK(std::equal(code.begin(), code.end(), engine::c_create2DeployerCode.begin(),
        engine::c_create2DeployerCode.end()));
    auto acct = view.get_account(engine::create2DeployerAddress());
    BOOST_REQUIRE(acct.has_value());
    BOOST_CHECK(acct->code_hash == engine::create2DeployerCodeHash());
}

// (b) One second below the activation: no code. This is the cell that proves the gate is the
// activation timestamp, not `fork >= Canyon` (the config at 999s is still Regolith).
BOOST_AUTO_TEST_CASE(PreCanyonActivationBlockDoesNotDeployCreate2Deployer)
{
    Fixture f;
    f.schedule = kCanyonAt1000;
    f.header.m_timestampMs = 999'000;  // blockTsSec == 999 (Canyon activates at 1000)
    auto dep = depositWithData(l1AttributesData(op::IsthmusL1AttributesLen));
    f.run(op::regolithConfig(), {kDepositEnvelope}, {dep});

    bcos::evm::evmstate::Storage2State<MutableStorage> view(f.storage);
    BOOST_REQUIRE(!view.poisoned());
    BOOST_CHECK(view.get_account_code(engine::create2DeployerAddress()).empty());
    BOOST_CHECK(!view.get_account(engine::create2DeployerAddress()).has_value());
}

// (c) Post-activation: the code persists as state across a later block (the gate does not run
// again, but the earlier write survives).
BOOST_AUTO_TEST_CASE(PostCanyonActivationBlockKeepsCreate2Deployer)
{
    Fixture f;
    f.schedule = kCanyonAt1000;
    auto dep = depositWithData(l1AttributesData(op::IsthmusL1AttributesLen));

    f.header.m_timestampMs = 1'000'000;  // activation writes the code
    f.run(op::canyonConfig(), {kDepositEnvelope}, {dep});
    BOOST_REQUIRE_EQUAL(
        create2DeployerCodeIn(f.storage).size(), engine::c_create2DeployerCode.size());

    f.header.m_timestampMs = 1'001'000;  // 1001s: still Canyon, past the activation timestamp
    f.run(op::canyonConfig(), {kDepositEnvelope}, {dep});
    auto code = create2DeployerCodeIn(f.storage);
    BOOST_CHECK_EQUAL(code.size(), engine::c_create2DeployerCode.size());
    BOOST_CHECK(std::equal(code.begin(), code.end(), engine::c_create2DeployerCode.begin(),
        engine::c_create2DeployerCode.end()));
}

// (c') A post-activation block against FRESH state must NOT deploy: upstream only fires at the
// activation timestamp itself, so `fork >= Canyon` (or `timestamp >= CanyonTime`) would be
// wrong. This is the post-direction counterpart to (b).
BOOST_AUTO_TEST_CASE(PostCanyonTimestampOnFreshStateDoesNotDeployCreate2Deployer)
{
    Fixture f;
    f.schedule = kCanyonAt1000;
    f.header.m_timestampMs = 1'001'000;  // post-activation, gate must be false
    auto dep = depositWithData(l1AttributesData(op::IsthmusL1AttributesLen));
    f.run(op::canyonConfig(), {kDepositEnvelope}, {dep});

    bcos::evm::evmstate::Storage2State<MutableStorage> view(f.storage);
    BOOST_REQUIRE(!view.poisoned());
    BOOST_CHECK(view.get_account_code(engine::create2DeployerAddress()).empty());
}

BOOST_AUTO_TEST_SUITE_END()
