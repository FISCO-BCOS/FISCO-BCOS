// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpDualPathEquivalenceTest.cpp — OP 双路径执行等价性 harness（plan v3 Task 5，P1 红阶段）。
//
// 路线 A（`OpSchedulerImpl::executeOpBlock`，生产单路径，Storage2State 桥）vs 路线 B
// （`runOpBlockInjection`，逐笔注入循环，OpstackExecutor 注入式入口）在 t8n/vectors 语料上
// 逐块双 fork（viewA/viewB 各持独立 MLS view）对比：
//   - hard（mechanics，任何分叉即 BOOST_ERROR）：gasUsed / txRoot / receipt 数 / 每笔 status /
//     gasUsed / cumulativeGasUsed / effectiveGasPrice / logsCount / log
//     内容（topics/data/address）。
//   - soft（ALLOWLIST 驱动）：stateRoot / seal 五字段 / 每笔 output / _op_*（从 opStackMeta()
//     集合驱动，不硬编码数量）。沿 `t8n/vectors/DIVERGENCES.md` ALLOWLIST 范式
//     （OpT8nReplayTest.cpp:266-328 DivergenceLedger），本 harness 条目 entryId 用
//     `FINDING-dual-*` 前缀（与既有 6 条 contract_create 条目并存不冲突）。
//   - deposit_basefee×2 绿守卫：三方一致（A==B==golden），不列 ALLOWLIST。
//   - golden 三方（path A.stateRoot == 向量 `_op_expected.header.stateRoot`）P3 翻硬但带作用域
//     （G1，第四轮裁定）：isthmus/jovian 非 contract_create 向量 mismatch 即 BOOST_CHECK 硬失败
//     （防「两路径一起错」）；pre-isthmus（golden fork 不匹配是预期产物）与 contract_create
//     （OpT8nReplay 域已知 route-A-vs-golden 分歧）保持软 REPORT。
//
// fork 模型：`forkTimestampsFor(bool jovian)` + `configAt`（isthmus/jovian）。语料中
// ecotone/fjord/granite 向量按 isthmus 语义双路径一致执行（两路径同 cfg → A-vs-B 仍有效；
// golden 三方因 fork 不匹配 REPORT mismatch，软——pre-isthmus 在 golden-hard 作用域外）。
//
// 第三轮 P2 fork 平价：路径 B 显式算 cfg = configAt(timestamp/1000, forkTimestampsFor(jovian))
// 传给 runOpBlockInjection 第 6 参，并断言与路径 A scheduler 内部 configAt 解析同源。
// 第三轮 P3 normalTxs 对齐：normalTxs[k] = 第 k 个非 deposit 交易（跳过 deposit、按块内序），
// 对齐注入器 normalIdx 仅非 deposit 分支 ++。
// 审查 I-1 异常捕获：逐向量 catch 用 catch(std::exception)/catch(...)（路径 B 的
// OpTxValidationFailed 是 bcos::Exception 非 runtime_error；且 libevmone -fno-rtti 使 typed
// catch 不可靠）——catch → BOOST_ERROR + 继续，不因异常类型差异判 divergence。
// 第三轮 P4：has_storage 扫描（同块 create 提前 triage，先扫不预先修）+ /sys tripwire 派生
// 表前缀断言（accountTableName(addr) 前缀 == apps/）+ assertCanonicalRoundTrip（decodeOneRawTx
// 内部已含 whole-envelope round-trip）。

#include "support/GoldenSample.h"
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
#include <opstack-executor/OpBlockInjector.h>
#include <opstack-executor/OpSchedulerImpl.h>
#include <opstack-executor/OpTxDecode.h>
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

// ── jsoncpp .at() equivalent（OpT8nReplayTest.cpp:60-84 同源）─────────────────
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

// ── Canonical printing（OpT8nReplayTest.cpp:200-240 同源）─────────────────────
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

/// bcos::u256 -> "0x" + lowercase no-leading-zero hex（同 OpT8nReplayTest.cpp:457-460）。
std::string hexU256Bcos(const bcos::u256& v)
{
    return "0x" + v.str(0, std::ios_base::hex);
}

// ── DivergenceLedger（OpT8nReplayTest.cpp:266-337 范式；entryId 前缀 FINDING-dual- 专属）─────
// 本 harness 与 OpT8nReplayTest 共享 t8n/vectors/DIVERGENCES.md：各自 ledger 只管理自己前缀的
// 条目，避免 finish() stale 检查跨套件误报（既有 FINDING-create-output 条目归 OpT8nReplay；
// FINDING-dual-* 归本 harness）。

struct AllowEntry
{
    std::string vectorId, field, entryId, attribution, status, want, got;
    bool exempt = false;
    int hits = 0;
};

class DivergenceLedger
{
public:
    // Missing ledger file = FAILURE（ledger 是 gate 交付物；缺失绝不等于 all-exempt/all-empty）。
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

    // soft：走 ALLOWLIST（未列出即 BOOST_ERROR）。
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

    // hard：mechanics，任何分叉即失败（不经 ALLOWLIST）。
    void checkHard(const std::string& field, const std::string& want, const std::string& got)
    {
        ++comparisons;
        if (want != got)
            BOOST_ERROR(id << ": HARD-DIVERGE " << field << " want=" << want << " got=" << got);
    }
};

// ── Fixture（OpNewPayloadRpcE2eTest.cpp:48-167 镜像）──────────────────────────
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
    // 单桶 CONCURRENT 后端：stateRoot 的 range(SYS_TABLES) 扫描依赖 RANGE_SEEK 语义，多桶扫错
    // （OpNewPayloadRpcE2eTest.cpp:147-152）。
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

// ── 帮助函数 ──────────────────────────────────────────────────────────────────

/// bcos::h256 from a vector hex string, tolerating "0"/"0x0" (zero). jsoncpp vectors write
/// prev_randao as "0x0" when zero; bcos::h256(string) needs full 64 hex.
bcos::h256 jsonH256(const std::string& s)
{
    if (s.empty() || s == "0" || s == "0x0")
        return bcos::h256{};
    return bcos::h256(s);
}

/// bcos::u256 from a vector hex string ("0x...")——boost cpp_int 原生解析 0x 前缀。
bcos::u256 jsonBcosU256(const std::string& s)
{
    return bcos::u256(s);
}

/// chain 向量无 golden：从 env（当前块）构建 FISCO BlockHeaderImpl。toBlockInfo 读
/// number/timestamp/gasLimit/baseFee/coinbase/prevRandao/parentBeaconBlockRoot/extraData/
/// blobGasUsed（OpRlpDecode.h:106-121，可选字段 .value()）；parentInfo 服务 RecentBlockHashes。
bcostars::protocol::BlockHeaderImpl::Ptr buildHeaderFromEnv(const Json::Value& env)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    const int64_t number =
        static_cast<int64_t>(w6test::jsonU64(jAt(env, "currentNumber").asString()));
    h->setNumber(number);
    // FISCO tars 存毫秒；向量 currentTimestamp 是秒（OP 语义）。toBlockInfo 再 /1000 取回秒。
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

/// 从向量 block.transactions 构建 raw envelope 字节（供 txRoot / executeOpBlock 解码）：
/// deposit → 从 _op_deposit 重建 DepositTx → canonicalEnvelopeBytes（OpTxDecode.h:307）；
/// normal → _op_raw 原样。断言 _op_raw 存在（链向量每笔 normal 都带）。
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
            bcos::evm::opstack::OpBlockTx btx{dep, {}};
            rawTxBytes.push_back(detail::canonicalEnvelopeBytes(btx));
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

/// evmone tx 字段级相等（buildFiscoTx pre-flight）：bcosTransactionToEvmone 不搬 r/s/v
/// （签名在 tars 的 signature* 字段，该函数不读）；sender 匹配等价覆盖 r/s/v 解码一致性
/// （decodeOneRawTx 从 r/s/v ecrecover，opEnvelopeToTars 从信封签名者提取）。
/// First mismatching field name (diagnostic; "<none>" when equal).
std::string evmoneTxFieldDiff(
    const evmone::state::Transaction& a, const evmone::state::Transaction& b)
{
    if (a.type != b.type)
        return "type";
    if (a.sender != b.sender)
        return "sender";
    if (a.to != b.to)
        return "to";
    if (a.nonce != b.nonce)
        return "nonce";
    if (a.gas_limit != b.gas_limit)
        return "gas_limit";
    if (a.max_gas_price != b.max_gas_price)
        return "max_gas_price";
    if (a.max_priority_gas_price != b.max_priority_gas_price)
        return "max_priority_gas_price";
    if (a.value != b.value)
        return "value";
    if (a.data != b.data)
        return "data";
    if (a.access_list != b.access_list)
        return "access_list";
    if (a.authorization_list.size() != b.authorization_list.size())
        return "authorization_list";
    for (std::size_t i = 0; i < a.authorization_list.size(); ++i)
    {
        const auto& x = a.authorization_list[i];
        const auto& y = b.authorization_list[i];
        if (x.chain_id != y.chain_id)
            return "authorization_list[" + std::to_string(i) + "].chain_id";
        if (x.addr != y.addr)
            return "authorization_list[" + std::to_string(i) + "].addr";
        if (x.nonce != y.nonce)
            return "authorization_list[" + std::to_string(i) + "].nonce";
        if (x.v != y.v)
            return "authorization_list[" + std::to_string(i) + "].v";
        if (x.r != y.r)
            return "authorization_list[" + std::to_string(i) + "].r";
        if (x.s != y.s)
            return "authorization_list[" + std::to_string(i) + "].s";
        // signer 不参与：decodeOneRawTx 留空（恢复延迟到 evmone process_authorization_list），
        // opEnvelopeToTars 预填（解码时已恢复）——执行等价，仅表示差异。
    }
    return "<none>";
}

/// buildFiscoTx pre-flight 字段级校验。**不含 chain_id**：bcosTransactionToEvmone 把 tars 的
/// chainID 十进制串（takeToTarsTransaction 存 std::to_string(chainId)，0x2105 → "8451"）当
/// hex-quantity 解析（safeFromQuantity 剥可选 0x 后按 hex），8451 → 0x8451=33873——这是该
/// 转换函数的已知表示缺陷（BCOS2Evmone.cpp:221 注释：validate_transaction 不查 chain_id、
/// 语义由签名上游绑定），非 buildFiscoTx/opEnvelopeToTars 错误。chain_id 是否忠实由端到端
/// A-vs-B 对比兜底（若某 tx 读 CHAINID opcode，stateRoot/output 必分叉 → 红）。
bool evmoneTxFieldsEqual(const evmone::state::Transaction& a, const evmone::state::Transaction& b)
{
    if (a.type != b.type)
        return false;
    if (a.sender != b.sender)
        return false;
    if (a.to != b.to)
        return false;
    if (a.nonce != b.nonce)
        return false;
    if (a.gas_limit != b.gas_limit)
        return false;
    if (a.max_gas_price != b.max_gas_price)
        return false;
    if (a.max_priority_gas_price != b.max_priority_gas_price)
        return false;
    if (a.value != b.value)
        return false;
    if (a.data != b.data)
        return false;
    if (a.access_list != b.access_list)
        return false;
    if (a.authorization_list.size() != b.authorization_list.size())
        return false;
    for (std::size_t i = 0; i < a.authorization_list.size(); ++i)
    {
        const auto& x = a.authorization_list[i];
        const auto& y = b.authorization_list[i];
        // signer 不参与（恢复延迟 vs 预填，执行等价）；仅比较签名元组字段。
        if (x.chain_id != y.chain_id || x.addr != y.addr || x.nonce != y.nonce || x.r != y.r ||
            x.s != y.s || x.v != y.v)
            return false;
    }
    return true;
}

/// buildFiscoTx（第三轮 P2 提升为显式步骤）：opEnvelopeToTars(env, hash)（EngineServiceImpl.h:168）
/// + 覆盖 extraTransactionBytes=env（仅普通交易；deposit 已完整）+ pre-flight
/// bcosTransactionToEvmone(重建tx) == decodeOneRawTx(env) 字段级校验——路径 B 正确性关键。
/// 返回 nullptr 时调用方须 BOOST_ERROR（opEnvelopeToTars 失败 / pre-flight 不一致）。
bcos::protocol::Transaction::Ptr buildFiscoTx(
    const bcos::evm::opstack::OpBlockTx& btx, bcos::crypto::Hash::Ptr const& hashImpl)
{
    bcos::bytes env(btx.signedEnvelope.begin(), btx.signedEnvelope.end());
    const auto txHash = hashImpl->hash(env);
    auto tarsTx = bcos::engine::detail::opEnvelopeToTars(env, txHash);
    if (!tarsTx)
    {
        BOOST_ERROR("buildFiscoTx opEnvelopeToTars nullopt: env=" << hexBytes(
                        evmc::bytes_view(env.data(), env.size())));
        return nullptr;
    }
    // 覆盖：takeToTarsTransaction 存 signing preimage，executeTransaction 读
    // extraTransactionBytes 当完整信封（OpstackExecutor.h:280-281）。tars vector<byte> 是
    // int8_t 元素，须 assign（同 OpNewPayloadRpcE2eTest txHash 的 assign 惯例）。
    tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(*tarsTx)]() mutable { return &tars; });

    // pre-flight：FISCO tx -> evmone tx 与直接信封解码同源。
    const auto rebuilt = eth::bcosTransactionToEvmone(*tx);
    bcos::evm::opstack::OpBlockTx decoded;
    try
    {
        decoded = detail::decodeOneRawTx(env, kChainId);
    }
    catch (const std::exception& e)
    {
        BOOST_ERROR("buildFiscoTx pre-flight decodeOneRawTx threw: " << e.what());
        return nullptr;
    }
    const auto* evmTx = std::get_if<evmone::state::Transaction>(&decoded.tx);
    if (evmTx == nullptr || !evmoneTxFieldsEqual(rebuilt, *evmTx))
    {
        BOOST_ERROR("buildFiscoTx pre-flight mismatch: env="
                    << hexBytes(evmc::bytes_view(env.data(), env.size()))
                    << " diff=" << evmoneTxFieldDiff(rebuilt, *evmTx) << " decoded_sender="
                    << hexAddr(evmTx->sender) << " rebuilt_sender=" << hexAddr(rebuilt.sender));
        return nullptr;
    }
    return tx;
}

/// opStackMeta 集合驱动：deposit（envelope 首字节 0x7E）只带 nonce/version；normal 带 fee 字段。
/// 字段集不硬编码数量——按 receipt->opStackMeta() 实际 present 字段动态提取。
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

/// stateRoot 分叉诊断：双视图 visitAccounts，地址键控 balance/nonce/codeHash/storage，上限 20 条。
/// Storage2State 构造取 Storage&（非 const），故参数为非 const 引用（viewA/viewB 是本地 fork）。
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

/// /sys tripwire（第三轮定案）：派生表前缀断言——每个向量 pre/postState/tx to/from/coinbase 地址
/// 的 accountTableName(addr) 前缀必须 == apps/（比枚举 c_systemTxsAddress 更健壮）。
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

/// has_storage 扫描（第三轮 P4，先扫不预先修）：P1 扫「同块 create」提前 triage——语料大概率不
/// 触发 StorageStateView has_storage 读端不对称；扫到只 REPORT 不失败。
void hasStorageScan(const std::string& id, const std::vector<bcos::evm::opstack::OpBlockTx>& txs)
{
    int creates = 0;
    for (const auto& btx : txs)
    {
        if (const auto* tx = std::get_if<evmone::state::Transaction>(&btx.tx))
        {
            if (!tx->to.has_value())
                ++creates;
        }
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

    // hard：块级 mechanics。
    ctx.checkHard("gasUsed", hexU64(resultA.gasUsed), hexU64(resultB.gasUsed));
    ctx.checkHard("txRoot", resultA.txRoot.hexPrefixed(), resultB.txRoot.hexPrefixed());

    // soft：stateRoot（ALLOWLIST 驱动）+ seal 五字段。
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

    // 分叉诊断：stateRoot 不一致时 dumpAccountDiff（上限 20 条）。
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
        // F1：type 判别从 rawTxBytes 推（envelope 首字节 0x7E=deposit），两路径输入相同天然一致；
        // "type" 不参与 A-vs-B 对比（同义反复），仅用于 opStackMeta 字段提取。
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

        // v2（B8）：log 内容（address/topics/data）——同 count 异内容只在 logsBloom 露头，P1 就抓。
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

        // soft：output + _op_*（opStackMeta 集合驱动）。
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

/// golden 三方（G1，P1 软 → P3 带作用域翻硬）：path A.stateRoot == 向量
/// _op_expected.header.stateRoot。hardGolden=true 时 mismatch 即 BOOST_CHECK 硬失败
/// （作用域=isthmus/jovian 非 contract_create）；greenGuard（deposit_basefee 绿守卫）始终硬；
/// 其余（pre-isthmus / contract_create）保持软 REPORT。
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

/// 单块等价执行：seedPreState 已在外部完成；本函数 fork 双 view（A/B 各持独立 MLS view）、
/// 双路径执行、assertEquivalent、golden REPORT。chain 继承：route A 是权威路径，本块结束后
/// mergeView(viewA)（viewB 对比完即弃）。
void runBlockEquivalence(const std::string& id, Fixture& fixture,
    bcos::protocol::BlockHeader::Ptr const& header, const std::vector<bcos::bytes>& rawTxBytes,
    const JsonValue& vec, bool jovian, const bcos::evm::opstack::OpForkConfig& vectorCfg,
    DivergenceLedger& ledger, bool greenGuard, GoldenStats& stats)
{
    // 第三轮 P2 fork 平价：cfg = configAt(timestamp/1000, forkTimestampsFor(jovian))，与路径 A
    // scheduler 内部 configAt 解析同源（同一 forkTimestampsFor，静态单例同一对象）。
    const auto& cfg =
        op::configAt(static_cast<uint64_t>(header->timestamp()) / 1000, forkTimestampsFor(jovian));
    BOOST_CHECK_MESSAGE(&cfg == &vectorCfg, id << ": fork parity broken: block cfg != vector cfg");

    // 解码 txs（decodeOneRawTx 内部含 assertCanonicalRoundTrip，即 P4 兜底）。
    std::vector<op::OpBlockTx> txs;
    txs.reserve(rawTxBytes.size());
    try
    {
        for (const auto& raw : rawTxBytes)
            txs.push_back(detail::decodeOneRawTx(raw, kChainId));
    }
    catch (const std::exception& e)
    {
        BOOST_ERROR(id << ": decodeOneRawTx threw: " << e.what());
        return;
    }

    // 第三轮 P3：normalTxs[k] = 第 k 个非 deposit 交易（跳过 deposit、按块内序），对齐注入器
    // normalIdx 仅非 deposit 分支 ++。
    std::vector<bcos::protocol::Transaction::Ptr> normalTxs;
    for (const auto& btx : txs)
    {
        if (std::holds_alternative<op::DepositTx>(btx.tx))
            continue;
        auto tx = buildFiscoTx(btx, fixture.hashImpl);
        if (tx == nullptr)
        {
            BOOST_ERROR(id << ": buildFiscoTx failed (opEnvelopeToTars nullopt / pre-flight)");
            return;
        }
        normalTxs.push_back(std::move(tx));
    }

    // 路线 A：executeOpBlock（生产单路径）。viewA 生命周期覆盖整个对比（dumpAccountDiff 读它）。
    auto viewA = fixture.multiLayerStorage.fork();
    viewA.newMutable();
    engine::OpExecuteBlockResult resultA;
    try
    {
        resultA = bcos::task::syncWait(engine::OpSchedulerImpl<ViewType>(
            fixture.receiptFactory, kChainId, forkTimestampsFor(jovian))
                                           .executeOpBlock(viewA, *header, rawTxBytes));
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

    // 路线 B：runOpBlockInjection（逐笔注入循环）。viewB 生命周期覆盖整个对比。
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
        resultB = engine::runOpBlockInjection(executor, viewB, *header, txs, normalTxs, cfg,
            kChainId, ledgerConfig, rawTxBytes, fixture.hashImpl);
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

    // A-vs-B 对比（hard 全绿 + soft ALLOWLIST）。
    try
    {
        assertEquivalent(id, resultA, resultB, viewA, viewB, rawTxBytes, ledger);

        // chain 继承：route A 权威 post-state merge 进 MLS（viewB 丢弃）。
        bcos::task::syncWait(fixture.multiLayerStorage.mergeView(std::move(viewA)));

        // golden 三方（G1 P3 带作用域翻硬）：isthmus/jovian 非 contract_create → hard；
        // pre-isthmus（fork 不匹配预期）与 contract_create（已知分歧）保持软 REPORT。
        if (vec.isMember("_op_expected"))
        {
            const auto hardfork = jAt(jAt(vec, "_info"), "hardfork").asString();
            const bool hardGolden = (hardfork == "isthmus" || hardfork == "jovian") &&
                                    (id.find("contract_create") == std::string::npos);
            reportGolden(id, vec, resultA.stateRoot, greenGuard, hardGolden, stats);
        }

        // /sys tripwire（每个向量 pre/postState/tx/coinbase 派生表前缀）。
        checkSysTripwire(id, vec);

        // has_storage 扫描（P4 提前 triage，只 REPORT）。
        hasStorageScan(id, txs);
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

/// 单块向量：seedPreState → runBlockEquivalence。golden 从 .golden.json 手动加载（isJovianVector
/// 对 pre-isthmus 抛错，不能走 loadVectorSample）。
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
    // 单块向量头：isthmus/jovian 用 decodeGoldenHeader（golden 权威 op-geth 头）；pre-isthmus
    // （ecotone/fjord/granite）的 encodedHeaderHex 走 decodeOpHeader 严格 21 字段反解会抛
    // （RTTI-bypass runtime_error，已实测），故退化为 buildHeaderFromEnv（与 chain 同源）。
    // 两路径同 cfg（configAt→isthmusConfig），A-vs-B 等价对比仍有效；golden 三方因 fork 不匹配
    // REPORT mismatch（P1 软）。
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
    // golden rawTransactions（单块向量：deposit 已是 0x7E envelope、normal 已是 0x02 envelope）。
    std::vector<bcos::bytes> rawTxBytes;
    for (const auto& raw : sample.golden["rawTransactions"])
        rawTxBytes.push_back(bcos::fromHex(raw.asString()));
    const auto& vectorCfg =
        op::configAt(static_cast<uint64_t>(header->timestamp()) / 1000, forkTimestampsFor(jovian));
    runBlockEquivalence(
        id, fixture, header, rawTxBytes, vec, jovian, vectorCfg, ledger, greenGuard, stats);
}

/// chain 向量：逐块双 fork、A/B 对比后 route A mergeView 继承（权威路径）。块 0 pre 显式，
/// 后续块继承前块 route A post-state。
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

    // 遍历目录：跳过 invalid_ 前缀与 _op_expected.reject；chain 走 blocks[] 分支。
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
                continue;  // reject 类由 OpT8nReplay/OpNewPayloadRpcE2e 消费，harness 跳过
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

    // 绿守卫 + golden 汇总报告。
    std::cout << "dual-path summary: flat=" << stats.flat << " chainBlocks=" << stats.chainBlocks
              << " goldenMatch=" << stats.match << " goldenMismatch=" << stats.mismatch
              << " greenGuard=" << stats.greenGuardOk << "\n";

    ledger.finish();
    std::cout << "dual-path KNOWN-DIVERGE total=" << ledger.knownCount() << "\n";
}

BOOST_AUTO_TEST_SUITE_END()
