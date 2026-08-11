// OpT8nReplayTest.cpp — M-B3+M6 Task 3: OP block-level differential replay gate.
//
// Replays test/opstack/t8n/vectors/*.json (schema v3-block, op-geth
// GenerateChain+InsertChain golden, generator in t8n/generator/) block-by-block
// through processOpBlock -> sealOpBlock, comparing header fields, per-receipt
// fields, and postState (bidirectional + applyDiff write-set coverage) against
// _op_expected.
//
// Hard assertion discipline: A) dir *.json set == manifest.txt set; parse
// failure / missing required field = named ADD_FAILURE; per-vector comparison
// count recorded, 0 = FAILURE. B) required fields via j.at(); hardfork must be
// exactly ecotone|fjord|granite|holocene|isthmus|jovian (no default fork);
// unknown _op_type / receipt count mismatch = FAILURE (no zip-min).
// D) comparisons routed through checkField/checkOptional into DivergenceLedger;
// checkOptional never gated on has_value() (one-sided absence = DIVERGE
// <absent>); bloom always 512 hex; postState bidirectional with zero-slot trie
// reduction (0 == absent) + write-set coverage. E) exemptions only from
// DIVERGENCES.md ALLOWLIST tuples (a:PENDING-FIX / c:SIGNED-OFF); dangling
// entry= or never-hit exemptions = FAILURE.

#include "StateDiffWriteback.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <cxxabi.h>
#include <evmone/evmone.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpBlockSeal.h>
#include <opstack-executor/OpSchedulerImpl.h>  // decodeOneRawTx (blob decode-class reject repro)
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <test/utils/rlp.hpp>
#include <test/utils/test_state.hpp>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace bcos::evm::opstack;
using namespace evmone;

// ── Local subset re-implementation of evmone test::from_json ─────────────────
// The vcpkg evmone package does not ship test/utils/statetest.hpp (which declares
// evmone's test::from_json<T>); the t8n gate needs a few of its conversions.
// Declare the primary template and define only the used specializations, with
// semantics identical to evmone statetest_loader.cpp (ints accept number or "0x"
// hex strings; bytes/address/hash256 via evmc::from_hex; TestState parses
// account objects and drops zero-valued storage slots).
namespace evmone::test
{
template <typename T>
T from_json(const nlohmann::json& j) = delete;

template <>
int64_t from_json<int64_t>(const nlohmann::json& j)
{
    if (j.is_number_integer())
        return j.get<int64_t>();
    if (!j.is_string())
        throw std::invalid_argument("from_json<int64_t>: must be integer or string of integer");
    const auto s = j.get<std::string>();
    size_t num_processed = 0;
    const auto v = static_cast<int64_t>(std::stoull(s, &num_processed, 0));
    if (num_processed == 0 || num_processed != s.size())
        throw std::invalid_argument("from_json<int64_t>: must be integer or string of integer");
    return v;
}

template <>
uint64_t from_json<uint64_t>(const nlohmann::json& j)
{
    if (j.is_number_integer())
        return j.get<uint64_t>();
    if (!j.is_string())
        throw std::invalid_argument("from_json<uint64_t>: must be integer or string of integer");
    const auto s = j.get<std::string>();
    size_t num_processed = 0;
    const auto v = static_cast<uint64_t>(std::stoull(s, &num_processed, 0));
    if (num_processed == 0 || num_processed != s.size())
        throw std::invalid_argument("from_json<uint64_t>: must be integer or string of integer");
    return v;
}

template <>
intx::uint256 from_json<intx::uint256>(const nlohmann::json& j)
{
    return intx::from_string<intx::uint256>(j.get<std::string>());
}

template <>
evmone::bytes from_json<evmone::bytes>(const nlohmann::json& j)
{
    return evmc::from_hex(j.get<std::string>()).value();
}

template <>
evmc::address from_json<evmc::address>(const nlohmann::json& j)
{
    const auto v = evmc::from_hex<evmc::address>(j.get<std::string>());
    if (!v.has_value())
        throw std::invalid_argument("from_json<address>: must be hexadecimal string");
    return *v;
}

// Note: evmone::hash256 is a using-alias of evmc::bytes32, so this one specialization serves both
// from_json<hash256> (header hashes) and from_json<bytes32> (storage keys/values).
template <>
evmc::bytes32 from_json<evmc::bytes32>(const nlohmann::json& j)
{
    const auto s = j.get<std::string>();
    if (s == "0" || s == "0x0")  // Special case to handle "0". Required by exec-spec-tests.
        return evmc::bytes32{};
    const auto v = evmc::from_hex<evmc::bytes32>(s);
    if (!v.has_value())
        throw std::invalid_argument("from_json<bytes32>: must be hexadecimal string");
    return *v;
}

template <>
evmone::test::TestState from_json<evmone::test::TestState>(const nlohmann::json& j)
{
    evmone::test::TestState o;
    assert(j.is_object());
    for (const auto& [j_addr, j_acc] : j.items())
    {
        auto& acc = o[from_json<evmc::address>(nlohmann::json(j_addr))] = {
            .nonce = from_json<uint64_t>(j_acc.at("nonce")),
            .balance = from_json<intx::uint256>(j_acc.at("balance")),
            .code = from_json<evmone::bytes>(j_acc.at("code"))};
        if (const auto storage_it = j_acc.find("storage"); storage_it != j_acc.end())
        {
            for (const auto& [j_key, j_value] : storage_it->items())
            {
                if (const auto value = from_json<evmc::bytes32>(j_value); !evmc::is_zero(value))
                    acc.storage[from_json<evmc::bytes32>(nlohmann::json(j_key))] = value;
            }
        }
    }
    return o;
}
}  // namespace evmone::test

namespace
{
// ── Canonical printing (the only want/got form written by DIVERGE/ALLOWLIST) ─
// Numeric: "0x"-prefixed lowercase minimal hex (same shape as generator
// hexutil.EncodeUint64/EncodeBig). Hash/address: "0x" fixed-length lowercase.
// The absent side is always "<absent>".

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

std::string hexHash(const hash256& h)
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

// bytes32 slot keys/values are written as minimal-numeric hex (trie semantics: 0 == absent, compared after normalization).
std::string hexSlot(const evmc::bytes32& b)
{
    return hexU256(intx::be::load<intx::uint256>(b));
}

intx::uint256 parseU256(const Json& j)
{
    return intx::from_string<intx::uint256>(j.get<std::string>());
}

// ── DivergenceLedger (brief block E) ────────────────────────────────────────

struct AllowEntry
{
    std::string vectorId, field, entryId, attribution, status, want, got;
    bool exempt = false;
    int hits = 0;
};

class DivergenceLedger
{
public:
    // Missing ledger file = FAILURE (the ledger is a gate deliverable; a missing file must never imply all-exempt/all-empty).
    static DivergenceLedger load(const fs::path& path)
    {
        DivergenceLedger ledger;
        std::ifstream input(path);
        if (!input.is_open())
        {
            BOOST_ERROR("DIVERGENCES.md missing: " << path);
            return ledger;
        }
        // Mirrors the DIVERGENCES.md "machine format" section verbatim.
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
                ledger.m_entries.push_back(std::move(e));
            }
        }
        // Dangling entry= (no matching "## <ENTRY-ID>" heading) = FAILURE: an
        // ALLOWLIST row must hang under a real FINDING/entry section; a lone row has no evidence.
        for (const auto& e : ledger.m_entries)
        {
            if (!headings.contains(e.entryId))
                BOOST_ERROR("DIVERGENCES.md ALLOWLIST entry="
                            << e.entryId << " (vectorId=" << e.vectorId << " field=" << e.field
                            << ") has no matching '## " << e.entryId << "' heading");
        }
        return ledger;
    }

    // Single divergence-reporting entry point: full 4-tuple match with exempt
    // status -> KNOWN-DIVERGE (stdout + count); otherwise ADD_FAILURE. The
    // 4-tuple match prevents new regressions on the same field riding old exemptions.
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

    // An exemption never hit this run = FAILURE (stale exemption turns red; must be cleared after fix/vector regen).
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

private:
    std::vector<AllowEntry> m_entries;
    int m_knownCount = 0;
};

// ── Per-vector comparison context (comparison count + field prefix) ─────────

struct VectorContext
{
    DivergenceLedger& ledger;
    std::string id;
    int comparisons = 0;

    void checkField(const std::string& field, const std::string& want, const std::string& got)
    {
        ++comparisons;
        if (want != got)
            ledger.diverge(id, field, want, got);
    }

    // checkOptional semantics (brief block D): want present + got absent ->
    // got=<absent>; want absent + got present -> want=<absent>; both absent pass.
    // Never gate on has_value() before comparing.
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
};

// ── BlockHashes: return env.parentHash only for number-1 ────────────────────
// (EIP-2935 system call in block 1 stores the genesis hash = env.parentHash;
// other heights are never queried by this corpus, so returning zero exposes any
// out-of-range query instead of silently fabricating a hash chain.)

struct ParentOnlyBlockHashes final : state::BlockHashes
{
    int64_t blockNumber = 0;
    hash256 parentHash{};

    evmc::bytes32 get_block_hash(int64_t block_number) const noexcept override
    {
        return block_number == blockNumber - 1 ? parentHash : evmc::bytes32{};
    }
};

// ── EIP-7702 authority recovery ─────────────────────────────────────────────
// The in-module recoverAuthority (OpTransition.cpp:34-45) lives in an anonymous
// namespace and is not exported; minimally re-implement it here (same formula
// keccak256(0x05 || rlp([chain_id,address,nonce])) + evmmax secp256k1 ecrecover).
// After building, assert signer.has_value() — evmone/module transition silently
// skips tuples whose signer was not recovered, so corpus signatures must be recoverable.

// ── structurallyUnrecoverable: secp256k1 structural-validity predicate ──────
// (EIP-2/EIP-7702 malleability boundary; no ecrecover, only whether r/s/v fall
// outside the recoverable domain.)

inline const intx::uint256 kSecpN = intx::from_string<intx::uint256>(
    "0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141");
inline const intx::uint256 kSecpHalfN = kSecpN >> 1;

inline bool structurallyUnrecoverable(const evmone::state::Authorization& a)
{
    return a.v > 1 || a.s > kSecpHalfN || a.r == 0 || a.r >= kSecpN || a.s == 0 || a.s >= kSecpN;
}

std::optional<evmc::address> replayRecoverAuthority(const state::Authorization& auth)
{
    const auto msg = bytes{0x05} + rlp::encode_tuple(auth.chain_id, auth.addr, auth.nonce);
    const auto h = keccak256(msg);
    const auto r = intx::be::store<evmc::bytes32>(auth.r);
    const auto s = intx::be::store<evmc::bytes32>(auth.s);
    return evmmax::secp256k1::ecrecover(std::span<const uint8_t, 32>{h.bytes, 32},
        std::span<const uint8_t, 32>{r.bytes, 32}, std::span<const uint8_t, 32>{s.bytes, 32},
        auth.v != 0);
}

// ── manifest.txt: one required vector filename per line ('#' comments and blank lines ignored) ─

std::set<std::string> loadManifest(const fs::path& path)
{
    std::set<std::string> names;
    std::ifstream input(path);
    if (!input.is_open())
    {
        BOOST_ERROR("manifest.txt missing: " << path);
        return names;
    }
    std::string line;
    while (std::getline(input, line))
    {
        // trim
        const auto b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos)
            continue;
        const auto e = line.find_last_not_of(" \t\r");
        line = line.substr(b, e - b + 1);
        if (line.empty() || line[0] == '#')
            continue;
        names.insert(line);
    }
    return names;
}

// ── Corpus chain id ──────────────────────────────────────────────────────────
// The generator buildChainConfig (generator/main.go) pins all cases to 8453
// (0x2105). Vectors with normal txs use tx.chainId (asserted consistent across
// the block); deposit-only vectors carry no chainId, so this corpus constant is
// used — a mirror of the generator's constant, not a fallback default.

constexpr uint64_t kCorpusChainId = 0x2105;

// ── Current API adaptation helpers ───────────────────────────────────────────
// After plan A phase 2 (cf8d1af70): processOpBlock takes 9 params
// (receiptFactory), receipts are bcos::protocol::TransactionReceipt::Ptr, OP
// fields come via opStackMeta() (bcos::u256 / uint64).
// bcos::u256 -> "0x" + lowercase no-leading-zero hex (same shape as setOpStackMeta's u256ToHex).
std::string hexU256Bcos(const bcos::u256& v)
{
    return "0x" + v.str(0, std::ios_base::hex);
}

/// Same receiptFactory construction as the W6 harness (OpNewPayloadRpcE2eTest.cpp:93-95).
bcos::protocol::TransactionReceiptFactory::Ptr makeTestReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(
        std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr));
}

// ── TestState -> stateRootOf Ledger bridge ───────────────────────────────────
// bcos::evm::stateRootOf<Ledger> (adapter/StateRootCompute.h) is a template
// building a secure trie over any Ledger exposing `bool visitAccounts(Visitor) const`.
// evmone::test::TestState is a std::map, not a Ledger, so expose the account
// visit surface here. AccountView mirrors MemoryLedger::AccountView's root-building
// fields (addr/nonce/balance/codeHash/storage); stateRootOf uses only these five.
struct TestStateLedger
{
    const evmone::test::TestState& state;

    template <class Visitor>
    bool visitAccounts(Visitor&& visitor) const noexcept
    {
        for (const auto& [addr, account] : state)
        {
            struct View
            {
                const evmc::address& addr;
                uint64_t nonce;
                const intx::uint256& balance;
                evmc::bytes32 codeHash;
                const std::map<evmc::bytes32, evmc::bytes32>& storage;
                [[nodiscard]] const evmc::bytes& code() const noexcept { return m_code; }
                const evmc::bytes& m_code;
            };
            const View view{.addr = addr,
                .nonce = account.nonce,
                .balance = account.balance,
                .codeHash = evmone::keccak256(account.code),
                .storage = account.storage,
                .m_code = account.code};
            if (!visitor(view))
                return false;
        }
        return true;
    }
};

// ── Single-block load context (shared by replaySingleBlockInto / assertRejectThrow) ──
// Pure move of the load section from the original replayVector path (previously
// :456-643), no assertion-logic change. On failure (invalid hardfork /
// inconsistent intra-block chainId / unknown _op_type) it BOOST_ERRORs and
// returns false; the caller returns directly.

struct BlockContext
{
    const OpForkConfig* cfg = nullptr;
    bool isJovian = false;
    state::BlockInfo blk;
    ParentOnlyBlockHashes hashes;
    std::vector<OpBlockTx> txs;
    uint64_t chainId = kCorpusChainId;
    // decode-class reject (blob): processOpBlock never reaches raw-tx decode
    // (txs are already OpBlockTx), so the load section reproduces the real
    // decode rejection via decodeOneRawTx and records the message here for
    // assertRejectThrow to assert directly.
    std::optional<std::string> decodeRejectMessage;
};

bool loadBlockContext(const std::string& id, const Json& blk, BlockContext& out)
{
    // _info.hardfork must be exactly ecotone|fjord|granite|holocene|isthmus|jovian,
    // anything else = FAILURE. No default fork (the default-Isthmus precedent is a
    // known hole, not ported). isJovian drives the blobGasUsed header gate and the
    // _op_da_footprint expectation — ecotone/fjord/granite/holocene are all false,
    // matching isthmus semantics (has_da_footprint true only on Jovian).
    const auto hardfork = blk.at("_info").at("hardfork").get<std::string>();
    if (hardfork == "isthmus")
        out.cfg = &isthmusConfig();
    else if (hardfork == "jovian")
    {
        out.cfg = &jovianConfig();
        out.isJovian = true;
    }
    else if (hardfork == "ecotone")
        out.cfg = &ecotoneConfig();
    else if (hardfork == "fjord")
        out.cfg = &fjordConfig();
    else if (hardfork == "granite")
        out.cfg = &graniteConfig();
    else if (hardfork == "holocene")
        out.cfg = &holoceneConfig();
    else
    {
        BOOST_ERROR(id << ": _info.hardfork must be exactly "
                          "ecotone|fjord|granite|holocene|isthmus|jovian, got '"
                       << hardfork << "' (no default fork)");
        return false;
    }

    // env (all 8 fields required) -> hand-built BlockInfo.
    const auto& env = blk.at("env");
    auto& bi = out.blk;
    bi.number = test::from_json<int64_t>(env.at("currentNumber"));
    bi.timestamp = test::from_json<int64_t>(env.at("currentTimestamp"));
    bi.gas_limit = test::from_json<int64_t>(env.at("currentGasLimit"));
    bi.base_fee = test::from_json<uint64_t>(env.at("currentBaseFee"));
    bi.coinbase = test::from_json<evmc::address>(env.at("currentCoinbase"));
    bi.prev_randao = test::from_json<hash256>(env.at("currentRandom"));
    bi.parent_beacon_block_root = test::from_json<hash256>(env.at("parentBeaconBlockRoot"));

    auto& hs = out.hashes;
    hs.blockNumber = bi.number;
    hs.parentHash = test::from_json<hash256>(env.at("parentHash"));

    // Three transaction arms (deposit / eip1559 / setcode). Unknown _op_type = FAILURE.
    auto& txs = out.txs;
    std::optional<uint64_t> vectorChainId;
    for (const auto& t : blk.at("block").at("transactions"))
    {
        const auto opType = t.at("_op_type").get<std::string>();
        if (opType == "deposit")
        {
            const auto& d = t.at("_op_deposit");
            DepositTx dep;
            dep.source_hash = test::from_json<hash256>(d.at("source_hash"));
            dep.from = test::from_json<evmc::address>(d.at("from"));
            dep.to = d.at("to").is_null() ?
                         std::nullopt :
                         std::optional{test::from_json<evmc::address>(d.at("to"))};
            dep.mint = d.contains("mint") ? std::optional{parseU256(d.at("mint"))} : std::nullopt;
            dep.value = d.contains("value") ? parseU256(d.at("value")) : intx::uint256{0};
            dep.gas_limit = test::from_json<int64_t>(d.at("gas"));
            dep.is_system_tx = d.at("is_system_tx").get<bool>();
            dep.data = test::from_json<bytes>(t.at("data"));
            txs.push_back({.tx = std::move(dep), .signedEnvelope = {}});
        }
        else if (opType == "eip1559" || opType == "setcode")
        {
            state::Transaction tx;
            tx.type = opType == "setcode" ? state::Transaction::Type::set_code :
                                            state::Transaction::Type::eip1559;
            tx.sender = test::from_json<evmc::address>(t.at("sender"));
            tx.to = t.at("to").is_null() ?
                        std::nullopt :
                        std::optional{test::from_json<evmc::address>(t.at("to"))};
            tx.nonce = test::from_json<uint64_t>(t.at("nonce"));
            tx.gas_limit = test::from_json<int64_t>(t.at("gas"));
            tx.max_gas_price = parseU256(t.at("maxFeePerGas"));
            tx.max_priority_gas_price = parseU256(t.at("maxPriorityFeePerGas"));
            tx.value = parseU256(t.at("value"));
            tx.data = test::from_json<bytes>(t.at("data"));
            // EIP-2930 访问列表（可选字段；旧 25 向量零命中，休眠路径）。
            if (t.contains("accessList"))
            {
                for (const auto& e : t.at("accessList"))
                {
                    std::vector<evmc::bytes32> keys;
                    for (const auto& k : e.at("storageKeys"))
                        keys.push_back(test::from_json<hash256>(k));
                    tx.access_list.emplace_back(
                        test::from_json<evmc::address>(e.at("address")), std::move(keys));
                }
            }
            tx.chain_id = test::from_json<uint64_t>(t.at("chainId"));
            if (vectorChainId.has_value() && *vectorChainId != tx.chain_id)
            {
                BOOST_ERROR(id << ": inconsistent chainId across txs: " << hexU64(*vectorChainId)
                               << " vs " << hexU64(tx.chain_id));
                return false;
            }
            vectorChainId = tx.chain_id;
            if (opType == "setcode")
            {
                // hasMarked/hasUnmarked/anchorOk（spec rev.3 ③④）：标记元组（结构不可恢复，
                // _op_signer_unrecoverable=true）与未标记元组（既有恢复路径）混合校验，
                // 且标记元组存在时须有 >=1 未标记元组把委托锚落到 postState。
                bool hasMarked = false;
                bool hasUnmarked = false;
                bool anchorOk = false;
                for (const auto& a : t.at("_op_authorization_list"))
                {
                    state::Authorization auth;
                    auth.chain_id = parseU256(a.at("chainId"));
                    auth.addr = test::from_json<evmc::address>(a.at("address"));
                    auth.nonce = test::from_json<uint64_t>(a.at("nonce"));
                    auth.r = parseU256(a.at("r"));
                    auth.s = parseU256(a.at("s"));
                    auth.v = parseU256(a.at("yParity"));

                    const bool marked = a.contains("_op_signer_unrecoverable");
                    if (marked && (!a.at("_op_signer_unrecoverable").is_boolean() ||
                                      !a.at("_op_signer_unrecoverable").get<bool>()))
                    {
                        BOOST_ERROR(id << ": _op_signer_unrecoverable must be literal true");
                        continue;
                    }
                    if (marked)
                    {
                        // 逆向验证（结构谓词，禁裸 ecrecover）；signer 留空，元组原样入列——
                        // 生产 OpTransition.cpp:46-135 真 ecrecover
                        // 并按其谓词跳过（真实差分路径）。
                        if (!structurallyUnrecoverable(auth))
                            BOOST_ERROR(
                                id << ": marked unrecoverable but structurally recoverable");
                        hasMarked = true;
                    }
                    else
                    {
                        // signer 恢复填入 + 逐 tuple 断言（evmone 对未设 signer 静默跳过）。
                        auth.signer = replayRecoverAuthority(auth);
                        if (!auth.signer.has_value())
                            BOOST_ERROR(id << ": authorization signer recovery failed (unmarked "
                                              "tuple)");
                        else
                        {
                            hasUnmarked = true;
                            // 非空洞·委托锚存在性（spec rev.3 ③）：该 authority 在向量 postState
                            // 须携 0xef0100‖tuple.addr 委托码（仅当本 tx 含标记元组时才要求，
                            // 见下方 (4)）。
                            const auto authAddr = hexAddr(*auth.signer);
                            const auto& post = blk.at("postState");
                            if (post.contains(authAddr))
                            {
                                const std::string wantCode =
                                    "0xef0100" + hexAddr(auth.addr).substr(2);
                                if (post.at(authAddr).value("code", "") == wantCode)
                                    anchorOk = true;
                            }
                        }
                    }
                    tx.authorization_list.push_back(std::move(auth));
                }
                if (hasMarked && !hasUnmarked)
                    BOOST_ERROR(id << ": setcode tx with marked tuples must contain >=1 unmarked "
                                      "tuple");
                if (hasMarked && hasUnmarked && !anchorOk)
                    BOOST_ERROR(id << ": marked-tuple tx has no applied delegation anchor in "
                                      "postState");
            }
            auto envelope = test::from_json<bytes>(t.at("_op_raw"));
            txs.push_back({.tx = std::move(tx), .signedEnvelope = std::move(envelope)});
        }
        else if (opType == "legacy")
        {
            // Task 3 F1 legacy 臂：type-0 EIP-155 保护交易。单一 gasPrice（无
            // maxFeePerGas/maxPriorityFeePerGas）；evmone legacy 的 priority==max==gasPrice。
            state::Transaction tx;
            tx.type = state::Transaction::Type::legacy;
            tx.sender = test::from_json<evmc::address>(t.at("sender"));
            tx.to = t.at("to").is_null() ?
                        std::nullopt :
                        std::optional{test::from_json<evmc::address>(t.at("to"))};
            tx.nonce = test::from_json<uint64_t>(t.at("nonce"));
            tx.gas_limit = test::from_json<int64_t>(t.at("gas"));
            const auto gasPrice = parseU256(t.at("gasPrice"));
            tx.max_gas_price = gasPrice;
            tx.max_priority_gas_price = gasPrice;
            tx.value = parseU256(t.at("value"));
            tx.data = test::from_json<bytes>(t.at("data"));
            tx.chain_id = test::from_json<uint64_t>(t.at("chainId"));
            if (vectorChainId.has_value() && *vectorChainId != tx.chain_id)
            {
                BOOST_ERROR(id << ": inconsistent chainId across txs: " << hexU64(*vectorChainId)
                               << " vs " << hexU64(tx.chain_id));
                return false;
            }
            vectorChainId = tx.chain_id;
            auto envelope = test::from_json<bytes>(t.at("_op_raw"));
            txs.push_back({.tx = std::move(tx), .signedEnvelope = std::move(envelope)});
        }
        else if (opType == "blob")
        {
            // Task 4 blob 臂：type-0x3 blob 交易在 OP 链上 decode-class 拒绝。processOpBlock
            // 走不到 raw-tx decode（txs 已是 OpBlockTx），用 decodeOneRawTx 复现真实 decode
            // 拒绝并记录消息，assertRejectThrow 直接断言（消费端先行，review R16 consumer:both）。
            const auto raw = test::from_json<bytes>(t.at("_op_raw"));
            try
            {
                // decodeOneRawTx 在 bcos::evm::engine::detail，形参 bcos::bytes（vector）；
                // raw 是 evmc::bytes（basic_string），逐字节转存。
                const bcos::bytes rawVec(raw.begin(), raw.end());
                (void)bcos::evm::engine::detail::decodeOneRawTx(
                    rawVec, vectorChainId.value_or(kCorpusChainId));
                BOOST_ERROR(id << ": blob raw envelope must be rejected at decode");
                return false;
            }
            catch (const std::runtime_error& e)
            {
                // 注意：不得用 catch(std::exception)——libevmone(-fno-rtti) 带入 std::exception
                // 的 hidden non-unique typeinfo，typed catch 对 runtime_error 子树不可靠绑定
                // （OpSchedulerImpl.h:1083-1104 注释）；runtime_error 分支实测可绑（assertRejectThrow）。
                out.decodeRejectMessage = std::string(e.what());
            }
            // 占位 tx：保持 deposit 后非 deposit 结构（decodeRejectMessage 分支不走
            // processOpBlock，占位仅结构完整性）。
            state::Transaction placeholder;
            placeholder.type = state::Transaction::Type::blob;
            placeholder.sender = test::from_json<evmc::address>(t.at("sender"));
            placeholder.to = t.at("to").is_null() ?
                                 std::nullopt :
                                 std::optional{test::from_json<evmc::address>(t.at("to"))};
            placeholder.gas_limit = test::from_json<int64_t>(t.at("gas"));
            placeholder.value = parseU256(t.at("value"));
            placeholder.data = test::from_json<bytes>(t.at("data"));
            placeholder.max_gas_price = t.contains("maxFeePerGas") ?
                                            parseU256(t.at("maxFeePerGas")) :
                                            intx::uint256{};
            placeholder.max_priority_gas_price = t.contains("maxPriorityFeePerGas") ?
                                                     parseU256(t.at("maxPriorityFeePerGas")) :
                                                     intx::uint256{};
            placeholder.chain_id = vectorChainId.value_or(kCorpusChainId);
            txs.push_back({.tx = std::move(placeholder), .signedEnvelope = std::move(raw)});
        }
        else
        {
            BOOST_ERROR(id << ": unknown _op_type '" << opType << "'");
            return false;
        }
    }
    out.chainId = vectorChainId.value_or(kCorpusChainId);
    return true;
}

/// 单块执行：blk 的 env/hardfork/transactions + ts（pre 已就绪或继承）。pre 为 nullptr 时跳过
/// pre 解析（链式块 i>0 的 pre:null → 继承上一块 applyDiff 回写后的 ts）。touchedAddrs/
/// touchedSlots 每块新建（block N 的 .uncovered 不能看到 block 0..N-1 触及的地址）。
void replaySingleBlockInto(const std::string& id, const Json& blk, evmone::test::TestState& ts,
    const Json* pre, DivergenceLedger& ledger, evmc::VM& vm,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory)
{
    VectorContext ctx{ledger, id};
    BlockContext bc;
    if (!loadBlockContext(id, blk, bc))
        return;
    const auto& cfg = *bc.cfg;
    const bool isJovian = bc.isJovian;

    // pre → TestState（evmone 金 loader；账户四字段 balance/nonce/code 必填，storage 零值槽由
    // loader 按 trie 语义剔除）。pre 为 nullptr 时继承调用方已就绪的 ts（链式块 i>0）。
    if (pre != nullptr)
        ts = test::from_json<test::TestState>(*pre);

    // 执行：applyDiff 回调既写回 TestState，也累计写集（决策记录 8 覆盖断言用）。
    std::set<evmc::address> touchedAddrs;
    std::map<evmc::address, std::set<evmc::bytes32>> touchedSlots;
    const auto apply = [&](const state::StateDiff& d) {
        for (const auto& m : d.modified_accounts)
        {
            touchedAddrs.insert(m.addr);
            for (const auto& [k, val] : m.modified_storage)
                touchedSlots[m.addr].insert(k);
        }
        for (const auto& a : d.deleted_accounts)
            touchedAddrs.insert(a);
        bcos::evm::applyStateDiffStrict(ts, d);
    };

    OpBlockResult result;
    try
    {
        result = processOpBlock(
            ts, bc.blk, bc.hashes, bc.txs, cfg, vm, bc.chainId, receiptFactory, apply);
    }
    catch (const std::exception& e)
    {
        BOOST_ERROR(id << ": processOpBlock threw block-level error: " << e.what());
        return;
    }
    catch (...)
    {
        // typed-catch RTTI 兜底（docs/audits/2026-07-12-typed-catch-rtti-investigation.md）：
        // libevmone.a（-fno-rtti）带入 hidden 非唯一 typeinfo for std::exception 拷贝，
        // 链接后本二进制所有 catch(std::exception&) 与 libc++ 抛出侧基类 typeinfo
        // 判不等而漏接（arm64 非唯一 RTTI 位混合比较规则）。诊断不吞：点名动态
        // 类型后按 typed 分支同语义收尾（红仍红、流程不变）。
        const auto* excType = abi::__cxa_current_exception_type();
        BOOST_ERROR(id << ": processOpBlock threw block-level error (typed catch "
                       << "bypassed, exception type: " << (excType ? excType->name() : "<unknown>")
                       << ")");
        return;
    }

    // seal：message passer storage = 块尾（finalize 后）快照（OpBlockSeal.h 契约）。
    const std::map<evmc::bytes32, evmc::bytes32> mpStorage =
        ts.contains(OP_L2_TO_L1_MESSAGE_PASSER) ? ts.at(OP_L2_TO_L1_MESSAGE_PASSER).storage :
                                                  std::map<evmc::bytes32, evmc::bytes32>{};
    const auto seal = sealOpBlock(result, cfg, mpStorage);

    // ── header 六字段 ────────────────────────────────────────────────────────
    const auto& h = blk.at("_op_expected").at("header");
    ctx.checkField("gasUsed", hexU256(parseU256(h.at("gasUsed"))),
        hexU64(static_cast<uint64_t>(result.gasUsed)));
    ctx.checkField("receiptsRoot", hexHash(test::from_json<hash256>(h.at("receiptsRoot"))),
        hexHash(seal.receiptsRoot));
    // bloom 恒 512 hex 字符比对（零 bloom 是全零串非缺席）。
    {
        auto wantBloom = h.at("logsBloom").get<std::string>();
        std::ranges::transform(wantBloom, wantBloom.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (wantBloom.size() != 2 + 512 || !wantBloom.starts_with("0x"))
        {
            BOOST_ERROR(id << ": header.logsBloom must be 0x + 512 hex chars, got "
                           << wantBloom.size() << " chars");
            return;
        }
        ctx.checkField("logsBloom", wantBloom, hexBytes(evmc::bytes_view(seal.logsBloom)));
    }
    ctx.checkField("withdrawalsRoot", hexHash(test::from_json<hash256>(h.at("withdrawalsRoot"))),
        hexHash(seal.withdrawalsRoot));
    // ── header.stateRoot（spec §4.4 rev.2：单腿，验执行+引擎 vs op-geth 共识根）─
    // 时点：seal 段 ts 已是 finalize 后完整世界终态（与 messagePasserStorage 快照
    // 同锚），后续 postState 比对只读不写 ts——此处建根安全。
    // 引擎（evmone mpt_hash）正确性由上游套件外锚，此处不重复自证；
    // 翻红优先归因执行/落账或 pre-alloc 完整性（见 plan 风险 4），非引擎。
    ctx.checkField("stateRoot", hexHash(test::from_json<hash256>(h.at("stateRoot"))),
        hexHash(bcos::evm::stateRootOf(TestStateLedger{ts})));
    // requestsHash：预 Prague（ecotone/fjord/…，含 ecotone_upgrade_fjord_activation）
    // 向量头不发射该键（op-geth t8n omitempty，Prague 才有 EIP-7685 requests），期望侧
    // 按在场性走 checkOptional（与下方 blobGasUsed 同 pattern，无门控）。
    ctx.checkOptional("requestsHash",
        h.contains("requestsHash") ?
            std::optional{hexHash(test::from_json<hash256>(h.at("requestsHash")))} :
            std::nullopt,
        seal.requestsHash.has_value() ? std::optional{hexHash(*seal.requestsHash)} : std::nullopt);
    // blobGasUsed：六 fork 的向量都发射（op-geth 各头真带 0x0，Ecotone+ 的 4844 遗留字段）。
    // Jovian → 值比对（"0x0" 是在场的零，如 jovian_first_block）；其余五 fork → 断言 C++
    // 侧缺席（seal.blobGasUsed 语义 = Jovian DA footprint 头字段，Isthmus- 无此重用位；
    //  向量的 0x0 是 op-geth 头的信息性 4844 遗留字段，不比值）。
    {
        const auto wantBlobGas = parseU256(h.at("blobGasUsed"));  // 必填（Ecotone+ 恒发射）
        const auto gotBlobGas =
            seal.blobGasUsed.has_value() ? std::optional{hexU64(*seal.blobGasUsed)} : std::nullopt;
        if (isJovian)
            ctx.checkOptional("blobGasUsed", std::optional{hexU256(wantBlobGas)}, gotBlobGas);
        else
            ctx.checkOptional("blobGasUsed", std::nullopt, gotBlobGas);
    }

    // ── receipts ────────────────────────────────────────────────────────────
    const auto& expReceipts = blk.at("_op_expected").at("receipts");
    if (expReceipts.size() != result.receipts.size())
    {
        BOOST_ERROR(id << ": receipts count mismatch: expected " << expReceipts.size() << " got "
                       << result.receipts.size() << " (no zip-min)");
        return;
    }
    for (size_t i = 0; i < expReceipts.size(); ++i)
    {
        const auto& er = expReceipts[i];
        const std::string p = "receipts[" + std::to_string(i) + "]";

        // 实测侧统一视图（方案 A 阶段 2 API）：FISCO TransactionReceipt::Ptr + 平行
        // txTypes 字节（EIP-2718 type）。OP 字段经 opStackMeta()；deposit 与普通 tx 由
        // txTypes 的 kDepositTxType 判别（等价旧 OpDepositReceipt/OpTxReceipt 变体判别）。
        const auto& receipt = result.receipts[i];
        const bool isDeposit = (result.txTypes[i] == static_cast<uint8_t>(kDepositTxType));
        const auto& meta = receipt->opStackMeta();
        std::optional<std::string> gotDepNonce, gotDepVersion, gotL1Fee, gotOperatorFee,
            gotDaFootprint, gotL1GasPrice, gotL1BlobBaseFee, gotL1GasUsed, gotL1BaseFeeScalar,
            gotL1BlobBaseFeeScalar, gotOpFeeScalar, gotOpFeeConstant, gotDaFootprintGasScalar;
        if (isDeposit)
        {
            if (meta && meta->deposit_nonce.has_value())
                gotDepNonce = hexU64(*meta->deposit_nonce);
            if (meta && meta->deposit_receipt_version.has_value())
                gotDepVersion = hexU64(*meta->deposit_receipt_version);
        }
        else
        {
            if (meta && meta->l1_fee.has_value())
                gotL1Fee = hexU256Bcos(*meta->l1_fee);
            // _op_operator_fee 在场规则镜像 op-geth deriveOPStackFields（槽 8 scalar/
            // constant 全零不发射）；本侧同规则的载体是 meta.operator_fee_scalar/
            // constant（deriveOpReceiptMeta 仅在非全零时填）。meta.operator_fee 本身是
            // FISCO 扩展、Isthmus+ 恒填（含 0），直接比对会把「表示差异」误报成分歧。
            if (meta &&
                (meta->operator_fee_scalar.has_value() || meta->operator_fee_constant.has_value()))
                gotOperatorFee = hexU256Bcos(meta->operator_fee.value_or(bcos::u256{0}));
            if (meta && meta->da_footprint.has_value())
                gotDaFootprint = hexU64(*meta->da_footprint);
            // 全字段比对（fieldmap ecotone/fjord/granite/holocene/isthmus/jovian）：
            // u256 → hexU256Bcos、uint64 → hexU64。got-reads 位于非 deposit 分支内（与生成器
            // !IsDepositTx 发射规则对齐：deposit 回执双端都不带费用字段，未 gate 会假分歧）。
            // operator-fee 预 Isthmus 缺席：ecotone/fjord/granite/holocene 的 has_operator_fee
            // =false → deriveOpReceiptMeta 不填 operator_fee* → got 全 nullopt；生成器对预
            // Isthmus 不发射 _op_operator_fee → optWant 也 nullopt → 双缺席 pass（无假分歧）。
            // l1_gas_used 无条件保留——即便向量无该 key（optWant nullopt），也断言 FISCO 侧
            // 存在性（Task 4 补算；Fjord+ 恒发射，Ecotone 为 bedrockCalldataGasUsed）。
            if (meta && meta->l1_gas_price.has_value())
                gotL1GasPrice = hexU256Bcos(*meta->l1_gas_price);
            if (meta && meta->l1_blob_base_fee.has_value())
                gotL1BlobBaseFee = hexU256Bcos(*meta->l1_blob_base_fee);
            if (meta && meta->l1_gas_used.has_value())
                gotL1GasUsed = hexU64(*meta->l1_gas_used);
            if (meta && meta->l1_base_fee_scalar.has_value())
                gotL1BaseFeeScalar = hexU64(*meta->l1_base_fee_scalar);
            if (meta && meta->l1_blob_base_fee_scalar.has_value())
                gotL1BlobBaseFeeScalar = hexU64(*meta->l1_blob_base_fee_scalar);
            if (meta && meta->operator_fee_scalar.has_value())
                gotOpFeeScalar = hexU64(*meta->operator_fee_scalar);
            if (meta && meta->operator_fee_constant.has_value())
                gotOpFeeConstant = hexU64(*meta->operator_fee_constant);
            if (meta && meta->da_footprint_gas_scalar.has_value())
                gotDaFootprintGasScalar = hexU64(*meta->da_footprint_gas_scalar);
        }

        ctx.checkField(p + ".type", hexU256(parseU256(er.at("type"))),
            hexU64(static_cast<uint64_t>(result.txTypes[i])));
        ctx.checkField(p + ".status", hexU256(parseU256(er.at("status"))),
            receipt->status() == 0 ? "0x1" : "0x0");
        ctx.checkField(p + ".gasUsed", hexU256(parseU256(er.at("gasUsed"))),
            hexU64(static_cast<uint64_t>(receipt->gasUsed())));
        ctx.checkField(p + ".cumulativeGasUsed", hexU256(parseU256(er.at("cumulativeGasUsed"))),
            std::string{receipt->cumulativeGasUsed()});
        ctx.checkField(p + ".logsCount", std::to_string(er.at("logsCount").get<int64_t>()),
            std::to_string(receipt->logEntries().size()));
        // 回执 output（tx 执行返回数据）：生成器恒发射（空 = "0x"）；FISCO 侧 output() 返回
        // 原始字节，hexBytes 归一化为 "0x"+小写 hex。双缺席/双在场逐字节比对——wrapper 的
        // returndata 截断与 p256 的 32-byte-1 都由它钉死。
        ctx.checkOptional(p + ".output",
            er.contains("output") ? std::optional{er.at("output").get<std::string>()} :
                                    std::nullopt,
            std::optional{
                hexBytes(evmc::bytes_view{receipt->output().data(), receipt->output().size()})});

        const auto optWant = [&](const char* key) -> std::optional<std::string> {
            return er.contains(key) ? std::optional{hexU256(parseU256(er.at(key)))} : std::nullopt;
        };
        ctx.checkOptional(p + "._op_deposit_nonce", optWant("_op_deposit_nonce"), gotDepNonce);
        ctx.checkOptional(p + "._op_deposit_receipt_version",
            optWant("_op_deposit_receipt_version"), gotDepVersion);
        ctx.checkOptional(p + "._op_l1_fee", optWant("_op_l1_fee"), gotL1Fee);
        ctx.checkOptional(p + "._op_operator_fee", optWant("_op_operator_fee"), gotOperatorFee);
        ctx.checkOptional(p + "._op_da_footprint", optWant("_op_da_footprint"), gotDaFootprint);
        ctx.checkOptional(p + "._op_l1_gas_price", optWant("_op_l1_gas_price"), gotL1GasPrice);
        ctx.checkOptional(
            p + "._op_l1_blob_base_fee", optWant("_op_l1_blob_base_fee"), gotL1BlobBaseFee);
        ctx.checkOptional(p + "._op_l1_gas_used", optWant("_op_l1_gas_used"), gotL1GasUsed);
        ctx.checkOptional(
            p + "._op_l1_base_fee_scalar", optWant("_op_l1_base_fee_scalar"), gotL1BaseFeeScalar);
        ctx.checkOptional(p + "._op_l1_blob_base_fee_scalar",
            optWant("_op_l1_blob_base_fee_scalar"), gotL1BlobBaseFeeScalar);
        ctx.checkOptional(
            p + "._op_operator_fee_scalar", optWant("_op_operator_fee_scalar"), gotOpFeeScalar);
        ctx.checkOptional(p + "._op_operator_fee_constant", optWant("_op_operator_fee_constant"),
            gotOpFeeConstant);
        ctx.checkOptional(p + "._op_da_footprint_gas_scalar",
            optWant("_op_da_footprint_gas_scalar"), gotDaFootprintGasScalar);
    }

    // ── postState 双向（决策记录 8）─────────────────────────────────────────
    // 前向：向量逐账户逐槽 vs 回放终态。零槽/零账户按 trie 语义规约（0 ≡ 缺席）：
    // 向量把「块后不存在的候选账户」发射为 {"balance":"0x0"}——比对语义即
    // 「四字段全零 + 全槽零」，实测侧账户缺失时以零账户视之，恒等 ⇒ pass；
    // 实测侧存在但为空同样 pass（EIP-161 空账户 ≡ trie 不存在）。
    const auto& post = blk.at("postState");
    std::set<evmc::address> postAddrs;
    static const test::TestAccount kZeroAccount{};
    for (const auto& [addrStr, acc] : post.items())
    {
        const auto addr = test::from_json<evmc::address>(Json(addrStr));
        postAddrs.insert(addr);
        const auto ap = "postState." + hexAddr(addr);

        const auto it = ts.find(addr);
        const test::TestAccount& got = it != ts.end() ? it->second : kZeroAccount;

        ctx.checkField(
            ap + ".balance", hexU256(parseU256(acc.at("balance"))), hexU256(got.balance));
        ctx.checkField(ap + ".nonce",
            hexU64(acc.contains("nonce") ? test::from_json<uint64_t>(acc.at("nonce")) : 0),
            hexU64(got.nonce));
        ctx.checkField(ap + ".code",
            acc.contains("code") ? hexBytes(test::from_json<bytes>(acc.at("code"))) : "0x",
            hexBytes(got.code));

        // 槽并集 = 向量声明槽 ∪ 实测非零槽 ∪ 回放写集触及槽（覆盖断言 (ii) 的槽维度：
        // 触及槽必然入并集被显式比对——终值非零而向量未列 ⇒ want=0x0 翻红；终值零而
        // 向量未列 ⇒ 0 ≡ 缺席双零 pass，即"已覆盖"）。
        std::map<evmc::bytes32, intx::uint256> wantStorage;
        if (acc.contains("storage"))
        {
            for (const auto& [slotStr, valJ] : acc.at("storage").items())
                wantStorage[test::from_json<hash256>(Json(slotStr))] = parseU256(valJ);
        }
        std::set<evmc::bytes32> slots;
        for (const auto& [k, val] : wantStorage)
            slots.insert(k);
        for (const auto& [k, val] : got.storage)
            slots.insert(k);
        if (const auto tIt = touchedSlots.find(addr); tIt != touchedSlots.end())
            slots.insert(tIt->second.begin(), tIt->second.end());
        for (const auto& slot : slots)
        {
            const auto wIt = wantStorage.find(slot);
            const auto want = wIt != wantStorage.end() ? wIt->second : intx::uint256{0};
            const auto gIt = got.storage.find(slot);
            const auto gotVal = gIt != got.storage.end() ?
                                    intx::be::load<intx::uint256>(gIt->second) :
                                    intx::uint256{0};
            ctx.checkField(ap + ".storage." + hexSlot(slot), hexU256(want), hexU256(gotVal));
        }
    }
    // 反向存在性：回放终态里向量未列的非空账户 = DIVERGE（向量候选集声称覆盖全部
    // 写到的账户；空账户 ≡ trie 不存在，规约后 pass）。
    for (const auto& [addr, acc] : ts)
    {
        if (postAddrs.contains(addr))
            continue;
        const bool storageAllZero = std::ranges::all_of(
            acc.storage, [](const auto& kv) { return evmc::is_zero(kv.second); });
        if (acc.nonce != 0 || acc.balance != 0 || !acc.code.empty() || !storageAllZero)
            ledger.diverge(id, "postState." + hexAddr(addr) + ".exists", kAbsent, "<present>");
    }
    // 覆盖断言 (ii) 地址维度：回放 applyDiff 累计触及、但向量 postState 未列出的
    // 地址 = DIVERGE .uncovered（触及后又被删的账户生成器会以 {"balance":"0x0"}
    // 候选形式发射，仍在 postAddrs 内——不在即语料候选集漏账户）。
    for (const auto& addr : touchedAddrs)
    {
        if (!postAddrs.contains(addr))
            ledger.diverge(
                id, "postState." + hexAddr(addr) + ".uncovered", "<covered>", "<uncovered>");
    }

    // 每向量比对计数：0 = FAILURE（防"空转绿"）。
    if (ctx.comparisons == 0)
        BOOST_ERROR(id << ": zero comparisons executed");
}

// ── 单向量回放（扁平向量：pre 顶层；链式向量由 replayChainVector 逐块调用）────────

void replayVector(const std::string& id, const Json& v, DivergenceLedger& ledger, evmc::VM& vm,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory)
{
    evmone::test::TestState ts;
    replaySingleBlockInto(id, v, ts, &v.at("pre"), ledger, vm, receiptFactory);
}

// ── reject 分支（增强语料 §1a/§3b）────────────────────────────────────────────
// 顶层 _op_expected.reject 存在 → 无效向量。T8n 端只消费 executor/both（engine 直连字段损坏
// 类由 OpNewPayloadRpcE2eTest 消费，Task 2）。

bool hasReject(const Json& v)
{
    return v.contains("_op_expected") && v.at("_op_expected").contains("reject");
}

std::string rejectConsumer(const Json& v)
{
    // consumer 缺省视为 executor（T8n 是执行层回放器；engine 类向量生成器会显式写 engine）。
    return v.at("_op_expected").at("reject").at("fisco").value("consumer", "executor");
}

/// reject(executor/both) 断言：processOpBlock 必须抛 std::runtime_error 且 what() 含期望子串。
/// 复用 loadBlockContext 装载（env→BlockInfo / ParentOnlyBlockHashes / transactions→OpBlockTx），
/// 但跳过"执行成功断言"路径（header/receipts/postState 不比——reject 向量无该比对语义）。
/// throw 侧经验证可被 typed-catch 接住：OpSchedulerImplSmokeTest.cpp:161 用 BOOST_CHECK_THROW(
/// ..., std::runtime_error) 接 processOpBlock 空块拒绝，绿（FISCO 侧 throw 用 libc++ 唯一
/// typeinfo，非 libevmone -fno-rtti hidden 拷贝）。
void assertRejectThrow(const std::string& id, const Json& v, evmc::VM& vm,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory)
{
    BlockContext bc;
    if (!loadBlockContext(id, v, bc))
        return;
    // decode-class reject（blob）：装载段已复现 decodeOneRawTx 拒绝并记录消息。processOpBlock
    // 走不到该 decode（txs 已是 OpBlockTx），此处直接断言记录的消息（与 engine 面同串）。
    if (bc.decodeRejectMessage.has_value())
    {
        const auto expected = v.at("_op_expected")
                                  .at("reject")
                                  .at("fisco")
                                  .at("validation_error_contains")
                                  .get<std::string>();
        BOOST_CHECK_MESSAGE(bc.decodeRejectMessage->find(expected) != std::string::npos,
            id << ": decode reject message missing '" << expected
               << "', got: " << *bc.decodeRejectMessage);
        return;
    }
    evmone::test::TestState ts = test::from_json<test::TestState>(v.at("pre"));
    // applyDiff 回调与 replaySingleBlockInto 执行段同参（含回写 ts）；reject 后 ts 被丢弃。
    std::set<evmc::address> touchedAddrs;
    std::map<evmc::address, std::set<evmc::bytes32>> touchedSlots;
    const auto apply = [&](const state::StateDiff& d) {
        for (const auto& m : d.modified_accounts)
        {
            touchedAddrs.insert(m.addr);
            for (const auto& [k, val] : m.modified_storage)
                touchedSlots[m.addr].insert(k);
        }
        for (const auto& a : d.deleted_accounts)
            touchedAddrs.insert(a);
        bcos::evm::applyStateDiffStrict(ts, d);
    };
    try
    {
        // ⚠️ 首笔 deposit 之后插入的非法 tx 才会抛 "invalid non-deposit tx"；
        //    deposit-only 块不抛（合法执行）——向量必须含非法交易。
        processOpBlock(
            ts, bc.blk, bc.hashes, bc.txs, *bc.cfg, vm, bc.chainId, receiptFactory, apply);
    }
    catch (const std::runtime_error& e)
    {
        const auto expected = v.at("_op_expected")
                                  .at("reject")
                                  .at("fisco")
                                  .at("validation_error_contains")
                                  .get<std::string>();
        BOOST_CHECK_MESSAGE(std::string(e.what()).find(expected) != std::string::npos,
            id << ": throw message missing '" << expected << "', got: " << e.what());
        return;
    }
    catch (...)
    {
        // typed-catch RTTI 兜底（机理见 replaySingleBlockInto 的 typed-catch 注释）——点名
        // 动态类型，按 typed 分支同语义收尾（红仍红、流程不变）。
        const auto* excType = abi::__cxa_current_exception_type();
        BOOST_ERROR(id << ": threw non-runtime_error (typed catch bypassed, exception type: "
                       << (excType ? excType->name() : "<unknown>") << ")");
        return;
    }
    BOOST_ERROR(id << ": expected processOpBlock to reject, but it executed");
}

// ── 链式回放（增强语料 §1b/§3b）────────────────────────────────────────────
// blocks[0] 播种 pre；blocks[i>0] 的 pre:null → 用上一块 post-state（applyDiff 回写后的 ts）。
// chainState 以引用跨块传递（blocks[i>0] 跳过 pre 解析——pre:null 会命中 from_json 的
// is_object 断言，故 pre 指针为 nullptr）。ParentOnlyBlockHashes 只回答 blockNumber-1（:325-334），
// 链式向量不得含读历史 blockhash 的 tx（transfer 安全；审查 R13）。

void replayChainVector(const std::string& id, const Json& v, DivergenceLedger& ledger, evmc::VM& vm,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory)
{
    const auto& blocks = v.at("blocks");
    evmone::test::TestState chainState;
    for (std::size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& blk = blocks[i];
        const Json* pre = nullptr;
        if (blk.contains("pre") && !blk["pre"].is_null())
        {
            chainState = test::from_json<test::TestState>(blk.at("pre"));
            pre = &blk["pre"];
        }
        replaySingleBlockInto(
            id + "[" + std::to_string(i) + "]", blk, chainState, pre, ledger, vm, receiptFactory);
    }
}
}  // namespace
// ── structurallyUnrecoverable 谓词边界单测（防退化恒真/恒假后标记逃逸复活）──
BOOST_AUTO_TEST_SUITE(OpT8nReplay)

BOOST_AUTO_TEST_CASE(StructurallyUnrecoverablePredicateBoundaries)
{
    auto mk = [](uint64_t v, const intx::uint256& r, const intx::uint256& s) {
        evmone::state::Authorization a{};
        a.v = v;
        a.r = r;
        a.s = s;
        return a;
    };
    const intx::uint256 one{1};
    BOOST_CHECK(!structurallyUnrecoverable(mk(0, one, one)));
    BOOST_CHECK(!structurallyUnrecoverable(mk(1, one, kSecpHalfN)));       // s == N/2 合法
    BOOST_CHECK(structurallyUnrecoverable(mk(2, one, one)));               // v > 1
    BOOST_CHECK(structurallyUnrecoverable(mk(0, one, kSecpHalfN + 1)));    // s > N/2
    BOOST_CHECK(structurallyUnrecoverable(mk(0, intx::uint256{0}, one)));  // r == 0
    BOOST_CHECK(!structurallyUnrecoverable(mk(0, kSecpN - 1, one)));
    BOOST_CHECK(structurallyUnrecoverable(mk(0, kSecpN, one)));            // r >= N
    BOOST_CHECK(structurallyUnrecoverable(mk(0, one, intx::uint256{0})));  // s == 0
}

BOOST_AUTO_TEST_CASE(Vectors)
{
    const fs::path vectorsDir = OP_T8N_VECTORS_DIR;
    BOOST_REQUIRE_MESSAGE(fs::is_directory(vectorsDir), vectorsDir);

    // A) 集合相等：目录 *.json 文件名集合 == manifest.txt 清单（缺/多均 FAILURE）。
    const auto manifest = loadManifest(vectorsDir / "manifest.txt");
    BOOST_REQUIRE_MESSAGE(!manifest.empty(), "empty manifest");
    std::set<std::string> present;
    for (const auto& entry : fs::directory_iterator(vectorsDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            present.insert(entry.path().filename().string());
    }
    for (const auto& name : manifest)
    {
        if (!present.contains(name))
            BOOST_ERROR("manifest lists " << name << " but file is missing");
    }
    // Task 6（§4c item 3/12 排除，强制）：expectedBlobVersionedHashes / executionRequests 经
    // GoldenSample loader 不可表达，生成器按 brief 仍发射文件但强制不入 manifest（task-3-report）。
    // 集合相等豁免这两个已知未注册 static 面文件（后缀匹配，base 无关）。
    const auto isUnregisteredStatic = [](std::string const& n) {
        // "_static_3.json" = 14 chars, "_static_12.json" = 15 chars（后缀匹配，base 无关）
        return (n.size() >= 14 && n.rfind("_static_3.json") == n.size() - 14) ||
               (n.size() >= 15 && n.rfind("_static_12.json") == n.size() - 15);
    };
    for (const auto& name : present)
    {
        if (!manifest.contains(name) && !isUnregisteredStatic(name))
            BOOST_ERROR("unmanifested vector file present: " << name);
    }

    auto ledger = DivergenceLedger::load(vectorsDir / "DIVERGENCES.md");
    auto vm = evmc::VM{evmc_create_evmone()};
    auto receiptFactory = makeTestReceiptFactory();

    // 只回放两集合交集（缺/多已各自 FAILURE；不让集合错误级联成 parse 崩溃）。
    for (const auto& name : manifest)
    {
        if (!present.contains(name))
            continue;
        const auto file = vectorsDir / name;
        // parse 失败/必填字段缺失 → ADD_FAILURE 点名文件后进入下一个文件；绝不静默。
        try
        {
            std::ifstream input(file);
            const auto doc = Json::parse(input);
            std::string id;
            const Json* vec = nullptr;
            for (const auto& [key, val] : doc.items())
            {
                if (key == "_op_test_vectors")
                    continue;
                if (vec != nullptr)
                    throw std::runtime_error("more than one vector object in file");
                id = key;
                vec = &val;
            }
            if (vec == nullptr)
                throw std::runtime_error("no vector object in file");
            const auto stem = file.stem().string();
            if (id != stem)
                throw std::runtime_error("vector id '" + id + "' != filename stem '" + stem + "'");
            // ⚠️ 顺序：chain 向量顶层只有 blocks、无 _op_expected——reject 判断
            // （v.at("_op_expected")）会抛 out_of_range，必须先判 blocks。
            if (vec->contains("blocks"))
            {
                replayChainVector(id, *vec, ledger, vm, receiptFactory);
                continue;
            }
            if (hasReject(*vec))
            {
                const auto consumer = rejectConsumer(*vec);
                if (consumer == "engine")
                    continue;  // 字段损坏类：仅 OpNewPayloadRpcE2eTest（Task 2）
                assertRejectThrow(id, *vec, vm, receiptFactory);  // executor/both
                continue;
            }
            replayVector(id, *vec, ledger, vm, receiptFactory);
        }
        catch (const std::exception& e)
        {
            BOOST_ERROR(name << ": " << e.what());
        }
        catch (...)
        {
            // typed-catch RTTI 兜底（同上：机理见 docs/audits/ 排查报告）——点名类型，
            // 与 typed 分支同语义（记 FAILURE 后继续下一向量文件）。
            const auto* excType = abi::__cxa_current_exception_type();
            BOOST_ERROR(name << ": exception escaped typed catch (exception type: "
                             << (excType ? excType->name() : "<unknown>") << ")");
        }
    }

    // E) 尾账：过期豁免即红 + KNOWN-DIVERGE 总数入 RecordProperty。
    ledger.finish();
}

// ── reject 分支（增强语料 §1a/§3b）：processOpBlock 对非法非 deposit tx 必须抛
//    std::runtime_error("op block: invalid non-deposit tx: ...")（OpBlockExecute.cpp:190），
//    断言 throw + what() 子串（审查 HIGH#5）。内联向量（非语料文件，本任务直接内嵌）——
//    字段必须满足装载器硬要求（OpT8nReplayTest.cpp:513-643）：
//    deposit 的 data 位于交易对象顶层（装载器 t.at("data")，非 _op_deposit 内）；
//    from=OP_DEPOSITOR(0xdead...0001) to=OP_L1_BLOCK(0x...0015)（OpPredeploys.h:11/:22、
//    OpBlockExecute.cpp:31）；mint/value/gas 一律 hex 字符串。
//    第二笔 eip1559 必须带 _op_raw 真实签名 EIP-2718 信封（opValidate 强制 signedTxEnvelope
//    非空，OpTransition.cpp:362）；gas=0 → validate_transaction 抛 INTRINSIC_GAS_TOO_LOW
//    （eth/state/errors.hpp:51）→ processOpBlock 抛 "op block: invalid non-deposit tx:
//    intrinsic gas too low"（OpBlockExecute.cpp:190）。
BOOST_AUTO_TEST_CASE(RejectExecutorSurface)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    auto receiptFactory = makeTestReceiptFactory();
    // ⚠️ DivergenceLedger 默认构造；load("") 会 BOOST_ERROR("DIVERGENCES.md missing")。
    // assertRejectThrow 不消费 ledger（reject 向量无 postState/豁免比对），仅按 brief 保留。
    DivergenceLedger ledger;
    // 手造向量用 Json::parse（nlohmann 的 Json::object{...} 多层 initializer_list 在本版本
    // 有编译歧义——basic_json(initializer_list_t) 无可行转换；raw string 直写 schema 更稳）。
    Json v = Json::parse(R"({
        "_info": {"hardfork": "isthmus"},
        "env": {
            "currentNumber": 1, "currentTimestamp": "0x64",
            "currentGasLimit": "0x989680", "currentBaseFee": "0x3b9aca00",
            "currentCoinbase": "0x0000000000000000000000000000000000000000",
            "currentRandom": "0x0000000000000000000000000000000000000000000000000000000000000000",
            "parentBeaconBlockRoot": "0x0000000000000000000000000000000000000000000000000000000000000000",
            "parentHash": "0x0000000000000000000000000000000000000000000000000000000000000000"
        },
        "pre": {
            "0x0000000000000000000000000000000000000001": {
                "balance": "0xde0b6b3a7640000", "nonce": "0x0", "code": "0x"
            }
        },
        "block": {
            "transactions": [
                {
                    "_op_type": "deposit",
                    "_op_deposit": {
                        "source_hash": "0x0000000000000000000000000000000000000000000000000000000000000000",
                        "from": "0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001",
                        "to": "0x4200000000000000000000000000000000000015",
                        "mint": "0x0", "value": "0x0", "gas": "0x186a0",
                        "is_system_tx": false
                    },
                    "data": "0x"
                },
                {
                    "_op_type": "eip1559",
                    "_op_raw": "0x02f874822105808405f5e100847735940082520894b0b0000000000000000000000000000000000001880de0b6b3a764000080c001a0e37533ddb9f696c0b21788f1b00c78adc4a81b1d811d84e70fad672096fc924ea00ae693f4d68955a4c01ee8bab26f5be740ee416dd2556822f68b747d5aab7714",
                    "chainId": "0x2105", "nonce": "0x0",
                    "to": "0xb0b0000000000000000000000000000000000001",
                    "gas": "0x0", "maxFeePerGas": "0x77359400", "maxPriorityFeePerGas": "0x5f5e100",
                    "value": "0x0", "data": "0x",
                    "sender": "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"
                }
            ]
        },
        "_op_expected": {
            "reject": {
                "op_geth": "intrinsic gas too low",
                "fisco": {
                    "consumer": "executor", "classification": "INVALID",
                    "latest_valid_hash": "parent",
                    "validation_error_contains": "invalid non-deposit tx"
                }
            }
        }
    })");
    assertRejectThrow("reject_executor_intrinsic", v, vm, receiptFactory);
}

// ── blob decode-class reject（Task 4，consumer:both）──────────────────────
// blob 臂用 decodeOneRawTx 复现真实 decode 拒绝（processOpBlock 走不到 raw-tx decode），
// 消息 = "unsupported tx type byte 0x3"（OpSchedulerImpl.h:893）。_op_raw 仅需 type-0x03
// 首字节即命中 decode 分支（decode 在任何字段解析前检查 type byte）。
BOOST_AUTO_TEST_CASE(RejectBlobDecode)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    auto receiptFactory = makeTestReceiptFactory();
    DivergenceLedger ledger;
    Json v = Json::parse(R"({
        "_info": {"hardfork": "isthmus"},
        "env": {
            "currentNumber": 1, "currentTimestamp": "0x64",
            "currentGasLimit": "0x989680", "currentBaseFee": "0x3b9aca00",
            "currentCoinbase": "0x0000000000000000000000000000000000000000",
            "currentRandom": "0x0000000000000000000000000000000000000000000000000000000000000000",
            "parentBeaconBlockRoot": "0x0000000000000000000000000000000000000000000000000000000000000000",
            "parentHash": "0x0000000000000000000000000000000000000000000000000000000000000000"
        },
        "pre": {
            "0x0000000000000000000000000000000000000001": {
                "balance": "0xde0b6b3a7640000", "nonce": "0x0", "code": "0x"
            }
        },
        "block": {
            "transactions": [
                {
                    "_op_type": "deposit",
                    "_op_deposit": {
                        "source_hash": "0x0000000000000000000000000000000000000000000000000000000000000000",
                        "from": "0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001",
                        "to": "0x4200000000000000000000000000000000000015",
                        "mint": "0x0", "value": "0x0", "gas": "0x186a0",
                        "is_system_tx": false
                    },
                    "data": "0x"
                },
                {
                    "_op_type": "blob",
                    "_op_raw": "0x03",
                    "chainId": "0x2105", "nonce": "0x0",
                    "to": "0xb0b0000000000000000000000000000000000001",
                    "gas": "0x186a0", "maxFeePerGas": "0x77359400", "maxPriorityFeePerGas": "0x5f5e100",
                    "value": "0x0", "data": "0x",
                    "sender": "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"
                }
            ]
        },
        "_op_expected": {
            "reject": {
                "op_geth": "data blobs present in block body",
                "fisco": {
                    "consumer": "both", "classification": "INVALID",
                    "latest_valid_hash": "parent",
                    "validation_error_contains": "unsupported tx type byte 0x3"
                }
            }
        }
    })");
    assertRejectThrow("reject_blob_decode", v, vm, receiptFactory);
}

// ── legacy 臂装载（Task 3 F1）─────────────────────────────────────────────
// 验证 _op_type "legacy" 装载出 type=legacy 交易且 gasPrice 同时填 max/priority（evmone legacy
// 单价格语义）。_op_raw 仅结构占位（装载段不 decode）；golden 全路径由语料 isthmus/jovian_
// legacy_transfer 向量覆盖（regen 后 Vectors 全量回放）。
BOOST_AUTO_TEST_CASE(LegacyArmBuildsLegacyTx)
{
    Json v = Json::parse(R"({
        "_info": {"hardfork": "isthmus"},
        "env": {
            "currentNumber": 1, "currentTimestamp": "0x64",
            "currentGasLimit": "0x989680", "currentBaseFee": "0x3b9aca00",
            "currentCoinbase": "0x0000000000000000000000000000000000000000",
            "currentRandom": "0x0000000000000000000000000000000000000000000000000000000000000000",
            "parentBeaconBlockRoot": "0x0000000000000000000000000000000000000000000000000000000000000000",
            "parentHash": "0x0000000000000000000000000000000000000000000000000000000000000000"
        },
        "pre": {
            "0x0000000000000000000000000000000000000001": {
                "balance": "0xde0b6b3a7640000", "nonce": "0x0", "code": "0x"
            }
        },
        "block": {
            "transactions": [
                {
                    "_op_type": "deposit",
                    "_op_deposit": {
                        "source_hash": "0x0000000000000000000000000000000000000000000000000000000000000000",
                        "from": "0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001",
                        "to": "0x4200000000000000000000000000000000000015",
                        "mint": "0x0", "value": "0x0", "gas": "0x186a0",
                        "is_system_tx": false
                    },
                    "data": "0x"
                },
                {
                    "_op_type": "legacy",
                    "_op_raw": "0x01",
                    "chainId": "0x2105", "nonce": "0x0",
                    "to": "0xb0b0000000000000000000000000000000000001",
                    "gas": "0x5208", "gasPrice": "0x4a817c800",
                    "value": "0xde0b6b3a7640000", "data": "0x",
                    "sender": "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"
                }
            ]
        }
    })");
    BlockContext bc;
    BOOST_REQUIRE_MESSAGE(loadBlockContext("legacy_arm_load", v, bc), "legacy vector must load");
    BOOST_REQUIRE_EQUAL(bc.txs.size(), 2u);
    const auto* tx = std::get_if<state::Transaction>(&bc.txs[1].tx);
    BOOST_REQUIRE_MESSAGE(tx != nullptr, "tx[1] must be a normal tx");
    BOOST_CHECK(tx->type == state::Transaction::Type::legacy);
    BOOST_CHECK_EQUAL(tx->chain_id, uint64_t{0x2105});
    // legacy 单价格：max == priority == gasPrice
    BOOST_CHECK(tx->max_gas_price == tx->max_priority_gas_price);
    BOOST_CHECK(tx->max_gas_price == parseU256(Json::parse("\"0x4a817c800\"")));
    BOOST_CHECK(bc.txs[1].signedEnvelope.size() > 0);
}

BOOST_AUTO_TEST_SUITE_END()
