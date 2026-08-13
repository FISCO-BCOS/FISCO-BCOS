// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpDualPathEquivalenceTest.cpp — OP dual-path execution equivalence harness (plan v3 Task 5,
// P1 red phase; Task 6 P1-8 surgery: route A moved to OpScheduler.executeBlock).
//
// Route A (from Task 6 onward: `OpScheduler.executeBlock` — skeleton drives the execute hook =
// route B, view lifecycle owned by the skeleton) vs route B (`runOpBlockInjection` direct call,
// per-tx injection loop, OpstackExecutor injectable entry) compared per-block under a dual fork on
// the t8n/vectors corpus (viewA = executed view the skeleton pushed into MLS, viewB = independent
// fork):
//   - hard (mechanics; any divergence is BOOST_ERROR): gasUsed / txRoot / receipt count / per-tx
//     status / gasUsed / cumulativeGasUsed / effectiveGasPrice / logsCount / log content
//     (topics/data/address).
//   - soft (ALLOWLIST-driven): stateRoot / seal five fields / per-tx output / _op_* (driven by
//     the opStackMeta() set, no hardcoded count). Following the `t8n/vectors/DIVERGENCES.md`
//     ALLOWLIST pattern (OpT8nReplayTest.cpp:266-328 DivergenceLedger), this harness uses the
//     `FINDING-dual-*` entryId prefix (coexists without conflict with the existing 6
//     contract_create entries).
//   - deposit_basefee×2 green guard: three-way consistent (A==B==golden), not listed in ALLOWLIST.
//   - golden three-way (path A.stateRoot == vector `_op_expected.header.stateRoot`) hardened in P3
//     but scoped (G1, round 4 ruling): for isthmus/jovian non-contract_create vectors, mismatch is
//     a hard BOOST_CHECK failure (guards against "both paths wrong together"); pre-isthmus (golden
//     fork mismatch is expected output) and contract_create (known route-A-vs-golden divergence in
//     the OpT8nReplay domain) stay soft REPORT.
//
// Fork model: `forkTimestampsFor(bool jovian)` + `configAt` (isthmus/jovian). In the corpus,
// ecotone/fjord/granite vectors execute consistently on both paths under isthmus semantics (both
// paths share the same cfg → A-vs-B still valid; golden three-way REPORTs mismatch due to fork
// mismatch, soft — pre-isthmus is outside the golden-hard scope).
//
// Round 3 P2 fork parity: route B explicitly computes cfg = configAt(timestamp/1000,
// forkTimestampsFor(jovian)) and passes it as arg 6 to runOpBlockInjection, asserting it resolves
// from the same source as route A's scheduler-internal configAt.
// (refactor) Route B consumes the block's Transaction objects (block order) + decoded DepositTx —
// no raw-tx parse, no normalTxs conversion.
// Review I-1 exception handling: per-vector catches use catch(std::exception)/catch(...) (route
// B's OpTxValidationFailed is a bcos::Exception, not a runtime_error; and libevmone -fno-rtti
// makes typed catch unreliable) — catch → BOOST_ERROR + continue, never judge divergence by
// exception type.
// Round 3 P4: has_storage scan (same-block create pre-triage; scan without pre-fixing) + /sys
// tripwire derived-table prefix assertion (accountTableName(addr) prefix == apps/).

#include "support/GoldenSample.h"
#include "support/OpDepositEncode.h"  // encodeDepositEnvelope (deposit envelope reconstruction)
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
#include <ethereum-executor/BCOS2Evmone.h>
#include <json/json.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpScheduler.h>  // route A surgery (Task 6 P1-8): executeBlock drives
#include <opstack-executor/OpSchedulerImpl.h>
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
inline const Json::Value& jAt(const Json::Value& v, const std::string& key)
{
    if (!v.isMember(key))
        throw std::invalid_argument("missing required field: " + key);
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

// ── Canonical printing (shared with OpT8nReplayTest.cpp:200-240) ─────────────────────
constexpr const char* kAbsent = "<absent>";

std::string hexU64(uint64_t v)
{
    std::ostringstream out;
    out << "0x" << std::hex << v;
    return out.str();
}

std::string hexU256(const intx::uint256& v)
{
    return "0x" + intx::to_string(v, 16);
}

std::string hexHash(const evmone::hash256& h)
{
    return "0x" + evmc::hex(evmc::bytes_view{h.bytes, sizeof(h.bytes)});
}

std::string hexAddr(const evmc::address& a)
{
    return "0x" + evmc::hex(evmc::bytes_view{a.bytes, sizeof(a.bytes)});
}

std::string hexBytes(evmc::bytes_view b)
{
    return "0x" + evmc::hex(b);
}

/// bcos::u256 -> "0x" + lowercase no-leading-zero hex (same as OpT8nReplayTest.cpp:457-460).
std::string hexU256Bcos(const bcos::u256& v)
{
    return "0x" + v.str(0, std::ios_base::hex);
}

// ── DivergenceLedger (OpT8nReplayTest.cpp:266-337 pattern; dedicated FINDING-dual- entryId prefix)
// ───── This harness shares t8n/vectors/DIVERGENCES.md with OpT8nReplayTest: each ledger only
// manages entries with its own prefix, so finish()'s stale check does not false-positive across
// suites (existing FINDING-create-output entries belong to OpT8nReplay; FINDING-dual-* to this
// harness).

struct AllowEntry
{
    std::string vectorId, field, entryId, attribution, status, want, got;
    bool exempt = false;
    int hits = 0;
};

class DivergenceLedger
{
public:
    // Missing ledger file = FAILURE (ledger is a gate deliverable; absence never means
    // all-exempt/all-empty).
    static DivergenceLedger load(const fs::path& path)
    {
        DivergenceLedger ledger;
        std::ifstream input(path);
        if (!input.is_open())
        {
            BOOST_ERROR("DIVERGENCES.md missing: " << path);
            return ledger;
        }
        static const std::regex linePattern(
            R"(<!--\s*ALLOWLIST\s+vectorId=(\S+)\s+field=(\S+)\s+entry=(\S+)\s+attribution=(\S+)\s+status=(\S+)\s+want=(\S+)\s+got=(\S+)\s*-->)");
        static const std::regex headingPattern(R"(^##\s+(\S+))");
        std::string line;
        std::set<std::string> headings;
        while (std::getline(input, line))
        {
            std::smatch m;
            if (std::regex_search(line, m, headingPattern))
                headings.insert(m[1].str());
            if (std::regex_search(line, m, linePattern))
            {
                AllowEntry e{m[1].str(), m[2].str(), m[3].str(), m[4].str(), m[5].str(), m[6].str(),
                    m[7].str()};
                e.exempt = (e.attribution == "a" && e.status == "PENDING-FIX") ||
                           (e.attribution == "c" && e.status == "SIGNED-OFF");
                // Scope: only FINDING-dual-* entries belong to this (A-vs-B) harness. The
                // FISCO↔op-geth entries (FINDING-create-output, ...) are managed by OpT8nReplay's
                // own ledger instance; each finish() only checks its own domain.
                if (e.entryId.rfind("FINDING-dual-", 0) == 0)
                    ledger.m_entries.push_back(std::move(e));
            }
        }
        for (const auto& e : ledger.m_entries)
        {
            if (!headings.contains(e.entryId))
                BOOST_ERROR("DIVERGENCES.md ALLOWLIST entry="
                            << e.entryId << " (vectorId=" << e.vectorId << " field=" << e.field
                            << ") has no matching '## " << e.entryId << "' heading");
        }
        return ledger;
    }

    void diverge(const std::string& vectorId, const std::string& field, const std::string& want,
        const std::string& got)
    {
        for (auto& e : m_entries)
        {
            if (e.exempt && e.vectorId == vectorId && e.field == field && e.want == want &&
                e.got == got)
            {
                ++e.hits;
                ++m_knownCount;
                std::cout << "KNOWN-DIVERGE " << vectorId << " " << e.entryId << " field=" << field
                          << " want=" << want << " got=" << got << "\n";
                return;
            }
        }
        BOOST_ERROR("DIVERGE " << vectorId << " " << field << " want=" << want << " got=" << got);
    }

    void finish() const
    {
        for (const auto& e : m_entries)
        {
            if (e.exempt && e.hits == 0)
                BOOST_ERROR("stale ALLOWLIST exemption never hit this run: entry="
                            << e.entryId << " vectorId=" << e.vectorId << " field=" << e.field
                            << " want=" << e.want << " got=" << e.got);
        }
    }

    int knownCount() const { return m_knownCount; }

private:
    std::vector<AllowEntry> m_entries;
    int m_knownCount = 0;
};

// ── Per-vector comparison context ─────────────────────────────────────────────
struct VectorContext
{
    DivergenceLedger& ledger;
    std::string id;
    int comparisons = 0;

    // soft: goes through ALLOWLIST (unlisted => BOOST_ERROR).
    void checkField(const std::string& field, const std::string& want, const std::string& got)
    {
        ++comparisons;
        if (want != got)
            ledger.diverge(id, field, want, got);
    }

    void checkOptional(const std::string& field, const std::optional<std::string>& want,
        const std::optional<std::string>& got)
    {
        ++comparisons;
        if (!want.has_value() && !got.has_value())
            return;
        const auto w = want.value_or(kAbsent);
        const auto g = got.value_or(kAbsent);
        if (w != g)
            ledger.diverge(id, field, w, g);
    }

    // hard: mechanics; any divergence is a failure (no ALLOWLIST).
    void checkHard(const std::string& field, const std::string& want, const std::string& got)
    {
        ++comparisons;
        if (want != got)
            BOOST_ERROR(id << ": HARD-DIVERGE " << field << " want=" << want << " got=" << got);
    }
};

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
using ViewType = typename MLS::ViewType;

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

bcos::evm::opstack::OpForkTimestamps forkTimestampsFor(bool jovian)
{
    return bcos::evm::opstack::OpForkTimestamps{
        .isthmusTime = 0,
        .jovianTime = jovian ? 0 : std::numeric_limits<uint64_t>::max(),
    };
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
/// parentBeaconBlockRoot/extraData/blobGasUsed (OpRlpDecode.h:106-121, optional fields .value());
/// parentInfo serves RecentBlockHashes.
bcostars::protocol::BlockHeaderImpl::Ptr buildHeaderFromEnv(const Json::Value& env)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    const int64_t number =
        static_cast<int64_t>(w6test::jsonU64(jAt(env, "currentNumber").asString()));
    h->setNumber(number);
    // FISCO tars store milliseconds; the vector currentTimestamp is in seconds (OP semantics).
    // toBlockInfo then /1000 restores seconds.
    h->setTimestamp(
        static_cast<int64_t>(w6test::jsonU64(jAt(env, "currentTimestamp").asString()) * 1000));
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
            dep.from = w6test::jsonAddress(jAt(d, "from").asString());
            dep.to = jAt(d, "to").isNull() ?
                         std::nullopt :
                         std::optional{w6test::jsonAddress(jAt(d, "to").asString())};
            dep.mint = d.isMember("mint") ?
                           std::optional{w6test::jsonU256(jAt(d, "mint").asString())} :
                           std::nullopt;
            dep.value = d.isMember("value") ? w6test::jsonU256(jAt(d, "value").asString()) :
                                              intx::uint256{0};
            dep.gas_limit = static_cast<int64_t>(w6test::jsonU64(jAt(d, "gas").asString()));
            dep.is_system_tx = jAt(d, "is_system_tx").asBool();
            dep.data = w6test::jsonBytes(jAt(t, "data").asString());
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

/// FISCO tx for block assembly (Task 6 P1-8 harness surgery): built from the raw envelope
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

/// Backfill the announced header with route B's real commitments (Task 6 P1-8 surgery): once
/// route A goes through OpScheduler.executeBlock, the skeleton's verifyResult always does the
/// six-field comparison for OP (OpScheduler.h verifyResult ignores the verify boolean) — the
/// announced header must carry real commitments (finishExecute only fills the commitment fields,
/// leaving the rest incomplete). Run route B first and backfill from resultB so route A's verify
/// passes on re-run (dual-path equivalence ⇒ commitments agree).
/// Fields match OpSchedulerTest.cpp:191-204 fillAnnouncedHeader. blobGasUsed does not participate
/// in OP execution (opstack never reads blk.blob_gas_used; blob txs are rejected by opValidate),
/// it is only consumed by the seal six-field comparison — filling it does not change execution.
void fillAnnouncedHeader(bcos::protocol::BlockHeader::Ptr const& header,
    bcos::evm::engine::OpExecuteBlockResult const& result)
{
    header->setStateRoot(result.stateRoot);
    header->setTxsRoot(result.txRoot);
    header->setReceiptsRoot(detail::toBcosH256(result.seal.receiptsRoot));
    header->setGasUsed(bcos::u256(result.gasUsed));
    header->setLogsBloom(bcos::bytesConstRef(result.seal.logsBloom.bytes, 256));
    header->setWithdrawalsRoot(detail::toBcosH256(result.seal.withdrawalsRoot));
    if (result.seal.requestsHash.has_value())
        header->setRequestsHash(detail::toBcosH256(*result.seal.requestsHash));
    if (result.seal.blobGasUsed.has_value())
        header->setBlobGasUsed(bcos::u256(*result.seal.blobGasUsed));
}

/// Driven by the opStackMeta set: deposit (envelope first byte 0x7E) only carries nonce/version;
/// normal carries the fee fields. The field set has no hardcoded count — extracted dynamically
/// from the fields actually present in receipt->opStackMeta().
std::map<std::string, std::string> opMetaFields(
    const bcos::protocol::TransactionReceipt::Ptr& receipt, bool isDeposit)
{
    std::map<std::string, std::string> out;
    const auto& meta = receipt->opStackMeta();
    if (!meta)
        return out;
    if (isDeposit)
    {
        if (meta->deposit_nonce.has_value())
            out["_op_deposit_nonce"] = hexU64(*meta->deposit_nonce);
        if (meta->deposit_receipt_version.has_value())
            out["_op_deposit_receipt_version"] = hexU64(*meta->deposit_receipt_version);
        return out;
    }
    if (meta->l1_fee.has_value())
        out["_op_l1_fee"] = hexU256Bcos(*meta->l1_fee);
    if (meta->l1_gas_price.has_value())
        out["_op_l1_gas_price"] = hexU256Bcos(*meta->l1_gas_price);
    if (meta->l1_blob_base_fee.has_value())
        out["_op_l1_blob_base_fee"] = hexU256Bcos(*meta->l1_blob_base_fee);
    if (meta->l1_gas_used.has_value())
        out["_op_l1_gas_used"] = hexU64(*meta->l1_gas_used);
    if (meta->l1_base_fee_scalar.has_value())
        out["_op_l1_base_fee_scalar"] = hexU64(*meta->l1_base_fee_scalar);
    if (meta->l1_blob_base_fee_scalar.has_value())
        out["_op_l1_blob_base_fee_scalar"] = hexU64(*meta->l1_blob_base_fee_scalar);
    if (meta->operator_fee.has_value())
        out["_op_operator_fee"] = hexU256Bcos(*meta->operator_fee);
    if (meta->operator_fee_scalar.has_value())
        out["_op_operator_fee_scalar"] = hexU64(*meta->operator_fee_scalar);
    if (meta->operator_fee_constant.has_value())
        out["_op_operator_fee_constant"] = hexU64(*meta->operator_fee_constant);
    if (meta->da_footprint.has_value())
        out["_op_da_footprint"] = hexU64(*meta->da_footprint);
    if (meta->da_footprint_gas_scalar.has_value())
        out["_op_da_footprint_gas_scalar"] = hexU64(*meta->da_footprint_gas_scalar);
    return out;
}

/// stateRoot divergence diagnostics: visitAccounts over both views, address-keyed
/// balance/nonce/codeHash/storage, capped at 20 entries.
/// Storage2State construction takes Storage& (non-const), so parameters are non-const refs
/// (viewA/viewB are local forks).
void dumpAccountDiff(ViewType& viewA, ViewType& viewB)
{
    struct AccView
    {
        intx::uint256 balance;
        uint64_t nonce;
        evmc::bytes32 codeHash;
        std::map<evmc::bytes32, evmc::bytes32> storage;
    };
    std::map<evmc::address, AccView> accsA, accsB;
    const auto collect = [](ViewType& view, std::map<evmc::address, AccView>& out) {
        bcos::evm::evmstate::Storage2State<ViewType> bridge(view);
        bridge.visitAccounts([&](auto const& acc) {
            AccView& v = out[acc.addr];
            v.balance = acc.balance;
            v.nonce = acc.nonce;
            v.codeHash = acc.codeHash;
            v.storage = acc.storage;
            return true;
        });
    };
    collect(viewA, accsA);
    collect(viewB, accsB);
    std::set<evmc::address> addrs;
    for (const auto& [a, v] : accsA)
        addrs.insert(a);
    for (const auto& [a, v] : accsB)
        addrs.insert(a);
    int printed = 0;
    for (const auto& a : addrs)
    {
        if (printed >= 20)
            break;
        const auto itA = accsA.find(a);
        const auto itB = accsB.find(a);
        const bool pA = itA != accsA.end();
        const bool pB = itB != accsB.end();
        if (pA && pB)
        {
            const auto& x = itA->second;
            const auto& y = itB->second;
            if (x.balance == y.balance && x.nonce == y.nonce && x.codeHash == y.codeHash &&
                x.storage == y.storage)
                continue;
        }
        std::cout << "  account-diff " << hexAddr(a) << " A=" << (pA ? "present" : "absent")
                  << " B=" << (pB ? "present" : "absent");
        if (pA && pB)
        {
            std::cout << " balanceA=" << hexU256(itA->second.balance)
                      << " balanceB=" << hexU256(itB->second.balance)
                      << " nonceA=" << itA->second.nonce << " nonceB=" << itB->second.nonce;
        }
        std::cout << "\n";
        ++printed;
    }
}

/// /sys tripwire (round 3 decision): derived-table prefix assertion — for every vector
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
                    bcos::evm::evmstate::accountTableName(w6test::jsonAddress(j[k].asString())));
        }
    };
    if (vec.isMember("pre"))
    {
        for (const auto& a : vec["pre"].getMemberNames())
            tables.insert(bcos::evm::evmstate::accountTableName(w6test::jsonAddress(a)));
    }
    if (vec.isMember("postState"))
    {
        for (const auto& a : vec["postState"].getMemberNames())
            tables.insert(bcos::evm::evmstate::accountTableName(w6test::jsonAddress(a)));
    }
    if (vec.isMember("env"))
    {
        const auto& cb = vec["env"]["currentCoinbase"];
        if (cb.isString())
            tables.insert(
                bcos::evm::evmstate::accountTableName(w6test::jsonAddress(cb.asString())));
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

/// has_storage scan (round 3 P4; scan without pre-fixing): P1 scans same-block creates for
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

// ── assertEquivalent：A vs B ──────────────────────────────────────────────────

void assertEquivalent(const std::string& id, const engine::OpExecuteBlockResult& resultA,
    const engine::OpExecuteBlockResult& resultB, ViewType& viewA, ViewType& viewB,
    const std::vector<bcos::bytes>& rawTxBytes, DivergenceLedger& ledger)
{
    VectorContext ctx{ledger, id};

    // hard: block-level mechanics.
    ctx.checkHard("gasUsed", hexU64(resultA.gasUsed), hexU64(resultB.gasUsed));
    ctx.checkHard("txRoot", resultA.txRoot.hexPrefixed(), resultB.txRoot.hexPrefixed());

    // soft: stateRoot (ALLOWLIST-driven) + seal five fields.
    ctx.checkField("stateRoot", resultA.stateRoot.hexPrefixed(), resultB.stateRoot.hexPrefixed());
    ctx.checkField("seal.receiptsRoot", hexHash(resultA.seal.receiptsRoot),
        hexHash(resultB.seal.receiptsRoot));
    ctx.checkField("seal.logsBloom", hexBytes(evmc::bytes_view(resultA.seal.logsBloom)),
        hexBytes(evmc::bytes_view(resultB.seal.logsBloom)));
    ctx.checkField("seal.withdrawalsRoot", hexHash(resultA.seal.withdrawalsRoot),
        hexHash(resultB.seal.withdrawalsRoot));
    ctx.checkOptional("seal.requestsHash",
        resultA.seal.requestsHash.has_value() ? std::optional{hexHash(*resultA.seal.requestsHash)} :
                                                std::nullopt,
        resultB.seal.requestsHash.has_value() ? std::optional{hexHash(*resultB.seal.requestsHash)} :
                                                std::nullopt);
    ctx.checkOptional("seal.blobGasUsed",
        resultA.seal.blobGasUsed.has_value() ? std::optional{hexU64(*resultA.seal.blobGasUsed)} :
                                               std::nullopt,
        resultB.seal.blobGasUsed.has_value() ? std::optional{hexU64(*resultB.seal.blobGasUsed)} :
                                               std::nullopt);

    // Divergence diagnostics: dumpAccountDiff when stateRoot differs (capped at 20).
    if (resultA.stateRoot != resultB.stateRoot)
    {
        std::cout << "  stateRoot-divergence " << id << " A=" << resultA.stateRoot.hexPrefixed()
                  << " B=" << resultB.stateRoot.hexPrefixed() << "\n";
        dumpAccountDiff(viewA, viewB);
    }

    // per-receipt hard + soft。
    if (resultA.receipts.size() != resultB.receipts.size())
    {
        BOOST_ERROR(id << ": receipt count mismatch: A=" << resultA.receipts.size()
                       << " B=" << resultB.receipts.size() << " (no zip-min)");
        return;
    }
    for (std::size_t i = 0; i < resultA.receipts.size(); ++i)
    {
        const std::string p = "receipts[" + std::to_string(i) + "]";
        const auto& ra = resultA.receipts[i];
        const auto& rb = resultB.receipts[i];
        // F1: the type discriminator is derived from rawTxBytes (envelope first byte 0x7E =
        // deposit); both paths share the same input so it is inherently consistent. "type" is not
        // part of the A-vs-B comparison (tautology); it only drives opStackMeta field extraction.
        const bool isDeposit = !rawTxBytes[i].empty() && rawTxBytes[i][0] == 0x7e;

        ctx.checkHard(p + ".status", std::to_string(ra->status()), std::to_string(rb->status()));
        ctx.checkHard(p + ".gasUsed", hexU64(op::narrowGasUsed(ra->gasUsed())),
            hexU64(op::narrowGasUsed(rb->gasUsed())));
        ctx.checkHard(p + ".cumulativeGasUsed", std::string(ra->cumulativeGasUsed()),
            std::string(rb->cumulativeGasUsed()));
        ctx.checkHard(p + ".effectiveGasPrice", std::string(ra->effectiveGasPrice()),
            std::string(rb->effectiveGasPrice()));
        ctx.checkHard(p + ".logsCount", std::to_string(ra->logEntries().size()),
            std::to_string(rb->logEntries().size()));

        // v2 (B8): log content (address/topics/data) — same count with different content would
        // only surface in logsBloom, so catch it here in P1.
        const auto la = ra->logEntries();
        const auto lb = rb->logEntries();
        if (la.size() == lb.size())
        {
            for (std::size_t k = 0; k < la.size(); ++k)
            {
                const std::string lp = p + ".logs[" + std::to_string(k) + "]";
                const auto addrA = la[k].address();
                const auto addrB = lb[k].address();
                ctx.checkHard(lp + ".address",
                    "0x" + bcos::toHex(bcos::bytesConstRef(
                               reinterpret_cast<const bcos::byte*>(addrA.data()), addrA.size())),
                    "0x" + bcos::toHex(bcos::bytesConstRef(
                               reinterpret_cast<const bcos::byte*>(addrB.data()), addrB.size())));
                const auto ta = la[k].topics();
                const auto tb = lb[k].topics();
                ctx.checkHard(
                    lp + ".topicsCount", std::to_string(ta.size()), std::to_string(tb.size()));
                for (std::size_t q = 0; q < std::min(ta.size(), tb.size()); ++q)
                    ctx.checkHard(
                        lp + ".topics[" + std::to_string(q) + "]", ta[q].hex(), tb[q].hex());
                const auto da = la[k].data();
                const auto db = lb[k].data();
                ctx.checkHard(lp + ".data", "0x" + bcos::toHex(da), "0x" + bcos::toHex(db));
            }
        }

        // soft: output + _op_* (opStackMeta-set-driven).
        ctx.checkField(p + ".output",
            hexBytes(evmc::bytes_view{ra->output().data(), ra->output().size()}),
            hexBytes(evmc::bytes_view{rb->output().data(), rb->output().size()}));
        const auto metaA = opMetaFields(ra, isDeposit);
        const auto metaB = opMetaFields(rb, isDeposit);
        std::set<std::string> metaKeys;
        for (const auto& [k, v] : metaA)
            metaKeys.insert(k);
        for (const auto& [k, v] : metaB)
            metaKeys.insert(k);
        for (const auto& k : metaKeys)
        {
            const auto itA = metaA.find(k);
            const auto itB = metaB.find(k);
            ctx.checkOptional(p + "." + k,
                itA != metaA.end() ? std::optional{itA->second} : std::nullopt,
                itB != metaB.end() ? std::optional{itB->second} : std::nullopt);
        }
    }

    if (ctx.comparisons == 0)
        BOOST_ERROR(id << ": zero comparisons executed");
}

/// golden three-way (G1: soft in P1 → scoped hard in P3): path A.stateRoot == vector
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

/// Single-block equivalence run: seedPreState is already done externally; this function forks
/// viewB first (route A pushes the executed view into MLS, so viewB must read pre-state), calls
/// route B directly, backfills the announced header with resultB, runs route A through
/// OpScheduler.executeBlock, takes resultA + viewA from the skeleton (fork reads through to the
/// executed view the skeleton pushed), assertEquivalent, then mergeBackStorage persists route A's
/// authoritative post-state (viewB is discarded after comparison).
void runBlockEquivalence(const std::string& id, Fixture& fixture,
    bcos::protocol::BlockHeader::Ptr const& header, const std::vector<bcos::bytes>& rawTxBytes,
    const JsonValue& vec, bool jovian, const bcos::evm::opstack::OpForkConfig& vectorCfg,
    DivergenceLedger& ledger, bool greenGuard, GoldenStats& stats)
{
    // Round 3 P2 fork parity: cfg = configAt(timestamp/1000, forkTimestampsFor(jovian)), resolved
    // from the same source as route A's scheduler-internal configAt (same forkTimestampsFor, same
    // static singleton object).
    const auto& cfg =
        op::configAt(static_cast<uint64_t>(header->timestamp()) / 1000, forkTimestampsFor(jovian));
    BOOST_CHECK_MESSAGE(&cfg == &vectorCfg, id << ": fork parity broken: block cfg != vector cfg");

    // deposits + block-order transactions（buildBlockTx 已支持 deposit，Web3Transaction 0x7e）。
    std::vector<op::DepositTx> deposits;
    std::vector<bcos::protocol::Transaction::ConstPtr> transactions;
    transactions.reserve(rawTxBytes.size());
    for (auto const& raw : rawTxBytes)
    {
        if (raw[0] == static_cast<uint8_t>(op::kDepositTxType))
            deposits.push_back(detail::decodeDepositTx(raw));
        auto tx = buildBlockTx(raw, fixture.hashImpl);
        if (tx == nullptr)
        {
            BOOST_ERROR(id << ": buildBlockTx failed");
            return;
        }
        transactions.push_back(tx);
    }

    // Route B: runOpBlockInjection (per-tx injection loop) — run first, backfill the announced
    // header from resultB for route A's verify (see fillAnnouncedHeader). viewB forks before
    // route A — route A's executeBlock pushes the executed view into MLS, so viewB must read
    // pre-state. viewB lives for the whole comparison (dumpAccountDiff reads it).
    auto viewB = fixture.multiLayerStorage.fork();
    viewB.newMutable();
    engine::OpExecuteBlockResult resultB;
    try
    {
        bcos::ledger::LedgerConfig ledgerConfig;
        ledgerConfig.setEVMCRevision(cfg.rev);
        ledgerConfig.setGasLimit({30000000, 0});
        ledgerConfig.setGasPrice({"0x0", 0});
        bcos::executor_v1::opstack::OpstackExecutor executor{
            fixture.receiptFactory, fixture.hashImpl, cfg};
        resultB = engine::runOpBlockInjection(executor, viewB, *header, transactions, deposits,
            cfg, kChainId, ledgerConfig, rawTxBytes, fixture.hashImpl);
    }
    catch (const std::exception& e)
    {
        BOOST_ERROR(id << ": route B threw: " << e.what());
        return;
    }
    catch (...)
    {
        const auto* excType = abi::__cxa_current_exception_type();
        BOOST_ERROR(id << ": route B threw (typed catch bypassed, exception type: "
                       << (excType ? excType->name() : "<unknown>") << ")");
        return;
    }

    // Backfill the announced header with route B's real commitments (route A's verify always
    // does the six-field comparison and needs the commitments; blobGasUsed does not participate in
    // OP execution, only consumed by the seal comparison).
    fillAnnouncedHeader(header, resultB);

    // Route A (Task 6 P1-8 surgery): OpScheduler.executeBlock — view lifecycle owned by the
    // skeleton (fork/pushView inside it), execute hook is route B (runOpBlockInjection). Result is
    // taken from the skeleton's m_results (peekExecuteResult); post-state from the executed view
    // the skeleton pushed into MLS (fork reads through).
    engine::OpExecuteBlockResult resultA;
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
            fixture.receiptFactory, fixture.hashImpl, kChainId, forkTimestampsFor(jovian),
            fixture.blockFactory, fixture.multiLayerStorage, /*ledger=*/nullptr);

        bcos::Error::Ptr routeAErr;
        opScheduler->executeBlock(block, /*verify=*/true,
            [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr, bool) {
                routeAErr = std::move(e);
            });
        if (routeAErr)
        {
            BOOST_ERROR(id << ": route A executeBlock failed: " << routeAErr->errorMessage());
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

    // viewA: route A post-state (the executed view the skeleton pushed into MLS, from the
    // skeleton's storage).
    auto viewA = fixture.multiLayerStorage.fork();

    // A-vs-B comparison (hard all-green + soft ALLOWLIST).
    try
    {
        assertEquivalent(id, resultA, resultB, viewA, viewB, rawTxBytes, ledger);

        // Chain inheritance: route A's authoritative post-state was already pushed into MLS by
        // the skeleton's pushView; only mergeBackStorage to persist (do not pushView again —
        // mergeView would push another layer; viewB is discarded after comparison).
        bcos::task::syncWait(fixture.multiLayerStorage.mergeBackStorage());

        // golden three-way (G1, P3 scoped hard): isthmus/jovian non-contract_create → hard;
        // pre-isthmus (expected fork mismatch) and contract_create (known divergence) stay soft
        // REPORT.
        if (vec.isMember("_op_expected"))
        {
            const auto hardfork = jAt(jAt(vec, "_info"), "hardfork").asString();
            const bool hardGolden = (hardfork == "isthmus" || hardfork == "jovian") &&
                                    (id.find("contract_create") == std::string::npos);
            reportGolden(id, vec, resultA.stateRoot, greenGuard, hardGolden, stats);
        }

        // /sys tripwire (derived table prefix for every vector pre/postState/tx/coinbase).
        checkSysTripwire(id, vec);

        // has_storage scan (P4 pre-triage; REPORT only).
        hasStorageScan(id, deposits);
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

/// Single-block vector: seedPreState → runBlockEquivalence. golden is loaded manually from
/// .golden.json (isJovianVector throws for pre-isthmus, so loadVectorSample cannot be used).
void runSingleVector(const std::string& id, const JsonValue& vec, Fixture& fixture,
    DivergenceLedger& ledger, bool greenGuard, GoldenStats& stats)
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
        w6test::seedPreState(fixture.multiLayerStorage, vec["pre"]);
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
    // so fall back to buildHeaderFromEnv (same source as chain). Both paths share the same cfg
    // (configAt → isthmusConfig), so the A-vs-B equivalence comparison still holds; the golden
    // three-way REPORTs mismatch due to fork mismatch (soft in P1).
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
    const auto& vectorCfg =
        op::configAt(static_cast<uint64_t>(header->timestamp()) / 1000, forkTimestampsFor(jovian));
    runBlockEquivalence(
        id, fixture, header, rawTxBytes, vec, jovian, vectorCfg, ledger, greenGuard, stats);
}

/// Chain vector: dual fork per block, A/B compare, then route A mergeView inherits
/// (authoritative path). Block 0 pre is explicit; later blocks inherit the previous block's
/// route A post-state.
void runChainVector(const std::string& id, const JsonValue& vec, Fixture& fixture,
    DivergenceLedger& ledger, GoldenStats& stats)
{
    const auto& blocks = jAt(vec, "blocks");
    for (std::size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& blk = blocks[static_cast<Json::ArrayIndex>(i)];
        const std::string bid = id + "[" + std::to_string(i) + "]";
        if (blk.isMember("pre") && !blk["pre"].isNull())
            w6test::seedPreState(fixture.multiLayerStorage, blk["pre"]);
        const auto header = buildHeaderFromEnv(jAt(blk, "env"));
        const auto rawTxBytes = buildRawTxBytes(blk, bid);
        const bool jovian = (jAt(jAt(blk, "_info"), "hardfork").asString() == "jovian");
        const auto& vectorCfg = op::configAt(
            static_cast<uint64_t>(header->timestamp()) / 1000, forkTimestampsFor(jovian));
        runBlockEquivalence(bid, fixture, header, rawTxBytes, blk, jovian, vectorCfg, ledger,
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

    auto ledger = DivergenceLedger::load(vectorsDir / "DIVERGENCES.md");

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
                runChainVector(id, *vec, fixture, ledger, stats);
                continue;
            }
            if (vec->isMember("_op_expected") && (*vec)["_op_expected"].isMember("reject"))
                continue;  // reject vectors are consumed by OpT8nReplay/OpNewPayloadRpcE2e;
                           // harness skips them
            ++stats.flat;
            const bool greenGuard = (id.find("deposit_basefee_observer") != std::string::npos);
            runSingleVector(id, *vec, fixture, ledger, greenGuard, stats);
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
    std::cout << "dual-path summary: flat=" << stats.flat << " chainBlocks=" << stats.chainBlocks
              << " goldenMatch=" << stats.match << " goldenMismatch=" << stats.mismatch
              << " greenGuard=" << stats.greenGuardOk << "\n";

    ledger.finish();
    std::cout << "dual-path KNOWN-DIVERGE total=" << ledger.knownCount() << "\n";
}

BOOST_AUTO_TEST_SUITE_END()
