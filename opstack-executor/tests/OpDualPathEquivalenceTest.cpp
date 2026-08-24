// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpDualPathEquivalenceTest.cpp — OP single-path golden execution harness (route B retired with
// runOpBlockInjection, Task 5). Each t8n vector / chain block is driven through
// OpScheduler.executeBlock — the production OP execution path (preBlockOpSteps →
// SchedulerSerialImpl(serial=true) → finalizeOpBlockResult) — with the ANNOUNCED header carrying
// the op-geth golden commitments (`_op_expected.header` + the deterministic computeOpTxRoot), so
// the skeleton's unconditional six-way verify asserts FISCO reproduces op-geth's block commitments.
//
// Scope (per vector):
//   - isthmus/jovian (incl. chain): route A MUST succeed — the six-way verify is a hard gate
//     (FISCO == op-geth commitments) — then the golden three-way (path A.stateRoot ==
//     `_op_expected.header.stateRoot`) is hard for non-contract_create, soft REPORT for
//     contract_create (the create-output divergence is a known base-layer issue); green guard
//     (deposit_basefee ×2) is always hard; receipt-count sanity is asserted.
//   - pre-isthmus (ecotone/fjord/granite): FISCO executes under isthmus semantics by design (the
//     golden fork mismatch is expected output); the announced golden commitments are a different
//     fork's, so route A is expected to be rejected at the six-way verify → soft REPORT (never
//     hard). Any OTHER failure (shape/validation) is a real bug → BOOST_ERROR.
//
// Fork model: `forkFlagsFor(bool jovian)` + `configAt` (feature-op_jovian: isthmus/jovian). Fork
// parity is
// asserted by resolving cfg from the same source as the scheduler's internal configAt.
// Exception handling: per-vector catches use catch(std::exception)/catch(...) (libevmone -fno-rtti
// makes typed catch unreliable) — catch → BOOST_ERROR + continue.
// has_storage scan (same-block create pre-triage) + /sys tripwire derived-table prefix assertion
// (accountTableName(addr) prefix == apps/).

#include "support/GoldenSample.h"
#include <opstack-executor/OpDepositEncode.h>  // encodeDepositEnvelope (deposit envelope reconstruction)
#include "support/SeedPreState.h"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <cxxabi.h>
#include <engine/bcos-engine/EngineServiceImpl.h>
#include <opstack-executor/Storage2State.h>
#include <json/json.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpScheduler.h>  // route A surgery (Task 6 P1-8): executeBlock drives
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/Storage2State.h>
#include <opstack-executor/Storage2StateHelpers.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using JsonValue = Json::Value;

namespace
{
namespace op = bcos::evm::opstack;
namespace engine = bcos::evm::engine;
namespace eth = bcos::executor_v1::eth;
namespace detail = bcos::evm::engine::detail;

// ── jsoncpp .at() equivalent (shared with OpT8nReplayTest.cpp:60-84) ─────────────────
// key is `const char*` (not `const std::string&`) deliberately: a string literal argument
// would materialize a temporary std::string, and GCC-14's -Wdangling-reference flags any
// reference-returning call that receives a class-type temporary — even though the returned
// reference aliases `v`, never `key` (false positive). A `const char*` argument creates no
// temporary, so the warning cannot fire.
inline const Json::Value& jAt(const Json::Value& v, const char* key)
{
    if (!v.isMember(key))
        throw std::invalid_argument(std::string("missing required field: ") + key);
    return v[key];
}

inline Json::Value jParse(std::istream& input)
{
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(input, root))
        throw std::runtime_error("JSON parse failed: " + reader.getFormattedErrorMessages());
    return root;
}


// ── Fixture (mirrored from OpNewPayloadRpcE2eTest.cpp:48-167) ──────────────────────────
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;
    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const&) & { std::abort(); }
    void createCheckpoint(Storage&, CheckpointName const&) {}
    void deleteCheckpoint(CheckpointName const&) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

bcos::protocol::BlockFactory::Ptr makeBlockFactory()
{
    auto cryptoSuite = makeCryptoSuite();
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

constexpr uint64_t kChainId = 0x2105;

bcos::evm::opstack::OpForkFlags forkFlagsFor(bool jovian)
{
    return bcos::evm::opstack::OpForkFlags{.jovianActive = jovian};
}

struct Fixture
{
    // Single-bucket CONCURRENT backend: the range(SYS_TABLES) scan for stateRoot relies on
    // RANGE_SEEK semantics; multi-bucket would scan wrong (OpNewPayloadRpcE2eTest.cpp:147-152).
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    bcos::crypto::CryptoSuite::Ptr cryptoSuite{makeCryptoSuite()};
    bcos::crypto::Hash::Ptr hashImpl{cryptoSuite->hashImpl()};
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    // Task 4: SchedulerSerialImpl (route A's per-tx driver) needs a live io pool.
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
};

struct GoldenStats
{
    int flat = 0;
    int chainBlocks = 0;
    int match = 0;
    int mismatch = 0;
    int greenGuardOk = 0;
};

// ── Helper functions ─────────────────────────────────────────────────────────────────

/// bcos::h256 from a vector hex string, tolerating "0"/"0x0" (zero). jsoncpp vectors write
/// prev_randao as "0x0" when zero; bcos::h256(string) needs full 64 hex.
bcos::h256 jsonH256(const std::string& s)
{
    if (s.empty() || s == "0" || s == "0x0")
        return bcos::h256{};
    return bcos::h256(s);
}

/// bcos::u256 from a vector hex string ("0x...") — boost cpp_int natively parses the 0x prefix.
bcos::u256 jsonBcosU256(const std::string& s)
{
    return bcos::u256(s);
}

/// chain vectors have no golden: build a FISCO BlockHeaderImpl from env (current block).
/// toBlockInfo reads number/timestamp/gasLimit/baseFee/coinbase/prevRandao/
/// parentBeaconBlockRoot/extraData/blobGasUsed (OpCommon.h:106-121, optional fields .value());
/// parentInfo serves RecentBlockHashes.
bcostars::protocol::BlockHeaderImpl::Ptr buildHeaderFromEnv(const Json::Value& env)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    const int64_t number =
        static_cast<int64_t>(opstack_test::jsonU64(jAt(env, "currentNumber").asString()));
    h->setNumber(number);
    // FISCO tars store milliseconds; the vector currentTimestamp is in seconds (OP semantics).
    // toBlockInfo then /1000 restores seconds.
    h->setTimestamp(
        static_cast<int64_t>(opstack_test::jsonU64(jAt(env, "currentTimestamp").asString()) * 1000));
    h->setGasLimit(jsonBcosU256(jAt(env, "currentGasLimit").asString()));
    h->setGasUsed(bcos::u256(0));
    h->setBaseFee(jsonBcosU256(jAt(env, "currentBaseFee").asString()));
    h->setCoinbase(bcos::Address(jAt(env, "currentCoinbase").asString()));
    h->setPrevRandao(jsonH256(jAt(env, "currentRandom").asString()));
    h->setParentBeaconBlockRoot(jsonH256(jAt(env, "parentBeaconBlockRoot").asString()));
    const auto parentHash = jsonH256(jAt(env, "parentHash").asString());
    h->setParentInfo(bcos::protocol::ParentInfo{
        .blockNumber = (number > 0 ? number - 1 : 0), .blockHash = parentHash});
    h->setExtraData(bcos::bytes{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setWithdrawalsRoot(bcos::h256{});
    h->setRequestsHash(bcos::h256{});
    return h;
}

/// Build raw envelope bytes from vector block.transactions (for txRoot / executeOpBlock decode):
/// deposit → rebuild DepositTx from _op_deposit → encodeDepositEnvelope (tests/support);
/// normal → _op_raw as-is. Asserts _op_raw is present (every chain-vector normal tx carries it).
std::vector<bcos::bytes> buildRawTxBytes(const Json::Value& blk, const std::string& id)
{
    std::vector<bcos::bytes> rawTxBytes;
    for (const auto& t : jAt(jAt(blk, "block"), "transactions"))
    {
        const auto opType = jAt(t, "_op_type").asString();
        if (opType == "deposit")
        {
            const auto& d = jAt(t, "_op_deposit");
            op::DepositTx dep;
            dep.source_hash = detail::toEvmcBytes32(jsonH256(jAt(d, "source_hash").asString()));
            dep.from = opstack_test::jsonAddress(jAt(d, "from").asString());
            dep.to = jAt(d, "to").isNull() ?
                         std::nullopt :
                         std::optional{opstack_test::jsonAddress(jAt(d, "to").asString())};
            dep.mint = d.isMember("mint") ?
                           std::optional{opstack_test::jsonU256(jAt(d, "mint").asString())} :
                           std::nullopt;
            dep.value = d.isMember("value") ? opstack_test::jsonU256(jAt(d, "value").asString()) :
                                              intx::uint256{0};
            dep.gas_limit = static_cast<int64_t>(opstack_test::jsonU64(jAt(d, "gas").asString()));
            dep.is_system_tx = jAt(d, "is_system_tx").asBool();
            dep.data = opstack_test::jsonBytes(jAt(t, "data").asString());
            rawTxBytes.push_back(encodeDepositEnvelope(dep));
        }
        else
        {
            if (!t.isMember("_op_raw"))
                throw std::invalid_argument(id + ": normal tx missing _op_raw");
            rawTxBytes.push_back(bcos::fromHex(jAt(t, "_op_raw").asString()));
        }
    }
    return rawTxBytes;
}

/// FISCO tx for block assembly: built from the raw envelope
/// (opEnvelopeToTars + SEV-8 full-envelope override) — block assembly must also build txs for
/// deposits (getTransactions returns the full set; the execute hook pulls raw bytes from
/// extraTransactionBytes and classifies by type byte). Precedent:
/// EngineServiceImpl.h:1176-1196 buildOpBlock.
bcos::protocol::Transaction::Ptr buildBlockTx(
    bcos::bytes const& env, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto txHash = hashImpl->hash(env);
    auto tarsTx = bcos::engine::detail::opEnvelopeToTars(env, txHash);
    if (!tarsTx)
        return nullptr;
    tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(*tarsTx)]() mutable { return &tars; });
}

/// Backfill the announced header's commitment fields from the vector's golden
/// `_op_expected.header` (op-geth's real block, from the t8n generator), so OpScheduler's
/// unconditional six-way verify compares FISCO's execution against the op-geth golden. txRoot is
/// absent from _op_expected — it is the deterministic trie root over rawTxBytes (computeOpTxRoot,
/// the same function finalizeOpBlockResult uses → equal by construction).
/// withdrawalsRoot/requestsHash/blobGasUsed are set only when present; blobGasUsed MUST be filled
/// when the golden carries it (a buildHeaderFromEnv default of 0 would otherwise false-mismatch a
/// Jovian block whose seal carries a non-zero value).
void fillAnnouncedHeaderFromGolden(bcos::protocol::BlockHeader::Ptr const& header,
    const JsonValue& vec, const std::vector<bcos::bytes>& rawTxBytes)
{
    const auto& ex = jAt(jAt(vec, "_op_expected"), "header");
    header->setStateRoot(jsonH256(jAt(ex, "stateRoot").asString()));
    header->setReceiptsRoot(jsonH256(jAt(ex, "receiptsRoot").asString()));
    header->setGasUsed(jsonBcosU256(jAt(ex, "gasUsed").asString()));
    header->setTxsRoot(bcos::evm::engine::computeOpTxRoot(rawTxBytes));
    auto bloom = bcos::fromHex(jAt(ex, "logsBloom").asString());
    header->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
    if (ex.isMember("withdrawalsRoot"))
        header->setWithdrawalsRoot(jsonH256(ex["withdrawalsRoot"].asString()));
    if (ex.isMember("requestsHash"))
        header->setRequestsHash(jsonH256(ex["requestsHash"].asString()));
    if (ex.isMember("blobGasUsed"))
        header->setBlobGasUsed(jsonBcosU256(ex["blobGasUsed"].asString()));
}

/// /sys tripwire: derived-table prefix assertion — for every vector
/// pre/postState/tx to/from/coinbase address, accountTableName(addr) must prefix with apps/
/// (more robust than enumerating c_systemTxsAddress).
void checkSysTripwire(const std::string& id, const JsonValue& vec)
{
    std::set<std::string> tables;
    const auto add = [&](const JsonValue& j) {
        if (j.isNull() || !j.isObject())
            return;
        for (const char* k : {"to", "from", "sender"})
        {
            if (j.isMember(k) && !j[k].isNull())
                tables.insert(
                    bcos::evm::evmstate::accountTableName(opstack_test::jsonAddress(j[k].asString())));
        }
    };
    if (vec.isMember("pre"))
    {
        for (const auto& a : vec["pre"].getMemberNames())
            tables.insert(bcos::evm::evmstate::accountTableName(opstack_test::jsonAddress(a)));
    }
    if (vec.isMember("postState"))
    {
        for (const auto& a : vec["postState"].getMemberNames())
            tables.insert(bcos::evm::evmstate::accountTableName(opstack_test::jsonAddress(a)));
    }
    if (vec.isMember("env"))
    {
        const auto& cb = vec["env"]["currentCoinbase"];
        if (cb.isString())
            tables.insert(
                bcos::evm::evmstate::accountTableName(opstack_test::jsonAddress(cb.asString())));
    }
    if (vec.isMember("block") && vec["block"].isMember("transactions"))
    {
        for (const auto& t : vec["block"]["transactions"])
        {
            add(t);
            if (t.isMember("_op_deposit"))
                add(t["_op_deposit"]);
        }
    }
    for (const auto& t : tables)
    {
        if (t.rfind(bcos::ledger::SYS_DIRECTORY::USER_APPS, 0) != 0)
            BOOST_ERROR(
                id << ": /sys tripwire: derived table '" << t << "' does not start with apps/");
    }
}

/// has_storage scan (without pre-fixing): P1 scans same-block creates for
/// early triage — the corpus likely does not trigger StorageStateView's has_storage read-side
/// asymmetry; a hit only REPORTs, never fails. Post-refactor the block's txs are deposits +
/// FISCO Transactions; contract creations are visible as deposits with a nullopt `to` (the L1
/// attributes deposit always carries a `to`, so a hit would be a genuine create deposit).
void hasStorageScan(
    const std::string& id, const std::vector<bcos::evm::opstack::DepositTx>& deposits)
{
    int creates = 0;
    for (const auto& dep : deposits)
    {
        if (!dep.to.has_value())
            ++creates;
    }
    if (creates > 0)
        std::cout << "  has-storage-triage " << id << " creates=" << creates
                  << " (zero-write/CREATE2-same-addr collision risk scan: corpus not expected to "
                     "trigger)\n";
}

/// golden three-way: path A.stateRoot == vector
/// _op_expected.header.stateRoot. When hardGolden=true, a mismatch is a hard BOOST_CHECK failure
/// (scope = isthmus/jovian non-contract_create); greenGuard (deposit_basefee green guard) is
/// always hard; the rest (pre-isthmus / contract_create) stays soft REPORT.
void reportGolden(const std::string& id, const JsonValue& vec, const bcos::h256& stateRootA,
    bool greenGuard, bool hardGolden, GoldenStats& stats)
{
    const auto want = jAt(jAt(vec, "_op_expected"), "header")["stateRoot"].asString();
    const auto got = stateRootA.hexPrefixed();
    if (want == got)
    {
        ++stats.match;
        if (greenGuard)
        {
            ++stats.greenGuardOk;
            std::cout << "  GREEN-GUARD " << id << " three-way consistent stateRoot=" << got
                      << "\n";
        }
        return;
    }
    ++stats.mismatch;
    if (greenGuard)
    {
        BOOST_CHECK_MESSAGE(false, id << ": green-guard three-way mismatch A.stateRoot=" << got
                                      << " vector.stateRoot=" << want);
    }
    else if (hardGolden)
    {
        BOOST_CHECK_MESSAGE(false, id << ": golden three-way mismatch A.stateRoot=" << got
                                      << " vector.stateRoot=" << want
                                      << " (P3 hard, scope=isthmus/jovian non-contract_create)");
    }
    else
    {
        std::cout
            << "  GOLDEN-REPORT " << id << " stateRoot MISMATCH A=" << got << " vector=" << want
            << " (soft REPORT: pre-isthmus fork-mismatch or contract_create known-divergence)\n";
    }
}

/// Single-block execution: seedPreState is already done externally; the announced header carries
/// the golden commitments (filled by the caller via fillAnnouncedHeaderFromGolden), route A runs
/// through OpScheduler.executeBlock, and the golden three-way + receipt sanity are checked.
/// isthmus/jovian must pass the six-way verify (FISCO == op-geth commitments); pre-isthmus is
/// expected to be rejected at the verify (fork mismatch) → soft REPORT. mergeBackStorage persists
/// route A's authoritative post-state (chain inheritance).
void runBlockEquivalence(const std::string& id, Fixture& fixture,
    bcos::protocol::BlockHeader::Ptr const& header, const std::vector<bcos::bytes>& rawTxBytes,
    const JsonValue& vec, bool jovian, const bcos::evm::opstack::OpForkConfig& vectorCfg,
    bool greenGuard, GoldenStats& stats)
{
    // Fork parity: cfg = configAt(forkFlagsFor(jovian)), resolved from the same source as the
    // scheduler's internal configAt (same forkFlagsFor, same static singleton object).
    // Bind forkFlagsFor(jovian) to a named lvalue first: configAt takes const OpForkFlags&, and
    // GCC-14's -Wdangling-reference flags passing a prvalue temporary here even though the
    // returned reference aliases the static config, never the flags (false positive). The named
    // lvalue preserves the reference + its address identity (the &cfg == &vectorCfg check below).
    const auto forkFlags = forkFlagsFor(jovian);
    const auto& cfg = op::configAt(forkFlags);
    BOOST_CHECK_MESSAGE(&cfg == &vectorCfg, id << ": fork parity broken: block cfg != vector cfg");

    const auto hardfork = jAt(jAt(vec, "_info"), "hardfork").asString();
    const bool isIsthmusJovian = (hardfork == "isthmus" || hardfork == "jovian");
    const bool hardGolden = isIsthmusJovian && (id.find("contract_create") == std::string::npos);

    // Deposits (has_storage triage scan), built from the block-order Transaction objects
    // (mirroring the execute hook, no RLP parse).
    std::vector<bcos::protocol::Transaction::ConstPtr> transactions;
    transactions.reserve(rawTxBytes.size());
    for (auto const& raw : rawTxBytes)
    {
        auto tx = buildBlockTx(raw, fixture.hashImpl);
        if (tx == nullptr)
        {
            BOOST_ERROR(id << ": buildBlockTx failed");
            return;
        }
        transactions.push_back(tx);
    }
    std::vector<op::DepositTx> deposits;
    deposits.reserve(rawTxBytes.size());
    for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
        if (rawTxBytes[i][0] == static_cast<uint8_t>(op::kDepositTxType))
            deposits.push_back(
                bcos::executor_v1::opstack::OpstackExecutor::depositFromTransaction(*transactions[i]));

    // Route A: OpScheduler.executeBlock — view lifecycle owned by the skeleton (fork/pushView
    // inside it). The announced header carries the golden commitments (filled by the caller), so
    // the unconditional six-way verify is the FISCO-vs-op-geth gate.
    engine::OpExecuteBlockResult resultA;
    bcos::Error::Ptr routeAErr;
    try
    {
        // Block assembly: extraTransactionBytes = full envelope (SEV-8, overridden by
        // buildBlockTx).
        auto block = fixture.blockFactory->createBlock();
        block->setBlockHeader(header);
        for (const auto& raw : rawTxBytes)
        {
            auto tx = buildBlockTx(raw, fixture.hashImpl);
            if (tx == nullptr)
            {
                BOOST_ERROR(id << ": buildBlockTx failed (opEnvelopeToTars nullopt)");
                return;
            }
            block->appendTransaction(std::move(tx));
        }

        auto opScheduler = std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(
            fixture.receiptFactory, fixture.hashImpl, kChainId, forkFlagsFor(jovian),
            fixture.blockFactory, fixture.multiLayerStorage, /*ledger=*/nullptr,
            fixture.ioServicePool);

        opScheduler->executeBlock(block, /*verify=*/true,
            [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr, bool) {
                routeAErr = std::move(e);
            });
        if (routeAErr)
        {
            // isthmus/jovian: any executeBlock error is a real failure (FISCO must reproduce
            // op-geth). pre-isthmus: a six-way commitment mismatch is the expected fork-mismatch
            // divergence → soft REPORT; any other error is a real bug.
            const std::string msg = routeAErr->errorMessage();
            const bool sixWayMismatch =
                msg.find("six-way commitment mismatch") != std::string::npos;
            if (!isIsthmusJovian && sixWayMismatch)
            {
                ++stats.mismatch;
                std::cout << "  GOLDEN-REPORT " << id
                          << " route A rejected at six-way verify (pre-isthmus fork mismatch): "
                          << msg << "\n";
                return;
            }
            BOOST_ERROR(id << ": route A executeBlock failed: " << msg);
            return;
        }
        auto peeked = opScheduler->peekExecuteResult();
        if (!peeked)
        {
            BOOST_ERROR(id << ": route A executeBlock produced no result");
            return;
        }
        resultA = std::move(*peeked);
    }
    catch (const std::exception& e)
    {
        BOOST_ERROR(id << ": route A threw: " << e.what());
        return;
    }
    catch (...)
    {
        const auto* excType = abi::__cxa_current_exception_type();
        BOOST_ERROR(id << ": route A threw (typed catch bypassed, exception type: "
                       << (excType ? excType->name() : "<unknown>") << ")");
        return;
    }

    // Route A succeeded — six-way verify passed against the announced golden commitments.
    // Receipt sanity + golden three-way + /sys tripwire + has_storage scan.
    try
    {
        if (resultA.receipts.size() != rawTxBytes.size())
            BOOST_ERROR(id << ": receipt count mismatch: got " << resultA.receipts.size()
                           << " want " << rawTxBytes.size());
        BOOST_CHECK_GT(resultA.gasUsed, 0);

        reportGolden(id, vec, resultA.stateRoot, greenGuard, hardGolden, stats);
        checkSysTripwire(id, vec);
        hasStorageScan(id, deposits);

        // Chain inheritance: route A's authoritative post-state was already pushed into MLS by the
        // skeleton's pushView; only mergeBackStorage to persist (do not pushView again).
        bcos::task::syncWait(fixture.multiLayerStorage.mergeBackStorage());
    }
    catch (const std::exception& e)
    {
        BOOST_ERROR(id << ": tail threw: " << e.what());
    }
    catch (...)
    {
        const auto* excType = abi::__cxa_current_exception_type();
        BOOST_ERROR(id << ": tail threw (typed catch bypassed, exception type: "
                       << (excType ? excType->name() : "<unknown>") << ")");
    }
}

/// Single-block vector: seedPreState → build the announced header (golden commitments) →
/// runBlockEquivalence. golden is loaded manually from .golden.json (isJovianVector throws for
/// pre-isthmus, so loadVectorSample cannot be used).
void runSingleVector(const std::string& id, const JsonValue& vec, Fixture& fixture,
    bool greenGuard, GoldenStats& stats)
{
    w6test::GoldenSample sample;
    sample.id = id;
    sample.vector = vec;
    sample.golden =
        w6test::loadJsonFile(std::string(OP_T8N_GOLDEN_ENGINE_DIR) + "/" + id + ".golden.json");
    const bool jovian = (jAt(jAt(vec, "_info"), "hardfork").asString() == "jovian");
    sample.jovian = jovian;
    try
    {
        opstack_test::seedPreState(fixture.multiLayerStorage, vec["pre"]);
    }
    catch (const std::exception& e)
    {
        BOOST_ERROR(id << ": seedPreState threw: " << e.what());
        return;
    }
    catch (...)
    {
        const auto* excType = abi::__cxa_current_exception_type();
        BOOST_ERROR(id << ": seedPreState threw (typed catch bypassed, exception type: "
                       << (excType ? excType->name() : "<unknown>") << ")");
        return;
    }
    bcos::protocol::BlockHeader::Ptr header;
    // Single-block vector header: isthmus/jovian use decodeGoldenHeader (golden authoritative
    // op-geth header); pre-isthmus (ecotone/fjord/granite) encodedHeaderHex would throw under
    // decodeOpHeader's strict 21-field decode (RTTI-bypass runtime_error, verified empirically),
    // so fall back to buildHeaderFromEnv (same source as chain). Both execute under the same cfg
    // (configAt → isthmusConfig); the golden three-way REPORTs a fork mismatch for pre-isthmus
    // (soft) and the six-way verify is expected to reject them (see runBlockEquivalence).
    const auto hardfork = jAt(jAt(vec, "_info"), "hardfork").asString();
    try
    {
        if (hardfork == "isthmus" || hardfork == "jovian")
            header = w6test::decodeGoldenHeader(sample);
        else
            header = buildHeaderFromEnv(jAt(vec, "env"));
    }
    catch (const std::exception& e)
    {
        BOOST_ERROR(id << ": decodeGoldenHeader threw: " << e.what());
        return;
    }
    catch (...)
    {
        const auto* excType = abi::__cxa_current_exception_type();
        BOOST_ERROR(id << ": decodeGoldenHeader threw (typed catch bypassed, exception type: "
                       << (excType ? excType->name() : "<unknown>") << ")");
        return;
    }
    // golden rawTransactions (single-block vector: deposit is already a 0x7E envelope, normal
    // already a 0x02 envelope).
    std::vector<bcos::bytes> rawTxBytes;
    for (const auto& raw : sample.golden["rawTransactions"])
        rawTxBytes.push_back(bcos::fromHex(raw.asString()));
    // The announced header carries the golden commitments (route A's six-way verify = the
    // FISCO-vs-op-geth gate).
    fillAnnouncedHeaderFromGolden(header, vec, rawTxBytes);
    // Named-lvalue first (see runBlockEquivalence's fork-parity comment): GCC-14
    // -Wdangling-reference false positive on a prvalue OpForkFlags argument.
    const auto forkFlags = forkFlagsFor(jovian);
    const auto& vectorCfg = op::configAt(forkFlags);
    runBlockEquivalence(id, fixture, header, rawTxBytes, vec, jovian, vectorCfg, greenGuard, stats);
}

/// Chain vector: per-block runBlockEquivalence with route A mergeView inheritance (authoritative
/// path). Block 0 pre is explicit; later blocks inherit the previous block's route A post-state.
/// Each block's announced header carries its golden commitments (isthmus/jovian → the six-way
/// verify is a hard FISCO-vs-op-geth gate).
void runChainVector(const std::string& id, const JsonValue& vec, Fixture& fixture,
    GoldenStats& stats)
{
    const auto& blocks = jAt(vec, "blocks");
    for (std::size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& blk = blocks[static_cast<Json::ArrayIndex>(i)];
        const std::string bid = id + "[" + std::to_string(i) + "]";
        if (blk.isMember("pre") && !blk["pre"].isNull())
            opstack_test::seedPreState(fixture.multiLayerStorage, blk["pre"]);
        const auto header = buildHeaderFromEnv(jAt(blk, "env"));
        const auto rawTxBytes = buildRawTxBytes(blk, bid);
        fillAnnouncedHeaderFromGolden(header, blk, rawTxBytes);
        const bool jovian = (jAt(jAt(blk, "_info"), "hardfork").asString() == "jovian");
        const auto forkFlags = forkFlagsFor(jovian);  // named-lvalue first (GCC-14 dangling false positive)
        const auto& vectorCfg = op::configAt(forkFlags);
        runBlockEquivalence(bid, fixture, header, rawTxBytes, blk, jovian, vectorCfg,
            /*greenGuard=*/false, stats);
        ++stats.chainBlocks;
    }
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpDualPathEquivalence)

BOOST_AUTO_TEST_CASE(Vectors)
{
    const fs::path vectorsDir = OP_T8N_VECTORS_DIR;
    BOOST_REQUIRE_MESSAGE(fs::is_directory(vectorsDir), vectorsDir);

    // Iterate the directory: skip the invalid_ prefix and _op_expected.reject; chain goes
    // through the blocks[] branch.
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(vectorsDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            const auto name = entry.path().filename().string();
            if (name.rfind("invalid_", 0) == 0)
                continue;
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());

    GoldenStats stats;
    for (const auto& name : names)
    {
        try
        {
            std::ifstream input(vectorsDir / name);
            const auto doc = jParse(input);
            std::string id;
            const JsonValue* vec = nullptr;
            for (const auto& key : doc.getMemberNames())
            {
                if (key == "_op_test_vectors")
                    continue;
                if (vec != nullptr)
                    throw std::runtime_error("more than one vector object in file");
                id = key;
                vec = &doc[key];
            }
            if (vec == nullptr)
                throw std::runtime_error("no vector object in file");
            if (id != fs::path(name).stem().string())
                throw std::runtime_error("vector id '" + id + "' != filename stem");

            Fixture fixture;
            if (vec->isMember("blocks"))
            {
                runChainVector(id, *vec, fixture, stats);
                continue;
            }
            if (vec->isMember("_op_expected") && (*vec)["_op_expected"].isMember("reject"))
                continue;  // reject vectors are consumed by OpT8nReplay/OpNewPayloadRpcE2e;
                           // harness skips them
            ++stats.flat;
            const bool greenGuard = (id.find("deposit_basefee_observer") != std::string::npos);
            runSingleVector(id, *vec, fixture, greenGuard, stats);
        }
        catch (const std::exception& e)
        {
            BOOST_ERROR(name << ": " << e.what());
        }
        catch (...)
        {
            const auto* excType = abi::__cxa_current_exception_type();
            BOOST_ERROR(name << ": exception escaped typed catch (exception type: "
                             << (excType ? excType->name() : "<unknown>")
                             << ") diag: " << boost::current_exception_diagnostic_information());
        }
    }

    // green guard + golden summary report.
    std::cout << "single-path summary: flat=" << stats.flat << " chainBlocks=" << stats.chainBlocks
              << " goldenMatch=" << stats.match << " goldenMismatch=" << stats.mismatch
              << " greenGuard=" << stats.greenGuardOk << "\n";
}

BOOST_AUTO_TEST_SUITE_END()
