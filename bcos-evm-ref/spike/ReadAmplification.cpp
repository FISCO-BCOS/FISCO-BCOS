// M3.5 Phase 1 spike（spec §7.2 go/no-go 的第一步，不接真账本）
//
// 测什么：evmone 的 3 方法 `StateView` 在真实工作负载（EEST state fixtures）下
// 会产生多少次「账本读等价」，以及其中多少来自粗粒度 `get_account` 的读放大。
//
// 为什么这样测：bcos-evm 的 `storage/LedgerStateView.h` 已经是生产中的
// StateView-over-协程账本适配器（每次读 `task::syncWait`），其注释记录了一个
// 已知教训——回落到粗粒度 `get_account` 意味着每次查找 5 次账本读，因此他们把
// 自己的 StateView 加宽到 7 个窄读方法。evmone 的 StateView 恰好是那个窄三方法
// 接口。故 go/no-go 的真实问题不是「同步能否接协程」（已在生产验证），而是
// 「粗粒度 get_account 的读放大有多大、是否被 storage 读稀释」。
//
// 读代价模型逐条对照 LedgerStateView.h 的 lambda 实现。注意每个 lambda 都先
// `syncWait(account.exists())` 并在为假时**提前返回**，故 miss 只花 1 次读：
//   get_account       命中 -> exists + balance + nonce + codeHash = 4  (+1 若需探测 has_storage)
//                     未命中 -> exists                            = 1
//   get_account_code  命中 -> exists + code                       = 2   (m_codeRead)
//   get_storage       命中 -> exists + storage                    = 2   (m_storageRead)
//
// has_storage 的唯一消费者是 evmone host.cpp:91 的 EIP-7610 CREATE 碰撞检查，
// 且被 `nonce != 0 || code_hash != EMPTY` 短路。故只有 nonce==0 且无 code 的
// 账户才真正需要探测——本工具单独统计这一比例，用于判断能否条件化探测。
//
// 用法：EVM_REF_EEST_ROOT=<fixtures> ./bcos-evm-ref-read-amplification [max_files]

#include <bcos-evm-ref/eth/EthTransition.h>
#include <evmone/evmone.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <test/state/account.hpp>
#include <test/utils/statetest.hpp>

namespace fs = std::filesystem;
using namespace evmone;

namespace
{
// LedgerStateView.h 的每方法账本读次数
constexpr int kReadsAccountHit = 4;   // exists + balance + nonce + codeHash
constexpr int kReadsAccountMiss = 1;  // exists（lambda 提前返回）
constexpr int kReadsHasStorageProbe = 1;
constexpr int kReadsGetCode = 2;     // exists + code
constexpr int kReadsGetStorage = 2;  // exists + storage

struct Counters
{
    // 原始调用次数
    uint64_t accountCalls = 0;
    uint64_t accountFound = 0;
    uint64_t accountMissing = 0;  // 返回 nullopt：evmone 的 State 不缓存这些（上游 TODO）
    uint64_t accountNeedsProbe = 0;  // found 且 nonce==0 && code_hash==EMPTY -> has_storage 被消费
    uint64_t codeCalls = 0;
    uint64_t storageCalls = 0;

    // 冗余：同一 tx 内对同一个不存在账户的重复查询（上游 TODO 的实际代价）
    uint64_t redundantMissingCalls = 0;

    void operator+=(const Counters& o)
    {
        accountCalls += o.accountCalls;
        accountFound += o.accountFound;
        accountMissing += o.accountMissing;
        accountNeedsProbe += o.accountNeedsProbe;
        codeCalls += o.codeCalls;
        storageCalls += o.storageCalls;
        redundantMissingCalls += o.redundantMissingCalls;
    }

    // 账户相关的账本读（含条件化的 has_storage 探测）
    [[nodiscard]] uint64_t accountReads() const
    {
        return accountFound * kReadsAccountHit + accountMissing * kReadsAccountMiss +
               accountNeedsProbe * kReadsHasStorageProbe + codeCalls * kReadsGetCode;
    }
    [[nodiscard]] uint64_t storageReads() const { return storageCalls * kReadsGetStorage; }
    [[nodiscard]] uint64_t totalReads() const { return accountReads() + storageReads(); }

    // 若 has_storage 无条件探测（naive 实现：每个命中账户都探测）
    [[nodiscard]] uint64_t totalReadsNaiveProbe() const
    {
        return totalReads() + (accountFound - accountNeedsProbe) * kReadsHasStorageProbe;
    }

    /// 窄接口下限：命中账户假设只需 exists + 1 个字段 = 2 读（bcos-evm 窄读的最好情况），
    /// miss 同为 1 读，code/storage 不变。用于给出「加宽接口最多能省多少」的上界。
    [[nodiscard]] uint64_t narrowLowerBoundReads() const
    {
        return accountFound * 2 + accountMissing * kReadsAccountMiss +
               accountNeedsProbe * kReadsHasStorageProbe + codeCalls * kReadsGetCode +
               storageCalls * kReadsGetStorage;
    }
};

/// 计数用 StateView：转发给底层 TestState，统计调用并归类。
class CountingStateView final : public evmone::state::StateView
{
public:
    explicit CountingStateView(const test::TestState& backing) noexcept : m_backing{backing} {}

    std::optional<Account> get_account(const address& addr) const noexcept override
    {
        ++m_c.accountCalls;
        const auto acc = m_backing.get_account(addr);
        if (!acc.has_value())
        {
            ++m_c.accountMissing;
            if (!m_missingSeen.insert(addr).second)
                ++m_c.redundantMissingCalls;
            return acc;
        }
        ++m_c.accountFound;
        // has_storage 只有在 nonce==0 且 code 为空时才被 EIP-7610 检查读取（host.cpp:87-92）
        if (acc->nonce == 0 && acc->code_hash == state::Account::EMPTY_CODE_HASH)
            ++m_c.accountNeedsProbe;
        return acc;
    }

    bytes get_account_code(const address& addr) const noexcept override
    {
        ++m_c.codeCalls;
        return m_backing.get_account_code(addr);
    }

    bytes32 get_storage(const address& addr, const bytes32& key) const noexcept override
    {
        ++m_c.storageCalls;
        return m_backing.get_storage(addr, key);
    }

    [[nodiscard]] const Counters& counters() const noexcept { return m_c; }

private:
    const test::TestState& m_backing;
    mutable Counters m_c;
    mutable std::set<address> m_missingSeen;
};

enum class TxKind
{
    Transfer,
    Call,
    Create
};

const char* kindName(TxKind k)
{
    switch (k)
    {
    case TxKind::Transfer:
        return "transfer";
    case TxKind::Call:
        return "call";
    case TxKind::Create:
        return "create";
    }
    return "?";
}

TxKind classify(const state::Transaction& tx, const test::TestState& pre)
{
    if (!tx.to.has_value())
        return TxKind::Create;
    const auto it = pre.find(*tx.to);
    if (it != pre.end() && !it->second.code.empty())
        return TxKind::Call;
    return TxKind::Transfer;
}

struct Bucket
{
    uint64_t txs = 0;
    Counters c;
};

fs::path stateTestsDir(const fs::path& root)
{
    if (fs::exists(root / "state_tests"))
        return root / "state_tests";
    return root / "fixtures" / "state_tests";
}

void printRow(const char* label, uint64_t txs, const Counters& c)
{
    if (txs == 0)
        return;
    const auto per = [txs](
                         uint64_t v) { return static_cast<double>(v) / static_cast<double>(txs); };
    const auto total = c.totalReads();
    const double storagePct =
        total == 0 ? 0.0 :
                     100.0 * static_cast<double>(c.storageReads()) / static_cast<double>(total);
    // 读放大 = 粗粒度总读数 / 窄接口下限总读数（后者是「加宽接口」的最好情况）
    const auto lb = c.narrowLowerBoundReads();
    const double amplification =
        lb == 0 ? 1.0 : static_cast<double>(total) / static_cast<double>(lb);

    std::cout << std::left << std::setw(10) << label << std::right << std::setw(9) << txs
              << std::fixed << std::setprecision(2) << std::setw(11) << per(c.accountCalls)
              << std::setw(10) << per(c.codeCalls) << std::setw(12) << per(c.storageCalls)
              << std::setw(13) << per(c.accountReads()) << std::setw(13) << per(c.storageReads())
              << std::setw(12) << per(total) << std::setw(11) << storagePct << "%" << std::setw(10)
              << amplification << "x\n";
}
}  // namespace

int main(int argc, char** argv)
{
    const char* root = std::getenv("EVM_REF_EEST_ROOT");
    if (root == nullptr)
    {
        std::cerr << "EVM_REF_EEST_ROOT not set\n";
        return 2;
    }
    const size_t maxFiles = argc > 1 ? std::stoul(argv[1]) : SIZE_MAX;

    const auto dir = stateTestsDir(root);
    if (!fs::exists(dir))
    {
        std::cerr << "no such dir: " << dir << "\n";
        return 2;
    }

    evmc::VM vm{evmc_create_evmone()};
    std::map<TxKind, Bucket> buckets;
    size_t files = 0;
    uint64_t skippedCases = 0;

    for (const auto& entry : fs::recursive_directory_iterator(dir))
    {
        if (files >= maxFiles)
            break;
        if (entry.path().extension() != ".json" || entry.path().filename() == "index.json")
            continue;
        ++files;
        std::ifstream f{entry.path()};
        std::vector<test::StateTransitionTest> tests;
        try
        {
            tests = test::load_state_tests(f);
        }
        catch (const std::exception&)
        {
            continue;
        }

        for (const auto& t : tests)
        {
            for (const auto& [rev, expectations, block] : t.cases)
            {
                if (rev < EVMC_CANCUN)
                    continue;
                const auto blobParams = test::get_blob_params(rev, t.blob_schedule);
                for (const auto& expected : expectations)
                {
                    const auto tx = t.multi_tx.get(expected.indexes);
                    // 只统计成功进入执行的 tx——被 validate 拒绝的没有代表性
                    if (expected.exception)
                    {
                        ++skippedCases;
                        continue;
                    }

                    CountingStateView cv{t.pre_state};
                    const auto res = bcos::evmref::eth::runTransaction(cv, block, t.block_hashes,
                        tx, rev, vm, block.gas_limit,
                        static_cast<int64_t>(state::max_blob_gas_per_block(blobParams)));
                    if (!std::holds_alternative<state::TransactionReceipt>(res))
                    {
                        ++skippedCases;
                        continue;
                    }

                    auto& b = buckets[classify(tx, t.pre_state)];
                    ++b.txs;
                    b.c += cv.counters();
                }
            }
        }
    }

    Counters all;
    uint64_t allTxs = 0;
    for (const auto& [k, b] : buckets)
    {
        all += b.c;
        allTxs += b.txs;
    }

    std::cout << "\n=== M3.5 Phase 1: evmone StateView 读放大（EEST state, Cancun+） ===\n";
    std::cout << "files=" << files << "  txs=" << allTxs
              << "  skipped(invalid/expected-fail)=" << skippedCases << "\n";
    std::cout
        << "读代价模型（对照 bcos-evm/storage/LedgerStateView.h，lambda 在 !exists 时提前返回）:\n"
           "  get_account 命中=4 / 未命中=1, +has_storage条件探测=1, "
           "get_account_code=2, get_storage=2\n"
           "  amplif = 粗粒度总读 / 窄接口下限总读（命中账户按 exists+1字段=2 计）\n\n";

    std::cout << std::left << std::setw(10) << "kind" << std::right << std::setw(9) << "txs"
              << std::setw(11) << "acct/tx" << std::setw(10) << "code/tx" << std::setw(12)
              << "slot/tx" << std::setw(13) << "acctRd/tx" << std::setw(13) << "stoRd/tx"
              << std::setw(12) << "totRd/tx" << std::setw(12) << "storage%" << std::setw(11)
              << "amplif\n";
    std::cout << std::string(113, '-') << "\n";
    for (const auto& [k, b] : buckets)
        printRow(kindName(k), b.txs, b.c);
    std::cout << std::string(113, '-') << "\n";
    printRow("ALL", allTxs, all);

    std::cout << "\n--- has_storage 探测 ---\n";
    std::cout << "get_account 调用总数         : " << all.accountCalls << "  (命中 "
              << all.accountFound << " / 未命中 " << all.accountMissing << ")\n";
    std::cout << "  其中命中且需要 has_storage : " << all.accountNeedsProbe << "  ("
              << std::setprecision(1)
              << (all.accountCalls ? 100.0 * static_cast<double>(all.accountNeedsProbe) /
                                         static_cast<double>(all.accountCalls) :
                                     0.0)
              << "% —— 仅这些需真实探测存储)\n";
    std::cout << "  返回 nullopt（不存在账户） : " << all.accountMissing << "\n";
    std::cout << "条件化探测总读数             : " << all.totalReads() << "\n";
    std::cout << "无条件探测总读数（naive）    : " << all.totalReadsNaiveProbe() << "  (+"
              << std::setprecision(1)
              << (all.totalReads() ?
                         100.0 *
                             static_cast<double>(all.totalReadsNaiveProbe() - all.totalReads()) /
                             static_cast<double>(all.totalReads()) :
                         0.0)
              << "%)\n";

    std::cout << "\n--- 两条优化路径的收益对比（关键结论） ---\n";
    const auto total = all.totalReads();
    const auto negCacheSaved = all.redundantMissingCalls * kReadsAccountMiss;
    const auto narrowSaved = total - all.narrowLowerBoundReads();
    const auto pct = [total](uint64_t v) {
        return total ? 100.0 * static_cast<double>(v) / static_cast<double>(total) : 0.0;
    };
    std::cout << "基线总读数                               : " << total << "\n";
    std::cout << "A) 适配器加负缓存（不存在账户 per-tx 缓存）: 省 " << negCacheSaved << " ("
              << std::setprecision(1) << pct(negCacheSaved) << "%)  —— 纯适配器侧, 不碰 evmone\n";
    std::cout << "B) 把 StateView 加宽为窄读（需 fork evmone）: 省 " << narrowSaved << " ("
              << pct(narrowSaved) << "%)  —— 上界, 需改上游接口\n";

    std::cout << "\n--- 上游 TODO：不存在账户不被 State 缓存（state.cpp:249） ---\n";
    std::cout << "同一 tx 内对同一不存在地址的重复 get_account : " << all.redundantMissingCalls
              << "  (占 get_account 调用 " << std::setprecision(1)
              << (all.accountCalls ? 100.0 * static_cast<double>(all.redundantMissingCalls) /
                                         static_cast<double>(all.accountCalls) :
                                     0.0)
              << "%)\n";
    return 0;
}
