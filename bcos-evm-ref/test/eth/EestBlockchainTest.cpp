// M3：EEST blockchain 级 fixture 对照 harness。
//
// 移植自 evmone REF `3585c2cb`（vcpkg 锁定 fork）的
// `test/blockchaintest/blockchaintest_runner.cpp`（422 行，逐行对照）与
// `test/utils/blockchaintest.hpp`；范围裁剪见 spec §7.1 "M3 范围决策"：
//   纳入：块执行循环（system_call_block_start → 逐笔 bcos::evmref::eth::runTransaction +
//         写回 → runBlockFinalize → system_call_block_end → requests）、validate_block
//         头校验、侧链/canonical 追踪、过渡 fork（RevisionSchedule）、四 root + gas_used +
//         requests_hash 判据、invalid block 必须被拒绝且不进 block_data。
//   排除：blockchain_tests_engine*/_sync 目录、genesis_rev < Cancun 的整个 test、
//         ommers 与 mining reward（post-merge 恒空/nullopt）、calculate_difficulty
//         （pre-Paris ethash only）、print_state 调试函数。
//
// 与上游 apply_block() 的关键差异：上游直接调用 test::transition() 原地改写 TestState；
// 本模块必须走 bcos::evmref::eth::runTransaction（不写回，见 EthTransition.h 契约），
// 因此每笔交易成功后要显式 applyStateDiff() 落账，runBlockFinalize 同理。
#include <bcos-evm-ref/adapter/StateDiffWriteback.h>
#include <bcos-evm-ref/eth/EthTransition.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <ranges>
#include <string>
#include <test/state/requests.hpp>
#include <test/utils/blockchaintest.hpp>
#include <test/utils/mpt_hash.hpp>
#include <test/utils/rlp.hpp>
#include <test/utils/rlp_encode.hpp>
#include <unordered_map>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
using namespace evmone;

namespace
{
/// EIP-7934：CL gossip 协议对信标区块内容的最大尺寸约束。
constexpr size_t kMaxBlockSize = 10 * 1024 * 1024;
/// EIP-7934：信标区块内容的安全余量。
constexpr size_t kSafetyMargin = 2 * 1024 * 1024;
/// EIP-7934：EL 区块 RLP 编码后的最大尺寸（= 10MiB - 2MiB）。
constexpr size_t kMaxRlpBlockSize = kMaxBlockSize - kSafetyMargin;

// EEST 根下 blockchain fixtures 目录：兼容 <root>/blockchain_tests 与
// <root>/fixtures/blockchain_tests（与 EestStateTest.cpp 的 stateTestsDir 同一约定）。
fs::path blockchainTestsDir(const fs::path& root)
{
    if (fs::exists(root / "blockchain_tests"))
        return root / "blockchain_tests";
    return root / "fixtures" / "blockchain_tests";
}

struct RejectedTransaction
{
    hash256 hash;
    size_t index;
    std::string message;
};

struct BlockResult
{
    std::vector<state::TransactionReceipt> receipts;
    std::vector<RejectedTransaction> rejected;
    std::optional<std::vector<state::Requests>> requests;
    int64_t gas_used = 0;
    state::BloomFilter bloom;
    int64_t blob_gas_left = 0;
    test::TestState block_state;
};

/// 逐块执行：system_call_block_start → 逐笔交易（runTransaction + 显式写回）→
/// 收集 requests（EIP-6110 deposits + system_call_block_end）→ runBlockFinalize
/// （withdrawals；block reward 恒 nullopt，理由见下）。
///
/// mining_reward()（上游 pre-Paris 区块奖励表）与 has_ommers 簿记（喂给
/// calculate_difficulty，同为 pre-Paris only）均不在 M3 范围内：Cancun+ 下
/// mining_reward() 恒返回 std::nullopt，post-merge 区块也不应携带 ommers
/// （validateBlock 已强制 ommers 为空），故此处直接把 block reward 硬编码为
/// std::nullopt，无需移植 mining_reward() 本体。
BlockResult applyBlock(const test::TestState& preState, evmc::VM& vm, const state::BlockInfo& block,
    const state::BlockHashes& blockHashes, const std::vector<state::Transaction>& txs,
    evmc_revision rev)
{
    test::TestState blockState(preState);
    // EIP-4788（beacon root）/ EIP-2935（history storage）系统调用。
    test::system_call_block_start(blockState, block, blockHashes, rev, vm);

    std::vector<state::Log> txsLogs;
    int64_t blockGasLeft = block.gas_limit;
    auto blobGasLeft = static_cast<int64_t>(block.blob_gas_used.value_or(0));

    std::vector<RejectedTransaction> rejectedTxs;
    std::vector<state::TransactionReceipt> receipts;
    int64_t cumulativeGasUsed = 0;

    for (size_t i = 0; i < txs.size(); ++i)
    {
        const auto& tx = txs[i];
        const auto computedTxHash = keccak256(rlp::encode(tx));

        // runTransaction 不写回状态（EthTransition.h 契约）：成功时必须显式 applyStateDiff。
        auto res = bcos::evmref::eth::runTransaction(
            blockState, block, blockHashes, tx, rev, vm, blockGasLeft, blobGasLeft);

        if (std::holds_alternative<std::error_code>(res))
        {
            const auto ec = std::get<std::error_code>(res);
            rejectedTxs.push_back({computedTxHash, i, ec.message()});
        }
        else
        {
            auto& receipt = std::get<state::TransactionReceipt>(res);
            bcos::evmref::applyStateDiff(blockState, receipt.state_diff);

            const auto& txLogs = receipt.logs;
            txsLogs.insert(txsLogs.end(), txLogs.begin(), txLogs.end());
            cumulativeGasUsed += receipt.gas_used;
            receipt.cumulative_gas_used = cumulativeGasUsed;
            // 上游对 rev < EVMC_BYZANTIUM 会把 post_state 设为逐笔 stateRoot；M3 范围恒
            // Cancun+，该分支必不触达，故省略（spec §1.2 目标 fork Cancun+）。

            blockGasLeft -= receipt.gas_used;
            blobGasLeft -= static_cast<int64_t>(tx.blob_gas_used());
            receipts.emplace_back(std::move(receipt));
        }
    }

    auto requests = [&]() -> std::optional<std::vector<state::Requests>> {
        std::vector<state::Requests> collected;

        if (rev >= EVMC_PRAGUE)
        {
            auto optDeposits = state::collect_deposit_requests(receipts);
            if (!optDeposits.has_value())
                return std::nullopt;
            collected.emplace_back(std::move(*optDeposits));
        }

        // withdrawal/consolidation requests 的系统合约调用。
        auto requestsResult = test::system_call_block_end(blockState, block, blockHashes, rev, vm);
        if (!requestsResult.has_value())
            return std::nullopt;
        std::ranges::move(*requestsResult, std::back_inserter(collected));

        return collected;
    }();

    bcos::evmref::applyStateDiff(
        blockState, bcos::evmref::eth::runBlockFinalize(blockState, rev, block.coinbase,
                        std::nullopt, block.ommers, block.withdrawals));

    const auto bloom = state::compute_bloom_filter(receipts);

    return {std::move(receipts), std::move(rejectedTxs), std::move(requests), cumulativeGasUsed,
        bloom, blobGasLeft, std::move(blockState)};
}

/// 区块头校验（不含单笔交易语义，那部分在 applyBlock 内按笔校验/执行）。
///
/// 相对上游 validate_block() 的裁剪（均见 spec §7.1 "M3 范围决策"）：
///  - 难度校验：上游用 calculate_difficulty(parent, parent_has_ommers, ..., rev) 重算并比对；
///    calculate_difficulty() 是 pre-Paris ethash 难度调整算法，不在 M3 范围（M3 只跑
///    genesis_rev >= Cancun 的 fixture，恒 rev >= EVMC_PARIS）。EIP-3675 之后区块难度恒为
///    0，故直接断言 difficulty == 0，语义等价于 calculate_difficulty 在 rev >= PARIS 下的
///    返回值，无需移植该函数本体或 parent_has_ommers 簿记。
///  - ommers 校验：post-merge 区块不应携带 ommers（上游同样用 rev >= EVMC_PARIS 守卫，此处
///    因 rev 恒满足而省略守卫）；ommer delta 范围校验（喂给 calculate_difficulty 的难度奖励
///    机制）随难度校验一起排除。
bool validateBlock(evmc_revision rev, const state::BlobParams& blobParams,
    const test::TestBlock& testBlock, const test::BlockHeader* parentHeader) noexcept
{
    // 父块头缺失（未知父块或父块本身无效）：整块无效。
    if (parentHeader == nullptr)
        return false;

    if (testBlock.block_info.number != parentHeader->block_number + 1)
        return false;

    if (testBlock.block_info.gas_used > testBlock.block_info.gas_limit)
        return false;

    // 部分 fixture 把 gas limit 设到 INT64_MAX，转 uint64_t 避免溢出（照抄上游注释）。
    const auto parentGasLimit = static_cast<uint64_t>(parentHeader->gas_limit);
    const auto blockGasLimit = static_cast<uint64_t>(testBlock.block_info.gas_limit);
    if (blockGasLimit >= parentGasLimit + parentGasLimit / 1024)
        return false;
    if (blockGasLimit <= parentGasLimit - parentGasLimit / 1024)
        return false;

    // Yellow Paper 规定的区块 gas limit 下限。
    if (testBlock.block_info.gas_limit < 5000)
        return false;

    // 部分 fixture 的 timestamp 放不进 int64_t，按上游注释转 uint64_t 比较。
    if (static_cast<uint64_t>(testBlock.block_info.timestamp) <=
        static_cast<uint64_t>(parentHeader->timestamp))
        return false;

    // 见函数级注释：calculate_difficulty() 排除在外，post-merge 难度恒为 0。
    if (testBlock.block_info.difficulty != 0)
        return false;

    // 见函数级注释：post-merge 区块必须没有 ommers。
    if (!testBlock.block_info.ommers.empty())
        return false;

    if (testBlock.block_info.extra_data.size() > 32)
        return false;

    // EIP-1559：重算 base fee 并与区块头声明值比对（rev >= London，Cancun+ 恒真）。
    const auto calculatedBaseFee = state::calc_base_fee(
        parentHeader->gas_limit, parentHeader->gas_used, parentHeader->base_fee_per_gas);
    if (testBlock.block_info.base_fee != calculatedBaseFee)
        return false;

    // EIP-4844/EIP-7918：excess_blob_gas、blob_gas_used 在 Cancun 后必填并重算校验
    // （rev >= Cancun，M3 范围内恒真）。
    if (!testBlock.block_info.excess_blob_gas.has_value() ||
        !testBlock.block_info.blob_gas_used.has_value())
        return false;

    // EIP-7918 要求用当前块的 rev/blob_params 重算 parent 的 blob base fee。
    const auto parentBlobBaseFee =
        state::compute_blob_gas_price(blobParams, parentHeader->excess_blob_gas.value_or(0));
    if (*testBlock.block_info.excess_blob_gas !=
        state::calc_excess_blob_gas(rev, blobParams, parentHeader->blob_gas_used.value_or(0),
            parentHeader->excess_blob_gas.value_or(0), parentHeader->base_fee_per_gas,
            parentBlobBaseFee))
        return false;

    // 单块消耗的 blob gas 不得超过上限。
    if (*testBlock.block_info.blob_gas_used > state::max_blob_gas_per_block(blobParams))
        return false;

    // withdrawals 字段在 loader 层解析失败即整块无效。
    if (!testBlock.withdrawals_parse_success)
        return false;

    // EIP-7934：用 loader 给出的 rlp_size（只量长度，不解码 RLP）。
    if (rev >= EVMC_OSAKA && testBlock.rlp_size > kMaxRlpBlockSize)
        return false;

    return true;
}

/// 一个 fixture（一个 JSON 内的一条具名 test case）的完整区块链回放：
/// 逐块 validate_block → apply_block，侧链按 block hash 建 map、按 total_difficulty
/// 追踪 canonical 分支；末尾比对 last_block_hash 对应的 post_state。
void runBlockchainTest(const test::BlockchainTest& c, size_t caseIndex, evmc::VM& vm)
{
    const auto revSchedule = test::to_rev_schedule(c.network);
    SCOPED_TRACE(std::string{evmc::to_string(revSchedule.get_revision(0))} + '/' +
                 std::to_string(caseIndex) + '/' + c.name);

    // Genesis 头基本不变式（纯 fixture 自检，照抄上游）。
    EXPECT_EQ(c.genesis_block_header.block_number, 0);
    EXPECT_EQ(c.genesis_block_header.gas_used, 0);
    EXPECT_EQ(c.genesis_block_header.transactions_root, state::EMPTY_MPT_HASH);
    EXPECT_EQ(c.genesis_block_header.receipts_root, state::EMPTY_MPT_HASH);
    EXPECT_EQ(c.genesis_block_header.withdrawal_root,
        revSchedule.get_revision(c.genesis_block_header.timestamp) >= EVMC_SHANGHAI ?
            state::EMPTY_MPT_HASH :
            bytes32{});
    EXPECT_EQ(c.genesis_block_header.logs_bloom, bytes_view{state::BloomFilter{}});

    test::TestBlockHashes blockHashes{
        {c.genesis_block_header.block_number, c.genesis_block_header.hash}};

    struct BlockData
    {
        const test::BlockHeader* header;
        test::TestState post_state;
        intx::uint256 total_difficulty;
    };
    std::unordered_map<hash256, BlockData> blockData{{c.genesis_block_header.hash,
        {&c.genesis_block_header, c.pre_state, c.genesis_block_header.difficulty}}};
    const auto* canonicalState = &c.pre_state;
    intx::uint256 maxTotalDifficulty = c.genesis_block_header.difficulty;

    for (size_t i = 0; i < c.test_blocks.size(); ++i)
    {
        const auto& testBlock = c.test_blocks[i];
        const auto& bi = testBlock.block_info;

        const auto parentDataIt = blockData.find(testBlock.block_info.parent_hash);
        const auto* parentHeader =
            parentDataIt != blockData.end() ? parentDataIt->second.header : nullptr;

        const auto rev = revSchedule.get_revision(bi.timestamp);
        const auto blobParams = test::get_blob_params(c.network, c.blob_schedule, bi.timestamp);

        SCOPED_TRACE(std::string{evmc::to_string(rev)} + '/' + std::to_string(caseIndex) + '/' +
                     c.name + '/' + std::to_string(testBlock.block_info.number));

        if (testBlock.valid)
        {
            ASSERT_TRUE(validateBlock(rev, blobParams, testBlock, parentHeader))
                << "Expected block to be valid (validate_block)";

            // Block 有效保证其父块必然已找到。
            assert(parentDataIt != blockData.end());
            const auto& preState = parentDataIt->second.post_state;

            auto res = applyBlock(preState, vm, bi, blockHashes, testBlock.transactions, rev);

            ASSERT_TRUE(res.requests.has_value());

            blockHashes[testBlock.expected_block_header.block_number] =
                testBlock.expected_block_header.hash;
            const auto [insertedIt, _] = blockData.insert({testBlock.block_info.hash,
                {.header = &testBlock.expected_block_header,
                    .post_state = std::move(res.block_state),
                    .total_difficulty =
                        parentDataIt->second.total_difficulty + testBlock.block_info.difficulty}});
            if (insertedIt->second.total_difficulty >= maxTotalDifficulty)
            {
                canonicalState = &insertedIt->second.post_state;
                maxTotalDifficulty = insertedIt->second.total_difficulty;
            }

            EXPECT_TRUE(res.rejected.empty())
                << "Invalid transaction in block expected to be valid";
            EXPECT_TRUE(res.blob_gas_left == 0)
                << "Transactions used more or less blob gas than expected in block header";

            EXPECT_EQ(state::mpt_hash(insertedIt->second.post_state),
                testBlock.expected_block_header.state_root);

            if (rev >= EVMC_SHANGHAI)
            {
                EXPECT_EQ(state::mpt_hash(testBlock.block_info.withdrawals),
                    testBlock.expected_block_header.withdrawal_root);
            }

            EXPECT_EQ(state::mpt_hash(testBlock.transactions),
                testBlock.expected_block_header.transactions_root);
            EXPECT_EQ(state::mpt_hash(res.receipts), testBlock.expected_block_header.receipts_root);
            if (rev >= EVMC_PRAGUE)
            {
                EXPECT_EQ(state::calculate_requests_hash(*res.requests),
                    testBlock.expected_block_header.requests_hash);
            }
            EXPECT_EQ(res.gas_used, testBlock.expected_block_header.gas_used);
            EXPECT_EQ(
                bytes_view{res.bloom}, bytes_view{testBlock.expected_block_header.logs_bloom});
        }
        else
        {
            if (!validateBlock(rev, blobParams, testBlock, parentHeader))
                continue;

            // Block 有效保证其父块必然已找到。
            assert(parentDataIt != blockData.end());
            const auto& preState = parentDataIt->second.post_state;

            const auto res = applyBlock(preState, vm, bi, blockHashes, testBlock.transactions, rev);
            if (!res.requests.has_value())
                continue;
            if (!res.rejected.empty())
                continue;
            if (res.blob_gas_left != 0)
                continue;

            if (state::mpt_hash(res.block_state) != testBlock.expected_block_header.state_root)
                continue;

            if (rev >= EVMC_SHANGHAI && state::mpt_hash(testBlock.block_info.withdrawals) !=
                                            testBlock.expected_block_header.withdrawal_root)
                continue;
            if (state::mpt_hash(testBlock.transactions) !=
                testBlock.expected_block_header.transactions_root)
                continue;
            if (state::mpt_hash(res.receipts) != testBlock.expected_block_header.receipts_root)
                continue;
            if (rev >= EVMC_PRAGUE && state::calculate_requests_hash(*res.requests) !=
                                          testBlock.expected_block_header.requests_hash)
                continue;
            if (res.gas_used != testBlock.expected_block_header.gas_used)
                continue;
            if (bytes_view{res.bloom} != bytes_view{testBlock.expected_block_header.logs_bloom})
                continue;

            // invalid block（test_block.valid == false）没有被 validateBlock/执行结果
            // 判据拒绝，说明实现把一个理应无效的块当成了有效块——这是本 harness 要抓的
            // 缺陷，不进 block_data（照抄上游：无效块永远不会被插入侧链追踪表）。
            EXPECT_TRUE(false) << "Expected block to be invalid but resulted valid";
        }
    }

    const auto expectedPostHash =
        std::holds_alternative<test::TestState>(c.expectation.post_state) ?
            state::mpt_hash(std::get<test::TestState>(c.expectation.post_state)) :
            std::get<hash256>(c.expectation.post_state);
    EXPECT_EQ(state::mpt_hash(*canonicalState), expectedPostHash);
}

struct FileStats
{
    size_t casesRun = 0;
    size_t casesSkipped = 0;
    bool hadFailure = false;
    bool unsupported = false;
};

/// 加载并回放单个 fixture 文件内的全部具名 test case。
///
/// fixture 过滤（spec §7.1）：`to_rev_schedule(network).genesis_rev < EVMC_CANCUN` 的
/// test 整体跳过——过渡 fork 的起始 revision 早于 Cancun 时，转变前的那些区块落在 M3
/// 目标 fork（Cancun+）之外。
FileStats runFixtureFile(const fs::path& path, evmc::VM& vm)
{
    FileStats stats;
    SCOPED_TRACE(path.string());
    const auto failuresBefore =
        ::testing::UnitTest::GetInstance()->current_test_info()->result()->total_part_count();
    std::ifstream f{path};
    try
    {
        const auto tests = test::load_blockchain_tests(f);
        for (size_t i = 0; i < tests.size(); ++i)
        {
            const auto& c = tests[i];
            if (test::to_rev_schedule(c.network).genesis_rev < EVMC_CANCUN)
            {
                ++stats.casesSkipped;
                continue;
            }
            ++stats.casesRun;
            runBlockchainTest(c, i, vm);
        }
    }
    catch (const test::UnsupportedTestFeature& e)
    {
        // 上游 loader 主动声明"不支持"的 fixture 类别：全量扫描确认目前仅命中 2 个
        // cancun/eip4844_blobs fixture，均是"无效 RLP 编码的区块"（loader 需要原始 RLP
        // 解码才能构造这类 test_block，而这条 RLP 解码路径本就在 M3/bcos-evm-ref 范围之外
        // ——spec §1.2："区块 RLP 解码路径…不做，只消费 JSON 展开字段"）。不计入
        // failed_files，单独计数，报告中逐条列出。
        stats.unsupported = true;
        std::clog << "EEST blockchain: unsupported feature in " << path.string() << ": " << e.what()
                  << '\n';
    }
    catch (const std::exception& e)
    {
        ADD_FAILURE() << "loader: " << e.what();
    }
    catch (...)
    {
        // 防御性兜底，实测记录的一个 ABI 细节：vcpkg 静态库内对 evmone::test::
        // UnsupportedTestFeature 这类完全内联在头文件里的自定义异常类型，
        // catch (const std::exception&) 在本工具链下有时不命中基类匹配（弱符号 typeinfo
        // 在库对象文件与消费侧对象文件之间未被折叠所致，逐个类型手工验证过，见
        // m3-report.md）——但按精确类型 catch 总能命中（上面那个 catch 分支即是证据）。
        // 全量扫描过 2848 个 fixture 未发现除 UnsupportedTestFeature 外的其它异常类型，
        // 这里只是保险丝，不应触达。
        ADD_FAILURE() << "loader: unknown exception type (not std::exception-catchable; "
                         "see m3-report.md RTTI note)";
    }
    const auto failuresAfter =
        ::testing::UnitTest::GetInstance()->current_test_info()->result()->total_part_count();
    stats.hadFailure = failuresAfter != failuresBefore;
    return stats;
}
}  // namespace

// 秒级冒烟：只跑 blockchain_tests/cancun/ 下前 20 个文件（按路径排序取前缀，保证确定性），
// 进 ctest 常驻。
TEST(EestBlockchain, Smoke)
{
    const char* root = std::getenv("EVM_REF_EEST_ROOT");
    if (root == nullptr)
        GTEST_SKIP() << "EVM_REF_EEST_ROOT not set";

    const auto dir = blockchainTestsDir(root) / "cancun";
    ASSERT_TRUE(fs::exists(dir)) << dir;

    std::vector<fs::path> jsonFiles;
    for (const auto& entry : fs::recursive_directory_iterator(dir))
        if (entry.path().extension() == ".json" && entry.path().filename() != "index.json")
            jsonFiles.push_back(entry.path());
    std::sort(jsonFiles.begin(), jsonFiles.end());

    constexpr size_t kSmokeLimit = 20;
    if (jsonFiles.size() > kSmokeLimit)
        jsonFiles.resize(kSmokeLimit);

    evmc::VM vm{evmc_create_evmone()};
    size_t files = 0;
    size_t skippedCases = 0;
    size_t unsupportedFiles = 0;
    size_t failedFiles = 0;
    for (const auto& path : jsonFiles)
    {
        ++files;
        const auto stats = runFixtureFile(path, vm);
        skippedCases += stats.casesSkipped;
        if (stats.unsupported)
            ++unsupportedFiles;
        if (stats.hadFailure)
            ++failedFiles;
    }
    std::clog << "EEST blockchain (smoke): files=" << files << " skipped_cases=" << skippedCases
              << " unsupported_files=" << unsupportedFiles << " failed_files=" << failedFiles
              << "\n";
    EXPECT_GT(files, 0u);
}

// 全量：blockchain_tests/ 下全部 ~2848 个文件、3.2GB，夜跑级，默认跳过。
// 设置 EVM_REF_EEST_BLOCKCHAIN_FULL=1 才会运行。
TEST(EestBlockchain, Full)
{
    if (std::getenv("EVM_REF_EEST_BLOCKCHAIN_FULL") == nullptr)
    {
        GTEST_SKIP() << "set EVM_REF_EEST_BLOCKCHAIN_FULL=1 to run the full EEST blockchain "
                        "suite (3.2GB / ~2848 files, minutes)";
    }

    const char* root = std::getenv("EVM_REF_EEST_ROOT");
    if (root == nullptr)
        GTEST_SKIP() << "EVM_REF_EEST_ROOT not set";

    const auto dir = blockchainTestsDir(root);
    ASSERT_TRUE(fs::exists(dir)) << dir;

    evmc::VM vm{evmc_create_evmone()};
    size_t files = 0;
    size_t skippedCases = 0;
    size_t unsupportedFiles = 0;
    size_t failedFiles = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir))
    {
        if (entry.path().extension() != ".json" || entry.path().filename() == "index.json")
            continue;  // index.json 是 fixture 索引，不是测试（同 EestStateTest.cpp 约定）。
        ++files;
        const auto stats = runFixtureFile(entry.path(), vm);
        skippedCases += stats.casesSkipped;
        if (stats.unsupported)
            ++unsupportedFiles;
        if (stats.hadFailure)
            ++failedFiles;
    }
    std::clog << "EEST blockchain (full): files=" << files << " skipped_cases=" << skippedCases
              << " unsupported_files=" << unsupportedFiles << " failed_files=" << failedFiles
              << "\n";
    EXPECT_GT(files, 0u);
}
