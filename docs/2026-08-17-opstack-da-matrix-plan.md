# opstack DA / Operator Fee 参数化矩阵实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 FISCO opstack 的 Jovian DA gas / operator fee 建立四端(FFISCO/op-geth/op-revm/Solidity)参数化对拍矩阵 + A 层单测补齐,锚定 op-geth 参考值。

**Architecture:** 共享 JSON 网格(唯一输入源)驱动四个薄 runner(FISCO C++ / op-geth Go / op-revm Rust / GasPriceOracle.sol)各自产出,逐位对拍后提交快照入 CI;A 层单测锚定快照。FISCO runner 与单测经 `has_operator_fee` 门封装(镜像 `OpTransition.cpp:395`)。fork 枚举裁剪为 `{ecotone,fjord,granite,holocene,isthmus,jovian,karst}`;bedrock/regolith/首块回退为 FISCO 实现缺口,单独立案。

**Tech Stack:** C++17(FISCO runner + 单测,链接 bcos-evm/evmc/intx/evmone)、Go 1.23(op-geth runner,本地 `/Users/octopus/octo/code/blockchain-impl/op-geth`)、Rust(op-revm 20.0.0,本地 `/Users/octopus/octo/code/blockchain-impl/optimism/rust/op-revm`)、Solidity(本地 `/Users/octopus/octo/code/blockchain-impl/optimism/packages/contracts-bedrock/src/L2/GasPriceOracle.sol`)。

## Global Constraints

- **测试不可退步**:只增补测试与工具;网格/单测发现实现级分歧 → 登记 `known_divergence` 或单独立案,不修复 DA/operator fee 实现。
- 基准行参数组钉死 op-geth 值:`baseFee=1000e6、blobBaseFee=10e6、baseScalar=2、blobScalar=3、opScalar=1439103868、opConst=1256417826609331460、gas=1618`(否则参考常量 ithmusOperatorFee=1256417826611659930 / jovianOperatorFee=1256650673615173860 / fjordFee=3203000 无法复现)。
- 网格 `slots` 只用 L1Block 槽 1/3/7/8;全 max 用例 slot1/slot7 约束 `< 2^256`(避免 FISCO L1 饱和 2^256 vs op-geth 无界分歧)。
- fork→config 映射:`ecotoneConfig()/fjordConfig()/graniteConfig()/holoceneConfig()/isthmusConfig()/jovianConfig()/karstConfig()`(OpForkSchedule.h:33-39)。
- 溢出上限:Isthmus 77 bits、Jovian 103 bits(< 2^256,`intx::uint256` 无回绕)。
- 单测与网格快照值必须逐位一致。
- 关键路径 pinning:op-geth = v1.101702.2;op-revm = 本地 checkout(实现时记录 commit);SolBidity = 本地 contracts-bedrock checkout。

---

### Task 1: FISCO `has_operator_fee` 门封装 + 单测

**Files:**
- Modify: `bcos-evm/bcos-evm/opstack/RollupCost.h`(新增 inline wrapper)
- Test: `bcos-evm/test/opstack/RollupCostTest.cpp`

**Interfaces:**
- Consumes: `computeOperatorCost(const OpFeeParams&, uint64_t gas, const OpForkConfig&)`、`OpForkConfig::has_operator_fee`(已有)
- Produces: `intx::uint256 computeChargedOperatorCost(const OpFeeParams& params, uint64_t gas, const OpForkConfig& cfg) noexcept` — 镜像 `OpTransition.cpp:395` 的 `cfg.has_operator_fee ? computeOperatorCost(...) : 0` 三元门

- [ ] **Step 1: 写失败测试**(RollupCostTest.cpp,RollupCostSuite 内追加)

```cpp
// 门封装:has_operator_fee=false(fjord/granite/holocene)→ 恒 0,即使 opScalar/opConst 非零
BOOST_AUTO_TEST_CASE(OperatorFeeGateOffReturnsZero)
{
    const auto p = feeParams(0, 0, 0, 0, /*opScalar=*/2000000, /*opConst=*/500);
    BOOST_CHECK_EQUAL(computeChargedOperatorCost(p, 1000, fjordConfig()), intx::uint256{0});
    BOOST_CHECK_EQUAL(computeChargedOperatorCost(p, 1000, graniteConfig()), intx::uint256{0});
    BOOST_CHECK_EQUAL(computeChargedOperatorCost(p, 1000, holoceneConfig()), intx::uint256{0});
    // 门开(isthmus/jovian)→ 透传 computeOperatorCost 公式
    BOOST_CHECK_EQUAL(computeChargedOperatorCost(p, 1000, isthmusConfig()), intx::uint256{2500});
    BOOST_CHECK_EQUAL(
        computeChargedOperatorCost(p, 1000, jovianConfig()), intx::uint256{200000000500ull});
    // karst 别名 == jovian
    BOOST_CHECK_EQUAL(computeChargedOperatorCost(p, 1000, karstConfig()),
        computeChargedOperatorCost(p, 1000, jovianConfig()));
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cd build/bcos-evm/test && ./bcos-evm-opstack-tests --run_test=RollupCostSuite/OperatorFeeGateOffReturnsZero`
Expected: FAIL — `computeChargedOperatorCost` 未定义

- [ ] **Step 3: 最小实现**(RollupCost.h,RollupCost 命名空间内,RollupCost.cpp 声明处附近)

```cpp
/// Charged operator fee, mirroring the block-transition gate (OpTransition.cpp:395):
/// has_operator_fee ? computeOperatorCost(...) : 0. The fork-level 0-gate lives here for
/// callers (runners / tests) that must not re-implement the block-transition decision.
inline intx::uint256 computeChargedOperatorCost(
    const OpFeeParams& params, uint64_t gas, const OpForkConfig& cfg) noexcept
{
    return cfg.has_operator_fee ? computeOperatorCost(params, gas, cfg) : intx::uint256{0};
}
```
(若 `OpForkConfig` 未在 RollupCost.h 前置声明完整定义,加 `#include <bcos-evm/opstack/OpForkSchedule.h>` 或前置声明;以编译为准。)

- [ ] **Step 4: 跑测试确认通过**

Run: `cd build/bcos-evm/test && ./bcos-evm-opstack-tests --run_test=RollupCostSuite`
Expected: PASS(含新用例;既有 RollupCostSuite 全绿)

- [ ] **Step 5: 提交**

```bash
git add bcos-evm/bcos-evm/opstack/RollupCost.h bcos-evm/test/opstack/RollupCostTest.cpp
git commit -m "feat(da-matrix): has_operator_fee gate wrapper (computeChargedOperatorCost)"
```

---

### Task 2: JSON 网格 `da_matrix.json`(7 类覆盖 + envelope 注册表)

**Files:**
- Create: `opstack-executor/tests/da-matrix/da_matrix.json`
- Test: `opstack-executor/tests/da-matrix/da_matrix_schema_test.cpp`(或复用现有测试 target;校验 schema)

**Interfaces:**
- Consumes: 无(纯数据)
- Produces: `da_matrix.json`(Task 3-6 runner 的输入);schema 校验器确认字段齐全

- [ ] **Step 1: 写 schema 校验失败测试**(opstack-executor/tests 新 target `opstack-da-matrix-schema-tests`,或并入 detail-tests target;若并入 detail-tests 则在 `OpDaMatrixSchemaTest.cpp`)

```cpp
// 校验每个 case 必含: id/slots{1,3,7,8}/envelope_ref/gas/block_time/fork;
// fork ∈ {ecotone,fjord,granite,holocene,isthmus,jovian,karst};
// slots 各 32B hex;envelope_ref 必须在 envelopes 注册表;known_divergence 若存在必须在已知清单。
// (具体 JSON 解析用 repo 已有的 jsoncpp 或 nlohmann;以测试 target 已有依赖为准)
```

- [ ] **Step 2: 建空网格 → 跑测试确认失败**(schema 校验器对缺 case 报错)

- [ ] **Step 3: 写网格**(内容见下;envelope 复用 Task 7/现有 `RollupCostTest.cpp` 的 kEmptyTx/kContractCallTx 字节——见 Task 3 说明复制到网格)

```json
{
  "schema_version": 1,
  "envelopes": {
    "empty_tx": "0xdd80808094095e7baea6a6c7c4c2dfeb977efac326af552d878080808080",
    "contract_call_tx": "0x02f901550a758302df1483be21b88304743f94f80e51afb613d764fa61751affd3313c190a86bb870151bd62fd12adb8e41ef24f3f000000000000000000000000000000000000000000000000000000000000006e000000000000000000000000af88d065e77c8cc2239327c5edb3a432268e5831000000000000000000000000000000000000000000000000000000003c1e50000000000000000000000000000000000000000000000000000000000000000a00000000000000000000000000000000000000000000000000000000000000148c89ed219d02f1a5be012c689b4f5b731827bebe000000000000000000000000c001a033fd89cb37c31b2cba46b6466e040c61fc9b2a3675a7f5f493ebd5ad77c497f8a07cdf65680e238392693019b4092f610222e71b7cec06449cb922b93b6a12744e"
  },
  "cases": [
    {"id":"isthmus_baseline","slots":{"1":"0x000000000000000000000000000000000000000000000000000000003b9aca00","3":"0x0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000000000000003","7":"0x0000000000000000000000000000000000000000000000000000000000989680","8":"0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"},"envelope_ref":"contract_call_tx","gas":1618,"block_time":0,"fork":"isthmus","known_divergence":null},
    {"id":"jovian_baseline","slots":{"1":"0x000000000000000000000000000000000000000000000000000000003b9aca00","3":"0x0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000000000000003","7":"0x0000000000000000000000000000000000000000000000000000000000989680","8":"0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"},"envelope_ref":"contract_call_tx","gas":1618,"block_time":0,"fork":"jovian","known_divergence":null}
  ]
}
```
> 说明:slot3 按 OpFeeParams 布局 `bytes[16,20)=baseFeeScalar(2)@0x02、[20,24)=blobBaseFeeScalar(3)@0x03`。**slot8 基准行必须编码 opScalar=1439103868=0x55c10d3c @[20,24)、opConst=1256417826609331460 @[24,32)**,否则无法复现 op-geth 常量(实现时从钉死十进制值填 hex;上例 slot8 需按此改写,非全零)。`slot1=baseFee=1000e6=0x3b9aca00`、`slot7=blobBaseFee=10e6=0x989680` 已对。其余 5 类(全 max / 溢出 / pre-Isthmus / 缺失参数 / fork 切换)按 spec「网格覆盖 7 类」表补齐。

- [ ] **Step 4: 跑 schema 校验通过**(对完整 7 类网格)

- [ ] **Step 5: 提交**

```bash
git add opstack-executor/tests/da-matrix/
git commit -m "test(da-matrix): JSON grid schema + 7-class coverage"
```

---

### Task 3: FISCO runner(`run_fisco`)+ 基线 `out_fisco.json`

**Files:**
- Create: `opstack-executor/tests/da-matrix/run_fisco.cpp`
- Modify: `opstack-executor/tests/CMakeLists.txt`(新 target)
- Test: 运行 runner 输出 `out_fisco.json`,抽查与手算值一致

**Interfaces:**
- Consumes: `da_matrix.json`、`computeL1Cost/computeChargedOperatorCost/unpackOpFeeParams`(Task 1)、`OpForkSchedule.h` configs
- Produces: `out_fisco.json`(`[{id,l1_cost,operator_cost}]`);CLI `run_fisco [--check opgeth|oprevm|solidity] [--grid PATH] [--out PATH]`

- [ ] **Step 1: 写 runner**(C++17,读 jsoncpp 或 nlohmann——以 opstack-executor 测试 target 现有依赖为准)

```cpp
// run_fisco.cpp 结构:CLI 解析(--grid/--out/--check/--golden)→ 读 da_matrix.json →
// 逐 case:slot hex→evmc::bytes32(unpackOpFeeParams)→OpFeeParams;
// fork→const OpForkConfig& cfg(forkConfigFor(fork) 内部 switch ecotoneConfig()..karstConfig());
// envelope_ref→evmc::bytes_view;gas→uint64。
// l1_cost = computeL1Cost(params, envelope, cfg);
// operator_cost = computeChargedOperatorCost(params, gas, cfg);   // Task 1 门封装
// 输出 out_fisco.json:[{id,l1_cost:"0x..",operator_cost:"0x.."},...]
// --check <end> 模式:读 golden/<end>/out_<end>.json,逐 id 比 known_divergence(跳过并计数);
//   任一分歧 → 打印并 return 1。forkConfigFor 用 Task 7 快照值断言基准行。
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <nlohmann/json.hpp>  // 或 repo 现有 jsoncpp
// ... 完整实现按上述结构;关键调用面已钉死
```

- [ ] **Step 2: CMake 注册**(opstack-executor/tests/CMakeLists.txt,参照现有 detail-tests target)

```cmake
add_executable(opstack-da-matrix-runner da-matrix/run_fisco.cpp)
set_target_properties(opstack-da-matrix-runner PROPERTIES UNITY_BUILD OFF)
target_link_libraries(opstack-da-matrix-runner PRIVATE
    bcos-evm opstack-executor evmone::evmone intx::intx bcos-utilities)
add_test(NAME DaMatrixFiscoBaseline COMMAND opstack-da-matrix-runner
    --grid ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/da_matrix.json
    --out ${CMAKE_CURRENT_BINARY_DIR}/out_fisco.json)
```

- [ ] **Step 3: 跑 runner,抽查基准行**

Run: `cd build/opstack-executor/tests && ./opstack-da-matrix-runner`
Expected: `out_fisco.json` 的 isthmus_baseline 行 operator_cost == `0x116...`(与 op-geth 参考常量 ithmusOperatorFee=1256417826611659930 对拍后确认;先记下 FISCO 值)

- [ ] **Step 4: 提交**

```bash
git add opstack-executor/tests/da-matrix/run_fisco.cpp opstack-executor/tests/CMakeLists.txt
git commit -m "feat(da-matrix): FISCO runner + baseline"
```

---

### Task 4: op-geth runner(Go)+ 快照 + FISCO↔op-geth 对拍

**Files:**
- Create: `opstack-executor/tests/da-matrix/run_opgeth/go.mod`、`run_opgeth/main.go`
- Test: 跑 runner,校验基准行 == op-geth `rollup_cost_test.go` 常量

**Interfaces:**
- Consumes: `da_matrix.json`、本地 op-geth(`/Users/octopus/octo/code/blockchain-impl/op-geth`)
- Produces: `out_opgeth.json`;CI 侧 `run_fisco --check opgeth`(Task 6)

- [ ] **Step 1: go.mod + replace 本地 op-geth**

```
module fisco/da-matrix/opgeth
go 1.23
require github.com/ethereum/go-ethereum v1.101702.2
replace github.com/ethereum/go-ethereum => /Users/octopus/octo/code/blockchain-impl/op-geth
```

- [ ] **Step 2: main.go**(mock StateGetter + fork→ChainConfig + 调 NewL1CostFunc/NewOperatorCostFunc)

```go
package main

import (
    "encoding/json"
    "fmt"
    "math/big"
    "os"
    "github.com/ethereum/go-ethereum/common"
    "github.com/ethereum/go-ethereum/core/types"
    "github.com/ethereum/go-ethereum/params"
    "github.com/holiman/uint256"
)

type stateGetter map[common.Hash]common.Hash

func (s stateGetter) GetState(_ common.Address, key common.Hash) common.Hash { return s[key] }

var l1BlockAddr = common.HexToAddress("0x4200000000000000000000000000000000000015")
var l1BaseFeeSlot = common.BigToHash(big.NewInt(1))
var blobBaseFeeSlot = common.BigToHash(big.NewInt(7))
var gasParamsSlot   = common.BigToHash(big.NewInt(3))
var opFeeParamsSlot = common.BigToHash(big.NewInt(8))

// fork tag → ChainConfig:前置 fork 时间戳全置 0,仅目标 fork 激活(参照 params.OptimismTestConfig)。
// jovian 必须 IsthmusTime 与 JovianTime 同时置 0(IsOptimismIsthmus 只查 IsthmusTime)。
func cfgFor(fork string) *params.ChainConfig { /* ... switch ... */ }

func main() {
    // 读 da_matrix.json;逐 case:
    //   sg := stateGetter{ l1BaseFeeSlot: slot1, gasParamsSlot: slot3, blobBaseFeeSlot: slot7, opFeeParamsSlot: slot8 }
    //   cfg := cfgFor(fork)
    //   l1 := types.NewL1CostFunc(cfg, sg); op := types.NewOperatorCostFunc(cfg, sg)
    //   rcd, _ := types.NewRollupCostData(envelope)  // envelope 来自 envelopes 注册表
    //   l1Cost := l1(rcd, blockTime); opCost := op(gas, blockTime)
    //   输出 {id, l1_cost: l1Cost.String(), operator_cost: opCost.Dec()}
}
```

- [ ] **Step 3: 跑 runner,校验基准行 == op-geth 常量**

Run: `cd opstack-executor/tests/da-matrix/run_opgeth && go run . --grid ../../da_matrix.json`
Expected: isthmus_baseline operator_cost == `1256417826611659930`(ithmusOperatorFee)、jovian_baseline == `1256650673615173860`(jovianOperatorFee);l1_cost fjord 基准 == `3203000`(若含 fjord 基准行)

- [ ] **Step 4: FISCO↔op-geth 对拍**(首次人工;Task 6 自动化)

Run: 比对 `out_fisco.json` 与 `out_opgeth.json` 逐行;任一分歧 → 复查 grid 或登记 known_divergence

- [ ] **Step 5: 提交**

```bash
git add opstack-executor/tests/da-matrix/run_opgeth/
git commit -m "feat(da-matrix): op-geth runner + snapshot"
```

---

### Task 5: op-revm runner(Rust)+ Solidity 参考 + 四端对拍 + known_divergence

**Files:**
- Create: `opstack-executor/tests/da-matrix/run_oprevm/Cargo.toml`、`run_oprevm/src/main.rs`
- Create: `opstack-executor/tests/da-matrix/solidity/OperatorFeeCheck.sol`(forge 测试或脚本)
- Modify: `da_matrix.json`(补 known_divergence 字段)
- Test: 三端快照逐位对拍

**Interfaces:**
- Consumes: 本地 op-revm 20.0.0(`/Users/octopus/octo/code/blockchain-impl/optimism/rust/op-revm`)、本地 contracts-bedrock(`.../optimism/packages/contracts-bedrock/src/L2/GasPriceOracle.sol`)
- Produces: `out_oprevm.json`、Solidity 端输出;`known_divergence` 登记

- [ ] **Step 1: Cargo.toml**(依赖本地 op-revm)

```
[package]
name = "run_oprevm"
version = "0.1.0"
edition = "2021"

[dependencies]
op-revm = { path = "/Users/octopus/octo/code/blockchain-impl/optimism/rust/op-revm" }
serde_json = "1"
```
(以 op-revm 20.0.0 实际 crate 名/feature 为准;参考 `optimism/rust/Cargo.toml`。)

- [ ] **Step 2: main.rs**(op-revm 20.0.0 已钉签名,见下)

```rust
// 依赖:op_revm::L1BlockInfo、op_revm::spec::OpSpecId(本地 optimism/rust/op-revm/src/l1block.rs)。
// 逐 case:
//   let spec = OpSpecId::try_from(fork).unwrap();  // "ecotone".."karst" 均存在(JOVIAN/KARST 在 20.0.0)
//   // 构造 L1BlockInfo:operator_fee_scalar / operator_fee_constant 从网格 slot8 解出,
//   //   da_footprint_gas_scalar 从 slot8 bytes[18,20) 解出;l1_base_fee/blob_base_fee/scalars 同。
//   //   (参照 op_revm l1block.rs 的字段与 fetch_da_footprint_gas_scalar :62 / try_fetch)
//   let l1_cost = info.calculate_tx_l1_cost(&envelope, spec);   // 或 tx_cost(&tx, spec)(L1+operator 求和)
//   let operator_cost = info.operator_fee_charge(gas, spec);     // l1block.rs:174;Jovian×100 在 :184
// 输出 {id, l1_cost: l1_cost.to_string(), operator_cost: operator_cost.to_string()}
// 注意:只用 op-revm 20.0.0(有 JOVIAN/KARST、deposit 0x7E);不用 revm 19.4.0(0x7F 判定错误、无 JOVIAN)。
```

- [ ] **Step 3: Solidity 权威端**——forge 测试调 `GasPriceOracle.getOperatorFee` / `getL1Fee`,同网格输入,输出对拍;注意 `getL1Fee` 吃**未签名 tx + flz+68**,输入需换算(见 spec「Solidity 权威端」)。

- [ ] **Step 4: 四端对拍 + 裁决**

Run: 并排比对 4 份输出;分歧:
- `l1_fee_saturation`(slot1/7 ≥2^256)、`flz_zero_clamp`(空 envelope)、`karst_alias` → 网格对应 case 加 `known_divergence`
- 其余真实分歧 → 复查网格;仍分歧 → 按 DIVERGENCES.md 体系登记,单独立案

- [ ] **Step 5: 提交 4 份快照**(`opstack-executor/tests/da-matrix/golden/{fisco,opgeth,oprevm,solidity}/`)

```bash
git add opstack-executor/tests/da-matrix/
git commit -m "feat(da-matrix): four-source snapshots + known_divergence registry"
```

---

### Task 6: CI 接线(`add_test DaMatrixFiscoCheck` + `--check` 模式)

**Files:**
- Modify: `opstack-executor/tests/CMakeLists.txt`
- Test: `ctest -R DaMatrix` 绿

- [ ] **Step 1: runner 加 `--check` 模式**(run_fisco.cpp):读 golden 快照,逐 case 比 `known_divergence` 跳过并计数,非零分歧 → 非零退出。

- [ ] **Step 2: add_test**

```cmake
add_test(NAME DaMatrixFiscoCheck COMMAND opstack-da-matrix-runner
    --grid ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/da_matrix.json
    --check opgeth
    --golden ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/golden/opgeth)
add_test(NAME DaMatrixFiscoCheckOpRevm COMMAND opstack-da-matrix-runner
    --grid ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/da_matrix.json
    --check oprevm
    --golden ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/golden/oprevm)
```

- [ ] **Step 3: 跑 ctest 确认绿**

Run: `cd build && ctest -R "DaMatrix" --output-on-failure`
Expected: PASS(known_divergence 行跳过并计数)

- [ ] **Step 4: 提交**

```bash
git add opstack-executor/tests/da-matrix/run_fisco.cpp opstack-executor/tests/CMakeLists.txt
git commit -m "feat(da-matrix): CI check mode + add_test"
```

---

### Task 7: A 层单测增补(锚定快照值)

**Files:**
- Modify: `bcos-evm/test/opstack/OpFeeParamsTest.cpp`(+3)、`bcos-evm/test/opstack/RollupCostTest.cpp`(+6)
- Create: `opstack-executor/tests/OpJovianShapeTest.cpp`(+2)
- Test: 各 target 全绿

- [ ] **Step 1: OpFeeParamsTest 极端值解包(+3)**(锚定 Task 5 快照的 slot 值)

```cpp
BOOST_AUTO_TEST_CASE(UnpacksMaxValueScalars) { /* 全 max 槽解包,对照快照 */ }
BOOST_AUTO_TEST_CASE(PackedByteBleedIsolation) { /* [18,20)/[20,24)/[24,32) 相邻不串扰 */ }
BOOST_AUTO_TEST_CASE(MissingAllSlotsZero) { /* 4 槽全零 → 全参 0 */ }
```

- [ ] **Step 2: RollupCostTest 增补(+6)**(锚定快照;经 Task 1 门封装)

```cpp
BOOST_AUTO_TEST_CASE(OperatorFeeMissingParamsZero)  // slot8 全零 → computeChargedOperatorCost == 0(isthmus 下)
BOOST_AUTO_TEST_CASE(OperatorFeeMaxValuesNoWrap)    // gas=u64max,opScalar=u32max,opConst=u64max → Jovian 精确值,无回绕
BOOST_AUTO_TEST_CASE(TotalRollupCostFjordIsthmusJovian) // l1+operator 求和,锚定 op-geth 常量(gas=1618)
BOOST_AUTO_TEST_CASE(OperatorFeeForkSwitchSameInput)     // 同输入切 ecotone/fjord/isthmus/jovian/karst
BOOST_AUTO_TEST_CASE(JovianL1CostBlobScalarAnchor)       // 非平凡 blob scalar 的 L1 cost 绝对锚定
// (OperatorFeeGateOffReturnsZero 已在 Task 1 完成)
```

- [ ] **Step 3: OpJovianShapeTest(+2)**(opstack-executor 新 target 或并入 block-tests)

```cpp
// 锁定 validateJovianBlockShape(std::span<const OpBlockTx>, const OpForkConfig&)(OpBlockExecute.h:76):
BOOST_AUTO_TEST_CASE(ValidateJovianBlockShapeLenSelector)  // 176B/178B + selector 0x098999be/0x3db6be2b
BOOST_AUTO_TEST_CASE(ValidateJovianBlockShapeDaFootprintExtract) // calldata[176:178] da_footprint
```

- [ ] **Step 4: 跑各 target 全绿**

Run: `cd build && ctest -R "BcosEvmOpstack|OpstackExecutor" --output-on-failure`
Expected: 全绿(含新增用例;既有用例不回退)

- [ ] **Step 5: 提交**

```bash
git add bcos-evm/test/opstack/ opstack-executor/tests/
git commit -m "test(da-matrix): A-layer unit tests anchored to snapshots"
```

---

### Task 8: 全量回归 + C 层 t8n 向量(可选)

**Files:**
- Modify: `opstack-executor/tests/t8n/generator/cases.go`(可选,C 层)
- Test: 全量

- [ ] **Step 1: C++ 全量回归**

Run: `cd build && ctest --output-on-failure`
Expected: 全绿(重点 `BcosEvmOpstackTests/OpstackExecutorTests/OpstackExecutorBlockTests/DaMatrixFiscoCheck`)

- [ ] **Step 2: op-e2e 回归**

Run: `cd tools/op-e2e && bash run_all.sh`
Expected: ALL OP-E2E GREEN

- [ ] **Step 3(可选,C 层):t8n 向量加 jovian DA 极端值**(cases.go 加 case + `bash regen.sh` 重生成,复用现有 t8n harness;不与 op-geth 参考常量冲突)

- [ ] **Step 4: 提交**

```bash
git add -A && git commit -m "test(da-matrix): full regression green + optional t8n DA vectors"
```

---

## 自审(对照 spec v2)

| spec 需求 | 对应 Task |
|---|---|
| has_operator_fee 门封装 | Task 1 |
| JSON 网格 7 类 + envelope 注册表 + known_divergence | Task 2 / Task 5 |
| FISCO runner(经门封装,TESTS 域) | Task 3 / Task 6 |
| op-geth runner + mock StateGetter + 快照 | Task 4 |
| op-revm 20.0.0 + Solidity 端 + 四端对拍 | Task 5 |
| known_divergence 跳过表(l1_fee_saturation/flz_zero_clamp/karst_alias) | Task 5 / Task 6 |
| A 层单测(OpFeeParams+3 / RollupCost+6 / shape+2) | Task 7 |
| 裁剪 fork 枚举 + 单独立案 | Task 2(枚举)+ 实现缺口单独立案 |
| CI add_test 挂 ctest(非 tools) | Task 6 |
| 溢出上限 103 bits / 77 bits | Task 7(测试断言) |
| 基准行钉死 op-geth 参数组(gas=1618) | Task 2 |
| 全量回归 + C 层可选 | Task 8 |
