// OpT8nReplayTest.cpp — M-B3+M6 Task 3：OP 块级差分回放 gate。
//
// 消费 test/opstack/t8n/vectors/*.json（schema v3-block，op-geth
// GenerateChain+InsertChain 金标准，生成器见 t8n/generator/），整块回放
// processOpBlock → sealOpBlock，与 _op_expected（header 六字段 + 逐 receipt）
// 和 postState（决策记录 8：双向 + applyDiff 写集覆盖）比对。
//
// 硬断言纪律（plan rev.2 红队 1/2/7 强制）：
//   A) 目录 *.json 文件名集合 == manifest.txt 集合（缺/多均 FAILURE）；
//      JSON parse 失败/必填字段缺失 = ADD_FAILURE 点名文件（无静默 continue）；
//      每向量比对计数 RecordProperty，计数 0 = FAILURE。
//   B) 必填一律 j.at()；_info.hardfork 仅精确 "isthmus"|"jovian"（禁默认档）；
//      未知 _op_type = FAILURE；receipts 数不等 = FAILURE（禁 zip-min）。
//   D) 比对全经 checkField/checkOptional 汇入 DivergenceLedger；checkOptional
//      绝不 has_value() 门控（单侧缺席即 DIVERGE <absent>）；bloom 恒 512 hex；
//      postState 双向 + 零槽 trie 规约（0 ≡ 缺席）+ 写集覆盖断言。
//   E) 豁免唯一来源 = DIVERGENCES.md ALLOWLIST 四元组（仅 a:PENDING-FIX /
//      c:SIGNED-OFF）；entry= 悬空 = FAILURE；本轮未命中的豁免条目 = FAILURE。

#include <bcos-evm/adapter/StateDiffWriteback.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/opstack/OpBlockExecute.h>
#include <bcos-evm/opstack/OpBlockSeal.h>
#include <bcos-evm/opstack/OpDepositTx.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <cxxabi.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/utils/rlp.hpp>
#include <bcos-evm/eth/utils/statetest.hpp>
#include <bcos-evm/eth/utils/test_state.hpp>
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
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace bcos::evm::opstack;
using namespace evmone;

namespace
{
// ── 规范化打印（DIVERGE/ALLOWLIST want/got 的唯一书写形式）───────────────────
// 数值：0x 前缀小写最简 hex（与生成器 hexutil.EncodeUint64/EncodeBig 同形）。
// 哈希/地址：0x 定长小写。缺席端统一 "<absent>"。

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

// bytes32 槽键/槽值以数值最简 hex 书写（trie 语义下 0 ≡ 缺席，规范化后比对）。
std::string hexSlot(const evmc::bytes32& b)
{
    return hexU256(intx::be::load<intx::uint256>(b));
}

intx::uint256 parseU256(const Json& j)
{
    return intx::from_string<intx::uint256>(j.get<std::string>());
}

// ── DivergenceLedger（brief 块 E）────────────────────────────────────────────

struct AllowEntry
{
    std::string vectorId, field, entryId, attribution, status, want, got;
    bool exempt = false;
    int hits = 0;
};

class DivergenceLedger
{
public:
    // 缺台账文件 = FAILURE（它是本 gate 的交付物之一，绝不许"缺文件 ≡ 全豁免/全空"歧义）。
    static DivergenceLedger load(const fs::path& path)
    {
        DivergenceLedger ledger;
        std::ifstream input(path);
        if (!input.is_open())
        {
            ADD_FAILURE() << "DIVERGENCES.md missing: " << path;
            return ledger;
        }
        // 与 DIVERGENCES.md「机器格式」节逐字对应。
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
        // entry= 悬空（无匹配 "## <ENTRY-ID>" 标题）= FAILURE：ALLOWLIST 行必须
        // 挂在一个真实的 FINDING/条目章节下，孤行即无证据。
        for (const auto& e : ledger.m_entries)
        {
            if (!headings.contains(e.entryId))
                ADD_FAILURE() << "DIVERGENCES.md ALLOWLIST entry=" << e.entryId
                              << " (vectorId=" << e.vectorId << " field=" << e.field
                              << ") has no matching '## " << e.entryId << "' heading";
        }
        return ledger;
    }

    // 分歧上报唯一入口：四元组全匹配且状态豁免 → KNOWN-DIVERGE（stdout + 计数）；
    // 否则 ADD_FAILURE 翻红。四元组匹配防止同字段的新回归借旧豁免溜绿。
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
        ADD_FAILURE() << "DIVERGE " << vectorId << " " << field << " want=" << want
                      << " got=" << got;
    }

    // 本轮从未命中的豁免条目 = FAILURE（过期豁免即红——修复落地/向量重生成后必须清账）。
    void finish() const
    {
        for (const auto& e : m_entries)
        {
            if (e.exempt && e.hits == 0)
                ADD_FAILURE() << "stale ALLOWLIST exemption never hit this run: entry=" << e.entryId
                              << " vectorId=" << e.vectorId << " field=" << e.field
                              << " want=" << e.want << " got=" << e.got;
        }
        ::testing::Test::RecordProperty("known_diverges", m_knownCount);
    }

private:
    std::vector<AllowEntry> m_entries;
    int m_knownCount = 0;
};

// ── 单向量比较上下文（比对计数 + 字段前缀）──────────────────────────────────

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

    // checkOptional 语义（brief 块 D）：期望在场+实测缺席 → got=<absent>；
    // 期望缺席+实测在场 → want=<absent>；双缺席 pass。禁止 has_value() 门控后才比。
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

// ── BlockHashes：仅对 number−1 返回 env.parentHash ──────────────────────────
// （EIP-2935 系统调用在块 1 存的就是 genesis hash = env.parentHash；其余高度
//  本语料决不查询，返回零值即可暴露任何越界查询——不静默给出伪 hash 链。）

struct ParentOnlyBlockHashes final : state::BlockHashes
{
    int64_t blockNumber = 0;
    hash256 parentHash{};

    evmc::bytes32 get_block_hash(int64_t block_number) const noexcept override
    {
        return block_number == blockNumber - 1 ? parentHash : evmc::bytes32{};
    }
};

// ── EIP-7702 authority 恢复 ──────────────────────────────────────────────────
// 模块内 recoverAuthority（OpTransition.cpp:34-45）在匿名 namespace 未导出；
// 按 brief 以导出件最小复现（同公式 keccak256(0x05 || rlp([chain_id,address,nonce]))
// + evmmax secp256k1 ecrecover）。构建后断言 signer.has_value()——evmone/模块
// transition 对未恢复 signer 的 tuple 是静默 skip（plan 风险 9），语料签名必须可恢复。

// ── structurallyUnrecoverable：secp256k1 结构合法性谓词（EIP-2/EIP-7702 malleability
//    边界；不做 ecrecover，仅判定 r/s/v 数值是否落在可恢复域外）───────────────

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

// ── manifest.txt（'#' 注释与空行外，每行一个必须存在的向量文件名）────────────

std::set<std::string> loadManifest(const fs::path& path)
{
    std::set<std::string> names;
    std::ifstream input(path);
    if (!input.is_open())
    {
        ADD_FAILURE() << "manifest.txt missing: " << path;
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

// ── 语料 chain id ────────────────────────────────────────────────────────────
// 生成器 buildChainConfig（generator/main.go）把全部 25 案钉在 8453（0x2105）。
// 有普通 tx 的向量以 tx.chainId 为准（并断言全块一致）；deposit-only 向量无
// chainId 载体，用该语料常量（不是"默认档"——是生成器同一常量的镜像，若语料
// 换链 id，普通 tx 向量会先在一致性断言上翻红）。

constexpr uint64_t kCorpusChainId = 0x2105;

// ── 单向量回放 ───────────────────────────────────────────────────────────────

void replayVector(const std::string& id, const Json& v, DivergenceLedger& ledger, evmc::VM& vm)
{
    VectorContext ctx{ledger, id};

    // _info.hardfork：仅精确 "isthmus"|"jovian"，其他值 = FAILURE。禁止默认档
    // （M-T 先例的默认 Isthmus 是已知洞，不移植）。
    const auto hardfork = v.at("_info").at("hardfork").get<std::string>();
    const OpForkConfig* cfg = nullptr;
    if (hardfork == "isthmus")
        cfg = &isthmusConfig();
    else if (hardfork == "jovian")
        cfg = &jovianConfig();
    else
    {
        ADD_FAILURE() << id << ": _info.hardfork must be exactly isthmus|jovian, got '" << hardfork
                      << "' (no default fork)";
        return;
    }
    const bool jovian = (hardfork == "jovian");

    // env（8 字段全必填）→ BlockInfo 手建。
    const auto& env = v.at("env");
    state::BlockInfo blk;
    blk.number = test::from_json<int64_t>(env.at("currentNumber"));
    blk.timestamp = test::from_json<int64_t>(env.at("currentTimestamp"));
    blk.gas_limit = test::from_json<int64_t>(env.at("currentGasLimit"));
    blk.base_fee = test::from_json<uint64_t>(env.at("currentBaseFee"));
    blk.coinbase = test::from_json<evmc::address>(env.at("currentCoinbase"));
    blk.prev_randao = test::from_json<hash256>(env.at("currentRandom"));
    blk.parent_beacon_block_root = test::from_json<hash256>(env.at("parentBeaconBlockRoot"));

    ParentOnlyBlockHashes hashes;
    hashes.blockNumber = blk.number;
    hashes.parentHash = test::from_json<hash256>(env.at("parentHash"));

    // pre → TestState（evmone 金 loader；账户四字段 balance/nonce/code 必填，
    // storage 零值槽由 loader 按 trie 语义剔除）。
    test::TestState ts = test::from_json<test::TestState>(v.at("pre"));

    // 交易三臂构建（deposit / eip1559 / setcode）。未知 _op_type = FAILURE。
    std::vector<OpBlockTx> txs;
    std::optional<uint64_t> vectorChainId;
    for (const auto& t : v.at("block").at("transactions"))
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
                ADD_FAILURE() << id
                              << ": inconsistent chainId across txs: " << hexU64(*vectorChainId)
                              << " vs " << hexU64(tx.chain_id);
                return;
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
                        ADD_FAILURE() << id << ": _op_signer_unrecoverable must be literal true";
                        continue;
                    }
                    if (marked)
                    {
                        // 逆向验证（结构谓词，禁裸 ecrecover）；signer 留空，元组原样入列——
                        // 生产 OpTransition.cpp:46-135 真 ecrecover
                        // 并按其谓词跳过（真实差分路径）。
                        if (!structurallyUnrecoverable(auth))
                            ADD_FAILURE()
                                << id << ": marked unrecoverable but structurally recoverable";
                        hasMarked = true;
                    }
                    else
                    {
                        // signer 恢复填入 + 逐 tuple 断言（evmone 对未设 signer 静默跳过）。
                        auth.signer = replayRecoverAuthority(auth);
                        if (!auth.signer.has_value())
                            ADD_FAILURE() << id
                                          << ": authorization signer recovery failed (unmarked "
                                             "tuple)";
                        else
                        {
                            hasUnmarked = true;
                            // 非空洞·委托锚存在性（spec rev.3 ③）：该 authority 在向量 postState
                            // 须携 0xef0100‖tuple.addr 委托码（仅当本 tx 含标记元组时才要求，
                            // 见下方 (4)）。
                            const auto authAddr = hexAddr(*auth.signer);
                            const auto& post = v.at("postState");
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
                    ADD_FAILURE() << id
                                  << ": setcode tx with marked tuples must contain >=1 unmarked "
                                     "tuple";
                if (hasMarked && hasUnmarked && !anchorOk)
                    ADD_FAILURE() << id
                                  << ": marked-tuple tx has no applied delegation anchor in "
                                     "postState";
            }
            auto envelope = test::from_json<bytes>(t.at("_op_raw"));
            txs.push_back({.tx = std::move(tx), .signedEnvelope = std::move(envelope)});
        }
        else
        {
            ADD_FAILURE() << id << ": unknown _op_type '" << opType << "'";
            return;
        }
    }
    const uint64_t chainId = vectorChainId.value_or(kCorpusChainId);

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
        result = processOpBlock(ts, blk, hashes, txs, *cfg, vm, chainId, apply);
    }
    catch (const std::exception& e)
    {
        ADD_FAILURE() << id << ": processOpBlock threw block-level error: " << e.what();
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
        ADD_FAILURE() << id << ": processOpBlock threw block-level error (typed catch "
                      << "bypassed, exception type: " << (excType ? excType->name() : "<unknown>")
                      << ")";
        return;
    }

    // seal：message passer storage = 块尾（finalize 后）快照（OpBlockSeal.h 契约）。
    const std::map<evmc::bytes32, evmc::bytes32> mpStorage =
        ts.contains(OP_L2_TO_L1_MESSAGE_PASSER) ? ts.at(OP_L2_TO_L1_MESSAGE_PASSER).storage :
                                                  std::map<evmc::bytes32, evmc::bytes32>{};
    const auto seal = sealOpBlock(result, *cfg, mpStorage);

    // ── header 六字段 ────────────────────────────────────────────────────────
    const auto& h = v.at("_op_expected").at("header");
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
            ADD_FAILURE() << id << ": header.logsBloom must be 0x + 512 hex chars, got "
                          << wantBloom.size() << " chars";
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
        hexHash(bcos::evm::stateRootOf(ts)));
    // requestsHash：两 fork 恒必填的值比对（seal 侧 optional 经 checkOptional，无门控）。
    ctx.checkOptional("requestsHash",
        std::optional{hexHash(test::from_json<hash256>(h.at("requestsHash")))},
        seal.requestsHash.has_value() ? std::optional{hexHash(*seal.requestsHash)} : std::nullopt);
    // blobGasUsed：向量两 fork 都发射（op-geth Isthmus 头真带 0x0）。Jovian → 值比对
    // （"0x0" 是在场的零，如 jovian_first_block）；Isthmus → 断言 C++ 侧缺席
    // （seal.blobGasUsed 语义 = Jovian DA footprint 头字段，Isthmus 无此重用位；
    //  向量的 0x0 是 op-geth 头的 4844 遗留字段，信息性，不比值）。
    {
        const auto wantBlobGas = parseU256(h.at("blobGasUsed"));  // 必填（含 Isthmus）
        const auto gotBlobGas =
            seal.blobGasUsed.has_value() ? std::optional{hexU64(*seal.blobGasUsed)} : std::nullopt;
        if (jovian)
            ctx.checkOptional("blobGasUsed", std::optional{hexU256(wantBlobGas)}, gotBlobGas);
        else
            ctx.checkOptional("blobGasUsed", std::nullopt, gotBlobGas);
    }

    // ── receipts ────────────────────────────────────────────────────────────
    const auto& expReceipts = v.at("_op_expected").at("receipts");
    if (expReceipts.size() != result.receipts.size())
    {
        ADD_FAILURE() << id << ": receipts count mismatch: expected " << expReceipts.size()
                      << " got " << result.receipts.size() << " (no zip-min)";
        return;
    }
    for (size_t i = 0; i < expReceipts.size(); ++i)
    {
        const auto& er = expReceipts[i];
        const std::string p = "receipts[" + std::to_string(i) + "]";

        // 实测侧统一视图。
        const state::TransactionReceipt* receipt = nullptr;
        std::optional<std::string> gotDepNonce, gotDepVersion, gotL1Fee, gotOperatorFee,
            gotDaFootprint;
        if (const auto* dep = std::get_if<OpDepositReceipt>(&result.receipts[i]))
        {
            receipt = &dep->receipt;
            gotDepNonce = hexU64(dep->deposit_nonce);
            gotDepVersion = hexU64(dep->deposit_receipt_version);
        }
        else
        {
            const auto& tx = std::get<OpTxReceipt>(result.receipts[i]);
            receipt = &tx.receipt;
            if (tx.meta.l1_fee.has_value())
                gotL1Fee = hexU256(*tx.meta.l1_fee);
            // _op_operator_fee 在场规则镜像 op-geth deriveOPStackFields（槽 8 scalar/
            // constant 全零不发射）；本侧同规则的载体是 meta.operator_fee_scalar/
            // constant（OpReceiptMeta.cpp 仅在非全零时填）。meta.operator_fee 本身是
            // FISCO 扩展、Isthmus+ 恒填（含 0），直接比对会把「表示差异」误报成分歧。
            if (tx.meta.operator_fee_scalar.has_value() ||
                tx.meta.operator_fee_constant.has_value())
                gotOperatorFee = hexU256(tx.meta.operator_fee.value_or(intx::uint256{0}));
            if (tx.meta.da_footprint.has_value())
                gotDaFootprint = hexU64(*tx.meta.da_footprint);
        }

        ctx.checkField(p + ".type", hexU256(parseU256(er.at("type"))),
            hexU64(static_cast<uint64_t>(receipt->type)));
        ctx.checkField(p + ".status", hexU256(parseU256(er.at("status"))),
            receipt->status == EVMC_SUCCESS ? "0x1" : "0x0");
        ctx.checkField(p + ".gasUsed", hexU256(parseU256(er.at("gasUsed"))),
            hexU64(static_cast<uint64_t>(receipt->gas_used)));
        ctx.checkField(p + ".cumulativeGasUsed", hexU256(parseU256(er.at("cumulativeGasUsed"))),
            hexU64(static_cast<uint64_t>(receipt->cumulative_gas_used)));
        ctx.checkField(p + ".logsCount", std::to_string(er.at("logsCount").get<int64_t>()),
            std::to_string(receipt->logs.size()));

        const auto optWant = [&](const char* key) -> std::optional<std::string> {
            return er.contains(key) ? std::optional{hexU256(parseU256(er.at(key)))} : std::nullopt;
        };
        ctx.checkOptional(p + "._op_deposit_nonce", optWant("_op_deposit_nonce"), gotDepNonce);
        ctx.checkOptional(p + "._op_deposit_receipt_version",
            optWant("_op_deposit_receipt_version"), gotDepVersion);
        ctx.checkOptional(p + "._op_l1_fee", optWant("_op_l1_fee"), gotL1Fee);
        ctx.checkOptional(p + "._op_operator_fee", optWant("_op_operator_fee"), gotOperatorFee);
        ctx.checkOptional(p + "._op_da_footprint", optWant("_op_da_footprint"), gotDaFootprint);
    }

    // ── postState 双向（决策记录 8）─────────────────────────────────────────
    // 前向：向量逐账户逐槽 vs 回放终态。零槽/零账户按 trie 语义规约（0 ≡ 缺席）：
    // 向量把「块后不存在的候选账户」发射为 {"balance":"0x0"}——比对语义即
    // 「四字段全零 + 全槽零」，实测侧账户缺失时以零账户视之，恒等 ⇒ pass；
    // 实测侧存在但为空同样 pass（EIP-161 空账户 ≡ trie 不存在）。
    const auto& post = v.at("postState");
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
    ::testing::Test::RecordProperty(id + ".comparisons", ctx.comparisons);
    if (ctx.comparisons == 0)
        ADD_FAILURE() << id << ": zero comparisons executed";
}
}  // namespace

// ── structurallyUnrecoverable 谓词边界单测（防退化恒真/恒假后标记逃逸复活）──
TEST(OpT8nReplay, StructurallyUnrecoverablePredicateBoundaries)
{
    auto mk = [](uint64_t v, const intx::uint256& r, const intx::uint256& s) {
        evmone::state::Authorization a{};
        a.v = v;
        a.r = r;
        a.s = s;
        return a;
    };
    const intx::uint256 one{1};
    EXPECT_FALSE(structurallyUnrecoverable(mk(0, one, one)));
    EXPECT_FALSE(structurallyUnrecoverable(mk(1, one, kSecpHalfN)));       // s == N/2 合法
    EXPECT_TRUE(structurallyUnrecoverable(mk(2, one, one)));               // v > 1
    EXPECT_TRUE(structurallyUnrecoverable(mk(0, one, kSecpHalfN + 1)));    // s > N/2
    EXPECT_TRUE(structurallyUnrecoverable(mk(0, intx::uint256{0}, one)));  // r == 0
    EXPECT_FALSE(structurallyUnrecoverable(mk(0, kSecpN - 1, one)));
    EXPECT_TRUE(structurallyUnrecoverable(mk(0, kSecpN, one)));            // r >= N
    EXPECT_TRUE(structurallyUnrecoverable(mk(0, one, intx::uint256{0})));  // s == 0
}

TEST(OpT8nReplay, Vectors)
{
    const fs::path vectorsDir = OP_T8N_VECTORS_DIR;
    ASSERT_TRUE(fs::is_directory(vectorsDir)) << vectorsDir;

    // A) 集合相等：目录 *.json 文件名集合 == manifest.txt 清单（缺/多均 FAILURE）。
    const auto manifest = loadManifest(vectorsDir / "manifest.txt");
    ASSERT_FALSE(manifest.empty()) << "empty manifest";
    std::set<std::string> present;
    for (const auto& entry : fs::directory_iterator(vectorsDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            present.insert(entry.path().filename().string());
    }
    for (const auto& name : manifest)
    {
        if (!present.contains(name))
            ADD_FAILURE() << "manifest lists " << name << " but file is missing";
    }
    for (const auto& name : present)
    {
        if (!manifest.contains(name))
            ADD_FAILURE() << "unmanifested vector file present: " << name;
    }

    auto ledger = DivergenceLedger::load(vectorsDir / "DIVERGENCES.md");
    auto vm = evmc::VM{evmc_create_evmone()};

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
            replayVector(id, *vec, ledger, vm);
        }
        catch (const std::exception& e)
        {
            ADD_FAILURE() << name << ": " << e.what();
        }
        catch (...)
        {
            // typed-catch RTTI 兜底（同上：机理见 docs/audits/ 排查报告）——点名类型，
            // 与 typed 分支同语义（记 FAILURE 后继续下一向量文件）。
            const auto* excType = abi::__cxa_current_exception_type();
            ADD_FAILURE() << name << ": exception escaped typed catch (exception type: "
                          << (excType ? excType->name() : "<unknown>") << ")";
        }
    }

    // E) 尾账：过期豁免即红 + KNOWN-DIVERGE 总数入 RecordProperty。
    ledger.finish();
}
