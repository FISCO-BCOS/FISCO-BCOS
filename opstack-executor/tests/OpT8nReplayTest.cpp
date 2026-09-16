// OpT8nReplayTest.cpp — OP block-level differential replay gate.
//
// Replays test/opstack/t8n/vectors/*.json (schema v3-block, op-geth
// GenerateChain+InsertChain golden, generator in t8n/generator/) block-by-block
// through the production path (preBlockOpSteps → SchedulerSerialImpl →
// finalizeOpBlockResult), comparing header fields, per-receipt fields, and
// postState (bidirectional + write-set coverage) against _op_expected.
//
// Hard assertion discipline: A) dir *.json set == manifest.txt set; parse
// failure / missing required field = named ADD_FAILURE; per-vector comparison
// count recorded, 0 = FAILURE; the critical differential vectors
// (kCriticalStems below) must stay manifested AND replayed = REQUIRE. B) required fields via jAt();
// hardfork must be exactly regolith|canyon|ecotone|fjord|granite|holocene|isthmus|jovian (no
// default fork); unknown _op_type / receipt count mismatch = FAILURE (no zip-min). D) comparisons
// routed through checkField/checkOptional into DivergenceLedger; checkOptional never gated on
// has_value() (one-sided absence = DIVERGE <absent>); bloom always 512 hex; postState bidirectional
// with zero-slot trie reduction (0 == absent) + write-set coverage. E) exemptions only from
// DIVERGENCES.md ALLOWLIST tuples (a:PENDING-FIX / c:SIGNED-OFF); dangling
// entry= or never-hit exemptions = FAILURE.

#include "StateDiffWriteback.h"
#include "support/RunSharedPath.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/IOServicePool.h>
#include <cxxabi.h>
#include <evmone/evmone.h>
#include <fmt/format.h>
#include <json/json.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpDepositEncode.h>
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/Storage2State.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <array>
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
//
// key is `const char*` (not `const std::string&`) deliberately: a string literal
// argument would materialize a temporary std::string, and GCC-14's -Wdangling-reference
// flags any reference-returning call that receives a class-type temporary — even though
// the returned reference aliases `v`, never `key` (false positive). A `const char*`
// argument creates no temporary, so the warning cannot fire. Callers with a std::string
// key pass .c_str() (jAt(post, authAddr)).
inline const Json::Value& jAt(const Json::Value& v, const char* key)
{
    if (!v.isMember(key))
        throw std::invalid_argument(std::string("missing required field: ") + key);
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
            .storage = {},
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
// Receipts are bcos::protocol::TransactionReceipt::Ptr; OP fields come via
// opStackMeta() (bcos::u256 / uint64).
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
    std::vector<DepositTx> deposits;
    std::vector<bcos::bytes> rawTxBytes;
    uint64_t chainId = kCorpusChainId;
    // decode-class reject (blob): the production execute hook classifies by
    // type byte before any per-tx decode; the load section reproduces that
    // rejection and records the message here for assertRejectThrow.
    std::optional<std::string> decodeRejectMessage;
};

bool loadBlockContext(
    const std::string& id, const JsonValue& blk, BlockContext& out, bool wantPostState)
{
    // _info.hardfork must be exactly regolith|canyon|ecotone|fjord|granite|holocene|
    // isthmus|jovian, anything else = FAILURE. No default fork (the default-Isthmus
    // precedent is a known hole, not ported). isJovian drives the blobGasUsed header
    // gate and the _op_da_footprint expectation — ecotone/fjord/granite/holocene are
    // all false, matching isthmus semantics (has_da_footprint true only on Jovian).
    const auto hardfork = jAt(jAt(blk, "_info"), "hardfork").asString();
    if (hardfork == "regolith")
        out.cfg = &regolithConfig();
    else if (hardfork == "canyon")
        out.cfg = &canyonConfig();
    else if (hardfork == "isthmus")
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
                          "regolith|canyon|ecotone|fjord|granite|holocene|isthmus|jovian, got '"
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

    // Five transaction arms (deposit / eip1559 / accesslist / legacy / setcode). Unknown
    // _op_type = FAILURE.
    auto& deposits = out.deposits;
    auto& rawTxBytes = out.rawTxBytes;
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
            rawTxBytes.push_back(encodeDepositEnvelope(dep));
            deposits.push_back(std::move(dep));
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
                            // wantPostState 门控（与 replaySingleBlockInto 同语义）：
                            // 采样链向量（--poststate boundary，sampledBlocks 存在）的
                            // 未采样块不携带 postState，jAt 会 throw——这里只在采样块上
                            // 做委托锚存在性检查。P2-B ladder 7702（isthmus/jovian 链段
                            // 可达）的前置条件；全量模式（缺省/registered ladder）每块
                            // 都有 postState，行为不变。
                            if (wantPostState)
                            {
                                const auto& post = jAt(blk, "postState");
                                if (post.isMember(authAddr))
                                {
                                    const std::string wantCode =
                                        "0xef0100" + hexAddr(auth.addr).substr(2);
                                    if (jAt(post, authAddr.c_str())
                                            .get("code", Json::Value(""))
                                            .asString() == wantCode)
                                        anchorOk = true;
                                }
                            }
                        }
                    }
                    tx.authorization_list.push_back(std::move(auth));
                }
                if (hasMarked && !hasUnmarked)
                    BOOST_ERROR(id << ": setcode tx with marked tuples must contain >=1 unmarked "
                                      "tuple");
                // anchorOk 只在采样块（postState 携带）上可判定；未采样块跳过
                // （上面的 wantPostState 门控同样语义）。
                if (wantPostState && hasMarked && hasUnmarked && !anchorOk)
                    BOOST_ERROR(id << ": marked-tuple tx has no applied delegation anchor in "
                                      "postState");
            }
            auto envelope = test::from_json<bytes>(jAt(t, "_op_raw"));
            rawTxBytes.emplace_back(envelope.begin(), envelope.end());
        }
        else if (opType == "accesslist")
        {
            // P2-B（ladder tx-type diversity）accesslist 臂：type-0x01 EIP-2930
            // 信封。与其他签名臂相同的 parse-only 处理——执行从 _op_raw 重建
            // 交易（buildFiscoTxFromEnvelope -> web3TypedTxKind()==1 ->
            // Type::access_list），这里只做块内 chainId 一致性闸门并解析
            // accessList 形状（解析结果与 eip1559 臂同样不进入执行路径）。
            state::Transaction tx;
            tx.type = state::Transaction::Type::access_list;
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
            auto envelope = test::from_json<bytes>(jAt(t, "_op_raw"));
            rawTxBytes.emplace_back(envelope.begin(), envelope.end());
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
            rawTxBytes.emplace_back(envelope.begin(), envelope.end());
        }
        else if (opType == "blob")
        {
            // Task 4 blob arm: type-0x3 blob txs are decode-class rejected on OP chains.
            // Reproduce the real rejection via the type-byte classification the execute
            // hook applies (rawTxBytes[i][0] == 0x03 → OpConsensusError), record the
            // message, and let assertRejectThrow assert it directly.
            const auto raw = test::from_json<bytes>(jAt(t, "_op_raw"));
            const bcos::bytes rawVec(raw.begin(), raw.end());
            try
            {
                // Same type-byte classification as OpScheduler::execute / runOpBlockInjection:
                // blob (0x03) is not in {0x01, 0x02, 0x04} and not a legacy RLP list (>= 0xc0).
                if (rawVec.empty())
                    throw bcos::evm::OpConsensusError("op block: empty envelope");
                constexpr uint8_t kRlpListBase = 0xc0;
                const auto typeByte = rawVec[0];
                if (typeByte < kRlpListBase && typeByte != 0x01 && typeByte != 0x02 &&
                    typeByte != 0x04)
                    throw bcos::evm::OpConsensusError(
                        fmt::format("op block: unsupported tx type byte 0x{:02x}",
                            static_cast<unsigned>(typeByte)));
                BOOST_ERROR(
                    id << ": blob raw envelope must be rejected by type-byte classification");
                return false;
            }
            catch (const std::runtime_error& e)
            {
                // Note: must not use catch(std::exception) — libevmone(-fno-rtti)
                // brings in a hidden non-unique typeinfo for std::exception, so typed
                // catch does not reliably bind the runtime_error subtree
                // (see OpSchedulerSeam.h:1083-1104); the runtime_error branch is
                // verified to bind (assertRejectThrow). OpConsensusError is a FISCO-side
                // runtime_error subclass with libc++ unique typeinfo — it binds.
                out.decodeRejectMessage = std::string(e.what());
            }
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

bcos::Address evmcToAddress(const evmc::address& a)
{
    return bcos::Address(bcos::bytesConstRef(a.bytes, sizeof(a.bytes)));
}

bcos::h256 evmcToH256(const evmc::bytes32& h)
{
    return bcos::h256(bcos::bytesConstRef(h.bytes, sizeof(h.bytes)));
}

void seedStorageFromTestState(opstack_test::MutableStorage& storage, const test::TestState& ts)
{
    evmone::state::StateDiff diff;
    diff.modified_accounts.reserve(ts.size());
    for (const auto& [addr, account] : ts)
    {
        evmone::state::StateDiff::Entry entry;
        entry.addr = addr;
        entry.nonce = account.nonce;
        entry.balance = account.balance;
        if (!account.code.empty())
            entry.code = account.code;
        for (const auto& [k, val] : account.storage)
        {
            if (!evmc::is_zero(val))
                entry.modified_storage.emplace_back(k, val);
        }
        diff.modified_accounts.push_back(std::move(entry));
    }
    bcos::evm::evmstate::Storage2State<opstack_test::MutableStorage> bridge(storage);
    bridge.applyDiff(diff, /*seeding=*/true);
    if (bridge.poisoned())
        throw std::runtime_error("seedStorageFromTestState poisoned: " + bridge.firstError());
}

void fillTestStateFromStorage(opstack_test::MutableStorage& storage, test::TestState& ts)
{
    ts.clear();
    bcos::evm::evmstate::Storage2State<opstack_test::MutableStorage> bridge(storage);
    bridge.visitAccounts([&](auto const& acc) {
        test::TestAccount account;
        account.nonce = acc.nonce;
        account.balance = acc.balance;
        account.code = acc.code();
        account.storage = acc.storage;
        ts[acc.addr] = std::move(account);
        return true;
    });
    if (bridge.poisoned())
        throw std::runtime_error("fillTestStateFromStorage poisoned: " + bridge.firstError());
}

void markTouched(const test::TestState& before, const test::TestState& after,
    std::set<evmc::address>& touchedAddrs,
    std::map<evmc::address, std::set<evmc::bytes32>>& touchedSlots)
{
    auto consider = [&](const evmc::address& addr) {
        const auto bIt = before.find(addr);
        const auto aIt = after.find(addr);
        const test::TestAccount* b = bIt != before.end() ? &bIt->second : nullptr;
        const test::TestAccount* a = aIt != after.end() ? &aIt->second : nullptr;
        if (b == nullptr && a == nullptr)
            return;
        if (b == nullptr || a == nullptr || b->nonce != a->nonce || b->balance != a->balance ||
            b->code != a->code || b->storage != a->storage)
        {
            touchedAddrs.insert(addr);
            std::set<evmc::bytes32> slots;
            if (b != nullptr)
            {
                for (const auto& [k, _] : b->storage)
                    slots.insert(k);
            }
            if (a != nullptr)
            {
                for (const auto& [k, _] : a->storage)
                    slots.insert(k);
            }
            if (!slots.empty())
                touchedSlots[addr] = std::move(slots);
        }
    };
    for (const auto& [addr, _] : before)
        consider(addr);
    for (const auto& [addr, _] : after)
        consider(addr);
}

// ── Per-fork receipt meta field-set gate (WI-17 / Task B3) ─────────────────
// The RPC layer copies receipt meta by optional-presence only, fork-blind
// (bcos-rpc/web3jsonrpc/model/ReceiptResponse.cpp:100-140), so the per-fork
// field SHAPE can only be pinned here, on the layer that produces the meta.
//
// Step 1b reconciliation (actualMetaFields below vs the external oracle
// t8n/vectors/OP_RECEIPT_FIELDMAP.md, op-geth pin e8800cff), per fork:
//   - Deposit receipts are the "early-return" shape (FIELDMAP §4.1, op-geth
//     receipt_opstack.go:36-38): only deposit_nonce (+ deposit_receipt_version
//     from Canyon on); the FISCO side is isomorphic via runDeposit
//     (OpTransition.cpp:637-641), which derives nothing from opTransition —
//     no case to file. Regolith deposit receipts carry NO version (deposits
//     spec: consensus RLP omits it); registered divergence:
//     vectors/DIVERGENCES.md:255.
//   - Regolith/Canyon (Bedrock model): {l1_gas_price, l1_gas_used, l1_fee,
//     l1_fee_scalar} — matches regolith/canyon_transfer_basic receipt[1]
//     goldens; the blob/ecotone scalars stay absent pre-Ecotone (FIELDMAP §2).
//   - Ecotone..Holocene: {l1_gas_price, l1_gas_used, l1_fee, l1_blob_base_fee,
//     l1_base_fee_scalar, l1_blob_base_fee_scalar} — matches
//     ecotone_transfer_basic receipt[1]. Ecotone is pinned PRESENCE-ONLY: the
//     l1_gas_used value fork (FISCO 补算 =1600 vs op-geth
//     bedrockCalldataGasUsed) is gate-invisible — see FIELDMAP §6 归线 A.
//   - Isthmus: Ecotone set + operator_fee (see provenance note) +
//     {operator_fee_scalar, operator_fee_constant} iff the L1-attributes
//     calldata operator scalar/constant is non-zero (op-geth
//     receipt_opstack.go:44, FIELDMAP §5.4; transfer_basic 0/0 → absent,
//     fee_env_observer 5000/7777 → present = FIELDMAP §4.2 anchor).
//   - Jovian: Isthmus set + {da_footprint_gas_scalar, da_footprint} — FIELDMAP
//     §1 #5/#13 gate the DA pair on 非 deposit ∧ Jovian, NOT on the scalar
//     value: the jovian_transfer_basic golden carries _op_da_footprint=0x0 +
//     _op_da_footprint_gas_scalar=0x0 while its calldata DA scalar is 0, and
//     deriveOpReceiptMeta fills both unconditionally under has_da_footprint
//     (OpTransition.cpp:277-281). (The §4.3 da_mix anchor, DA scalar=400, fixes
//     the non-zero arm; §5.4's non-zero rule belongs to the operator pair.)
// Field-name provenance: `_op_operator_fee` (aggregate) is FISCO-derived with
// NO op-geth backing (FIELDMAP §5.2) — do not claim op-geth endorsement for it;
// its meta presence is "always filled on Isthmus+ incl. 0" (deriveOpReceiptMeta),
// a representation delta registered in vectors/DIVERGENCES.md B-3
// ("operator_fee=FISCO 扩展"). `da_footprint` is op-geth-derivable: its carrier
// is Receipt.BlobGasUsed (FIELDMAP §5.3).
//
// Vector gaps (registered corpus gaps, Plan C — not assertion exemptions):
// Karst has no *_deposit_only / *_transfer_basic vector at all; Granite has no
// *_transfer_basic (corpus S4 note "Granite fee 向量": its L1 fee formula
// equals Fjord's). So the (Karst, *) and (Granite, transfer_basic) cells stay
// unexercised.
//
// The two value-dependent flags are DERIVED per replayed vector from the
// L1-attributes deposit calldata (op-node marshalBinaryIsthmus/Jovian layout,
// see OpDepositEncode.h:110-117): operatorFeeScalar [164:168],
// operatorFeeConstant [168:176], Jovian DA scalar [176:178] — never from the
// receipt meta itself (that would be a tautology) and never hardcoded per
// family.
struct MetaExpectation
{
    const OpForkConfig& cfg;
    bool isDeposit;
    // op-geth receipt_opstack.go:44 — operator_fee_scalar/constant are written
    // only when scalar != 0 || constant != 0 (FIELDMAP §5.4). Derived from the
    // vector's L1-attributes calldata, never hardcoded.
    bool operatorFeeEmitted;
    // Jovian DA scalar from the calldata ([176:178]), recorded for diagnostics:
    // the DA meta pair is NOT value-gated (see reconciliation above).
    bool daScalarNonZero;
};

std::set<std::string> expectedMetaFields(const MetaExpectation& in)
{
    std::set<std::string> fields;
    if (in.isDeposit)
    {
        // Deposit: deposit_nonce (Regolith+); deposit_receipt_version from
        // Canyon on. Regolith's missing version is the registered divergence
        // vectors/DIVERGENCES.md:255 (consensus RLP omits it), not a new finding.
        fields.insert("deposit_nonce");
        // Producer mirror (runDeposit, OpTransition.cpp): fork >= Canyon covers the
        // protocol-ordered later forks, surviving a fork inserted below Regolith.
        if (in.cfg.fork >= bcos::evm::opstack::OpFork::Canyon)
            fields.insert("deposit_receipt_version");
        return fields;
    }
    // Non-deposit: the passthrough trio is unconditional (FIELDMAP §5.1:
    // L1GasUsed 恒发射 on every non-deposit receipt; FISCO Task 4 补算, FIXED 非豁免).
    fields.insert({"l1_gas_price", "l1_gas_used", "l1_fee"});
    if (in.cfg.l1_fee_model == L1FeeModel::Bedrock)
        fields.insert("l1_fee_scalar");  // raw Bedrock slot-6 scalar (DIVERGENCES.md S4 row)
    else
        // Steady-state assumption: an Ecotone block with dead L1 slots also takes the
        // Bedrock receipt shape on the executor side (zero-slot fallback,
        // OpTransition.cpp:456-459) and would want l1_fee_scalar here; no gated vector
        // hits that today.
        fields.insert({"l1_blob_base_fee", "l1_base_fee_scalar", "l1_blob_base_fee_scalar"});
    if (in.cfg.has_operator_fee)
    {
        // FISCO-only aggregate (no op-geth field, FIELDMAP §5.2): filled on
        // Isthmus+ regardless of the scalar value (incl. 0) — representation
        // delta registered in vectors/DIVERGENCES.md B-3. NOT gated by
        // operatorFeeEmitted (that gate is op-geth's receipt-face rule).
        fields.insert("operator_fee");
        if (in.operatorFeeEmitted)
            fields.insert({"operator_fee_scalar", "operator_fee_constant"});
    }
    if (in.cfg.has_da_footprint)
    {
        // Jovian-only, not value-gated (FIELDMAP §1 #5/#13; the
        // jovian_transfer_basic golden carries 0x0 with a zero calldata scalar).
        fields.insert({"da_footprint_gas_scalar", "da_footprint"});
    }
    return fields;
}

/// Step 1b reconciliation aid: print a field set as one human-readable line.
std::string joinFields(const std::set<std::string>& fields)
{
    std::string out;
    for (const auto& f : fields)
    {
        if (!out.empty())
            out += ",";
        out += f;
    }
    return out;
}

std::set<std::string> actualMetaFields(const bcos::protocol::TransactionReceipt& receipt)
{
    std::set<std::string> fields;
    const auto meta = receipt.opStackMeta();  // std::optional<OpStackReceiptMeta>, 14 fields
    if (!meta)
        return fields;
    if (meta->l1_gas_price)
        fields.insert("l1_gas_price");
    if (meta->l1_gas_used)
        fields.insert("l1_gas_used");
    if (meta->l1_fee)
        fields.insert("l1_fee");
    if (meta->l1_fee_scalar)
        fields.insert("l1_fee_scalar");
    if (meta->l1_blob_base_fee)
        fields.insert("l1_blob_base_fee");
    if (meta->l1_base_fee_scalar)
        fields.insert("l1_base_fee_scalar");
    if (meta->l1_blob_base_fee_scalar)
        fields.insert("l1_blob_base_fee_scalar");
    if (meta->operator_fee_scalar)
        fields.insert("operator_fee_scalar");
    if (meta->operator_fee_constant)
        fields.insert("operator_fee_constant");
    if (meta->operator_fee)
        fields.insert("operator_fee");
    if (meta->da_footprint_gas_scalar)
        fields.insert("da_footprint_gas_scalar");
    if (meta->da_footprint)
        fields.insert("da_footprint");
    if (meta->deposit_nonce)
        fields.insert("deposit_nonce");
    if (meta->deposit_receipt_version)
        fields.insert("deposit_receipt_version");
    return fields;
}

struct MetaFeeFlags
{
    bool operatorFeeEmitted = false;
    bool daScalarNonZero = false;
};

/// Derive the value-dependent emission flags from the vector's L1-attributes
/// deposit calldata (the first deposit whose data carries the
/// Isthmus/Jovian selector; op-node marshalBinary layout, OpDepositEncode.h).
/// Pre-Isthmus calldata has no operator/DA fields → both false (harmless: the
/// cfg gates close anyway — has_operator_fee is Isthmus+, has_da_footprint
/// Jovian-only).
MetaFeeFlags l1AttributesFeeFlags(const BlockContext& bc)
{
    for (const auto& dep : bc.deposits)
    {
        const auto& data = dep.data;
        if (data.size() < 4)
            continue;
        const bool isthmusShape = std::equal(
            IsthmusL1AttributesSelector.begin(), IsthmusL1AttributesSelector.end(), data.begin());
        const bool jovianShape = std::equal(
            JovianL1AttributesSelector.begin(), JovianL1AttributesSelector.end(), data.begin());
        if (!isthmusShape && !jovianShape)
            continue;
        MetaFeeFlags flags;
        if (data.size() >= IsthmusL1AttributesLen)
        {
            uint32_t opScalar = 0;
            for (std::size_t k = c_l1AttributesOperatorFeeScalarOffset;
                 k < c_l1AttributesOperatorFeeScalarOffset + 4; ++k)
                opScalar = (opScalar << 8) | data[k];
            uint64_t opConstant = 0;
            for (std::size_t k = c_l1AttributesOperatorFeeConstantOffset;
                 k < c_l1AttributesOperatorFeeConstantOffset + 8; ++k)
                opConstant = (opConstant << 8) | data[k];
            // op-geth receipt_opstack.go:44 (FIELDMAP §5.4): emit iff either non-zero.
            flags.operatorFeeEmitted = opScalar != 0 || opConstant != 0;
        }
        if (jovianShape && data.size() >= JovianL1AttributesLen)
        {
            const auto daScalar = static_cast<uint32_t>(
                (data[JovianL1AttributesLen - 2] << 8) | data[JovianL1AttributesLen - 1]);
            flags.daScalarNonZero = daScalar != 0;
        }
        return flags;
    }
    return {};
}

/// Proof the per-fork assertion actually ran (not a 0-cell green).
std::size_t g_metaForkCellsChecked = 0;

/// Executes one block on the production path. A nullptr pre inherits the caller's
/// storage / ts (chain block i>0). touchedAddrs/touchedSlots are per-block.
void replaySingleBlockInto(const std::string& id, const JsonValue& blk,
    opstack_test::MutableStorage& storage, evmone::test::TestState& ts, const JsonValue* pre,
    bool wantPostState, DivergenceLedger& ledger,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    bcos::crypto::Hash::Ptr const& hashImpl, bcos::IOServicePool::Ptr const& ioServicePool)
{
    VectorContext ctx{ledger, id};
    BlockContext bc;
    if (!loadBlockContext(id, blk, bc, /*wantPostState=*/wantPostState))
        return;
    const auto& cfg = *bc.cfg;
    const bool isJovian = bc.isJovian;

    // pre -> TestState + seed the production storage. A nullptr pre inherits both.
    if (pre != nullptr)
    {
        ts = test::from_json<test::TestState>(*pre);
        seedStorageFromTestState(storage, ts);
    }
    const auto before = ts;

    std::vector<bcos::protocol::Transaction::ConstPtr> transactions;
    transactions.reserve(bc.rawTxBytes.size());
    for (auto const& env : bc.rawTxBytes)
    {
        auto tx = opstack_test::buildFiscoTxFromEnvelope(env, hashImpl);
        if (!tx)
        {
            BOOST_ERROR(id << ": opEnvelopeToTars failed for envelope");
            return;
        }
        transactions.push_back(std::move(tx));
    }

    auto header = opstack_test::makeMinimalHeader(bc.blk.number, bc.blk.timestamp * 1000,
        bc.blk.gas_limit, bcos::u256(bc.blk.base_fee), evmcToAddress(bc.blk.coinbase),
        evmcToH256(bc.blk.prev_randao), evmcToH256(bc.blk.parent_beacon_block_root),
        evmcToH256(bc.hashes.parentHash));

    bcos::executor_v1::opstack::OpstackExecutor executor{receiptFactory, hashImpl, cfg};
    bcos::evm::engine::OpExecuteBlockResult executed;
    try
    {
        executed = opstack_test::runSharedPath(storage, *header, bc.rawTxBytes, transactions,
            bc.deposits, cfg, executor, bc.chainId, ioServicePool);
    }
    catch (const std::exception& e)
    {
        BOOST_ERROR(id << ": production path threw block-level error: " << e.what());
        return;
    }
    catch (...)
    {
        const auto* excType = abi::__cxa_current_exception_type();
        BOOST_ERROR(id << ": production path threw block-level error (typed catch "
                       << "bypassed, exception type: " << (excType ? excType->name() : "<unknown>")
                       << ")");
        return;
    }

    fillTestStateFromStorage(storage, ts);
    std::set<evmc::address> touchedAddrs;
    std::map<evmc::address, std::set<evmc::bytes32>> touchedSlots;
    markTouched(before, ts, touchedAddrs, touchedSlots);

    OpBlockResult result;
    result.receipts = executed.receipts;
    result.txTypes.reserve(bc.rawTxBytes.size());
    for (auto const& raw : bc.rawTxBytes)
    {
        if (raw.empty())
        {
            BOOST_ERROR(id << ": empty envelope after successful execute");
            return;
        }
        result.txTypes.push_back(classifyTxType(raw[0]));
    }
    result.gasUsed = static_cast<int64_t>(executed.gasUsed);
    const auto& seal = executed.seal;

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
    // withdrawalsRoot is a header field from Shanghai (EIP-4895) on: Canyon..Holocene
    // carry the empty-trie root, Isthmus+ the MessagePasser storage root. Regolith
    // (London) headers carry NO withdrawalsRoot — the vector omits the key and the
    // FISCO seal must keep the zero hash (field absent), not the empty-trie root.
    if (h.isMember("withdrawalsRoot"))
        ctx.checkField("withdrawalsRoot",
            hexHash(test::from_json<hash256>(jAt(h, "withdrawalsRoot"))),
            hexHash(seal.withdrawalsRoot));
    else
        ctx.checkField("withdrawalsRoot", hexHash(hash256{}), hexHash(seal.withdrawalsRoot));
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
    // blobGasUsed: Ecotone+ vectors always emit it (op-geth headers carry 0x0 for
    // blob-less blocks); Regolith/Canyon (London/Shanghai) headers predate 4844 and
    // the vectors omit the key — both sides must be absent. Jovian -> value compare
    // ("0x0" is an in-place zero, e.g. jovian_first_block); the other Ecotone+ forks
    // assert the C++ side is absent (seal.blobGasUsed semantics = Jovian
    // DA-footprint header field; pre-Isthmus has no such reuse bit).
    if (bc.cfg->fork >= OpFork::Ecotone)
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
    else
    {
        BOOST_REQUIRE(!h.isMember("blobGasUsed"));  // generator omits it pre-Cancun
        ctx.checkOptional("blobGasUsed", std::nullopt, std::nullopt);
    }

    // ── receipts ────────────────────────────────────────────────────────────
    const auto& expReceipts = jAt(jAt(blk, "_op_expected"), "receipts");
    if (expReceipts.size() != result.receipts.size())
    {
        BOOST_ERROR(id << ": receipts count mismatch: expected " << expReceipts.size() << " got "
                       << result.receipts.size() << " (no zip-min)");
        return;
    }
    // ── B3 per-fork meta field-set gate: only the two families whose shapes are
    // pinned by the FIELDMAP anchors (deposit_only × Regolith..Jovian = 8 legs,
    // 1 receipt each; transfer_basic × 7 forks — no Granite — 2 receipts each:
    // the L1-attributes deposit AND the non-deposit transfer, asserted through
    // the same helpers via isDeposit). invalid_*_transfer_basic_* reject vectors
    // never reach this loop (assertRejectThrow path).
    const bool metaGate = id.find("_deposit_only") != std::string::npos ||
                          id.find("_transfer_basic") != std::string::npos;
    const auto feeFlags = metaGate ? l1AttributesFeeFlags(bc) : MetaFeeFlags{};
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
        if (metaGate)
        {
            // B3: bidirectional field-set compare — want is derived from
            // OpForkConfig × the vector's calldata flags (NOT from the meta,
            // which would be a tautology); got is read off the receipt.
            const auto got = actualMetaFields(*receipt);
            const auto want = expectedMetaFields(
                {*bc.cfg, isDeposit, feeFlags.operatorFeeEmitted, feeFlags.daScalarNonZero});
            BOOST_TEST_INFO_SCOPE(id << " receipt[" << i << "] isDeposit=" << isDeposit
                                     << " fork=" << static_cast<int>(bc.cfg->fork)
                                     << " operatorFeeEmitted=" << feeFlags.operatorFeeEmitted
                                     << " daScalarNonZero=" << feeFlags.daScalarNonZero << " want=["
                                     << joinFields(want) << "] got=[" << joinFields(got) << "]");
            BOOST_CHECK_MESSAGE(
                got == want, id << ": receipt meta field set mismatch (both directions checked)");
            ++g_metaForkCellsChecked;
        }
        const auto& meta = receipt->opStackMeta();
        std::optional<std::string> gotDepNonce, gotDepVersion, gotL1Fee, gotOperatorFee,
            gotDaFootprint, gotL1GasPrice, gotL1BlobBaseFee, gotL1GasUsed, gotL1BaseFeeScalar,
            gotL1BlobBaseFeeScalar, gotL1FeeScalar, gotOpFeeScalar, gotOpFeeConstant,
            gotDaFootprintGasScalar;
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
            // Bedrock-era only (pre-Ecotone): FISCO stores the RAW whole-slot scalar. The
            // expected side below reconstructs the same raw value from the vector's
            // upstream-scaled FeeScalar, so this comparison is raw-to-raw and never applies
            // a truncating division to the executor's output.
            if (meta && meta->l1_fee_scalar.has_value())
                gotL1FeeScalar = hexU256Bcos(*meta->l1_fee_scalar);
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
        // Tier-2 Phase B: the stored field is DECIMAL (tars convention, RPC lexical_cast
        // semantics); the generator's golden is a hex quantity — compare by VALUE.
        ctx.checkField(p + ".cumulativeGasUsed", hexU256(parseU256(jAt(er, "cumulativeGasUsed"))),
            hexU256(parseU256(std::string{receipt->cumulativeGasUsed()})));
        ctx.checkField(p + ".logsCount", std::to_string(jAt(er, "logsCount").asInt64()),
            std::to_string(receipt->logEntries().size()));
        // D2（设计 v2 §4.2）：per-entry logs 对拍（address/topics/data）。仅当向量
        // 携带 "logs" 数组时激活——旧向量只有 logsCount，自动跳过。
        // 计数也走 DivergenceLedger（checkField）：BOOST_REQUIRE_EQUAL 会在首错中止
        // 整个测试，与「收集全部分歧再报告」的对拍语义冲突。数量不等时仍遍历
        // min(len) 收集逐项分歧。
        if (er.isMember("logs"))
        {
            const auto& receiptLogs = receipt->logEntries();
            const auto& goldenLogs = jAt(er, "logs");
            ctx.checkField(p + ".logsLen", std::to_string(static_cast<size_t>(goldenLogs.size())),
                std::to_string(receiptLogs.size()));
            const size_t logCount = std::min(
                static_cast<size_t>(receiptLogs.size()), static_cast<size_t>(goldenLogs.size()));
            for (size_t j = 0; j < logCount; ++j)
            {
                const auto& log = receiptLogs[j];
                const auto& el = goldenLogs[static_cast<Json::ArrayIndex>(j)];
                const std::string lp = p + ".logs[" + std::to_string(j) + "]";
                // bcos::byte == uint8_t == unsigned char，evmc::bytes_view 是
                // basic_string_view<unsigned char>，故原始指针可直接构造（与 .output
                // 比较同一 idiom）。
                const auto hexRaw = [](const unsigned char* data, size_t size) {
                    return hexBytes(evmc::bytes_view{data, size});
                };
                const auto addrView = log.address();  // std::string_view, 20 原始字节
                ctx.checkField(lp + ".address", jAt(el, "address").asString(),
                    hexRaw(
                        reinterpret_cast<const unsigned char*>(addrView.data()), addrView.size()));
                ctx.checkField(lp + ".topicsLen",
                    std::to_string(static_cast<size_t>(jAt(el, "topics").size())),
                    std::to_string(log.topics().size()));
                const size_t topicCount = std::min(static_cast<size_t>(log.topics().size()),
                    static_cast<size_t>(jAt(el, "topics").size()));
                for (size_t t = 0; t < topicCount; ++t)
                {
                    const auto& topic = log.topics()[t];  // bcos::h256 (FixedBytes<32>)
                    ctx.checkField(lp + ".topics[" + std::to_string(t) + "]",
                        jAt(el, "topics")[static_cast<Json::ArrayIndex>(t)].asString(),
                        hexRaw(topic.data(), topic.size()));
                }
                ctx.checkField(lp + ".data", jAt(el, "data").asString(),
                    hexRaw(log.data().data(), log.data().size()));
            }
        }
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
        // F5: op-geth's receipt FeeScalar is intToScaledFloat(scalar) = scalar/1e6
        // (core/types/rollup_cost.go:402-406); the corpus pins that SCALED value, while
        // FISCO stores the raw whole-slot scalar. Derive the upstream-implied raw scalar
        // (FeeScalar * 1e6) HERE, on the expected side, and compare raw-to-raw — the old
        // form divided the executor's own output, which is exact only for 1e6 multiples and
        // hid the raw-vs-scaled representation. The generator emits the field only when
        // FeeScalar is an exact integer (big.Exact), i.e. scalar is a multiple of 1e6, so
        // the reconstruction is exact for every corpus scalar.
        std::optional<std::string> wantL1FeeScalar;
        if (er.isMember("_op_l1_fee_scalar"))
            wantL1FeeScalar =
                hexU256(parseU256(jAt(er, "_op_l1_fee_scalar")) * intx::uint256{1'000'000});
        ctx.checkOptional(p + "._op_l1_fee_scalar", wantL1FeeScalar, gotL1FeeScalar);
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
    // Gate (design v2 §4.3): sampled ladder vectors omit postState on unsampled
    // blocks -- skip the whole account-level section there. header + receipts are
    // still compared every block (stateRoot every block at the header compare),
    // so detection power is preserved; only account-level localization is lost.
    if (wantPostState)
    {
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
    }

    // Per-vector comparison count: 0 = FAILURE (prevents a vacuous green).
    if (ctx.comparisons == 0)
        BOOST_ERROR(id << ": zero comparisons executed");
}

// ── Single-vector replay (flat vector: pre at top level; chain vectors call replayChainVector per
// block) ─

void replayVector(const std::string& id, const JsonValue& v, DivergenceLedger& ledger,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    bcos::crypto::Hash::Ptr const& hashImpl, bcos::IOServicePool::Ptr const& ioServicePool)
{
    opstack_test::MutableStorage storage;
    evmone::test::TestState ts;
    replaySingleBlockInto(id, v, storage, ts, &jAt(v, "pre"), /*wantPostState=*/true, ledger,
        receiptFactory, hashImpl, ioServicePool);
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

/// reject(executor/both) assertion: the production path must throw std::runtime_error
/// whose what() contains the expected substring.
void assertRejectThrow(const std::string& id, const JsonValue& v,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    bcos::crypto::Hash::Ptr const& hashImpl, bcos::IOServicePool::Ptr const& ioServicePool)
{
    // setcode_create: CREATE_SET_CODE_TX is only reachable via structured to:null in the
    // evmone mirror; a real signed 0x04 envelope always carries a (possibly zero) to address.
    if (id.find("setcode_create") != std::string::npos)
    {
        return;
    }
    BlockContext bc;
    if (!loadBlockContext(id, v, bc, /*wantPostState=*/true))
        return;
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
    opstack_test::MutableStorage storage;
    seedStorageFromTestState(storage, ts);

    std::vector<bcos::protocol::Transaction::ConstPtr> transactions;
    for (auto const& env : bc.rawTxBytes)
    {
        auto tx = opstack_test::buildFiscoTxFromEnvelope(env, hashImpl);
        if (!tx)
        {
            BOOST_ERROR(id << ": opEnvelopeToTars failed for envelope");
            return;
        }
        transactions.push_back(std::move(tx));
    }
    auto header = opstack_test::makeMinimalHeader(bc.blk.number, bc.blk.timestamp * 1000,
        bc.blk.gas_limit, bcos::u256(bc.blk.base_fee), evmcToAddress(bc.blk.coinbase),
        evmcToH256(bc.blk.prev_randao), evmcToH256(bc.blk.parent_beacon_block_root),
        evmcToH256(bc.hashes.parentHash));
    bcos::executor_v1::opstack::OpstackExecutor executor{receiptFactory, hashImpl, *bc.cfg};
    try
    {
        (void)opstack_test::runSharedPath(storage, *header, bc.rawTxBytes, transactions,
            bc.deposits, *bc.cfg, executor, bc.chainId, ioServicePool);
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
        const auto* excType = abi::__cxa_current_exception_type();
        BOOST_ERROR(id << ": threw non-runtime_error (typed catch bypassed, exception type: "
                       << (excType ? excType->name() : "<unknown>") << ")");
        return;
    }
    BOOST_ERROR(id << ": expected production path to reject, but it executed");
}

// ── Chain replay ─────────────────────────────────────────────────────────────
// blocks[0] seeds pre; blocks[i>0] with pre:null uses the previous block's
// post-state (ts after applyDiff write-back). chainState is passed by reference
// across blocks (blocks[i>0] skip pre parsing — pre:null would trip from_json's
// is_object assert, hence the nullptr pre pointer). ParentOnlyBlockHashes only
// answers blockNumber-1 (see above), so chain vectors must not contain txs that
// read historical blockhashes (transfer-safe; review R13).
// postState sampling (design v2 §4.3): a vector-level "sampledBlocks" that is
// absent means every block is sampled (legacy shape); when present only the
// listed 0-based block indices get the account-level postState compare. Unsampled
// blocks still compare header + receipts per block and stateRoot is compared on
// every header, so detection power is not reduced — only localization is.

void replayChainVector(const std::string& id, const JsonValue& v, DivergenceLedger& ledger,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    bcos::crypto::Hash::Ptr const& hashImpl, bcos::IOServicePool::Ptr const& ioServicePool)
{
    const auto& blocks = jAt(v, "blocks");
    // postState 采样（设计 v2 §4.3）：向量级 "sampledBlocks" 缺省 = 每块都比
    // （旧链向量形状不变）；存在则只比列出的 0-based 块号——未采样块仍逐块比
    // header + receipts，stateRoot 每块都比，故检测能力不降，只少了账户级定位。
    std::set<std::size_t> sampled;
    const bool sampledAll = !v.isMember("sampledBlocks");
    if (!sampledAll)
    {
        const auto& sb = v["sampledBlocks"];
        // 非数组（null/字符串/数字）会让 jsoncpp 的 size() 返回 0，静默关闭整条链的
        // 账户级状态比对而不报错——与「绿但空转」同类，必须显式拒绝。
        BOOST_REQUIRE_MESSAGE(sb.isArray(), "sampledBlocks must be an array");
        for (Json::ArrayIndex k = 0; k < sb.size(); ++k)
        {
            const auto& e = sb[k];
            BOOST_REQUIRE_MESSAGE(e.isInt64() || e.isUInt64(),
                "sampledBlocks[" + std::to_string(k) + "] must be an integer");
            const auto idx = e.asInt64();
            BOOST_REQUIRE_MESSAGE(idx >= 0 && static_cast<std::size_t>(idx) < blocks.size(),
                "sampledBlocks[" + std::to_string(k) + "] = " + std::to_string(idx) +
                    " out of range [0, " + std::to_string(blocks.size()) + ")");
            sampled.insert(static_cast<std::size_t>(idx));
        }
    }
    opstack_test::MutableStorage storage;
    evmone::test::TestState chainState;
    for (std::size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& blk = blocks[static_cast<Json::ArrayIndex>(i)];
        const JsonValue* pre = nullptr;
        if (blk.isMember("pre") && !blk["pre"].isNull())
            pre = &blk["pre"];
        const bool wantPostState = sampledAll || sampled.contains(i);
        replaySingleBlockInto(id + "[" + std::to_string(i) + "]", blk, storage, chainState, pre,
            wantPostState, ledger, receiptFactory, hashImpl, ioServicePool);
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
    // Order-independence: the B3 proof-of-run counter is per-run of this case,
    // not whatever a previously executed test case left behind.
    g_metaForkCellsChecked = 0;
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
    // WI-E13: the former forced exclusion is gone. expectedBlobVersionedHashes /
    // executionRequests (§4c static items 3/12) are now expressible — makeInvalidParamsJson
    // passes both _op_payload members through to engine_newPayloadV4 params[1]/params[3] —
    // so the generator registers them and the dir set must equal the manifest exactly.
    for (const auto& name : present)
    {
        if (!manifest.contains(name))
            BOOST_ERROR("unmanifested vector file present: " << name);
    }

    auto ledger = DivergenceLedger::load(vectorsDir / "DIVERGENCES.md");
    auto receiptFactory = makeTestReceiptFactory();
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto ioServicePool = std::make_shared<bcos::IOServicePool>(1);

    // Stems this suite actually dispatched to a replay path (chain replay /
    // reject replay / full vector replay). The engine-consumer reject class is
    // owned by OpNewPayloadRpcE2eTest and is deliberately NOT registered here —
    // registration means "this suite executed the vector", which is what the
    // critical-vector guard below asserts on.
    std::set<std::string> replayedStems;

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
                replayedStems.insert(stem);
                replayChainVector(id, *vec, ledger, receiptFactory, hashImpl, ioServicePool);
                continue;
            }
            if (hasReject(*vec))
            {
                const auto consumer = rejectConsumer(*vec);
                if (consumer == "engine")
                    continue;  // field-corruption class: OpNewPayloadRpcE2eTest only
                replayedStems.insert(stem);
                assertRejectThrow(id, *vec, receiptFactory, hashImpl, ioServicePool);
                continue;
            }
            replayedStems.insert(stem);
            replayVector(id, *vec, ledger, receiptFactory, hashImpl, ioServicePool);
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

    // ── Critical-vector guard (carrier integrity, same hard-fail discipline as
    // jAt): these two stems are the suite's core differential assets — the
    // 8-fork synthetic ladder baseline (1000 blocks, regolith→jovian) and the
    // real-derivation devnet snapshot (chainexport; the DIVERGENCES.md P3-1b
    // create-output exemptions bind to this stem). Removing either from
    // manifest.txt must fail loudly here, never silently shrink the gate.
    // Single source of truth: the stem *strings* are derived by the Go side
    // (generator prints LADDER-STEM into manifest.txt; chainexport derives
    // digest8 — see manifest.txt stem-rule comments); THIS list is the only
    // place the "critical" designation itself is pinned, and nothing else
    // (no run.sh / README) duplicates it.
    static constexpr std::array<const char*, 2> kCriticalStems{
        "ladder_1000_69292b10", "devnet_1875-2274_c38db356"};
    for (const auto* stem : kCriticalStems)
    {
        BOOST_REQUIRE_MESSAGE(
            replayedStems.contains(stem), "critical vector missing from replay: " << stem);
    }

    // 22 = 8 *_deposit_only (1 receipt each) + 7 *_transfer_basic (2 receipts each); bump
    // deliberately when the corpus grows (Karst/Granite gaps → Plan C).
    BOOST_CHECK_MESSAGE(g_metaForkCellsChecked == 22,
        "expected 22 per-fork meta cells, got " << g_metaForkCellsChecked);

    // E) End-of-run ledger: stale exemptions turn red + KNOWN-DIVERGE total into RecordProperty.
    ledger.finish();
}

// ── reject branch: production path must throw std::runtime_error whose what()
//    contains the validate failure. Inline vector (not a corpus file). The
//    `_op_raw` envelope is a signed 21000-gas transfer from an unfunded sender
//    → opValidate rejects with insufficient funds. JSON `gas` is not consulted
//    (envelopes are authoritative on the production path).
BOOST_AUTO_TEST_CASE(RejectExecutorSurface)
{
    auto receiptFactory = makeTestReceiptFactory();
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto ioServicePool = std::make_shared<bcos::IOServicePool>(1);
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
                "op_geth": "insufficient funds",
                "fisco": {
                    "consumer": "executor", "classification": "INVALID",
                    "latest_valid_hash": "parent",
                    "validation_error_contains": "insufficient funds"
                }
            }
        }
    })");
    assertRejectThrow("reject_executor_intrinsic", v, receiptFactory, hashImpl, ioServicePool);
}

// ── blob decode-class reject (consumer:both) ─────────────────────────
// The blob arm reproduces the real rejection via the type-byte classification;
// the message contains "unsupported tx type byte 0x03" (OpScheduler execute hook).
// _op_raw only needs the type-0x03 first byte to hit the classification branch (it checks the
// type byte before parsing any field).
BOOST_AUTO_TEST_CASE(RejectBlobDecode)
{
    auto receiptFactory = makeTestReceiptFactory();
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto ioServicePool = std::make_shared<bcos::IOServicePool>(1);
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
                    "validation_error_contains": "unsupported tx type byte 0x03"
                }
            }
        }
    })");
    assertRejectThrow("reject_blob_decode", v, receiptFactory, hashImpl, ioServicePool);
}

// ── legacy arm loading ───────────────────────────────────────────
// Verifies _op_type "legacy" is accepted by the loader and recorded as a raw
// envelope (execution classifies by type byte). Full golden path is the corpus
// isthmus/jovian_legacy_transfer vectors.
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
    BOOST_REQUIRE_MESSAGE(loadBlockContext("legacy_arm_load", v, bc, /*wantPostState=*/true),
        "legacy vector must load");
    BOOST_REQUIRE_EQUAL(bc.rawTxBytes.size(), 2u);
    BOOST_REQUIRE(!bc.rawTxBytes[0].empty());
    BOOST_REQUIRE(!bc.rawTxBytes[1].empty());
    BOOST_CHECK_EQUAL(bc.rawTxBytes[0][0], static_cast<uint8_t>(0x7e));
    BOOST_CHECK_EQUAL(bc.rawTxBytes[1][0], static_cast<uint8_t>(0x01));
    BOOST_CHECK_EQUAL(bc.deposits.size(), 1u);
    BOOST_CHECK_EQUAL(bc.chainId, uint64_t{0x2105});
}

// ── accesslist arm loading (P2-B ladder tx-type diversity) ───────
// Verifies _op_type "accesslist" (type-0x01 EIP-2930) is accepted by the
// loader, parses the accessList shape, and records the raw envelope. Full
// execution path is exercised by the regenerated ladder vector's probe blocks.
BOOST_AUTO_TEST_CASE(AccessListArmBuildsAccessListTx)
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
                    "_op_type": "accesslist",
                    "_op_raw": "0x01deadbeef",
                    "chainId": "0x2105", "nonce": "0x0",
                    "to": "0xb0b0000000000000000000000000000000000001",
                    "gas": "0x14930", "gasPrice": "0x4a817c800",
                    "value": "0x0", "data": "0x",
                    "accessList": [
                        {"address": "0xb0b0000000000000000000000000000000000001", "storageKeys": []},
                        {"address": "0xc0de00000000000000000000000000000000000b", "storageKeys": ["0x0000000000000000000000000000000000000000000000000000000000000000"]}
                    ],
                    "sender": "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"
                }
            ]
        }
    })");
    BlockContext bc;
    BOOST_REQUIRE_MESSAGE(loadBlockContext("accesslist_arm_load", v, bc, /*wantPostState=*/true),
        "accesslist vector must load");
    BOOST_REQUIRE_EQUAL(bc.rawTxBytes.size(), 1u);
    BOOST_CHECK_EQUAL(bc.rawTxBytes[0][0], static_cast<uint8_t>(0x01));
    BOOST_CHECK_EQUAL(bc.chainId, uint64_t{0x2105});
}

// Ported from op-alignment (audit O3): the persisted golden transactionsRoot must equal
// computeOpTxRoot over the golden's rawTransactions (raw EIP-2718 envelopes incl. the 0x7E
// deposit). Without this cross-check the golden txsRoot is never compared against FISCO's own
// derivation. Pre-Isthmus goldens (ecotone/fjord/granite) and Isthmus+ are both covered.
BOOST_AUTO_TEST_CASE(GoldenTransactionsRootMatches)
{
    const fs::path goldenDir = OP_T8N_GOLDEN_ENGINE_DIR;
    BOOST_REQUIRE_MESSAGE(fs::is_directory(goldenDir), goldenDir);
    int checked = 0;
    for (const auto& entry : fs::directory_iterator(goldenDir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        Json::Value j;
        {
            std::ifstream in(entry.path());
            in >> j;
        }
        if (!j.isMember("rawTransactions") || !j.isMember("transactionsRoot"))
            continue;
        std::vector<bcos::bytes> raws;
        for (auto const& rt : j["rawTransactions"])
            raws.push_back(bcos::fromHexWithPrefix(rt.asString()));
        auto root = bcos::evm::engine::computeOpTxRoot(raws);
        auto golden = bcos::h256(j["transactionsRoot"].asString());
        BOOST_CHECK_MESSAGE(root == golden,
            entry.path().filename() << ": txsRoot mismatch computed=" << root.hexPrefixed()
                                    << " golden=" << j["transactionsRoot"].asString());
        ++checked;
    }
    BOOST_CHECK_MESSAGE(checked >= 16, "expected >=16 golden txsRoot checks, got " << checked);
}

BOOST_AUTO_TEST_SUITE_END()
