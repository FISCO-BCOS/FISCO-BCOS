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
// count recorded, 0 = FAILURE. B) required fields via jAt(); hardfork must be
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
#include <json/json.h>
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
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <test/utils/rlp.hpp>
#include <test/utils/test_state.hpp>
#include <vector>

namespace fs = std::filesystem;
// Non-conflicting alias for jsoncpp's value type (a bare `using Json = Json::Value`
// would shadow the `Json` namespace and break Json::Reader/Json::Value).
using JsonValue = Json::Value;

// ── jsoncpp .at() equivalent ────────────────────────────────────────────────
// nlohmann's .at(key) throws on a missing member; jsoncpp's operator[] silently
// fabricates a null. Every required-field access in this replayer goes through
// jAt so a missing field is a named failure, never a silent null read.
inline const Json::Value& jAt(const Json::Value& v, const std::string& key)
{
    if (!v.isMember(key))
        throw std::invalid_argument("missing required field: " + key);
    return v[key];
}

// jsoncpp Reader-based parse (nlohmann Json::parse equivalent; string and stream).
inline Json::Value jParse(const std::string& input)
{
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(input, root))
        throw std::runtime_error("JSON parse failed: " + reader.getFormattedErrorMessages());
    return root;
}

inline Json::Value jParse(std::istream& input)
{
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(input, root))
        throw std::runtime_error("JSON parse failed: " + reader.getFormattedErrorMessages());
    return root;
}
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
T from_json(const Json::Value& j) = delete;

template <>
int64_t from_json<int64_t>(const Json::Value& j)
{
    if (j.isIntegral())
    {
        if (j.isInt64())
            return j.asInt64();
        throw std::invalid_argument("from_json<int64_t>: integer out of range");
    }
    if (!j.isString())
        throw std::invalid_argument("from_json<int64_t>: must be integer or string of integer");
    const auto s = j.asString();
    size_t num_processed = 0;
    const auto v = static_cast<int64_t>(std::stoull(s, &num_processed, 0));
    if (num_processed == 0 || num_processed != s.size())
        throw std::invalid_argument("from_json<int64_t>: must be integer or string of integer");
    return v;
}

template <>
uint64_t from_json<uint64_t>(const Json::Value& j)
{
    if (j.isIntegral())
    {
        if (j.isUInt64())
            return j.asUInt64();
        throw std::invalid_argument("from_json<uint64_t>: integer out of range");
    }
    if (!j.isString())
        throw std::invalid_argument("from_json<uint64_t>: must be integer or string of integer");
    const auto s = j.asString();
    size_t num_processed = 0;
    const auto v = static_cast<uint64_t>(std::stoull(s, &num_processed, 0));
    if (num_processed == 0 || num_processed != s.size())
        throw std::invalid_argument("from_json<uint64_t>: must be integer or string of integer");
    return v;
}

template <>
intx::uint256 from_json<intx::uint256>(const Json::Value& j)
{
    return intx::from_string<intx::uint256>(j.asString());
}

template <>
evmone::bytes from_json<evmone::bytes>(const Json::Value& j)
{
    return evmc::from_hex(j.asString()).value();
}

template <>
evmc::address from_json<evmc::address>(const Json::Value& j)
{
    const auto v = evmc::from_hex<evmc::address>(j.asString());
    if (!v.has_value())
        throw std::invalid_argument("from_json<address>: must be hexadecimal string");
    return *v;
}

// Note: evmone::hash256 is a using-alias of evmc::bytes32, so this one specialization serves both
// from_json<hash256> (header hashes) and from_json<bytes32> (storage keys/values).
template <>
evmc::bytes32 from_json<evmc::bytes32>(const Json::Value& j)
{
    const auto s = j.asString();
    if (s == "0" || s == "0x0")  // Special case to handle "0". Required by exec-spec-tests.
        return evmc::bytes32{};
    const auto v = evmc::from_hex<evmc::bytes32>(s);
    if (!v.has_value())
        throw std::invalid_argument("from_json<bytes32>: must be hexadecimal string");
    return *v;
}

template <>
evmone::test::TestState from_json<evmone::test::TestState>(const Json::Value& j)
{
    evmone::test::TestState o;
    assert(j.isObject());
    for (const auto& j_addr : j.getMemberNames())
    {
        const auto& j_acc = j[j_addr];
        auto& acc = o[from_json<evmc::address>(Json::Value(j_addr))] = {
            .nonce = from_json<uint64_t>(jAt(j_acc, "nonce")),
            .balance = from_json<intx::uint256>(jAt(j_acc, "balance")),
            .code = from_json<evmone::bytes>(jAt(j_acc, "code"))};
        if (j_acc.isMember("storage"))
        {
            const auto& storage = j_acc["storage"];
            for (const auto& j_key : storage.getMemberNames())
            {
                const auto& j_value = storage[j_key];
                if (const auto value = from_json<evmc::bytes32>(j_value); !evmc::is_zero(value))
                    acc.storage[from_json<evmc::bytes32>(Json::Value(j_key))] = value;
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

// bytes32 slot keys/values are written as minimal-numeric hex (trie semantics: 0 == absent,
// compared after normalization).
std::string hexSlot(const evmc::bytes32& b)
{
    return hexU256(intx::be::load<intx::uint256>(b));
}

intx::uint256 parseU256(const JsonValue& j)
{
    return intx::from_string<intx::uint256>(j.asString());
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
    // Missing ledger file = FAILURE (the ledger is a gate deliverable; a missing file must never
    // imply all-exempt/all-empty).
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
                // FINDING-dual-* entries belong to OpDualPathEquivalenceTest's own ledger
                // instance (A-vs-B harness); this suite's finish() stale-check must not see them.
                if (e.entryId.rfind("FINDING-dual-", 0) != 0)
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

    // An exemption never hit this run = FAILURE (stale exemption turns red; must be cleared after
    // fix/vector regen).
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
// visit surface here. AccountView mirrors MemoryState::AccountView's root-building
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

bool loadBlockContext(const std::string& id, const JsonValue& blk, BlockContext& out)
{
    // _info.hardfork must be exactly ecotone|fjord|granite|holocene|isthmus|jovian,
    // anything else = FAILURE. No default fork (the default-Isthmus precedent is a
    // known hole, not ported). isJovian drives the blobGasUsed header gate and the
    // _op_da_footprint expectation — ecotone/fjord/granite/holocene are all false,
    // matching isthmus semantics (has_da_footprint true only on Jovian).
    const auto hardfork = jAt(jAt(blk, "_info"), "hardfork").asString();
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
    const auto& env = jAt(blk, "env");
    auto& bi = out.blk;
    bi.number = test::from_json<int64_t>(jAt(env, "currentNumber"));
    bi.timestamp = test::from_json<int64_t>(jAt(env, "currentTimestamp"));
    bi.gas_limit = test::from_json<int64_t>(jAt(env, "currentGasLimit"));
    bi.base_fee = test::from_json<uint64_t>(jAt(env, "currentBaseFee"));
    bi.coinbase = test::from_json<evmc::address>(jAt(env, "currentCoinbase"));
    bi.prev_randao = test::from_json<hash256>(jAt(env, "currentRandom"));
    bi.parent_beacon_block_root = test::from_json<hash256>(jAt(env, "parentBeaconBlockRoot"));

    auto& hs = out.hashes;
    hs.blockNumber = bi.number;
    hs.parentHash = test::from_json<hash256>(jAt(env, "parentHash"));

    // Three transaction arms (deposit / eip1559 / setcode). Unknown _op_type = FAILURE.
    auto& txs = out.txs;
    std::optional<uint64_t> vectorChainId;
    for (const auto& t : jAt(jAt(blk, "block"), "transactions"))
    {
        const auto opType = jAt(t, "_op_type").asString();
        if (opType == "deposit")
        {
            const auto& d = jAt(t, "_op_deposit");
            DepositTx dep;
            dep.source_hash = test::from_json<hash256>(jAt(d, "source_hash"));
            dep.from = test::from_json<evmc::address>(jAt(d, "from"));
            dep.to = jAt(d, "to").isNull() ?
                         std::nullopt :
                         std::optional{test::from_json<evmc::address>(jAt(d, "to"))};
            dep.mint = d.isMember("mint") ? std::optional{parseU256(jAt(d, "mint"))} : std::nullopt;
            dep.value = d.isMember("value") ? parseU256(jAt(d, "value")) : intx::uint256{0};
            dep.gas_limit = test::from_json<int64_t>(jAt(d, "gas"));
            dep.is_system_tx = jAt(d, "is_system_tx").asBool();
            dep.data = test::from_json<bytes>(jAt(t, "data"));
            txs.push_back({.tx = std::move(dep), .signedEnvelope = {}});
        }
        else if (opType == "eip1559" || opType == "setcode")
        {
            state::Transaction tx;
            tx.type = opType == "setcode" ? state::Transaction::Type::set_code :
                                            state::Transaction::Type::eip1559;
            tx.sender = test::from_json<evmc::address>(jAt(t, "sender"));
            tx.to = jAt(t, "to").isNull() ?
                        std::nullopt :
                        std::optional{test::from_json<evmc::address>(jAt(t, "to"))};
            tx.nonce = test::from_json<uint64_t>(jAt(t, "nonce"));
            tx.gas_limit = test::from_json<int64_t>(jAt(t, "gas"));
            tx.max_gas_price = parseU256(jAt(t, "maxFeePerGas"));
            tx.max_priority_gas_price = parseU256(jAt(t, "maxPriorityFeePerGas"));
            tx.value = parseU256(jAt(t, "value"));
            tx.data = test::from_json<bytes>(jAt(t, "data"));
            // EIP-2930 access list (optional; no hits in the legacy 25 vectors, dormant path).
            if (t.isMember("accessList"))
            {
                for (const auto& e : jAt(t, "accessList"))
                {
                    std::vector<evmc::bytes32> keys;
                    for (const auto& k : jAt(e, "storageKeys"))
                        keys.push_back(test::from_json<hash256>(k));
                    tx.access_list.emplace_back(
                        test::from_json<evmc::address>(jAt(e, "address")), std::move(keys));
                }
            }
            tx.chain_id = test::from_json<uint64_t>(jAt(t, "chainId"));
            if (vectorChainId.has_value() && *vectorChainId != tx.chain_id)
            {
                BOOST_ERROR(id << ": inconsistent chainId across txs: " << hexU64(*vectorChainId)
                               << " vs " << hexU64(tx.chain_id));
                return false;
            }
            vectorChainId = tx.chain_id;
            if (opType == "setcode")
            {
                // hasMarked/hasUnmarked/anchorOk: mix-check marked tuples
                // (structurally unrecoverable, _op_signer_unrecoverable=true) and
                // unmarked tuples (existing recovery path); when marked tuples exist
                // there must be >=1 unmarked tuple anchoring the delegation in postState.
                bool hasMarked = false;
                bool hasUnmarked = false;
                bool anchorOk = false;
                for (const auto& a : jAt(t, "_op_authorization_list"))
                {
                    state::Authorization auth;
                    auth.chain_id = parseU256(jAt(a, "chainId"));
                    auth.addr = test::from_json<evmc::address>(jAt(a, "address"));
                    auth.nonce = test::from_json<uint64_t>(jAt(a, "nonce"));
                    auth.r = parseU256(jAt(a, "r"));
                    auth.s = parseU256(jAt(a, "s"));
                    auth.v = parseU256(jAt(a, "yParity"));

                    const bool marked = a.isMember("_op_signer_unrecoverable");
                    if (marked && (!jAt(a, "_op_signer_unrecoverable").isBool() ||
                                      !jAt(a, "_op_signer_unrecoverable").asBool()))
                    {
                        BOOST_ERROR(id << ": _op_signer_unrecoverable must be literal true");
                        continue;
                    }
                    if (marked)
                    {
                        // Reverse-verify with the structural predicate (no bare
                        // ecrecover); signer left empty and tuple passed through as-is —
                        // production OpTransition.cpp:46-135 does real ecrecover and
                        // skips per its predicate (the real differential path).
                        if (!structurallyUnrecoverable(auth))
                            BOOST_ERROR(
                                id << ": marked unrecoverable but structurally recoverable");
                        hasMarked = true;
                    }
                    else
                    {
                        // Fill recovered signer + assert per tuple (evmone silently skips tuples
                        // without a signer).
                        auth.signer = replayRecoverAuthority(auth);
                        if (!auth.signer.has_value())
                            BOOST_ERROR(id << ": authorization signer recovery failed (unmarked "
                                              "tuple)");
                        else
                        {
                            hasUnmarked = true;
                            // Non-empty delegation anchor existence: the authority must
                            // carry 0xef0100||tuple.addr delegation code in the vector
                            // postState (required only when this tx has marked tuples).
                            const auto authAddr = hexAddr(*auth.signer);
                            const auto& post = jAt(blk, "postState");
                            if (post.isMember(authAddr))
                            {
                                const std::string wantCode =
                                    "0xef0100" + hexAddr(auth.addr).substr(2);
                                if (jAt(post, authAddr).get("code", Json::Value("")).asString() ==
                                    wantCode)
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
            auto envelope = test::from_json<bytes>(jAt(t, "_op_raw"));
            txs.push_back({.tx = std::move(tx), .signedEnvelope = std::move(envelope)});
        }
        else if (opType == "legacy")
        {
            // Task 3 F1 legacy arm: type-0 EIP-155 protected tx. Single gasPrice (no
            // maxFeePerGas/maxPriorityFeePerGas); evmone legacy has priority==max==gasPrice.
            state::Transaction tx;
            tx.type = state::Transaction::Type::legacy;
            tx.sender = test::from_json<evmc::address>(jAt(t, "sender"));
            tx.to = jAt(t, "to").isNull() ?
                        std::nullopt :
                        std::optional{test::from_json<evmc::address>(jAt(t, "to"))};
            tx.nonce = test::from_json<uint64_t>(jAt(t, "nonce"));
            tx.gas_limit = test::from_json<int64_t>(jAt(t, "gas"));
            const auto gasPrice = parseU256(jAt(t, "gasPrice"));
            tx.max_gas_price = gasPrice;
            tx.max_priority_gas_price = gasPrice;
            tx.value = parseU256(jAt(t, "value"));
            tx.data = test::from_json<bytes>(jAt(t, "data"));
            tx.chain_id = test::from_json<uint64_t>(jAt(t, "chainId"));
            if (vectorChainId.has_value() && *vectorChainId != tx.chain_id)
            {
                BOOST_ERROR(id << ": inconsistent chainId across txs: " << hexU64(*vectorChainId)
                               << " vs " << hexU64(tx.chain_id));
                return false;
            }
            vectorChainId = tx.chain_id;
            auto envelope = test::from_json<bytes>(jAt(t, "_op_raw"));
            txs.push_back({.tx = std::move(tx), .signedEnvelope = std::move(envelope)});
        }
        else if (opType == "blob")
        {
            // Task 4 blob arm: type-0x3 blob txs are decode-class rejected on OP chains.
            // processOpBlock never reaches raw-tx decode (txs are already OpBlockTx), so
            // reproduce the real decode rejection via decodeOneRawTx, record the message,
            // and let assertRejectThrow assert it directly (consumer-first; review R16
            // consumer:both).
            const auto raw = test::from_json<bytes>(jAt(t, "_op_raw"));
            try
            {
                // decodeOneRawTx lives in bcos::evm::engine::detail and takes
                // bcos::bytes (a vector); raw is evmc::bytes (basic_string), copy byte-by-byte.
                const bcos::bytes rawVec(raw.begin(), raw.end());
                (void)bcos::evm::engine::detail::decodeOneRawTx(
                    rawVec, vectorChainId.value_or(kCorpusChainId));
                BOOST_ERROR(id << ": blob raw envelope must be rejected at decode");
                return false;
            }
            catch (const std::runtime_error& e)
            {
                // Note: must not use catch(std::exception) — libevmone(-fno-rtti)
                // brings in a hidden non-unique typeinfo for std::exception, so typed
                // catch does not reliably bind the runtime_error subtree
                // (see OpSchedulerImpl.h:1083-1104); the runtime_error branch is
                // verified to bind (assertRejectThrow).
                out.decodeRejectMessage = std::string(e.what());
            }
            // Placeholder tx: keeps the post-deposit non-deposit structure (the
            // decodeRejectMessage branch never runs processOpBlock; the placeholder
            // is only for structural completeness).
            state::Transaction placeholder;
            placeholder.type = state::Transaction::Type::blob;
            placeholder.sender = test::from_json<evmc::address>(jAt(t, "sender"));
            placeholder.to = jAt(t, "to").isNull() ?
                                 std::nullopt :
                                 std::optional{test::from_json<evmc::address>(jAt(t, "to"))};
            placeholder.gas_limit = test::from_json<int64_t>(jAt(t, "gas"));
            placeholder.value = parseU256(jAt(t, "value"));
            placeholder.data = test::from_json<bytes>(jAt(t, "data"));
            placeholder.max_gas_price =
                t.isMember("maxFeePerGas") ? parseU256(jAt(t, "maxFeePerGas")) : intx::uint256{};
            placeholder.max_priority_gas_price = t.isMember("maxPriorityFeePerGas") ?
                                                     parseU256(jAt(t, "maxPriorityFeePerGas")) :
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

/// Executes one block: blk env/hardfork/transactions + ts (pre ready or inherited).
/// A nullptr pre skips pre parsing (chain block i>0 pre:null inherits ts after the
/// previous block's applyDiff writes). touchedAddrs/touchedSlots are per-block
/// (block N's .uncovered must not see addresses touched in blocks 0..N-1).
void replaySingleBlockInto(const std::string& id, const JsonValue& blk, evmone::test::TestState& ts,
    const JsonValue* pre, DivergenceLedger& ledger, evmc::VM& vm,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory)
{
    VectorContext ctx{ledger, id};
    BlockContext bc;
    if (!loadBlockContext(id, blk, bc))
        return;
    const auto& cfg = *bc.cfg;
    const bool isJovian = bc.isJovian;

    // pre -> TestState (evmone golden loader; balance/nonce/code required, zero-valued
    // storage slots dropped per trie semantics). A nullptr pre inherits the caller's ts.
    if (pre != nullptr)
        ts = test::from_json<test::TestState>(*pre);

    // Execute: the applyDiff callback both writes back into TestState and accumulates the write set
    // (used by the coverage assertion).
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
        // typed-catch RTTI fallback (see docs/audits/2026-07-12-typed-catch-rtti-investigation.md):
        // libevmone.a (-fno-rtti) ships a hidden non-unique typeinfo copy for
        // std::exception, so after linking all catch(std::exception&) here compare
        // unequal to the libc++ throwing side's base typeinfo and miss (arm64
        // non-unique RTTI bit-mix rule). Diagnosis is not swallowed: name the dynamic
        // type, then finish with the same semantics as the typed branch.
        const auto* excType = abi::__cxa_current_exception_type();
        BOOST_ERROR(id << ": processOpBlock threw block-level error (typed catch "
                       << "bypassed, exception type: " << (excType ? excType->name() : "<unknown>")
                       << ")");
        return;
    }

    // seal: message passer storage = end-of-block (post-finalize) snapshot (OpBlockSeal.h
    // contract).
    const std::map<evmc::bytes32, evmc::bytes32> mpStorage =
        ts.contains(OP_L2_TO_L1_MESSAGE_PASSER) ? ts.at(OP_L2_TO_L1_MESSAGE_PASSER).storage :
                                                  std::map<evmc::bytes32, evmc::bytes32>{};
    const auto seal = sealOpBlock(result, cfg, mpStorage);

    // ── header six fields ────────────────────────────────────────────────────
    const auto& h = jAt(jAt(blk, "_op_expected"), "header");
    ctx.checkField("gasUsed", hexU256(parseU256(jAt(h, "gasUsed"))),
        hexU64(static_cast<uint64_t>(result.gasUsed)));
    ctx.checkField("receiptsRoot", hexHash(test::from_json<hash256>(jAt(h, "receiptsRoot"))),
        hexHash(seal.receiptsRoot));
    // bloom is always compared as 512 hex chars (a zero bloom is an all-zero string, not absent).
    {
        auto wantBloom = jAt(h, "logsBloom").asString();
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
    ctx.checkField("withdrawalsRoot", hexHash(test::from_json<hash256>(jAt(h, "withdrawalsRoot"))),
        hexHash(seal.withdrawalsRoot));
    // ── header.stateRoot (single leg: execution+engine vs op-geth consensus root) ─
    // Timing: the seal-stage ts is already the full post-finalize world state (same
    // anchor as the messagePasserStorage snapshot); later postState comparisons only
    // read ts, never write — safe to build the root here. Engine correctness
    // (evmone mpt_hash) is anchored upstream; not re-proven here. Red failures
    // should be attributed to execution/accounting or pre-alloc completeness first.
    ctx.checkField("stateRoot", hexHash(test::from_json<hash256>(jAt(h, "stateRoot"))),
        hexHash(bcos::evm::stateRootOf(TestStateLedger{ts})));
    // requestsHash: pre-Prague (ecotone/fjord/..., incl. ecotone_upgrade_fjord_activation)
    // vectors do not emit this key (op-geth t8n omitempty; only Prague has EIP-7685
    // requests), so the want side goes through checkOptional by presence.
    ctx.checkOptional("requestsHash",
        h.isMember("requestsHash") ?
            std::optional{hexHash(test::from_json<hash256>(jAt(h, "requestsHash")))} :
            std::nullopt,
        seal.requestsHash.has_value() ? std::optional{hexHash(*seal.requestsHash)} : std::nullopt);
    // blobGasUsed: all six forks emit it in vectors (op-geth headers really carry 0x0,
    // a 4844 leftover field from Ecotone+). Jovian -> value compare ("0x0" is an
    // in-place zero, e.g. jovian_first_block); the other five forks assert the C++
    // side is absent (seal.blobGasUsed semantics = Jovian DA-footprint header field;
    // pre-Isthmus has no such reuse bit, and the vector's 0x0 is informational only).
    {
        const auto wantBlobGas =
            parseU256(jAt(h, "blobGasUsed"));  // required (always emitted on Ecotone+)
        const auto gotBlobGas =
            seal.blobGasUsed.has_value() ? std::optional{hexU64(*seal.blobGasUsed)} : std::nullopt;
        if (isJovian)
            ctx.checkOptional("blobGasUsed", std::optional{hexU256(wantBlobGas)}, gotBlobGas);
        else
            ctx.checkOptional("blobGasUsed", std::nullopt, gotBlobGas);
    }

    // ── receipts ────────────────────────────────────────────────────────────
    const auto& expReceipts = jAt(jAt(blk, "_op_expected"), "receipts");
    if (expReceipts.size() != result.receipts.size())
    {
        BOOST_ERROR(id << ": receipts count mismatch: expected " << expReceipts.size() << " got "
                       << result.receipts.size() << " (no zip-min)");
        return;
    }
    for (size_t i = 0; i < expReceipts.size(); ++i)
    {
        const auto& er = expReceipts[static_cast<Json::ArrayIndex>(i)];
        const std::string p = "receipts[" + std::to_string(i) + "]";

        // Got-side unified view (plan A phase 2 API): FISCO TransactionReceipt::Ptr +
        // parallel txTypes byte (EIP-2718 type). OP fields via opStackMeta(); deposit vs
        // normal tx discriminated by kDepositTxType (equivalent to the old
        // OpDepositReceipt/OpTxReceipt variant discrimination).
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
            // _op_operator_fee presence mirrors op-geth deriveOPStackFields (slot 8
            // scalar/constant not emitted when all-zero); on this side the same rule
            // rides meta.operator_fee_scalar/constant (deriveOpReceiptMeta only fills
            // when non-zero). meta.operator_fee itself is a FISCO extension always
            // filled on Isthmus+ (incl. 0); comparing it directly would report a
            // representation difference as a divergence.
            if (meta &&
                (meta->operator_fee_scalar.has_value() || meta->operator_fee_constant.has_value()))
                gotOperatorFee = hexU256Bcos(meta->operator_fee.value_or(bcos::u256{0}));
            if (meta && meta->da_footprint.has_value())
                gotDaFootprint = hexU64(*meta->da_footprint);
            // Full-field compare across the ecotone/fjord/granite/holocene/isthmus/jovian
            // fieldmap: u256 -> hexU256Bcos, uint64 -> hexU64. got-reads live inside the
            // non-deposit branch (mirrors generator !IsDepositTx emission: deposit receipts
            // carry no fee fields on either side; ungated reads would false-diverge).
            // operator-fee absent pre-Isthmus: ecotone..holocene has_operator_fee=false ->
            // deriveOpReceiptMeta fills no operator_fee* -> all got nullopt; generator does
            // not emit _op_operator_fee pre-Isthmus -> optWant also nullopt -> both-absent
            // pass (no false divergence). l1_gas_used kept unconditionally — even when the
            // vector lacks the key (optWant nullopt), assert FISCO-side presence
            // (Task 4 recomputation; Fjord+ always emits, Ecotone uses bedrockCalldataGasUsed).
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

        ctx.checkField(p + ".type", hexU256(parseU256(jAt(er, "type"))),
            hexU64(static_cast<uint64_t>(result.txTypes[i])));
        ctx.checkField(p + ".status", hexU256(parseU256(jAt(er, "status"))),
            receipt->status() == 0 ? "0x1" : "0x0");
        ctx.checkField(p + ".gasUsed", hexU256(parseU256(jAt(er, "gasUsed"))),
            hexU64(static_cast<uint64_t>(receipt->gasUsed())));
        ctx.checkField(p + ".cumulativeGasUsed", hexU256(parseU256(jAt(er, "cumulativeGasUsed"))),
            std::string{receipt->cumulativeGasUsed()});
        ctx.checkField(p + ".logsCount", std::to_string(jAt(er, "logsCount").asInt64()),
            std::to_string(receipt->logEntries().size()));
        // Receipt output (tx return data): the generator always emits it (empty = "0x");
        // FISCO output() returns raw bytes, normalized to "0x"+lowercase hex by hexBytes.
        // Both-absent/both-present byte-exact compare — wrapper returndata truncation and
        // p256 32-byte-1 are both pinned by it.
        ctx.checkOptional(p + ".output",
            er.isMember("output") ? std::optional{jAt(er, "output").asString()} : std::nullopt,
            std::optional{
                hexBytes(evmc::bytes_view{receipt->output().data(), receipt->output().size()})});

        const auto optWant = [&](const char* key) -> std::optional<std::string> {
            return er.isMember(key) ? std::optional{hexU256(parseU256(jAt(er, key)))} :
                                      std::nullopt;
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

    // ── postState bidirectional (decision record 8) ───────────────────────────
    // Forward: vector per-account per-slot vs replay final state. Zero slots/accounts
    // reduced per trie semantics (0 == absent): the vector emits "candidate accounts
    // absent after the block" as {"balance":"0x0"} — the compare is "all four fields
    // zero + all slots zero", and a missing got-side account is treated as a zero
    // account, so identity => pass. A present-but-empty got account also passes
    // (EIP-161 empty account == absent from trie).
    const auto& post = jAt(blk, "postState");
    std::set<evmc::address> postAddrs;
    static const test::TestAccount kZeroAccount{};
    for (const auto& addrStr : post.getMemberNames())
    {
        const auto& acc = post[addrStr];
        const auto addr = test::from_json<evmc::address>(Json::Value(addrStr));
        postAddrs.insert(addr);
        const auto ap = "postState." + hexAddr(addr);

        const auto it = ts.find(addr);
        const test::TestAccount& got = it != ts.end() ? it->second : kZeroAccount;

        ctx.checkField(
            ap + ".balance", hexU256(parseU256(jAt(acc, "balance"))), hexU256(got.balance));
        ctx.checkField(ap + ".nonce",
            hexU64(acc.isMember("nonce") ? test::from_json<uint64_t>(jAt(acc, "nonce")) : 0),
            hexU64(got.nonce));
        ctx.checkField(ap + ".code",
            acc.isMember("code") ? hexBytes(test::from_json<bytes>(jAt(acc, "code"))) : "0x",
            hexBytes(got.code));

        // Slot union = vector-declared slots ∪ got non-zero slots ∪ replay write-set
        // touched slots (slot dimension of coverage assertion (ii): a touched slot is
        // always in the union and explicitly compared — final non-zero while unlisted
        // by the vector => want=0x0 turns red; final zero while unlisted => 0==absent
        // both-zero pass, i.e. "covered").
        std::map<evmc::bytes32, intx::uint256> wantStorage;
        if (acc.isMember("storage"))
        {
            const auto& storage = acc["storage"];
            for (const auto& slotStr : storage.getMemberNames())
                wantStorage[test::from_json<hash256>(Json::Value(slotStr))] =
                    parseU256(storage[slotStr]);
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
    // Reverse existence: a non-empty account in the replay final state not listed by
    // the vector = DIVERGE (the vector candidate set claims coverage of all written
    // accounts; empty accounts == absent from trie, reduced to pass).
    for (const auto& [addr, acc] : ts)
    {
        if (postAddrs.contains(addr))
            continue;
        const bool storageAllZero = std::ranges::all_of(
            acc.storage, [](const auto& kv) { return evmc::is_zero(kv.second); });
        if (acc.nonce != 0 || acc.balance != 0 || !acc.code.empty() || !storageAllZero)
            ledger.diverge(id, "postState." + hexAddr(addr) + ".exists", kAbsent, "<present>");
    }
    // Coverage assertion (ii) address dimension: an address touched by replay applyDiff
    // but not listed in the vector postState = DIVERGE .uncovered (an account touched
    // then deleted is emitted by the generator as {"balance":"0x0"} and stays in
    // postAddrs — absent means the corpus candidate set missed an account).
    for (const auto& addr : touchedAddrs)
    {
        if (!postAddrs.contains(addr))
            ledger.diverge(
                id, "postState." + hexAddr(addr) + ".uncovered", "<covered>", "<uncovered>");
    }

    // Per-vector comparison count: 0 = FAILURE (prevents a vacuous green).
    if (ctx.comparisons == 0)
        BOOST_ERROR(id << ": zero comparisons executed");
}

// ── Single-vector replay (flat vector: pre at top level; chain vectors call replayChainVector per
// block) ─

void replayVector(const std::string& id, const JsonValue& v, DivergenceLedger& ledger, evmc::VM& vm,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory)
{
    evmone::test::TestState ts;
    replaySingleBlockInto(id, v, ts, &jAt(v, "pre"), ledger, vm, receiptFactory);
}

// ── reject branch ────────────────────────────────────────────────────────────
// Top-level _op_expected.reject present -> invalid vector. The T8n side only
// consumes executor/both (engine direct-connect field-corruption classes are
// consumed by OpNewPayloadRpcE2eTest, Task 2).

bool hasReject(const JsonValue& v)
{
    return v.isMember("_op_expected") && jAt(v, "_op_expected").isMember("reject");
}

std::string rejectConsumer(const JsonValue& v)
{
    // consumer defaults to executor (T8n is an execution-layer replayer; engine-class vectors
    // explicitly write engine).
    return jAt(jAt(jAt(v, "_op_expected"), "reject"), "fisco")
        .get("consumer", Json::Value("executor"))
        .asString();
}

/// reject(executor/both) assertion: processOpBlock must throw std::runtime_error whose
/// what() contains the expected substring. Reuses loadBlockContext loading
/// (env->BlockInfo / ParentOnlyBlockHashes / transactions->OpBlockTx) but skips the
/// success-execution assertion path (no header/receipts/postState compares).
/// The throw side is verified catchable by typed catch: OpSchedulerImplSmokeTest.cpp:161
/// catches processOpBlock's empty-block rejection via BOOST_CHECK_THROW(..., std::runtime_error)
/// and is green (FISCO-side throws use libc++ unique typeinfo, not the libevmone
/// -fno-rtti hidden copy).
void assertRejectThrow(const std::string& id, const JsonValue& v, evmc::VM& vm,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory)
{
    BlockContext bc;
    if (!loadBlockContext(id, v, bc))
        return;
    // decode-class reject (blob): the load section already reproduced the decodeOneRawTx
    // rejection and recorded the message. processOpBlock never reaches that decode (txs
    // are already OpBlockTx); assert the recorded message directly (same string as the engine
    // side).
    if (bc.decodeRejectMessage.has_value())
    {
        const auto expected =
            jAt(jAt(jAt(jAt(v, "_op_expected"), "reject"), "fisco"), "validation_error_contains")
                .asString();
        BOOST_CHECK_MESSAGE(bc.decodeRejectMessage->find(expected) != std::string::npos,
            id << ": decode reject message missing '" << expected
               << "', got: " << *bc.decodeRejectMessage);
        return;
    }
    evmone::test::TestState ts = test::from_json<test::TestState>(jAt(v, "pre"));
    // applyDiff callback matches the replaySingleBlockInto execute section (incl. ts write-back);
    // ts is discarded after reject.
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
        // Warning: only an invalid tx inserted after the first deposit throws "invalid
        //    non-deposit tx"; a deposit-only block does not throw (legal execution) —
        //    the vector must contain an invalid transaction.
        processOpBlock(
            ts, bc.blk, bc.hashes, bc.txs, *bc.cfg, vm, bc.chainId, receiptFactory, apply);
    }
    catch (const std::runtime_error& e)
    {
        const auto expected =
            jAt(jAt(jAt(jAt(v, "_op_expected"), "reject"), "fisco"), "validation_error_contains")
                .asString();
        BOOST_CHECK_MESSAGE(std::string(e.what()).find(expected) != std::string::npos,
            id << ": throw message missing '" << expected << "', got: " << e.what());
        return;
    }
    catch (...)
    {
        // typed-catch RTTI fallback (mechanism in replaySingleBlockInto's typed-catch
        // comment) — name the dynamic type, then finish with typed-branch semantics.
        const auto* excType = abi::__cxa_current_exception_type();
        BOOST_ERROR(id << ": threw non-runtime_error (typed catch bypassed, exception type: "
                       << (excType ? excType->name() : "<unknown>") << ")");
        return;
    }
    BOOST_ERROR(id << ": expected processOpBlock to reject, but it executed");
}

// ── Chain replay ─────────────────────────────────────────────────────────────
// blocks[0] seeds pre; blocks[i>0] with pre:null uses the previous block's
// post-state (ts after applyDiff write-back). chainState is passed by reference
// across blocks (blocks[i>0] skip pre parsing — pre:null would trip from_json's
// is_object assert, hence the nullptr pre pointer). ParentOnlyBlockHashes only
// answers blockNumber-1 (see above), so chain vectors must not contain txs that
// read historical blockhashes (transfer-safe; review R13).

void replayChainVector(const std::string& id, const JsonValue& v, DivergenceLedger& ledger,
    evmc::VM& vm, const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory)
{
    const auto& blocks = jAt(v, "blocks");
    evmone::test::TestState chainState;
    for (std::size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& blk = blocks[static_cast<Json::ArrayIndex>(i)];
        const JsonValue* pre = nullptr;
        if (blk.isMember("pre") && !blk["pre"].isNull())
        {
            chainState = test::from_json<test::TestState>(jAt(blk, "pre"));
            pre = &blk["pre"];
        }
        replaySingleBlockInto(
            id + "[" + std::to_string(i) + "]", blk, chainState, pre, ledger, vm, receiptFactory);
    }
}
}  // namespace
// ── structurallyUnrecoverable predicate boundary unit test (prevents the predicate degenerating to
// always-true/always-false and letting marked tuples escape) ──
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
    BOOST_CHECK(!structurallyUnrecoverable(mk(1, one, kSecpHalfN)));       // s == N/2 is legal
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

    // A) Set equality: dir *.json filename set == manifest.txt list (missing or extra = FAILURE).
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
    // Task 6 exclusions (forced): expectedBlobVersionedHashes / executionRequests cannot
    // be expressed through the GoldenSample loader, so the generator still emits files
    // but keeps them out of the manifest. Set equality exempts these two known
    // unregistered static-face files (suffix match, base-independent).
    const auto isUnregisteredStatic = [](std::string const& n) {
        // "_static_3.json" = 14 chars, "_static_12.json" = 15 chars (suffix match,
        // base-independent)
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

    // Replay only the set intersection (missing/extra already FAILURE'd; don't let set errors
    // cascade into parse crashes).
    for (const auto& name : manifest)
    {
        if (!present.contains(name))
            continue;
        const auto file = vectorsDir / name;
        // parse failure / missing required field -> named ADD_FAILURE, then next file; never
        // silent.
        try
        {
            std::ifstream input(file);
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
            const auto stem = file.stem().string();
            if (id != stem)
                throw std::runtime_error("vector id '" + id + "' != filename stem '" + stem + "'");
            // Warning — order: chain vectors have only blocks at top level, no
            // _op_expected, so the reject check (jAt(v, "_op_expected")) would throw
            // invalid_argument; must test blocks first.
            if (vec->isMember("blocks"))
            {
                replayChainVector(id, *vec, ledger, vm, receiptFactory);
                continue;
            }
            if (hasReject(*vec))
            {
                const auto consumer = rejectConsumer(*vec);
                if (consumer == "engine")
                    continue;  // field-corruption class: OpNewPayloadRpcE2eTest (Task 2) only
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
            // typed-catch RTTI fallback (same mechanism as above) — name the type,
            // same semantics as the typed branch (record FAILURE then next vector file).
            const auto* excType = abi::__cxa_current_exception_type();
            BOOST_ERROR(name << ": exception escaped typed catch (exception type: "
                             << (excType ? excType->name() : "<unknown>") << ")");
        }
    }

    // E) End-of-run ledger: stale exemptions turn red + KNOWN-DIVERGE total into RecordProperty.
    ledger.finish();
}

// ── reject branch: processOpBlock must throw std::runtime_error
//    ("op block: invalid non-deposit tx: ...", OpBlockExecute.cpp:190) for an invalid
//    non-deposit tx; assert throw + what() substring (review HIGH#5). Inline vector
//    (not a corpus file, embedded directly here) — fields must satisfy loader
//    requirements: deposit data at the tx top level (jAt(t, "data")); from=OP_DEPOSITOR
//    to=OP_L1_BLOCK (OpPredeploys.h / OpBlockExecute.cpp); mint/value/gas as hex strings.
//    The second eip1559 tx must carry a real signed EIP-2718 _op_raw envelope
//    (opValidate forces non-empty signedTxEnvelope, OpTransition.cpp:362); gas=0 ->
//    validate_transaction throws INTRINSIC_GAS_TOO_LOW (eth/state/errors.hpp:51) ->
//    processOpBlock throws "op block: invalid non-deposit tx: intrinsic gas too low".
BOOST_AUTO_TEST_CASE(RejectExecutorSurface)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    auto receiptFactory = makeTestReceiptFactory();
    // Warning: DivergenceLedger is default-constructed; load("") would BOOST_ERROR
    // ("DIVERGENCES.md missing"). assertRejectThrow does not consume the ledger
    // (reject vectors have no postState/exemption compares); kept for brief conformance.
    DivergenceLedger ledger;
    // Hand-built vector uses the jsoncpp Reader on a raw string (a Json::Value
    // initializer-list tree would fight jsoncpp's aggregate Value constructors).
    JsonValue v = jParse(R"({
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

// ── blob decode-class reject (Task 4, consumer:both) ─────────────────────────
// The blob arm reproduces the real decode rejection via decodeOneRawTx
// (processOpBlock never reaches raw-tx decode); the message is
// "unsupported tx type byte 0x3" (OpSchedulerImpl.h:893). _op_raw only needs the
// type-0x03 first byte to hit the decode branch (decode checks the type byte before
// parsing any field).
BOOST_AUTO_TEST_CASE(RejectBlobDecode)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    auto receiptFactory = makeTestReceiptFactory();
    DivergenceLedger ledger;
    JsonValue v = jParse(R"({
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

// ── legacy arm loading (Task 3 F1) ───────────────────────────────────────────
// Verifies _op_type "legacy" loads a type=legacy tx and fills gasPrice into both
// max/priority (evmone legacy single-price semantics). _op_raw is structural
// placeholder only (load section does not decode); the full golden path is covered
// by the corpus isthmus/jovian_legacy_transfer vectors (replayed in full after regen).
BOOST_AUTO_TEST_CASE(LegacyArmBuildsLegacyTx)
{
    JsonValue v = jParse(R"({
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
    // legacy single-price: max == priority == gasPrice
    BOOST_CHECK(tx->max_gas_price == tx->max_priority_gas_price);
    BOOST_CHECK(tx->max_gas_price == parseU256(jParse("\"0x4a817c800\"")));
    BOOST_CHECK(bc.txs[1].signedEnvelope.size() > 0);
}

BOOST_AUTO_TEST_SUITE_END()
