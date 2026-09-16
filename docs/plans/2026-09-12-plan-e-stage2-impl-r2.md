# Plan E Stage 2 实施计划（R2）—— 步骤级

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.
>
> 规格源：`docs/plans/2026-09-12-plan-e-stage2-design.md`（设计）+ `docs/plans/2026-09-12-plan-e-spike-notes.md`
> （Task 0 台账）+ **本文件 Task R2-0 的 Stage-2 查重结果**（后者优先）。冲突以查重结果为准并回头改本文件。
> 本文件是 Stage 2 的执行版；Stage 1 的 R1 计划（`...-impl.md`）保持归档。

**Goal:** 补齐 Stage 2 的 fork 覆盖缺口——EIP-4788 接线、EIP-2537 BLS 执行面、OP requests
恒空语义、operator fee 残格、Jovian DA footprint 边界与等值缺陷格——并在独立夹具头上打开
激活块引擎路径与逐 fork L1Info/收据形状。

**Architecture:** 先 **R2-0 全量查重**（Stage 2 的格逐一定位既有覆盖——本轮侦察已发现 E6 的 ③⑤
与 E7 的 `>` 拒绝格很可能已被覆盖），再只对**确认缺失**的格写代码。零依赖格（E4/E6-residual/E7-residual）
在前；夹具链（E9 → E10 → E11 → E12）在后。E7 的**等值校验格**是期望 RED 的缺陷证明格（F-A2），
不随红灯改期望。

**Tech Stack:** C++20 / Boost.Test（decorator 单行 + `clang-format off/on`）/ evmone 0.21.0 / evmc /
CMake(Ninja) / oracle：op-geth `e8800cffe`（`core/block_validator.go:119-132`、`core/vm/testdata/precompiles/bls*.json`）、
op-revm `5f90f749ca`、specs（`jovian/exec-engine.md`、`isthmus/exec-engine.md`）。

**Worktree:** `merge-318-rehearsal`（`feat/karst-on-318-merged`）。基线 = Stage 1 终态
（HEAD `48b1bdaa5`，计数 engine 344 / rpc 334 / evm 182 / executor 143）。语料只读；不推送；
`docs/**` 永不入库；新用例必须带 `fork-<name>` 标签。

> **WI-24 裁定（2026-09-12，spec 审查 Approved）**：Stage 2 最终格数 = **净增 9 格**
> （E3 +2 / E7 +3 / WI-33 +2 / WI-34 +2）。**E4 +0**（corpus BLS 向量含输出字节值级断言，
> `OpT8nReplayTest.cpp:1137-1140`，建格取消——F-E4-1 收口）；**E5/E6/E10/E11/E12 = +0**（既有覆盖，
> 台账 Stage 2 表逐格有用例名）；**E9 夹具取消**（E10–E12 为 +0 后无消费者）。各 Task 的执行细节
> 以下文为准，但**格数与落点以台账 Stage 2 表为最终裁定**。
>
> **本计划的每个 Task 都受 §0「验证契约」约束**：Task 未填齐契约要求的验证块即视为未完成；
> R1（Stage 1 计划）与 Stage 2 设计文档同样引用本节，作为**唯一**的判据与验证标准。

---

## §0 验证契约（Verification Contract）—— 每个 Task 的强制部分

**为什么存在**：判据正确性无法"确保"，只能靠四道互相独立的闸门交叉，并对残留显式登记。
本会话已发生的 6 个"钉错行为/假覆盖"实例（E2 的 5 标签、112687 非对齐、E1a 零断言、E8 误判待建、
F2 影响夸大、F-S3-1 边界方向误判）**全部**是这些闸门发现的。以下触发点是强制的。

### 0.1 单格生命周期（触发 → 手段 → 产物 → 出口判据）

| # | 触发信号 | 强制手段 | 产物（可复核） | 出口判据 |
|---|---|---|---|---|
| 1 | 拟建任何格 | **用例名级查重**：grep + **读断言本身**（不是"有名字就算覆盖"） | 查重表行（格→关键字→既有覆盖含**用例名**→决定） | 未查重**不得**开格 |
| 2 | 写下一条断言 | oracle 阶梯选参照；**双侧边界**；值级/存在级/不变量级分清 | 断言旁的可引用依据（`<file>:<line>` 或 oracle JSON 键） | 能回答"凭什么期望它、为什么只测这一侧" |
| 3 | 用例**首次变绿** | **反向验证**：变异或篡改被测代码/数据 | RED 原始输出 + 还原后 `git diff` 空 | 判据被证明非恒真（探针落空要记录并重做） |
| 4 | 每个 Task 收尾 | **spec 审查**（fresh agent，明文"不信任实现者报告"）→ 通过后 **质量审查** | findings 列表 + 逐条处置 | 两步都 ✅；有 findings 则修完复审 |
| 5 | 一批 Task 收尾 | **终审** + 全量 harness + 四 target 计数 | 8→9 变体全 RED、控制组绿、计数只增不减、无残留 | Ready / Not ready（写明边界） |
| 6 | 任何新断言**变红** | 归因三选一：M0 台账／`DIVERGENCES.md`／**新 finding** | finding 编号 + 定性（含"是否被 header hash 兜住"类判断） | **禁止改期望表让它变绿** |
| 7 | 引入外部 oracle | **可再生**：`extract.sh` + pin 记录；**缺席语义**：入库契约=硬失败／语料=本地 skip+CI 硬失败 | 脚本 + JSON（含 repo/commit/文件清单）+ 重跑同字节 | 第三人可独立复算 |
| 8 | 自产 finding/声称 | **独立复核**（把 claim 当假设去证伪，优先试着推翻它） | verification JSON（复现命令 + expected/observed） | confirmed / refuted / undecidable 三选一 |
| 9 | 修复落地 | 按原 finding 的分类**重跑整段清单**（不是只确认机制存在）+ 移除期望-RED 的 decorator | 修前 RED / 修后 GREEN 双向证据 | 闭合；decorator 未移除即未闭合 |
| 10 | 新 fork/新 spec | 覆盖枚举（fork × 特性族矩阵 + label 切片） | 缺口清单（含 owner 与关闭条件） | 无静默空白 |

### 0.2 Task 验证块（执行期由实现者填写并随 Task 报告提交；缺项即该 Task 未完成）

**时点**：块内的证据（RED 输出、计数、台账行）只有在实现之后才存在，故**不在计划期预写**；
实现者在 Task 报告里给出该块，并把它回填到 `...-spike-notes.md`。R2-Z Step 1a 逐 Task 校验完整性。

```markdown
### 验证（Verification）
- 查重证据：<格 → 既有覆盖（用例名/文件:行）→ 决定>
- oracle 引用：<file:line 或 oracle JSON 键 + pin>
- 断言强度：<值级/存在级/不变量级；双侧边界分别是哪两侧>
- 反向验证：<变异/篡改方式 → 期望哪一格 RED → 实测输出摘要 → 还原后 git diff 空>
- 预期 RED 项（若有）：<哪一格、为什么它是交付物、用什么机制不破坏绿门（如 expected_failures）>
- 计数增量：<target +N；套件 N→M；二进制 N→M>
- 台账回填：<`...-spike-notes.md` 中回填的行>
```

### 0.3 失效模式 checklist（写测试前逐条自问；本会话均为实证）

1. **自指**：期望值由被测代码自己产生？（禁止；review 规则 #38）
2. **存在级冒充值级**：只比 `has_value()`/size 而代码写的是值？（F2）
3. **零断言执行**：通过路径下一条断言都不跑？（E1a 的 pre-Osaka 格）
4. **单侧边界**：只测 `+1` 不测 `==`；只测大输入被拒不测对界成功？（E1a/E2/E7）
5. **别名/键不一致**：JSON 键 `expected` vs 字段 `expectedHex`、裸 hex vs `0x` 前缀（N2/N5）
6. **oracle 不可达/不可再生**：gitignored、无脚本、pin 不记（#38；R2-3/R2-4）
7. **探针落空**：变异值与 fixture 巧合相同（F2 的 `64×'9'`）→ 必须复核变异是否真的改变了行为
8. **编译期拦截**：变异被 `static_assert` 拦下（V-M7）→ 改成"judgement 整体反转"再验运行时判据
9. **关键字选错导致假查重**：用 7934 查 7825 漏判（E1a）→ 关键字须含 EIP 号 + 语义词 + 实现符号
10. **固定点漂移**：head 变了行号失效 → 引用一律 `git show <pin>:<path>`，结论只对 pin 负责

### 0.4 归因与残留纪律

- 红 = 发现，不是障碍。三步定性：① 查 M0 台账 ② 查 `DIVERGENCES.md` ③ 都不是 → 立 finding（编号 + 定性 + owner）。
- **禁止**为了让测试变绿而改期望表；期望只能因"oracle 更新"或"已授权修复"而变，且必须在同一 commit 内说明依据。
- 残留（未覆盖输入域/oracle 局限/期望-RED 项）必须**具名登记**（缺什么、owner、关闭条件），不得只留在对话里。

### 0.5 手段自身的保障（不靠自觉）

| 手段 | 保障方式 |
|---|---|
| 查重 | 表格强制含**用例名**；R2-0 为前置门 |
| oracle | 脚本可再生 + pin；入库契约缺席即 `BOOST_REQUIRE` 失败 |
| 反向验证 | harness 自带 `restore` 与**脏树 gate**；探针必须实测 RED |
| 审查 | fresh agent + "不信任报告" + 关键命令**独立重跑** |
| 复核 | verification JSON 经 `validate_verification.py --repo` 校验（行号/代码引用/字段） |
| 全局 | pre-commit clang-format；GLOB 变更必重配；计数口径 `Running N test cases` |

## Task R2-0: Stage 2 全量查重（前置门，产出台账增补）

**Files:** Modify `docs/plans/2026-09-12-plan-e-spike-notes.md`（untracked）

**本轮控制器侦察已命中的既有覆盖（须逐条复核，不要盲目采信）**：
- E6 ③⑤ 疑似已覆盖：`OpTransitionTest.cpp` 的 `JovianReceiptMetaAndOperatorFormula`(:246)、
  `OperatorFeeConservesWhenCfgDisagreesWithProps`(:435)、`RoutesFeesToFourVaults`(:44)、
  `L1CostIsDebitedFromSenderAndConserves`(:583)
- E7 的 `>` 拒绝格疑似已覆盖：`OpL1EdgeGateTest.cpp` 头部注释 B-5b（"blobGasUsed exceeds gasLimit →
  INVALID in Step 2 static validation"）
- E4 的 Isthmus 档调用形态：`Op7702Test.cpp:88/101`、`OpFloorGasTest.cpp:87` 使用 `isthmusConfig()`

- [ ] **Step 1: 逐格查重（Stage 2 全集）**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal
for k in 'BEACON_ROOTS\|4788' 'BLS12\|bls\|0x0b' 'executionRequests\|6110\|requestsHash' \
         'operatorFee\|OperatorFeeVault\|operator_fee' 'daFootprint\|blobGasUsed\|DA footprint' \
         'withdrawalsRoot\|MessagePasser' 'l1-attributes\|L1Info\|l1Info' \
         'depositNonce\|l1BaseFeeScalar\|daFootprintGasScalar' \
         'gasMetered\|max(gasUsed' 'deposits-only\|blobGasUsed == 0\|blobGasUsed, 0'; do
  printf '\n=== %s ===\n' "$k"; git grep -n "$k" -- '*Test*.cpp' | head -8
done
```
产出：台账新增「Stage 2 查重」表，列为 格 → 关键字 → 既有覆盖（**用例名 + 文件:行**）→ 决定
（新格 / 已覆盖 / 降级 / 转 finding）。**每行必须有用例名**（Stage 1 的台账审查教训）。

- [ ] **Step 2: 读三个关键既有用例，判定其断言强度（不是"有名字就算覆盖"）**

```bash
sed -n '44,105p;246,300p;435,470p' bcos-evm/test/opstack/OpTransitionTest.cpp   # E6 的费用/vault/公式
sed -n '/B-5b/,/^}/p' opstack-executor/tests/OpL1EdgeGateTest.cpp | head -60   # E7 的 '>' 拒绝
```
逐条判：① 断言的**不变量**是什么（比值？vault 余额等式？status？）② 是否覆盖 Stage 2 设计里的
目标格（如 E6-⑤ 需要"未用 gas 的退款"这一**具体**不变量，不只是"守恒"）。

- [ ] **Step 3: 输出 Stage 2 的最终格数表**（本计划的后续 Task 据此增删）

预期（待 Step 1/2 确认）：E6 可能 **+0~1**（若"退款"无独立断言则 +1）；E7 的 `>` 格 **+0**、
剩余 `==` 合法 / 等值缺陷 / baseFee max / deposit-only **+4**；E5 可能 **+0~1**；E10 视三态缺口。

- [ ] **Step 4: 不提交**（过程文档）；确认 `git status --porcelain | grep -v '^??'` 为空。

---

## Task E4: EIP-2537 BLS12-381 执行面 —— **已取消（+0，WI-24 裁定）**

> 收口依据：corpus `isthmus_precompile_bls_*` 四向量在 `OpT8nReplayTest.cpp:1137-1140` 逐值断言 output，
> 可达性与 RequiredGas 亦被 corpus 证明（台账 F-E4-1）。本节保留为历史设计，不再执行。

**Files:**
- Modify: `bcos-evm/test/opstack/OpOsakaSemanticsTest.cpp`（复用其 evmone/`test::TestState` harness；
  **不新增测试文件**，避免 cmake 重配）
- Create: `bcos-evm/test/opstack/op_geth_oracle.json`（**入库契约**：随代码提交，缺席即失败——
  与 `op_revm_oracle.json` 的既有语义一致）
- Create: `tools/op-geth-oracle/extract.sh`（**可再生**：从 op-geth pin 抽取向量的唯一入口）

**已知风险（本 Task 的第一要务是证伪它）**：override 表**只列** `0x08 / P256 / 0x0c / 0x0e`
（`OpPrecompiles.cpp:26-31`），**0x0b / 0x0d / 0x0f / 0x10 / 0x11 不在任何表里**；计划唯一依赖是
`OpHost.cpp:131-132` 的**注释主张**——"Length-limit-only (0x08 / BLS): fall back to the base class"。
Task 0 的 S1 只证明 evmone **有** `bls.hpp`，**未证明本仓 host 能路由** 这些地址。

- [ ] **Step 0（门）：0x0b 可达性探针——失败则整个 Task 降级**

```bash
OPG=/Users/octopus/octo/code/blockchain-impl/op-geth
git -C "$OPG" show e8800cffe53d459cde8a07c8e8f1de9d86e79e07:core/vm/testdata/precompiles/blsG1Add.json \
  | python3 -c "import json,sys;d=json.load(sys.stdin);print(len(d));[print(i,v.get('Name',''),len(v['Input'])//2) for i,v in enumerate(d[:5])]"
```
写**一个临时用例**（不入库）：用其中**最短的合法向量**调 `0x0b`（`isthmusCfg()`，gasLimit 给足），
要求 `status()==0` 且 output 与 `Expected` 一致。
- **通过** → 删临时用例，继续 Step 1（并把"可达"结论写进台账）。
- **失败** → **停止**：记录 finding（"EL 未接 EIP-2537 地址分派"，附 evmone 0.21.0 有实现的对照），
  E4 降级为缺陷证明（0~1 格，期望 RED），**不得**继续写 5 个格。

- [ ] **Step 1: 写 `extract.sh` 并生成 oracle（R2-3/R2-4）**

```bash
cat > tools/op-geth-oracle/extract.sh <<'SH'
#!/usr/bin/env bash
# Regenerate bcos-evm/test/opstack/op_geth_oracle.json from the pinned op-geth checkout.
# The BLS vectors are judged by op-geth's own testdata, not by this repo's implementation.
set -euo pipefail
OP_GETH_REPO="${OP_GETH_REPO:-/Users/octopus/octo/code/blockchain-impl/op-geth}"
PIN="${OP_GETH_PIN:-e8800cffe53d459cde8a07c8e8f1de9d86e79e07}"
OUT="${1:-$(cd "$(dirname "$0")/../.." && pwd -P)/bcos-evm/test/opstack/op_geth_oracle.json}"
python3 - "$OP_GETH_REPO" "$PIN" "$OUT" <<'PY'
import json,subprocess,sys
repo,pin,out=sys.argv[1],sys.argv[2],sys.argv[3]
files={"0x0b":"blsG1Add","0x0c":"blsG1MultiExp","0x0d":"blsG2Add","0x0f":"blsPairing"}
vecs=[]
for addr,name in files.items():
    raw=subprocess.run(["git","-C",repo,"show",f"{pin}:core/vm/testdata/precompiles/{name}.json"],capture_output=True,text=True,check=True).stdout
    for i,e in enumerate(json.loads(raw)):
        vecs.append({"address":addr,"file":name+".json","name":e.get("Name",f"#{i}"),
                     "input":e["Input"],"expected":e["Expected"]})
json.dump({"source":{"repo":"blockchain-impl/op-geth","commit":pin,
                     "files":[f"core/vm/testdata/precompiles/{n}.json" for n in files.values()]},
           "vectors":vecs}, open(out,"w"), indent=2)
print("wrote",out,len(vecs),"vectors")
PY
SH
chmod +x tools/op-geth-oracle/extract.sh && ./tools/op-geth-oracle/extract.sh
```
Expected: `wrote ... N vectors`（N ≥ 12）；JSON 记录 repo/commit/文件清单。
**hex 形态（N2，硬约束）**：op-geth 的 `Input`/`Expected` 是**裸 hex（无 `0x` 前缀）**——
实测 `blsG1Add.json` 首条 `"Input": "0000…"`。读取端按裸 hex 解码，比较端也统一为裸 hex
（仓内 `toHex*` 多带前缀，必须显式去/补，不得直接比）。

- [ ] **Step 2: 写 5 格——**用例名由向量名决定，不得预设语义（R2-2）**

每个向量按 `loadOpGethBlsVector("<file>.json", "<name>")` 取输入与期望，断言统一为
**status 0 + output 字节 == Expected**；用例名含**该向量的实际 name**（Step 1 输出为准）：

| 格 | 地址 | 来源文件 | 选法 |
|---|---|---|---|
| 1 | `0x0b` | `blsG1Add.json` | 最短合法向量（name 由文件决定） |
| 2 | `0x0b` | `blsG1Add.json` | 次短合法向量（**不假设 identity 语义**） |
| 3 | `0x0c` | `blsG1MultiExp.json` | 最短合法向量 |
| 4 | `0x0d` | `blsG2Add.json` | 最短合法向量 |
| 5 | `0x0f` | `blsPairing.json` | 最短合法向量 |

```cpp
// Vector pinned in op_geth_oracle.json (regenerated by tools/op-geth-oracle/extract.sh).
// The case name embeds the vector's own name -- this test pins <name> only.
// clang-format off
BOOST_AUTO_TEST_CASE(IsthmusBlsG1AddVector_<name>, * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    auto const v = loadOpGethBlsVector("blsG1Add.json", "<name>");
    auto run = runOsakaPrecompileOpTx(ts, vm, evmc::address{0x0b}, v.input, isthmusCfg(), 30'000'000);
    BOOST_REQUIRE_EQUAL(run.receipt->status(), 0);
    // Output comparison uses THIS file's existing idiom (check how existing cases compare
    // receipt output; if none exists, hex-encode the output). Both sides are BARE hex --
    // the oracle stores no 0x prefix (see Step 1), so strip any prefix the encoder adds.
    BOOST_CHECK_EQUAL(outputHexOf(run), v.expectedHex);   // 裸 hex 对裸 hex
}
```

- [ ] **Step 3: helpers**

三个 helper，签名与 JSON 键的映射写死（N5）：
- `const OpForkConfig& isthmusCfg()` —— 返回 `isthmusConfig()`，照 `osakaCfg():47`。
- `BlsVector loadOpGethBlsVector(std::string_view file, std::string_view name)`，其中
  `struct BlsVector { std::vector<uint8_t> input; std::string expectedHex; };` —— 读
  `op_geth_oracle.json` 的 `vectors[]`，按 `file`+`name` 查找，**JSON 键 `input`/`expected`
  映射为 `input`/`expectedHex`（均裸 hex，无 0x；`input` 按裸 hex 解码为字节）**；
  **入库契约缺席即 `BOOST_REQUIRE` 失败**（与 `op_revm_oracle.json` 在 `OpPrecompilesTest` 同语义）。
- `std::string outputHexOf(OsakaTxRun const&)` —— 用**该文件既有的输出比较习惯**实现
  （R2-7：不得臆造 `toHex(receipt->output())`，实现前先读既有 output 断言），返回**裸 hex**
  以与 `expectedHex` 同形。

- [ ] **Step 4: 编译、跑、报数**：套件 20 → **25**；二进制 182 → **187**；`No errors detected`。

- [ ] **Step 5: 反向验证**：把 JSON 中 G1ADD 向量的 `expected` 首字节改 1 位（临时）→ 该格必 RED；
  `./tools/op-geth-oracle/extract.sh` 重新生成后复绿（同时证明脚本可再生产出同一契约）。

- [ ] **Step 6: 提交**：`test(evm): exercise the EIP-2537 BLS precompiles on the Isthmus path`
  （3 文件：测试 + oracle JSON + extract.sh）。

## Task E7: Jovian DA footprint 的边界与等值（**+3**，WI-24 裁定：`>` 拒绝与 baseFee max() 已覆盖）

**Files（四个格各自锁定落点，R2-5）：**
- Modify: `opstack-executor/tests/OpL1EdgeGateTest.cpp` —— **Step 1 边界格**（3 断言）与
  **Step 2 等值缺陷格**（期望 RED；两者都用该文件既有的 B-5b "构造 V4 payload → Step 2 静态校验" 路径）
- Modify: `bcos-tars-protocol/test/CalcOpBaseFeeTest.cpp` —— **Step 3 baseFee 耦合格**（该文件已有
  `CalcOpBaseFee` 用法可照）
- Modify: `opstack-executor/tests/OpKarstActivationTest.cpp` —— **Step 4 deposit-only 零格**（复用其
  deposits-only 夹具形态）
- Modify: `tools/mutation/variants/mapping.json` + Create `variants/V-DAFOOTPRINT-boundary.patch`

**oracle 原文（已核）**：op-geth `core/block_validator.go:119-132`——`BlobGasUsed == nil` 报错；
`blobGasUsed != CalcDAFootprint(txs)` 报错；`daFootprint > block.GasLimit()` 报错。

- [ ] **Step 1: 边界对齐格（1 用例 3 断言，期望绿）**——`==` **合法**（与 op-geth `>` 同界）：

```cpp
// op-geth uses '>' (block_validator.go:131), so == gasLimit is VALID; spec's "below, like
// gasUsed" (jovian/exec-engine.md:125) is <= semantics. Pin all three sides so neither a
// '>=' nor a '<' regression can hide.
// clang-format off
BOOST_AUTO_TEST_CASE(JovianDaFootprintGasLimitBoundary, * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    BOOST_CHECK(statusForDaFootprint(/*footprint=*/gasLimit - 1) == "VALID");
    BOOST_CHECK(statusForDaFootprint(/*footprint=*/gasLimit) == "VALID");      // == is legal
    BOOST_CHECK(statusForDaFootprint(/*footprint=*/gasLimit + 1) == "INVALID");
}
```
`statusForDaFootprint(uint64_t)` 复用本文件 B-5b 现有的 "构造 V4 payload + Step 2 静态校验" 路径
（实现时读该块并抽出最小 helper；**不得**复制整个 W6 夹具）。

- [ ] **Step 2: 等值缺陷格（**期望 RED**，F-A2 证据）**

```cpp
// op-geth rejects a header whose blobGasUsed disagrees with the locally recomputed
// footprint (block_validator.go:127). This engine validates only ranges
// (OpEngineService.cpp:180-199), so a remote!=local payload is ACCEPTED here -- the
// assertion pins the oracle behaviour and therefore fails until F-A2 is fixed.
// RED IS THE DELIVERABLE: record it as the finding's evidence, do NOT re-point the test.
// expected_failures(1) keeps the DEFECT PROOF without turning the target red (Boost 1.88
// decorator.hpp:142/:294). MUST be removed in the same commit that lands the §4 fix --
// a fixed implementation plus this decorator would be red the other way ("no errors but
// expected to fail").
// clang-format off
BOOST_AUTO_TEST_CASE(JovianDaFootprintMustEqualLocalRecomputation,
    * boost::unit_test::label("fork-jovian") * boost::unit_test::expected_failures(1))
// clang-format on
{
    // one plain tx whose local footprint is ~1e8/1e6 scaled; remote header says footprint-1.
    auto const status = statusForDaFootprintWithRemote(localFootprint, localFootprint - 1);
    BOOST_CHECK_EQUAL(status, "INVALID");   // expected to FAIL at the current head (F-A2)
}
```

- [ ] **Step 3: baseFee 耦合格**：`gasMetered := max(gasUsed, blobGasUsed)`（spec
  `jovian/exec-engine.md`）——DA 重块的下一块 baseFee 必须高于 `gasUsed` 口径的结果；判据用
  `CalcOpBaseFee`（`bcos-tars-protocol/test/CalcOpBaseFeeTest.cpp` 有既有用法可照）。

- [ ] **Step 4: deposit-only 块足迹为 0**：复用 `OpKarstActivationTest` 的 deposits-only 夹具形态
  （Jovian 激活块），断言 `seal.blobGasUsed == 0`。

- [ ] **Step 5: 变异变体**：注册 `V-DAFOOTPRINT-boundary`（把 `OpEngineService.cpp` 的 `>` 改成 `>=`）
  → 边界格里 `==` 断言必 RED；`make-variant.sh` + `mapping.json` + `run.sh` 复跑（照 E2 的做法）。

- [ ] **Step 6: 提交**：`test(executor): pin the Jovian DA footprint boundary and its equality gap (F-A2)`。

---

## Task E6: operator fee 残格 —— **已关闭（+0，WI-24 裁定）**

> 收口依据：③ 为拆分覆盖（Isthmus 数值 `RoutesFeesToFourVaults:44` / Jovian `g×1×100+500`
> `JovianReceiptMetaAndOperatorFormula:246`），⑤ 退款净额由 vault==f(gasUsed) 双用例钉死。

- [ ] **Step 1: 依 R2-0 结论决定**：若 `JovianReceiptMetaAndOperatorFormula`(:246) 已断言"同参数下
  Isthmus 与 Jovian 公式比值不同"且 `OperatorFeeConservesWhenCfgDisagreesWithProps`(:435) 已断言退款/
  守恒 → **+0**，写回台账并跳过建格。
- [ ] **Step 2: 若"未用 gas 退款"无独立不变量**，在 `OpTransitionTest.cpp` 补 1 格（照 :44 的
  `opValidate → opTransition → applyStateDiffStrict → vault 余额等式` 形态，断言
  `charge(gasLimit) - charge(gasUsed)`），标签按覆盖 fork；oracle 用 op-revm `l1block.rs:184-196`。
- [ ] **Step 3: 提交**（若有格）：`test(evm): pin the operator-fee refund on unused gas`。

---

## Task E3（+2）/ E5（+0）—— WI-24 已裁定

- [ ] **E3 Step 1（查重 + 接线确认）**：
```bash
git grep -n 'BEACON_ROOTS\|parent_beacon_block_root\|StorageSystemContract' -- '*Test*.cpp' bcos-evm/bcos-evm/eth/state | head -12
sed -n '30,45p' bcos-evm/bcos-evm/eth/state/system_contracts.cpp
```
判：既有 eth 层测试是否已覆盖系统合约行为；若是，本 Task 只补 **OP 路径接线格**（payload 字段 →
block context → 读出），3 格；否则按设计 §3 E3 全建。
- [ ] **E3 Step 2**：落格（代码形态：构造带 `parentBeaconBlockRoot = X` 的块上下文 + 调用
  `BEACON_ROOTS_ADDRESS`，断言返回值 == X；沿用查重时定位到的既有 fixture，**不新造 harness**）。
- [ ] **E5 Step 1（查重）**：确认 `EngineServiceTest.cpp:2077/1493/1776` 三个用例是否作用于 **OP**
  service（读其 fixture 构造）；是 → **+0** 并写回台账；否 → 建 1~2 格（`requestsHash ==
  c_emptyRequestsHash`，盖章点 `OpEngineService.cpp:429`）。
- [ ] **E3/E5 各自提交**（若有格）。

---

## Task E9–E12 —— **已取消（全 +0，WI-24 裁定）**

> 收口依据：E10 三态全有覆盖（`RegolithVerifyArmAcceptsAbsentWithdrawalsRoot` / `FcuV2Canyon…` /
> golden + `op_newpayload_accepts_announced_withdrawals_root`）；E11 由 `OpT8nReplayTest.cpp:1181-1196`
> 双向 postState 钉死；E12 由 `OpDepositTest.cpp:94/:123` + `OpReceiptMetaTest.cpp:34/:80/:105/:140`
> 覆盖。E9 夹具无消费者，不建。

### Task E9: `opstack-executor/tests/support/OpForkPayloadFixture.h`（新增）
- [ ] **Step 1**: 接口 `byFork(OpForkId)` / `byTimestamp(uint64_t)`；payload 形状复用 M2 的
  `clShapedPayload` 归一化逻辑（**复制该函数体并注明来源**，不引入第二份形状表语义——若可行，
  把 M2 的归一化抽成共享头由两边 include）。
- [ ] **Step 2**: 注册进 CMake（`support/` 已在 include 路径则只需重配）；确认四 target 计数只增不减。
- [ ] **Step 3**: 提交 `test(executor): fork-parameterised payload fixture for engine-path tests`。

### Task E10: 激活块引擎路径（+0~15，先复核三态缺口）
- [ ] **Step 1**: 复核 Isthmus withdrawalsRoot 三态在既有测试里的覆盖（`git grep` +
  `OpEngineApiVersionsTest`/`OpMismatchedFieldTest`/M2），列出**仍缺**的侧。
- [ ] **Step 2**: 用 E9 夹具建缺口格（预计：首个 Isthmus 块必带 MessagePasser root；pre-Isthmus 的
  nil / empty-code-hash 两形态负向）。
- [ ] **Step 3**: 每个激活块格断言 **±1 时间戳**两侧；提交
  `test(engine): sweep fork activation blocks through the engine path`。

### Task E11: 逐 fork L1Info 形状（+4）
- [ ] 3 正向（Ecotone/Isthmus/Jovian 逐字节）+ 1 负向（Fjord/Granite 无附加字段）；oracle =
  `{ecotone,isthmus,jovian}/l1-attributes.md` + op-node `l1_block_info.go`；提交
  `test(executor): byte-pin the per-fork L1 attributes deposit layout`。

### Task E12: 逐 fork 收据 meta 形状（+4，执行层）
- [ ] Canyon depositNonce / Ecotone scalar 组 / Isthmus operatorFee / Jovian DA scalar 各 1 格；
  oracle = op-geth `gen_receipt_json.go`/`receipt_opstack.go`；**不得**落在 rpc 层（fork-blind）；
  提交 `test(executor): pin the per-fork receipt meta shape at the execution layer`。

---

## Task R2-Z: 收尾回填（R2-8）

- [ ] **Step 1**: 把每个 Task 的最终格数、用例名、commit sha 回填 `...-spike-notes.md` 的 Stage 2 表
  （格 → 结果 → 用例数），并同步本文件的 Task 标题与计数。
- [ ] **Step 1a（契约）**: 逐 Task 检查 §0.2 验证块是否**填齐**（查重证据/oracle/断言强度/反向验证/
  预期 RED 项/计数增量/台账行）——缺任一项该 Task 记为未完成并回退到对应实现者补全。
- [ ] **Step 1b（N4）: F-S3-1 refuted 的**三处落地****——① 台账里把 S3 那节标 `refuted` 并附反证原文
  （op-geth `core/block_validator.go:131` 的 `>` 与 spec"like gasUsed"类比句）；② Stage 2 设计 §2
  已含该结论（核对无误即可）；③ **R1 计划（`...-impl.md`）的 E7 行**把"缺陷证明格（`>` 边界）"改锚为
  "边界对齐格 + **等值校验**缺陷证明格"。三处缺一即本 Task 未完成。
- [ ] **Step 2**: 复核四 target 计数（只增不减）且**全部 target 绿**——E7 的缺陷证明格由
  `expected_failures(1)` 承载，**不得**让 `opstack-executor-block-tests` 变红；再跑一次**全量**
  `bash tools/mutation/run.sh`
  （应含 Stage 1 的 8 个 + Stage 2 新增的 2 个变体），要求全 RED、控制组绿、exit 0、跑后 `git diff` 空。
- [ ] **Step 3**: 确认无生产代码残留（`git diff HEAD` 空）、`docs/**` 未入库、未推送。
- [ ] **Step 4**: 不提交任何过程文档；本 Task 无 commit。

## Task WI-33/34（delta 审计新增格）：eth_config 与 getProof 历史锚

> 登记与排期见 `docs/plans/2026-09-12-plan-e-workitems.md`（WI-33/34，源自全 fork delta 审计
> `docs/2026-09-12-opstack-fork-allforks-delta-audit.md` §4 的 G-3/G-4）。G-N1（WI-35）**待决策**，
> 不在本计划建格。两个 Task 均受 §0 验证契约约束。

- [ ] **WI-33（G-3）**: `bcos-rpc/test/unittests/rpc/EthConfigTest.cpp` 补 current/last 断言——
  现状：`EthEndpoint.cpp:135-144`/`EthConfig.cpp:31-72` 实现就绪、**全仓零测试**（delta 审计 §3.1
  S-KAR-5）。断言：eth_config 返回的 fork 档位字段与当前 schedule 一致。反向验证：改档位映射 → 必红。
- [ ] **WI-34（G-4）**: `bcos-rpc/test/unittests/rpc/EthGetProofIntegrationTest.cpp` 补**历史数字
  blockTag** 的出证用例——现状：代码闭合（`EthEndpoint.cpp:1344-1365/:1415-1420`）但既有测试全用
  `"latest"`（delta 审计 §3.2 BL-2 行）。断言：block-3 的 proof 由该块 header stateRoot 出证
  （照既有 `:206-248` 的独立验证器形态）。反向验证：把 stateRoot 来源改回 hardcoded 0 → 必红。

## Task V-CI: 把验证契约的**自动门**落进 CI（**触及共享 CI 配置，执行前需确认**）

**Files:** Modify `.github/workflows/workflow.yml`（或新增独立 workflow）
**Gate:** 该文件是共享 CI 配置——按会话硬约束，动手前需用户确认；本 Task 默认不执行。

- [ ] **Step 1: mutation harness job**（把"判据有效性"从本地纪律升级为门禁）
  - 复用现有 build 步骤后追加：`bash tools/mutation/run.sh 2>&1 | tail -30`；要求**所有变体 RED、
    控制组绿、exit 0**；失败即 job 失败。产物：job 日志（每个变体的 RED 行）。
- [ ] **Step 2: oracle 可再生校验 job**（防契约漂移）
  - 追加：`./tools/op-revm-oracle/extract.sh && ./tools/op-geth-oracle/extract.sh`，随后
    `git diff --exit-code -- bcos-evm/test/opstack/op_revm_oracle.json bcos-evm/test/opstack/op_geth_oracle.json`。
  - 通过判据：两步都无 diff（说明入库契约与 pin 源一致，且脚本可重放）。
- [ ] **Step 3: label 覆盖守卫**（防某 fork 归零）
  - 脚本断言 9 个 `--run_test=@fork-<name>` 各选出 **≥ N 例**（N 由当前实测下界取整，如 karst≥130、
    regolith≥89；写死下界并在注释里记来源与日期）。失败即 job 失败。
- [ ] **Step 4: 收口语料 pin 不一致（已登记 F-A4）**
  - 现状：CI 用 `opstack-t8n-regen@759a9af0…`，而 golden/向量生成 pin 记为 `e8800cffe`。
    裁定权威 pin 并**对齐 action ref 与文档记录**；若裁定为"两者各有用途"，须在
    `.t8n-pin`/设计文档写明分工（否则"最终结果可不可信"无据）。
- [ ] **Step 5: 本地先验**：上述三条命令在本机各跑一遍并留输出；确认 `git diff` 空、未推送。

## §4 生产修复（F-A2）——**仅在获得单独授权后执行**

- [ ] **Step 1: blast radius 复核（四项，逐条出结果）**
```bash
git grep -n 'blobGasUsed' -- 'opstack-executor/tests' 'engine/test' | head -20
sed -n '500,530p' opstack-executor/OpBlockExecute.cpp      # Σ 计算（抽共享调用的对象）
sed -n '175,200p' engine/bcos-engine/OpEngineService.cpp   # 现校验块（插入点）
```
判：① `OpL1EdgeGateTest` 的 DA 用例喂的是 remote==local 吗 ② deposit-only 块是否 0==0
⑤（新增）**E7 Step 2 的 `expected_failures(1)` decorator 是否随本次修复一并移除**——修复落地后
它必须消失，否则"无错却预期失败"会让 target 以另一种方式变红。
③ M2 语料 `blobGasUsed` 与交易是否一致 ④ 有无测试拿 blobGasUsed 当占位值。
- [ ] **Step 2**: 若四项均无冲突 → 实现等值校验（复用 `OpBlockExecute.cpp:508-525` 的同一 Σ 计算，
  **抽成共享函数**，判据与报错形照 op-geth）；跑四 target + 全量 harness。
- [ ] **Step 3**: 若任一项冲突 → 停下报告，交用户决定（不得牺牲既有格）。
- [ ] **Step 4**: E7 Step 2 的期望 RED 在修复后必须转绿——**两阶段都要留证**（修复前 RED = 缺陷证据；
  修复后 GREEN = 闭合证据），且**同一 commit 内移除 `expected_failures(1)`**（否则固定报"预期失败却无错"）。

---

## Self-Review

1. **Spec 覆盖**：design §3 的 E3/E4/E5/E6/E7/E9–E12 全部有 Task；E4/E7 有完整代码；E3/E5/E6/E9–E12
   以"查重/夹具先行"的步骤骨架给出，且每步绑定**具体命令与既有文件锚点**（不是 TODO）。
2. **Placeholder 扫描**：无 TBD；E7 的 `statusForDaFootprint*` 明确要求"读 B-5b 块并抽最小 helper"，
   不复制整夹具；E4 的向量由 Step 1 的具体命令产出。`+0~N` 的区间均附**判定步骤**（非猜测）。
3. **类型/命名一致性**：`runOsakaPrecompileOpTx(ts, vm, address, input, cfg, gasLimit)`（Stage 1 实测签名）、
   `opValidate/opTransition/applyStateDiffStrict`（`OpTransitionTest:85-92` 实测形态）、
   `isthmusConfig()`（`Op7702Test.cpp:88` 实测用法）、`c_emptyRequestsHash`（`EngineServiceCommon.h:169`）、
   `CalcDAFootprint`（op-geth）在全文一致。
4. **纪律**：新用例带 fork 标签 + 单行 decorator；E7 的期望 RED 明文标注"红即交付物"；
   生产变更单列 §4 且默认不动；E9 不触碰共享头。
5. **风险**：E6/E7 的"疑似已覆盖"必须经 R2-0 用例名级复核（Stage 1 的教训：E8 因此从 +2 变 +0，
   反向也可能：某条"看似覆盖"其实只断言了弱不变量）。

6. **第二轮计划审查（N1/N2/N4/N5）的落地**：
   - **N1** E7 的等值缺陷格改用 `* boost::unit_test::expected_failures(1)`（Boost 1.88
     `decorator.hpp:142/:294` 已核实存在）——缺陷证明**不再让 target 变红**；§4 的 blast radius
     增列第 ⑤ 项"修复时必须同 commit 移除该 decorator"，R2-Z 的绿门同步写明；
   - **N2** oracle 裸 hex（无 `0x`，实测 `blsG1Add.json` 首条）写死为硬约束：读取端按裸 hex 解码、
     比较端统一裸 hex；E4 片段注释同步；
   - **N4** R2-Z 增 **Step 1b**：F-S3-1 refuted 的**三处落地**（台账标 refuted + 反证原文／设计 §2／
     R1 计划 E7 行改锚），缺一即未完成；
   - **N5** E4 Step 3 把三个 helper 的签名与 **JSON 键 → 字段映射**写死（`input`/`expected` →
     `input`/`expectedHex`，均裸 hex）。
   - **N3** 经核实**不成立**（已记录）：op-geth 实际 9 个 BLS 文件名与计划所用一致
     （`blsG1Add/blsG1Mul/blsG1MultiExp/blsG2Add/blsG2Mul/blsG2MultiExp/blsMapG1/blsMapG2/blsPairing`）。

7. **验证契约固化（§0）**：10 个触发点的"手段/产物/出口判据"表、Task 验证块模板、10 条失效模式
   checklist（全部为本会话实证）、归因与残留纪律、以及"手段自身的保障"表——每个 Task 受其约束；
   自动门落成 **Task V-CI**（mutation/oracle 再生/label 守卫/pin 收口），标注需确认。

8. **前一轮计划审查（R2-1 ~ R2-8）的落地**：
   - **R2-1** E4 新增 **Step 0 可达性探针**（override 表无 0x0b/0x0d/0x0f，`OpHost.cpp:131-132`
     只是注释主张）——失败即降级为缺陷证明，不硬写 5 格；
   - **R2-2** 用例名由**向量实际 name**决定，删除了预设的 "Identity" 语义；
   - **R2-3** 新增 `tools/op-geth-oracle/extract.sh`（含完整脚本骨架），oracle 可再生；
   - **R2-4** `op_geth_oracle.json` 定为**入库契约**（缺席即 `BOOST_REQUIRE` 失败，与
     `op_revm_oracle.json` 同语义）；
   - **R2-5** E7 四格的**落点逐个锁定**（`OpL1EdgeGateTest` ×2 / `CalcOpBaseFeeTest` / `OpKarstActivationTest`）；
   - **R2-6** R2-0 关键字表补 `gasMetered|max(gasUsed` 与 deposit-only 两项；
   - **R2-7** 输出比较改用"该文件既有 output 断言习惯"，禁止臆造 `toHex(receipt->output())`；
   - **R2-8** 新增 **Task R2-Z 收尾回填**（台账/计数/全量 harness/无残留确认）。

### 口径说明（2026-09-13）：Stage 2「净增 9 格」的构成

裁定原文的 **净增 9 = 落地 7 + 阻塞 2**：落地 = E7 3（`380c775d2`）+ E3 2（`1993d9fe5`）+ WI-34 2（`746a80189`）；阻塞 = **WI-33 2 格**，因前提失效（`eth_config`/`EthConfig.cpp` 不在本分支，实现于 `feat/engine-cutover-on-prereqs` 的 `53534e6cd`，非祖先）而关闭为 branch-dependent。E4/E5/E6/E10/E11/E12 均为 +0（既有覆盖；E4 的「+5」在 WI-24 spec 审查中被证伪）。**不是少做两格**。
