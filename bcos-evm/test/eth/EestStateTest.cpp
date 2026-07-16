#include <bcos-evm/adapter/StateDiffWriteback.h>
#include <bcos-evm/eth/EthTransition.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <test/utils/mpt_hash.hpp>
#include <test/utils/statetest.hpp>

namespace fs = std::filesystem;
using namespace evmone;

namespace
{
// EEST 根下 state fixtures 目录：兼容 <root>/state_tests 与 <root>/fixtures/state_tests
fs::path stateTestsDir(const fs::path& root)
{
    if (fs::exists(root / "state_tests"))
        return root / "state_tests";
    return root / "fixtures" / "state_tests";
}

// skip 清单：$EVM_REF_EEST_SKIP 指向的文本文件，每行一个相对 state_tests 目录的
// fixture 路径（# 开头为注释）。准入规则见 Task 6：Cancun/Prague 必须 0 skip，
// 仅 Osaka 允许且逐条记录原因。
std::set<std::string> loadSkipList()
{
    std::set<std::string> skip;
    if (const char* path = std::getenv("EVM_REF_EEST_SKIP"))
    {
        std::ifstream f{path};
        for (std::string line; std::getline(f, line);)
            if (!line.empty() && line[0] != '#')
                skip.insert(line);
    }
    return skip;
}

void runStateTest(const test::StateTransitionTest& t, evmc::VM& vm)
{
    SCOPED_TRACE(t.name);
    for (const auto& [rev, expectations, block] : t.cases)
    {
        if (rev < EVMC_CANCUN)  // spec §1.2: 目标 fork Cancun+
            continue;
        test::validate_state(t.pre_state, rev);  // 与上游 runner 一致的 pre-state 卫检
        for (size_t i = 0; i != expectations.size(); ++i)
        {
            SCOPED_TRACE(std::string{evmc::to_string(rev)} + '/' + std::to_string(i));
            const auto& expected = expectations[i];
            const auto tx = t.multi_tx.get(expected.indexes);
            auto state = t.pre_state;
            const auto blobParams = test::get_blob_params(rev, t.blob_schedule);

            const auto res = bcos::evmref::eth::runTransaction(state, block, t.block_hashes, tx,
                rev, vm, block.gas_limit,
                static_cast<int64_t>(state::max_blob_gas_per_block(blobParams)));

            if (std::holds_alternative<state::TransactionReceipt>(res))
            {
                bcos::evmref::applyStateDiff(
                    state, std::get<state::TransactionReceipt>(res).state_diff);
                // state test 约定：block reward 0 的最小块收尾
                bcos::evmref::applyStateDiff(state,
                    bcos::evmref::eth::runBlockFinalize(state, rev, block.coinbase, 0, {}, {}));
            }

            if (expected.exception)
            {
                ASSERT_FALSE(std::holds_alternative<state::TransactionReceipt>(res))
                    << "unexpected valid transaction";
                EXPECT_EQ(test::logs_hash(std::vector<state::Log>{}), expected.logs_hash);
            }
            else
            {
                ASSERT_TRUE(std::holds_alternative<state::TransactionReceipt>(res))
                    << "unexpected invalid transaction: "
                    << std::get<std::error_code>(res).message();
                EXPECT_EQ(test::logs_hash(std::get<state::TransactionReceipt>(res).logs),
                    expected.logs_hash);
            }
            EXPECT_EQ(state::mpt_hash(state), expected.state_hash);
        }
    }
}
}  // namespace

TEST(EestState, Fixtures)
{
    const char* root = std::getenv("EVM_REF_EEST_ROOT");
    if (root == nullptr)
        GTEST_SKIP() << "EVM_REF_EEST_ROOT not set";

    const auto dir = stateTestsDir(root);
    ASSERT_TRUE(fs::exists(dir)) << dir;
    const auto skip = loadSkipList();

    evmc::VM vm{evmc_create_evmone()};
    size_t files = 0;
    size_t skipped = 0;
    size_t failedFiles = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir))
    {
        if (entry.path().extension() != ".json" || entry.path().filename() == "index.json")
            continue;  // index.json 是 fixture 索引，不是测试（上游 registrar 同样排除）
        if (skip.count(fs::relative(entry.path(), dir).generic_string()) != 0)
        {
            ++skipped;
            continue;
        }
        ++files;
        SCOPED_TRACE(entry.path().string());
        const auto failuresBefore =
            ::testing::UnitTest::GetInstance()->current_test_info()->result()->total_part_count();
        std::ifstream f{entry.path()};
        try
        {
            for (const auto& t : test::load_state_tests(f))
                runStateTest(t, vm);
        }
        catch (const std::exception& e)
        {
            ADD_FAILURE() << "loader: " << e.what();
        }
        const auto failuresAfter =
            ::testing::UnitTest::GetInstance()->current_test_info()->result()->total_part_count();
        if (failuresAfter != failuresBefore)
            ++failedFiles;
    }
    std::clog << "EEST state: files=" << files << " skipped=" << skipped
              << " failed_files=" << failedFiles << "\n";
    EXPECT_GT(files, 0u);
}
