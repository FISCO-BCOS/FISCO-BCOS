# Plan E 实施计划（fork 覆盖补漏）—— 步骤级

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.
>
> 规格源：`docs/plans/2026-09-12-plan-e-fork-coverage-gap-fill-design.md`（v3，只读引用，不复述）。
>
> **判据与验证标准（唯一）**：见 `docs/plans/2026-09-12-plan-e-stage2-impl-r2.md` §0「验证契约」——
> R1 的每个 Task 同样受其约束（查重 → oracle → 断言强度 → 反向验证 → 双段审查 → 归因 → 复核 → 收尾）。
> 本文件是执行版；两者冲突以 v3 设计为准并回头改本文件。

**Goal:** 把 fork 覆盖审查确认缺失的格（EIP-7825/7934、bn256 边界、4788/2537/requests 执行面、
operator fee 收取、DA footprint 累加、Holocene 负向、激活块引擎路径）落成可执行、可反向验证的测试，
并扩展夹具支持任意 fork 的引擎路径 payload。

**Architecture:** 两阶段。**Stage 1 = Task 0（S1–S8 spike/查重）+ 三个零依赖格（E1a/E2/E8）**，
全部代码在本文件内，可立即执行。**Stage 2 = E3/E4/E5/E6/E7 + Part 2（E9–E12）**——它们的
API 与行为正是 Task 0 要定的（evmone 支持面、OP 接线现状、DA 累加现状、7934 角色），
**故作披露式延迟**：Task 0 完成后以本文件 R2 修订补齐其代码块，避免编造未取证的行为。

**Tech Stack:** C++20 / Boost.Test（decorator 单行 + `clang-format off/on`）/ evmone / evmc /
CMake(Ninja) / 外部 oracle（op-geth `e8800cffe`+`d0734fd5`、op-revm `5f90f749ca`）。

> **执行期修订与 Stage 1 结果（2026-09-12，subagent-driven）**
> - **Task 0 ✓** 双审通过；台账 `docs/plans/2026-09-12-plan-e-spike-notes.md`（untracked）。它推翻本计划三处前提：
>   **E8 = +0**（两格已由 `JovianExtraDataTest.cpp:282-284` 的 `execution_payload_extra_data_shape_is_validated`
>   覆盖）；E1a 既有覆盖实为三例（`KarstOrdinaryTxRejectsGasOverEip7825Cap:563`、
>   `KarstEthCallSkipsEip7825MaxGasLimit:586`、`DepositExemptFromEip7825MaxGasLimit:609`）；
>   **E2 标签须限 granite/holocene/isthmus**（Jovian 81984 / Karst 57600，586 对在两者被合法拒绝，F-E2-1）。
> - **E1a ✓** `597bb6d00`：套件 16→**18**、二进制 178→**180**；`state.cpp:385` `>`→`>=` 变异实测 RED 并还原；
>   质量审 Approved（Minor：`jovianCfg` 别名、⑤ 诊断信息、tx 构造块重复——均非阻塞）。
> - **E2 ✓** `48b1bdaa5`：套件 18→**20**、二进制 180→**182**；`V-GRANITE-bn254cap` 经 `run.sh` 复跑
>   `RED as required`（exit 0、还原干净）；质量审 Approved（Minor：mapping why 文字、控制组表述、
>   586 可加 output 断言、helper 命名——均非阻塞）。
> - **E8 = +0**：不建格、无 commit。
> - 未决 R2 事项按台账路由：F-S4-1（evmone 数学失败码 → E2 的坏点探针）、F-S7-1（7934 角色 → 设计修订）、
>   F-E5-1/F-S5-1/F-S6-1/F-S3-1（分别归 E5/E1a/E8/E7 的 R2）。
> - **终审 Ready**（2026-09-12）：2 commits、全量 harness 8/8 RED + 控制组绿 + exit 0、四 target
>   344/334/182/143 全绿、标签切片无泄漏（granite 97 / jovian 114 / karst 130）、无生产代码残留、
>   无覆盖过度声称。终审 4 条 Minor（helper 命名与文件 `make*` 惯例、`jovianCfg` 单调用者、
>   "Granite/Isthmus" 注释措辞未含 holocene、587 仅断言被拒——后者由变体补偿并已在台账记 F-S4-1）
>   全部非阻塞，记录为已接受。

**Worktree:** `/Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal`（分支
`feat/karst-on-318-merged`）。语料 `~/.cache/fisco-t8n-corpus` 只读；不推送；过程文档不入库。

---

## 环境与操作契约（每个 Task 反复自查）

- 新增测试文件后**必须** `cmake -B build -S . -G Ninja ...`（GLOB 配置期展开）；本计划
  只有 Part 2 会新增文件，Stage 1 全部改既有文件。
- 新用例必须带 `fork-<name>` 标签，decorator **必须与声明同物理行**并用
  `// clang-format off/on` 包裹（工具链缺陷，见 commit `20e768419` 的 message）。
- 判定红/绿：`Running N test cases` 计数口径；判红**不得**用 `errors detected` 子串。
- 串行构建；每次 commit 前 `git diff --stat` 必须只剩本任务的文件。
- oracle 纪律：一切外部数值先 `git show <pin>:<path>` 定位再抓；禁止字面量自指。
- 语料只读；任何篡改-还原动作（如 F1 式反向验证）必须前后 `shasum -c` 校验。
- **每个 Task 提交前**更新 `docs/plans/2026-09-12-plan-e-spike-notes.md` 的对应行
  （格 → 结果 → 用例数），使台账始终等于已落地事实。

## 文件结构

**Stage 1 修改**
- `bcos-evm/test/opstack/OpOsakaSemanticsTest.cpp` —— E1a 的边界格（== 上限合法、pre-Osaka 不适用）
  与 **E2 的 bn256 执行边界**。两者都落在这里：该文件持有 evmone/`test::TestState` harness
  （`makeOsakaBlock():77`、预编译 helper `:192`）；**`OpPrecompilesTest.cpp` 保持只做表值断言、
  不动**——它没有 evmone/host（include 仅 TestPrinters + fork schedule + precompiles + jsoncpp），
  执行面代码放那里编译不过（审查 P1）。
- `tools/mutation/variants/V-GRANITE-bn254cap.patch` + `variants/mapping.json` —— E2 的变异变体
- `engine/test/unittests/engine/JovianExtraDataTest.cpp` —— E8 的 Holocene 负向格（查重后）
- `docs/plans/2026-09-12-plan-e-spike-notes.md` —— Task 0 产物（untracked）

**Stage 2（预览，R2 补代码）**
- `bcos-evm/test/opstack/op_geth_oracle.json` + `tools/op-geth-oracle/extract.sh`（新）
- `bcos-evm/test/opstack/Op4788Test.cpp`（新）/ `OpBls2537Test.cpp`（新）/ `OpOperatorFeeChargeTest.cpp`（新）
- `opstack-executor/tests/OpDaFootprintTest.cpp`（新）/ `OpReceiptMetaForkShapeTest.cpp`（新）
- `engine/test/unittests/engine/OpForkActivationBlockTest.cpp`（新，E10）
- `opstack-executor/tests/support/OpForkPayloadFixture.h`（新，E9 fallback 路径）

---

### Task 0: spike 与全量查重（前置，产出台账）

**Files:**
- Create: `docs/plans/2026-09-12-plan-e-spike-notes.md`（untracked，过程文档）

- [ ] **Step 1: S8 全量查重（先于一切新格）**

对 v3 设计里每个拟建格的关键字在测试域 grep，结果落台账「已有覆盖」列：

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal
for k in 'MAX_TX_GAS_LIMIT\|7825' 'MAX_TX_GAS_LIMIT' '112687' '4788\|BEACON_ROOTS' \
         '2537\|bls12\|G1ADD' 'executionRequests\|depositRequest\|6110' \
         'operator_fee_charge\|OperatorFeeVault\|operatorFee' 'daFootprint\|DA footprint' \
         'withdrawalsRoot' 'EIP1559Params\|1559Params'; do
  printf '=== %s ===\n' "$k"; git grep -ln "$k" -- '*Test*.cpp' | head -5
done
```
Expected: 每行列出既有测试文件；**已知先例**：`MAX_TX_GAS_LIMIT` 命中
`OpOsakaSemanticsTest.cpp`（:565-620 已有 `limit+1` 拒绝、`enforce_max_tx_gas=false` 接受、
具名用例 `DepositExemptFromEip7825MaxGasLimit`）——**E1a 据此只补边界剩余格**。
台账必含列：格 → 关键字 → 既有覆盖（文件:行/用例名）→ 决定（新格/已覆盖/降级）。

- [ ] **Step 2: S1 evmone 的 EIP-2537 支持面**

```bash
EV=/Users/octopus/octo/code/FISCO-BCOS/build/vcpkg_installed/arm64-osx/include/evmone
grep -n 'BLS12_381\|bls12' -r "$EV" | head -5; ls "$EV" | head -5   # 版本与支持面
ls /Users/octopus/octo/code/blockchain-impl/op-geth/core/vm/testdata/precompiles/bls*.json
```
Expected: 输出 evmone 的 BLS 符号与 op-geth 的 BLS 向量文件（后者可作 E4 的对照 oracle）。
若 evmone 无 BLS → 台账记「E4 转 finding」，E4 的 R2 代码改为缺陷证明用例。

- [ ] **Step 3: S2 OP 引擎路径是否喂 parentBeaconBlockRoot**

```bash
git grep -n 'parent_beacon_block_root\|setParentBeaconBlockRoot\|parentBeaconBlockRoot' \
  -- engine/bcos-engine opstack-executor/tests bcos-evm/bcos-evm/eth/state | head -10
```
Expected: `OpEngineService.cpp:425` 附近有 `setParentBeaconBlockRoot`（已知）；
台账记「block context 注入点 + 测试可读路径」，供 E3 的 R2 代码直接引用。

- [ ] **Step 4: S3 DA footprint 是否有块级累加**

```bash
git grep -n 'daFootprint\|da_footprint\|FootprintGasScalar\|blobGasUsed' -- \
  bcos-evm/bcos-evm opstack-executor/*.cpp opstack-executor/*.h engine/bcos-engine | head -12
```
Expected: 台账区分「仅 header 槽校验（OpEngineService.cpp:193-199）」vs「有 per-tx 累加实现」。
仅前者 → E7 的 ②③ 格即 F-A2 的证明格，台账标明与 F-A2 的关系。

- [ ] **Step 5: S4 bn256 size-check 拒绝可否与数学失败判别**

```bash
git grep -n 'max_input_size\|MAX_INPUT\|PrecompileOverrides' -- bcos-evm/bcos-evm/opstack | head -10
```
Expected: 找到消费 `max_input_size` 的包装层；读其错误类型（如 distinct halt/error）。
不可判别 → 台账记「E2 上侧只断言被拒 + 残留风险」。

- [ ] **Step 6: S5 EIP-7825 常量三方对账**

```bash
git show e8800cffe53d459cde8a07c8e8f1de9d86e79e07:params/protocol_params.go 2>/dev/null | grep -n 'TxGasLimit\|7825' | head -3
git show 5f90f749caea14398554afb75062f7111b1fc554:rust/op-revm/src/spec.rs 2>/dev/null | grep -n '7825\|MAX_TX' | head -3
sed -n '15,19p' bcos-evm/bcos-evm/eth/state/transaction.hpp
```
Expected: 本仓 `MAX_TX_GAS_LIMIT = 0x1000000`（2**24）；台账记 op-geth/op-revm 侧对应常量名与值。

- [ ] **Step 7: S6 Holocene 时序正文**

```bash
grep -n -A10 'Payload Attributes Processing' \
  /Users/octopus/octo/code/blockchain-impl/optimism/op-ai-obsidian/protocol/holocene/exec-engine.md | head -20
```
Expected: 有正文 → 台账摘录生效规则；无 → 台账记「E8 时序格删除」。

- [ ] **Step 8: S7 EIP-7934 角色定性**

```bash
git show e8800cffe53d459cde8a07c8e8f1de9d86e79e07:core/block_validator.go | sed -n '45,60p'
git grep -rn 'MaxBlockSize\|block.Size()' -- engine opstack-executor bcos-evm/bcos-evm/eth | head -5
```
Expected: op-geth 在区块导入校验；本仓 0 命中 → 判「EL 无需实现」（同 M0 格 11 先例）并写
cannot-determine + 最小读取集；若判「需要」→ 台账记 finding 编号。

- [ ] **Step 9: 提交台账（不入库）**

台账为过程文档：**不 commit**（`docs/**` 永不入库）。确认 `git status --porcelain | grep -v '^??'` 为空。

---

### Task E1a: EIP-7825 上限的**边界剩余格**（bcos-evm-opstack-tests，+2）

**Files:**
- Modify: `bcos-evm/test/opstack/OpOsakaSemanticsTest.cpp`（在 `DepositExemptFromEip7825MaxGasLimit` 附近新增两格）

**已有覆盖（S8 已确认，不重复）**：`limit+1` 拒绝、`enforce_max_tx_gas=false` 接受、
deposit 超限豁免（含 fork-karst 标签）。**本任务只补**：① 恰等于上限**合法**（比较符为 `>` 的
合法侧）② pre-Osaka（Jovian 档）上限**不适用**。

- [ ] **Step 1: 写边界正例（恰等上限合法）**

```cpp
// The cap compares with '>': exactly MAX_TX_GAS_LIMIT is legal. The existing case pins
// limit+1 only, so the legal side of the boundary is the untested half.
// clang-format off
BOOST_AUTO_TEST_CASE(OrdinaryTxAtExactlyEip7825CapIsAccepted, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    test::TestState ts;
    ts[kOsakaSender] = {.nonce = 0, .balance = intx::uint256{1} << 60, .storage = {}, .code = {}};
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kOsakaSender;
    tx.to = 0x0000000000000000000000000000000000001234_address;
    tx.gas_limit = evmone::state::MAX_TX_GAS_LIMIT;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;
    const std::vector<uint8_t> env{0x02, 0x11};
    const auto r = opValidate(
        ts, makeOsakaBlock(), tx, {env.data(), env.size()}, osakaCfg(), OpFeeParams{}, 30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(r));
}
```

- [ ] **Step 2: 写 pre-Osaka 不适用的负向格**

```cpp
// The EIP-7825 cap is Osaka-gated (state.cpp:385 'rev >= EVMC_OSAKA'): before Osaka the
// same over-cap tx must not be rejected BY THAT RULE. The assertion pins the rule's
// non-applicability, not "the whole validation passes" -- other rejection reasons must
// not make this cell red (and must not be misread as an implementation defect).
// clang-format off
BOOST_AUTO_TEST_CASE(OverCapTxIsNotRejectedByEip7825BeforeOsaka, * boost::unit_test::label("fork-jovian"))
// clang-format on
{
    test::TestState ts;
    ts[kOsakaSender] = {.nonce = 0, .balance = intx::uint256{1} << 60, .storage = {}, .code = {}};
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kOsakaSender;
    tx.to = 0x0000000000000000000000000000000000001234_address;
    tx.gas_limit = evmone::state::MAX_TX_GAS_LIMIT + 1;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;
    const std::vector<uint8_t> env{0x02, 0x11};
    const auto r = opValidate(
        ts, makeOsakaBlock(), tx, {env.data(), env.size()}, jovianCfg(), OpFeeParams{}, 30000000);
    if (std::holds_alternative<std::error_code>(r))
    {
        BOOST_CHECK(std::get<std::error_code>(r) !=
                    evmone::state::make_error_code(evmone::state::MAX_GAS_LIMIT_EXCEEDED));
    }
}
```
（Step 1 只需补**一个** helper：`jovianCfg()` 返回 `jovianConfig()`。**不需要**新块 fixture——
执行期侦察实测 `evmone::state::BlockInfo`（`bcos-evm/bcos-evm/eth/state/block.hpp:34-55`）**没有
rev/fork 字段**，fork 全来自 `cfg` 参数，`makeOsakaBlock():77` 可直接复用；原计划的
`makeJovianBlock()` 已删除。）

- [ ] **Step 3: 编译并跑**

```bash
ninja -C build bcos-evm-opstack-tests
./build/bcos-evm/test/bcos-evm-opstack-tests --run_test=OpOsakaSemanticsSuite --log_level=test_suite
```
Expected: 套件用例数 16 → 18，`*** No errors detected`。

- [ ] **Step 4: 反向验证（证伪机制）**

把 `state.cpp:385` 的 `tx.gas_limit > MAX_TX_GAS_LIMIT` 临时改为 `>=`，重编译后
`OrdinaryTxAtExactlyEip7825CapIsAccepted` 必须 RED；还原后 `git diff` 为空且复绿。
（`>=` 变异同时会被既有 `limit+1` 用例覆盖，属预期；本步只证明新正例非恒真。）

- [ ] **Step 5: 报数、提交**

```bash
./build/bcos-evm/test/bcos-evm-opstack-tests 2>&1 | grep -m1 -o 'Running [0-9]* test cases'   # 178 → 180
git add bcos-evm/test/opstack/OpOsakaSemanticsTest.cpp
git commit -m "test(evm): pin the legal side of the EIP-7825 cap and its pre-Osaka non-applicability"
```

---

### Task E2: Granite bn256Pairing 输入上限的执行边界（bcos-evm-opstack-tests，+2）—— **已执行：commit `48b1bdaa5`**

**Files:**
- Modify: `bcos-evm/test/opstack/OpOsakaSemanticsTest.cpp`（**不用** `OpPrecompilesTest.cpp`——该文件
  没有 evmone/host，执行面代码在那里编译不过，见审查 P1）
- Create: `tools/mutation/variants/V-GRANITE-bn254cap.patch`；Modify: `tools/mutation/variants/mapping.json`

**输入构造（硬约束）**：两侧都用**合法曲线点**拼对（每对 192B = G1 64B + G2 128B）；
下侧 586 对 = 112512B ≤ 112687 必须 pairing 成功；上侧 587 对 = 112704B > 112687 必须被拒。
用垃圾字节会让两侧都因数学失败 revert，格失效。

- [ ] **Step 1: 构造"每对自身为单位元"的输入（Task 0 已证伪原方案）**

**Task 0 实测（F-E2-2）**：op-geth `bn256Pairing.json` 的 14 条向量里**没有**"单对 Expected=true"
的记录（`one_point` 1 对期望 **false**；全部 true 向量是 2/3/10 对**互相抵消**）——"挑一个合法对
重复 N 次"不可行。

**实际构造（已执行）**：G1 用**无穷远点**（64 字节全零）+ G2 取 `two_point_match_3`（Expected=true，
index 9）的第二个点（128 字节），每对 `e(O, Q) == 1`。**前提验证**：先用 1 对送达一个临时 probe
并确认成功（probe 已删除、未入库），再放大到 586/587 对。

- [ ] **Step 2: 下侧正例（586 对成功）**

```cpp
// clang-format off
BOOST_AUTO_TEST_CASE(GraniteBn256PairingAt586PairsSucceeds, * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    auto input = repeatInfinityG1Pairs(586);
    // 45_000 + 34_000 * 586 ~= 19.97M gas: the helper default (10M) is too small.
    auto run = runOsakaPrecompileOpTx(ts, vm, kOsakaBn256Pairing, input, graniteCfg(), 30'000'000);
    BOOST_CHECK_EQUAL(run.receipt->status(), 0);
}
```
标签只挂 granite/holocene/isthmus：Jovian 上限 81984、Karst 57600，586 对在两者会被合法拒绝（F-E2-1）。

- [ ] **Step 3: 上侧负例（587 对被拒）**

```cpp
// clang-format off
BOOST_AUTO_TEST_CASE(GraniteBn256PairingAt587PairsIsRejected, * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    auto input = repeatInfinityG1Pairs(587);
    auto run = runOsakaPrecompileOpTx(ts, vm, kOsakaBn256Pairing, input, graniteCfg(), 30'000'000);
    BOOST_CHECK_NE(run.receipt->status(), 0);
}
```
（拒绝原因是否可与数学失败判别 = Task 0 残留 F-S4-1；本例只断言被拒。）

- [ ] **Step 4: helpers（已执行）**

`std::vector<uint8_t> repeatInfinityG1Pairs(size_t n)`：n 对拼接（每对 = 64 字节全零 G1 + 128 字节
G2 常量 `kBn256ValidG2PointHex`），共 192n 字节，并对 G2 常量长度 `BOOST_REQUIRE_EQUAL(..., 128)`；
`graniteCfg()` 返回 `graniteConfig()`（照 `osakaCfg():47`）。

- [ ] **Step 5: 编译、跑、报数（已执行）**

套件 `OpOsakaSemanticsSuite` 18 → **20**；二进制 180 → **182**；`*** No errors detected`。

- [ ] **Step 6: 手工反向验证（已执行）**

把 `kGraniteEntries` 的 0x08 从 `112687` 改为 `112704` → `GraniteBn256PairingAt587PairsIsRejected`
RED（实测：`check run.receipt->status() != 0 has failed [0 == 0]`）；还原后 `git diff` 为空并复绿。

- [ ] **Step 7: 注册变异变体（已执行）**

**只改 `kGraniteEntries`**（与 sibling `V-KARST-blscap` 同形）：

```bash
python3 - <<'EOF'
# One table, one judgement: kIsthmusEntries holds the same 112687 value and the negative
# control asserts the isthmus value -- replacing both turns the control RED ("NOT
# ATTRIBUTED").
p='bcos-evm/bcos-evm/opstack/OpPrecompiles.cpp'; s=open(p).read()
old='{.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 112687},'
new='{.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 112704},'
gi=s.index('constexpr PrecompileOverrides::Entry kGraniteEntries[]')
g_end=s.index('};', gi)
block=s[gi:g_end]
assert block.count(old)==1, block.count(old)
open(p,'w').write(s[:gi]+block.replace(old,new)+s[g_end:]); print('mutated granite only')
EOF
bash tools/mutation/make-variant.sh V-GRANITE-bn254cap bcos-evm/bcos-evm/opstack/OpPrecompiles.cpp
bash tools/mutation/run.sh V-GRANITE-bn254cap   # 实测：RED as required（exit 0）
```
mapping 条目（已入库）：filter `OpOsakaSemanticsSuite/GraniteBn256PairingAt587PairsIsRejected`、
also_green `OpPrecompilesSuite/Bn256PairingInputLimitNoGasOverride`。

- [ ] **Step 8: 提交（已执行）**

commit `48b1bdaa5`（3 文件：测试 + patch + mapping），message
`test(evm): pin the Granite bn256 pairing input cap with aligned valid pairs`。

### Task E8: Holocene/Jovian extraData 的负向剩余格（test-bcos-engine，+0~2）

**Files:**
- Modify: `engine/test/unittests/engine/JovianExtraDataTest.cpp`

**S8 已确认的既有覆盖（不重复）**：`wrong_length_eip1559_params_are_rejected`、
`mixed_zero_eip1559_params_are_rejected`、`min_base_fee_without_params_is_rejected`、
`eip1559_fields_are_rejected_below_version_three`。

- [ ] **Step 1: 查重后确认剩余格（决定 +0 还是 +2）**

```bash
grep -n 'version' engine/test/unittests/engine/JovianExtraDataTest.cpp | head -12
git grep -n 'validationError\|reject' engine/test/unittests/engine/JovianExtraDataTest.cpp | head -8
```
判据：spec `holocene/exec-engine.md` 的三条硬规则中，**解码/校验侧**的
(a) `version != 0`（Holocene 档）、(b) Jovian 档 `version != 1` 是否已有用例。
**Step 1 的产出还须写回本计划**：把实测的确切入口签名（函数名 + 参数 + 返回 optional/throw）
与既有覆盖清单补进本文件（R2），使第二次执行无需再猜（审查 P8）。
两者都已覆盖 → **本任务不建格**，把结论写入台账与 commit message（`+0`，诚实记零）。
仅缺其一 → 按 Step 2/3 补对应格。

- [ ] **Step 2: 写 (a) Holocene 档 version≠0 拒绝**（若 Step 1 判缺）

```cpp
// Spec: Holocene header extraData version MUST be 0. Encode side is covered; this pins
// the decode/validate side against a version the CL must never send.
// clang-format off
BOOST_AUTO_TEST_CASE(holocene_extra_data_with_nonzero_version_is_rejected, * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    bytes const wrongVersion{0x07, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0x06};
    auto const result = engine::detail::decodeEip1559Params(wrongVersion);   // 以实际入口为准，Step 1 已定位
    BOOST_CHECK(!result.has_value());
}
```

- [ ] **Step 3: 写 (b) Jovian 档 version≠1 拒绝**（若 Step 1 判缺）

```cpp
// clang-format off
BOOST_AUTO_TEST_CASE(jovian_extra_data_with_wrong_version_is_rejected, * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    bytes const holoceneVersion{0x00, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0x06,
        0, 0, 0, 0, 0, 0, 0, 0};
    auto const result = engine::detail::decodeOptimismExtraData(holoceneVersion);  // 以实际入口为准
    BOOST_CHECK(!result.has_value());
}
```
（两个入口名以 Step 1 的 grep 结果为准——**不得**猜测符号名；Step 1 必须回报确切签名。）

- [ ] **Step 4: 编译、跑、报数；反向验证（把校验的版本比较放宽 → 新格必红 → 还原）**

```bash
ninja -C build test-bcos-engine
./build/engine/test/test-bcos-engine --run_test=JovianExtraDataTest --log_level=test_suite
./build/engine/test/test-bcos-engine 2>&1 | grep -m1 -o 'Running [0-9]* test cases'   # 344 → 344~346
```

- [ ] **Step 5: 提交**

```bash
git add engine/test/unittests/engine/JovianExtraDataTest.cpp
git commit -m "test(engine): pin the remaining Holocene/Jovian extraData negative cells"
```

---

### Stage 2（R2 补代码）—— 每个 Task 的契约与步骤骨架

> **为什么延迟**：以下 Task 的断言形态取决于 Task 0 的结论（S1–S7）——现在写代码块就是编造。
> Task 0 完成后，本文件另起 R2 修订，为每个 Task 补齐「Step: 写测试（完整代码）」等步骤。
> 每个 Task 的 Files/Behavior/Oracle/格数/反向验证/提交信息**在此锁定**，R2 只补代码。

**共同步骤骨架**（R2 展开时每个 Task 都按此序列，逐步附完整代码）：
1. 查重（引用 Task 0 台账对应行）→ 2. 写第一个失败用例 → 3. 编译并跑（Expected: 具体失败
形态）→ 4. 写第二个用例 → 5. 全绿 + 报数（含 +N）→ 6. 反向验证（变异/篡改，红→还原→绿）
→ 7. 提交（提交信息已在下表锁定）。

| Task | Files（锁定） | Behavior（锁定） | Oracle（锁定） | 格数/增量 | 反向验证 | commit message |
|---|---|---|---|---|---|---|
| **E3** | `bcos-evm/test/opstack/Op4788Test.cpp`（新；CMake 重配）。**R2 首步**：引用 S2 结论 + 一条可执行检查，确认本 target 的执行路径会分派到系统合约（`system_contracts.cpp` 在 bcos-evm 库内，链接大概率无碍；风险在**host 接线**） | ① Ecotone+ 读系统合约得 payload 的 beacon root ② pre-Ecotone 无合约/空返回 ③ **未记录时间戳 → 空返回** | EIP-4788 + 本仓 `system_contracts.cpp:35-37`；S2 定的注入点 | 3 / +3 | 把注入点断掉（不设 parent_beacon_block_root）→ ① 必红 | `test(evm): wire the EIP-4788 beacon roots system contract on the Ecotone+ path` |
| **E4** | `bcos-evm/test/opstack/OpBls2537Test.cpp`（新） | 0x0b G1ADD（单位元/有效对）、0x0c G1MSM、0x0d G2ADD、0x0f PAIRING | ethereum/EIP-2537 `tests/` 向量或 execution-spec-tests fixtures（pin 进 `op_geth_oracle.json` 的 `vectors`）；对照 op-geth `testdata/precompiles/bls*.json` | 5 / +5 | 篡改一个向量字节 → 对应格必红 | `test(evm): exercise the EIP-2537 BLS precompiles on the Isthmus+ path` |
| **E5** | `engine/test/unittests/engine/OpEngineRequestsSemanticsTest.cpp`（**新文件**，锁定；需 CMake 重配 + SKIP_UNITY） | ① OP 头 `requestsHash` == `c_emptyRequestsHash`（盖章点 `OpEngineService.cpp:429`）② 非空 requests 拒绝（先查重 `OpEngineReviewFixTest`）③ 6110 排除语义（按 S3/查重结果定级） | spec `isthmus/exec-engine.md`；`Constants.h:30-34` | 2~3 / +2~3 | 把盖章点改成 h256{} → ① 必红 | `test(engine): pin the empty-requests hash and exclusion semantics on the OP path` |
| **E6** | `bcos-evm/test/opstack/OpOperatorFeeChargeTest.cpp`（新）+ `variants/V-OPERATORFEE-formula.patch` 与 mapping 条目（**v3 验收项**：变异进 harness 且 RED） | ① Isthmus scalar-only ② constant+scalar ③ Jovian 公式差 ④ 零参数零收费 ⑤ 未用 gas 退款 ⑥ 受益账户 = OperatorFeeVault | op-revm `l1block.rs:174-201`（**扩展** `op_revm_oracle.json` 的 `operator_fee_*`）+ op-geth `protocol_params.go` | 6 / +6 | 改公式常量（如 scalar 除子从 1e6 改 1e3）→ ①③ 必红 | `test(evm): charge and refund the operator fee per the op-revm formula` |
| **E7** | `opstack-executor/tests/OpDaFootprintTest.cpp`（新） | ① per-tx 足迹公式（Fjord 常量）② 累加 `< gasLimit` 有效 ③ `== gasLimit` **拒绝** ④ baseFee 用 `max(gasUsed, blobGasUsed)` ⑤ deposit-only 块足迹 0 —— ②③ 同时是 **F-A2** 的证明格 | `jovian/exec-engine.md` 伪码 + Fjord 常量 + op-geth（语料 pin 已含 Jovian） | 5 / +5 | 把 `==` 判为有效 → ③ 必红 | `test(executor): pin the Jovian DA footprint sum, its cap boundary and the base-fee coupling` |
| **E9** | `opstack-executor/tests/support/OpForkPayloadFixture.h`（**fallback**，新）；若要改共享头 `OpEngineKarstTestHarness.h` → **停下问用户** | `byFork(OpForkId)` / `byTimestamp(uint64_t)` 两工厂；payload 形状复用 M2 `clShapedPayload` 归一化 | M2 形状基线 + 现有夹具 | 夹具 / 0 | 既有引擎路径测试零回归 | `test(executor): fork-parameterised payload fixture for engine-path tests` |
| **E10** | `engine/test/unittests/engine/OpForkActivationBlockTest.cpp`（新） | Isthmus/Canyon/Holocene 激活块 ±1（含 Isthmus withdrawalsRoot 三态）+ Fjord/Granite 正向与反向 | `OpEngineService.cpp:96-146` + specs 各 fork 节 | 15 / +15 | 把某 fork 的窗口判定改早/改晚一块 → 对应 ±1 格必红 | `test(engine): sweep fork activation blocks through the engine path` |
| **E11** | `opstack-executor/tests/OpL1InfoDepositForkShapeTest.cpp`（新） | ① Ecotone ② Isthmus ③ Jovian 逐字节对拍 ④ Fjord/Granite 负向（无附加字段） | `{ecotone,isthmus,jovian}/l1-attributes.md` + op-node `l1_block_info.go` | 4 / +4 | 改打包字节序 → ①②③ 必红 | `test(executor): byte-pin the per-fork L1 attributes deposit layout` |
| **E12** | `opstack-executor/tests/OpReceiptMetaForkShapeTest.cpp`（新，**执行层**） | ① Canyon depositNonce ② Ecotone scalar 组切换 ③ Isthmus operatorFee ④ Jovian DA scalar | op-geth `gen_receipt_json.go`/`receipt_opstack.go` + specs | 4 / +4 | 把 meta 形状回退一档 → 对应格必红 | `test(executor): pin the per-fork receipt meta shape at the execution layer` |

---

## Self-Review（对照 v3 设计逐条核查）

1. **Spec 覆盖**：v3 的 E1a/E1b/E2/E3/E4/E5/E6/E7/E8/E9/E10/E11/E12 全部有落点——Stage 1 三个
   Task 有完整代码；Stage 2 九个 Task 契约锁定、代码按披露式延迟（理由在节首）。E1b 为 0 格定性
   Task，落在 Task 0 的 S7。
2. **Placeholder 扫描**：无 TBD/TODO；Stage 2 的"以实际入口为准"条目均绑定 Step 1 的**具体 grep
   命令**与"必须回报确切签名"的约束，不是留白；所有新增 helper 都给了签名与语义。
3. **类型/命名一致性**：`runOsakaPrecompileOpTx(ts, vm, precompile, input, cfg, gasLimit=10'000'000)`
   （`OpOsakaSemanticsTest.cpp:192` 实测签名）在 E2 使用一致；`opValidate(ts, block, tx, env, cfg,
   feeParams, gasLimit[, policy])` 形态与 `:578-604` 实测一致；`graniteCfg()/jovianCfg()` 定义为
   `osakaCfg()` 同类（`OpForkSchedule.h` 导出 `graniteConfig()/jovianConfig()`）；标签命名沿用
   `fork-<name>`；提交信息里 oracle 文件名统一为 `op_geth_oracle.json` / `op_revm_oracle.json`。
4. **风险登记**：E1a 的 `>=` 变异会同时命中既有 `limit+1` 用例（Step 4 已声明为预期）；
   E2 上侧拒绝原因不可判别时按 S4 残留处理；E8 允许 +0（诚实记零）。
5. **审查 P1–P9 的落地**：E2 落点改为 `OpOsakaSemanticsTest.cpp`（P1）并在 Step 1 强制验证
   "单对自身为单位元"（P2）；E1a 第二格改为"不得因 7825 规则失败"的断言 + 自建 jovian 块/配置
   fixture（P3）；E2/E6 的变异变体落成注册项（P4）；E3 的 R2 首步含接线检查（P5）；Task 0 S1
   命令改为可移植形式（P6）；E5 落点锁定新文件（P7）；E8 要求把实测签名写回（P8）；
   台账更新列为每个 Task 的提交前动作（P9）。
