# W8：记忆遗留清理 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 收口早期重构遗留的 3 项——全量 ctest 复测、`s_number_2_header` 落盘核实、四项决策（C1 搬运已批准修复 / C2 补测试 / C6 裁定放行 / 语料可复现性验证）。

**Architecture:** 5 个独立 task。T1 全量 ctest、T2 落盘核实（将确认 mergeBackStorage 缺口）、T3 C1 搬运（用户拍板 checkpoint）、T4 C2/C6 核对+补测试、T5 语料可复现性验证。多数为验证/调查，少数代码改动（C1 搬运 + C2 测试）。

**Tech Stack:** C++（RLPDecode.h/Web3TxHandler）+ Boost/GTest 测试 + ctest + Go regen（op-geth pin e8800cff）。

## Global Constraints

- **T1 基准口径**：当前分支全量 1909（`cd build && ctest`，⚠️ 必须进 build/）；记忆基准 1857/1858 只做比例参考（标签失实）；WsToolsTest 本机通过（环境相关）
- **T2 核实核心**：`EngineServiceImpl.h:1167` `pushView` 后**是否 `mergeBackStorage()`**（答案：否 → s_number_2_header 不落盘，仅内存 view）——确认缺口 + 范围决策
- **T3 C1**：上游已拍板 Plan A（`a37517327`/`245f47f0c`/`4e0848e2f`）；搬运进 `RLPDecode.h`（长字节/长列表长度前缀加 `lenOfLen>=2 && from[0]==0 → NonCanonicalSize`）+ `RLPTest` 用例 + **修 `OpSchedulerImpl.h:277-278` 注释失实** + 影响面回归（Web3Transaction/ledger MPT/TransactionImpl/Web3AccessListResolver/ChecksumAddress）
- **T4 C2**：typed-tx `signatureV ≤1` 校验已存在（Web3TxHandler.cpp:420/596/923）——**只补测试**（yParity>1 负向 / v=0/1 显式 / 7702 auth 宽度 `OpSchedulerImpl.h:374`）；**C6 = 裁定放行/等价留档**（非硬拒——op-geth 块执行层也放行 legacy/0x01）
- **T5 语料**：重生成无意义（结构性恒值）；改**可复现性验证**——`regen.sh` exit 0（git-diff 字节等同）；**无 SHA256SUMS**（manifest + git-diff 机制）；数量 34
- **T3 拍板 checkpoint**：T3 产证据包后暂停等用户拍板（确认搬运 Plan A vs 保持宽松）；T4/T5 与 T3 正交
- **构建/测试 target 名（W8 审查 A/B/C/D 修正）**：`test-bcos-codec`（非 bcos-codec-ut）、`test-bcos-rpc`（非 bcos-rpc-ut）、`bcos-evm-opstack-tests`（✓ 已对）；run 用 `./build/bcos-codec/test/test-bcos-codec` / `./build/bcos-rpc/test/test-bcos-rpc` / `./build/bcos-evm/test/bcos-evm-opstack-tests`
- **Boost suite 名**：`--run_test=testWeb3Type`（⚠️ 非 Web3TypeSuite）；`--run_test=RLPTest`（✓）；`--run_test=BcosEvmOpstackTests`
- **T3 修复片段**：只加 `if (lenOfLen>=2 && from[0]==0) return NonCanonicalSize;` 块（⚠️ `const auto lenOfLen` 已存在于 RLPDecode.h:69/:97，勿重复声明）
- **T3 注释修正范围**：`OpSchedulerImpl.h:277-278` + `:289-300`（⚠️ 后者也引用不存在的 OpSchedulerImplTest.cpp 命名测试）
- **T4 测试 (c) 落点**：7702 auth 宽度测试须在 `if(TARGET bcos-framework)` 条件块（OpSchedulerImplSmokeTest.cpp 或新增 decode 级测试）——不能进无条件 Op7702Test.cpp（会拉 OpSchedulerImpl.h 重依赖，破坏 standalone 构建）
- **T5**：golden/engine/SHA256SUMS **存在**（79 行，覆盖 golden+chained）；vectors 层无 SHA256SUMS（manifest + regen git-diff）；cases/ untracked 是常态（regen git-diff 不覆盖 untracked）；T5 commit 需 `git add` 目标文件（若 regen 无改动则无物可 commit）
- **Non-goals（W8 审查 A）**：批8（接缝 11 条）+ 非 20 字节 `/apps/` 表名状态根语义——spec §7 parked，不入本 plan（防 scope creep）
- commit `--no-verify`；worktree 内运行

---

### Task 1: #2 全量 ctest 复测

**Files:**
- Modify: 相关模块源码（若复测发现失败）
- Create: `.superpowers/sdd/2026-08-07-w8-legacy-cleanup/task-1-report.md`（回归报告）

**Interfaces:**
- Consumes: 既有模块测试
- Produces: 回归报告（全绿 / 失败清单 + 修复）

- [ ] **Step 1: 枚举 ctest 目标**

```bash
cd build && ctest -N
```

Expected: 枚举全量（当前分支约 1909 个测试；从仓库根跑 ctest 会见 0——必须进 build/）。确认相关模块在列：engine(11)/rpc(185)/ledger(187)/tars-protocol(1)/bcos-executor(363)/opstack-executor(9 gtest)/transaction-executor(173)/transaction-scheduler(113)。

- [ ] **Step 2: 跑全量 ctest**

```bash
cd build && ctest -j 8 2>&1 | tail -20
```

Expected: 全量结果。记录通过/失败比例。

- [ ] **Step 3: 分类失败项**

对失败的测试分类：pre-existing（如 WsToolsTest 环境相关——本机可能通过）/ 本次引入。**若 WsToolsTest 失败**，确认是环境（IPv6）非代码，记录即可。

- [ ] **Step 4: 修复失败项（如有）**

对非 pre-existing 失败：定位 + 修复 + 重跑该 target。若全绿，跳到 Step 5。

- [ ] **Step 5: 写报告 + Commit**

报告写 `.superpowers/sdd/2026-08-07-w8-legacy-cleanup/task-1-report.md`（全量结果 + 失败分类 + 修复清单）。若无源码改动，commit 只记录报告（SDD workspace gitignored）；若有修复 commit：

```bash
git commit --no-verify -m "fix(w8): #2 ctest 复测修复（如有）"
```

Expected: 全量 1909 全绿（或明确记录失败分类）。

---

### Task 2: #4 s_number_2_header 落盘核实

**Files:**
- Create: `.superpowers/sdd/2026-08-07-w8-legacy-cleanup/task-2-report.md`（核实报告）

**Interfaces:**
- Consumes: `EngineServiceImpl.h` / `Ledger.cpp` / `LedgerMethods.h`
- Produces: 核实报告（确认 mergeBackStorage 缺口 + 范围决策）

- [ ] **Step 1: 核对写侧**

读 `EngineServiceImpl.h:1191-1220`（`registerOpBlock`）——`SYS_NUMBER_2_BLOCK_HEADER` 写 tars BlockHeader 字节，key=blockNumber 十进制串。与 `Ledger.cpp:228-235`（FISCO 同表同格式）对拍。

- [ ] **Step 2: 核对提交路径（核心）**

读 `EngineServiceImpl.h:1158-1167`——OP 提交路径是否调用 `mergeBackStorage()`？对照 FISCO 路径 `:662-669`（pushView + mergeBackStorage 原子 mergeView）。

Expected: **OP 路径只 `pushView`，从不 `mergeBackStorage()`**——确认「s_number_2_header 行只存内存 view 层，未落后端 RocksDB」缺口。

- [ ] **Step 3: 核对读侧**

读 `EngineServiceImpl.h:897-908`（OP 父头读）/ `LedgerMethods.h:203-209`/:331-338 / `Ledger.cpp:1339`——读侧三处经 `createBlockHeader(bytesConstRef)`，与写格式一致。

- [ ] **Step 4: 写报告 + 范围决策**

报告确认：写/格式/读一致，**缺口 = mergeBackStorage 未调 → 不落盘**。给出范围决策：修复（接 mergeBackStorage，可能超收尾范围——最小 loop 是受控欠账）vs 留档（记入待办）。

- [ ] **Step 5: Commit（若修复）/ 记档**

若决定修复：改 `EngineServiceImpl.h:1167` 后加 `mergeBackStorage()`（仿 FISCO :662-669）+ 验证。否则报告留档即可（SDD workspace gitignored）。

Expected: 缺口确认 + 范围决策落定。

---

### Task 3: #5-C1 搬运已批准修复（含用户拍板 checkpoint）

**Files:**
- Modify: `bcos-codec/bcos-codec/rlp/RLPDecode.h`
- Modify: `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:277-278`（注释失实修正）
- Test: `bcos-codec/test/` 或 RLPTest（搬运 `RLPTest` 用例）

**Interfaces:**
- Consumes: 上游 `a37517327`（Plan A 修复）
- Produces: 搬运修复 + 注释修正 + 影响面回归 + 拍板记录

- [ ] **Step 1: 确认上游修复内容**

```bash
git show a37517327 --stat   # 上游已拍板 Plan A 修复（feat-op-validator-loop 系）
git show a37517327          # 读修复 diff：RLPDecode.h 长字节/长列表加前导零检查 + RLPTest 用例
```

Expected: 确认上游修复 = `decodeHeader` 长字节/长列表分支加 `if (lenOfLen>=2 && from[0]==0) return NonCanonicalSize`。

- [ ] **Step 2: 产证据包 + 用户拍板 checkpoint**

将证据（上游修复内容 + 本分支现状 + 影响面消费方清单 + 回归风险）呈现给 controller → **暂停等用户拍板**（确认搬运 Plan A vs 保持宽松）。拍板记录写入报告。

> **controller 处理**：收到 T3 证据包后暂停，向用户呈现拍板选项（搬运已批准 Plan A vs 保持宽松——附影响面/回归风险），获拍板后继续。

- [ ] **Step 3: 搬运修复 + 测试（拍板为搬运后）**

`RLPDecode.h` 的 `decodeHeader` 长字节（:69-83）与长列表（:96-112）分支，在 InputTooShort 检查后、`fromBigEndian` 前加（⚠️ **只加 `if` 块**——`const auto lenOfLen` 已存在于 :69/:97，勿重复声明）：

```cpp
if (lenOfLen >= 2 && from[0] == 0)
{
    return {BCOS_ERROR_UNIQUE_PTR(NonCanonicalSize,
                "Non-canonical length prefix: leading zero byte"), Header()};
}
```

搬运 `RLPTest` 的 `decodeRejectsNonCanonicalLengthPrefix` 用例（对照上游 a37517327；⚠️ 勿按上游行号盲搬——本地 RLPTest.cpp 与上游已分歧，按套件结构定位，插入在 `decodeRejectsMalformedInputs` 之后、`BOOST_AUTO_TEST_SUITE_END()` 之前）。

- [ ] **Step 4: 修注释失实（范围扩宽）**

`OpSchedulerImpl.h:277-278` 注释声称"C1 fixed the SHARED decoder (RLPDecode.h)"——改为如实描述。⚠️ **同时修 :289-300**：该段也引用本分支不存在的 `OpSchedulerImplTest.cpp` 及其命名测试（`NonCanonicalOuterFramingIsRejectedAtDecode` 等）——整段 stale 注释块核对修正（或注明本分支测试在 OpSchedulerImplSmokeTest）。

- [ ] **Step 5: 影响面回归 + Commit**

影响面消费方（⚠️ W8 审查 B 路径修正：`Web3Transaction.h`（header-only 非 .cpp）/ ledger MPT（Account/NodeDecoder/StorageValueCodec）/ `TransactionImpl.cpp`（bcos-tars-protocol）/ `Web3AccessListResolver.cpp`（**bcos-executor**）/ `ChecksumAddress.cpp`（**bcos-crypto**）+ 额外 EthEndpoint/BlockHeaderImpl/Web3TypeTest）——跑相关测试确认不破坏。

```bash
cmake --build build --target test-bcos-codec bcos-evm-opstack-tests test-bcos-rpc   # ⚠️ 实际 target 名
./build/bcos-codec/test/test-bcos-codec --run_test=RLPTest  # 新用例绿
git add bcos-codec/bcos-codec/rlp/RLPDecode.h bcos-codec/test/unittests/RLPTest.cpp bcos-evm/bcos-evm/engine/OpSchedulerImpl.h
git commit --no-verify -m "fix(w8): #5-C1 搬运 Plan A RLP 前导零修复 + 注释修正"
```

Expected: RLPTest 新用例绿 + 影响面回归不破 + 注释如实。

---

### Task 4: #5-C2/C6 核对 + 补测试

**Files:**
- Modify: `bcos-rpc/test/unittests/rpc/Web3TypeTest.cpp`（补测试）
- Create: `.superpowers/sdd/2026-08-07-w8-legacy-cleanup/task-4-report.md`（C6 裁定留档）

**Interfaces:**
- Consumes: `Web3TxHandler.cpp:420/596/923`（typed-tx ≤1 已实现）、`OpSchedulerImpl.h:374`（7702 auth 宽度已实现）
- Produces: 3 类测试 + C6 裁定留档

- [ ] **Step 1: 核对 C2 代码已实现**

grep `InvalidVInSignature`（Web3TxHandler.cpp:420/596/923 typed-tx ≤1 + OpSchedulerImpl.h:374 decodeAuthYParityScalar 7702 宽度）——确认无需再改代码。

- [ ] **Step 2: 补测试 (a) typed-tx yParity>1 负向**

在 `Web3TypeTest.cpp` 加用例（⚠️ W8 审查 C 给出精确改法）：从现有 rawTx（`testEIP1559Transaction` 等）改 yParity wire 字节 → signatureV=2。**wire 偏移**（RLP 解码实证）：EIP1559 字段[9] 的 `0x80` 在偏移 184、EIP2930 字段[8] 的 `0x80` 在偏移 178、EIP4844 字段[11] 的 `0x01` 在偏移 232——改为 `0x02`（单字节自编码，长度不变，外层前缀不受影响）。

```cpp
// yParity > 1 必须拒绝（typed tx signatureV 只允许 0/1）
BOOST_AUTO_TEST_CASE(typedTxYParityOverOneRejected)
{
    // 取 testEIP1559Transaction 的 rawTx，改 wire 偏移 184 的 0x80 → 0x02（signatureV=2）
    auto rawTx = /* EIP-1559 rawTx，bytes[184] = 0x02 */;
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx;
    auto e = codec::rlp::decode(bRef, tx);  // ⚠️ 实际 API：codec::rlp::decode 返回 Error::UniquePtr
    BOOST_CHECK(e != nullptr);
    BOOST_CHECK_EQUAL(e->errorCode(),
        static_cast<int64_t>(codec::rlp::DecodingError::InvalidVInSignature));  // Error.h:91
}
```

- [ ] **Step 3: 补测试 (b) v=0/1 合法样本显式断言**

现有 `testEIP2930Transaction`/`testEIP1559Transaction`/`testEIP4844Transaction` 的解码 rawTx 携带 v=0x00/0x01——加显式 `BOOST_CHECK_EQUAL(tx.signatureV, 0u/1u)` 断言。

- [ ] **Step 4: 补测试 (c) 7702 auth yParity 宽度**

⚠️ W8 审查 C：测试**须在 `if(TARGET bcos-framework)` 条件块**（`OpSchedulerImplSmokeTest.cpp` 扩展或新增 decode 级测试）——不能进无条件的 `Op7702Test.cpp`（会拉 OpSchedulerImpl.h 重依赖，破坏 standalone 构建）。用例：auth yParity 超宽编码（`0x82 0x01 0x00`==256）喂 `detail::decodeAuthYParityScalar`（或 `detail::decodeOneRawTx` 的 0x04 信封）应抛 `OpConsensusError`（`readCanonicalScalar(in,1)` 对 payloadLength=2 拒绝——代码实证）。

- [ ] **Step 5: C6 裁定留档 + Commit**

C6 裁定：legacy/0x01 块执行放行 = 与 op-geth 等价（`decodeOneRawTx` 分派 + opValidate 白名单 vs op-geth `state_transition.go` IsDepositTx-only）——**留档**（报告记录裁定 + 证据，不落地硬拒）。

```bash
cmake --build build --target test-bcos-rpc bcos-evm-opstack-tests   # ⚠️ target 名
./build/bcos-rpc/test/test-bcos-rpc --run_test=testWeb3Type  # ⚠️ suite 名（非 Web3TypeSuite）
./build/bcos-evm/test/bcos-evm-opstack-tests --run_test=Op7702Suite  # 若 (c) 落 smoke
git add bcos-rpc/test/unittests/rpc/Web3TypeTest.cpp bcos-evm/test/opstack/OpSchedulerImplSmokeTest.cpp
git commit --no-verify -m "test(w8): #5-C2 yParity 补测试 + C6 裁定留档"
```

Expected: 3 类测试绿 + C6 裁定记录。

---

### Task 5: #5-语料可复现性验证

**Files:**
- Create: `.superpowers/sdd/2026-08-07-w8-legacy-cleanup/task-5-report.md`（可复现性验证报告）

**Interfaces:**
- Consumes: `regen.sh`（W6 的 generator + op-geth pin e8800cff）
- Produces: 可复现性验证报告（regen exit 0 + 字节等同）

- [ ] **Step 1: 跑 regen.sh**

```bash
cd bcos-evm/test/opstack/t8n/generator && ./regen.sh
```

Expected: exit 0（终句 `git diff --exit-code` = 字节等同）；34 份向量 + golden + chained 全部与入库一致。

- [ ] **Step 2: 确认字节等同**

`git status` 应无 vectors/golden 改动（regen 是 no-op 可复现）。确认 currentRandom/currentCoinbase 恒值（结构性保证，非缺陷）。

- [ ] **Step 3: 写报告 + Commit**

报告记录：regen.sh exit 0 + 字节等同 + 34 份确认 + 「重生成无法多样化 currentRandom/coinbase（生成器断言 mixDigest==0 + 全 case 同 coinbase）——非缺陷，可复现性已验证」。

```bash
git status --short  # 应无 vectors/golden 改动；若有 git diff 则异常需查
git commit --no-verify -m "docs(w8): #5-语料可复现性验证报告（若有改动）"
```

Expected: regen exit 0 + 无意外改动 + 报告。

---

## 执行顺序与验收

```
Task 1 (ctest) → Task 2 (落盘) → Task 3 (C1 搬运+拍板) → Task 4 (C2/C6 测试) → Task 5 (可复现性)
```

⚠️ T3 含用户拍板 checkpoint（T3 证据包后暂停）；T4/T5 与 T3 正交——若 T3 等待拍板，T4/T5 可先执行。

- 每任务独立可测：T1 靠 ctest 1909；T2 靠 mergeBackStorage 缺口确认；T3 靠 RLPTest 新用例 + 影响面回归 + 拍板记录；T4 靠 3 类测试绿 + C6 留档；T5 靠 regen exit 0
- 验收（spec §6）：ctest 全绿（或明确失败清单）/ 落盘缺口确认 / C1 搬运+注释修正+回归 / C2 3 测试 / C6 裁定放行留档 / T5 可复现性
- 审查：SDD task-reviewer 逐任务 + 最终 whole-branch review（opus）
