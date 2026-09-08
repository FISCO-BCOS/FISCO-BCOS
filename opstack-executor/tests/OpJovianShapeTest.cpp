// OpJovianShapeTest — DA 矩阵 Task 7: A 层单测增补（锚定快照值）。
//
// 锁定 validateJovianBlockShape(std::span<const OpBlockTx>, const OpForkConfig&)
// 的 shape 校验矩阵（三代理 T4/Item7 修正：它只做 shape 校验，
// 不提取 da_footprint —— 提取在 processOpBlock 内，da_footprint 的数值断言由 C 层
// t8n 覆盖，故本文件不建「提取」测试名）：
//   - 178B + JovianL1AttributesSelector 0x3db6be2b（对照 op-geth rollup_cost.go）
//     的 attributes tx → no-throw；
//   - 176B 分支只查长度 + deposits-only，**不校验 0x098999be selector**
//     （validateJovianBlockShape 的 activation-block 分支）——测试注明；
//   - 非 176/178 长度 → throw("too short")；错 selector → throw("does not have Jovian
//     selector")；含非 deposit tx 的激活块 → throw("unexpected non-deposit transactions")；
//   - pre-Jovian 配置（cfg.has_da_footprint==false）恒 no-op。

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <opstack-executor/OpBlockExecute.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace bcos::evm::opstack;

namespace
{
// L1 attributes deposit：`dataLen` 字节 calldata，前 `prefix.size()` 字节为 `prefix`
// （用于盖上 Jovian selector），其余为零。
OpBlockTx attributesDeposit(size_t dataLen, std::span<const uint8_t> prefix = {})
{
    evmc::bytes data(dataLen, 0);
    std::copy(prefix.begin(), prefix.end(), data.begin());
    DepositTx dep{.source_hash = evmc::bytes32{},
        .from = OP_DEPOSITOR,
        .to = OP_L1_BLOCK,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 1'000'000,
        .is_system_tx = false,
        .data = std::move(data)};
    return OpBlockTx{.tx = std::move(dep), .signedEnvelope = {}};
}

// 普通（非 deposit）tx —— shape 检查只关心 variant 的另一个备选，字段值无关紧要。
OpBlockTx normalTx()
{
    return OpBlockTx{.tx = evmone::state::Transaction{}, .signedEnvelope = {}};
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpJovianShapeSuite)

BOOST_AUTO_TEST_CASE(ValidateJovianBlockShapeAcceptReject)
{
    const auto& jovian = jovianConfig();

    // —— 接受侧 ——
    {
        // 常规 Jovian 块：178B + Jovian selector
        std::vector<OpBlockTx> txs;
        txs.push_back(attributesDeposit(JovianL1AttributesLen, JovianL1AttributesSelector));
        BOOST_CHECK_NO_THROW(validateJovianBlockShape(txs, jovian));
    }
    {
        // 长度 >= 178 只查 selector：179B + 正确 selector 也接受
        std::vector<OpBlockTx> txs;
        txs.push_back(attributesDeposit(JovianL1AttributesLen + 1, JovianL1AttributesSelector));
        BOOST_CHECK_NO_THROW(validateJovianBlockShape(txs, jovian));
    }
    {
        // 激活块：176B，deposits-only（多笔 deposit 仍合法）；176B 分支不校验 0x098999be selector
        std::vector<OpBlockTx> txs;
        txs.push_back(attributesDeposit(IsthmusL1AttributesLen));
        txs.push_back(attributesDeposit(IsthmusL1AttributesLen));
        BOOST_CHECK_NO_THROW(validateJovianBlockShape(txs, jovian));
    }
    {
        // 首个 tx 非 deposit → 本函数不评判（processOpBlock 的兄弟检查负责），no-throw
        std::vector<OpBlockTx> txs;
        txs.push_back(normalTx());
        BOOST_CHECK_NO_THROW(validateJovianBlockShape(txs, jovian));
    }
    {
        // 空块 → no-op
        std::vector<OpBlockTx> txs;
        BOOST_CHECK_NO_THROW(validateJovianBlockShape(txs, jovian));
    }

    // —— 拒绝侧 ——
    const auto shapeError = [&](std::vector<OpBlockTx> txs) -> std::string {
        try
        {
            validateJovianBlockShape(txs, jovian);
        }
        catch (const bcos::evm::OpConsensusError& e)
        {
            return std::string{e.what()};
        }
        return std::string{};
    };

    {
        // 177B：介于 176 与 178 之间 → too short
        std::vector<OpBlockTx> txs;
        txs.push_back(attributesDeposit(IsthmusL1AttributesLen + 1));
        BOOST_CHECK_MESSAGE(shapeError(txs).find("too short") != std::string::npos,
            "177B 应报 too short,got: " << shapeError(txs));
    }
    {
        // 175B → too short
        std::vector<OpBlockTx> txs;
        txs.push_back(attributesDeposit(IsthmusL1AttributesLen - 1));
        BOOST_CHECK_MESSAGE(shapeError(txs).find("too short") != std::string::npos,
            "175B 应报 too short,got: " << shapeError(txs));
    }
    {
        // 178B 错 selector（全零，非 0x3db6be2b）→ does not have Jovian selector
        std::vector<OpBlockTx> txs;
        txs.push_back(attributesDeposit(JovianL1AttributesLen));
        BOOST_CHECK_MESSAGE(
            shapeError(txs).find("does not have Jovian selector") != std::string::npos,
            "178B 错 selector 应报 does not have Jovian selector,got: " << shapeError(txs));
    }
    {
        // 激活块（176B）带非 deposit tx → unexpected non-deposit transactions
        std::vector<OpBlockTx> txs;
        txs.push_back(attributesDeposit(IsthmusL1AttributesLen));
        txs.push_back(normalTx());
        BOOST_CHECK_MESSAGE(
            shapeError(txs).find("unexpected non-deposit transactions") != std::string::npos,
            "176B 激活块带普通 tx 应报 unexpected non-deposit transactions,got: " << shapeError(
                txs));
    }
}

// 第二个 shape 用例：pre-Jovian 配置（has_da_footprint==false）恒 no-op，
// 任意畸形 shape 不抛（validateJovianBlockShape 开头直接 return）。
BOOST_AUTO_TEST_CASE(ValidateJovianBlockShapeNoOpPreJovian)
{
    for (const OpForkConfig* cfg :
        {&ecotoneConfig(), &fjordConfig(), &graniteConfig(), &holoceneConfig()})
    {
        std::vector<OpBlockTx> txs;
        txs.push_back(attributesDeposit(3));  // 任意畸形 shape
        txs.push_back(normalTx());
        BOOST_CHECK_NO_THROW(validateJovianBlockShape(txs, *cfg));
    }
}

BOOST_AUTO_TEST_SUITE_END()
