# OP Stack 块级执行移植设计(bcos-evm-ref → bcos-evm vendored 底座)

日期:2026-07-24
状态:已评审(设计五节逐节用户确认)
来源分支:`feat-evm-mb1-block-execution`(worktree,挂本仓 .git,merge-base `6dcbcd07b`)
目标分支:`feat-evm-opstack-port`(自 `refactor-evmone-vm-hash-fn` HEAD 开出)

## 1. 背景与目标

`feat-evm-mb1-block-execution` 分支上的 `bcos-evm-ref` 模块已完成 OP Stack 块级执行全套:
`processOpBlock`/`sealOpBlock`、receipt/tx/state 三根建根、op-geth `v1.101702.2` 块级 t8n
差分 gate(31 向量、123 测试全绿、FINDING-1 已归零)。其底座是 vcpkg fork port
(`ywy2090/evmone@3585c2cb`,导出 `evmone::state`/`evmone::testutils` 导入目标)。

当前分支 `refactor-evmone-vm-hash-fn` 已将同一模块的 ETH 侧移植为 `bcos-evm`,换用
**vendored 底座**:evmone `test/state` 源码进树(`bcos-evm/eth/state/`),port 换官方
`ipsilon/evmone v0.21.0` 压缩包 + `fisco-sm3.patch`(hash_fn 挂 VM,port-version 7,
patch 只碰 `lib/evmone/*`,不碰 `test/`)。

本设计把 opstack 层(源码 + 全部验证资产)移植到 vendored 底座上,验收与 ref 侧同口径。

## 2. 已定决策

| 决策点 | 结论 |
|---|---|
| 验收范围 | **全保真**:源码 + 24 单测 + 31 向量 t8n gate + upstream-diff 重锚 |
| 测试框架 | **加 gtest + nlohmann-json**(vcpkg,仅测试目标链接),测试代码零改写 |
| 移植基准 | `efb6fd42e` + worktree 未提交译英批次(搬运前先在 worktree 侧提交固化,SHA 落账于此) |
| 分支策略 | 叠在 `refactor-evmone-vm-hash-fn` 上开 `feat-evm-opstack-port`,独立 PR |
| EEST 套件 | **不随本次**(EestStateTest/EestBlockchainTest + blockchaintest loader 排除) |
| 模块形态 | **方案 A 单模块扩展**:opstack 作为 `bcos-evm` 第二子库,ports/ 零改动 |

## 3. 总体架构

```
bcos-evm/
├── bcos-evm/
│   ├── eth/
│   │   ├── EthTransition.{h,cpp}     # 已有,本次补 sanitize 接线(§5)
│   │   ├── state/                    # 已有 vendored 14 文件,零改动
│   │   └── utils/                    # 新增 vendor(evmone test/utils 最小集,§4)
│   ├── adapter/
│   │   ├── StateViewAdapter.h        # 已有,与 ref 侧(含译英)对齐一次
│   │   └── StateDiffSanitize.h / StateDiffWriteback.h / StateRootCompute.{h,cpp}
│   └── opstack/                      # 17 文件整层移植
├── test/
│   ├── eth/                          # 已有 Boost.Test 目标,不动
│   └── opstack/                      # GTest 目标 + t8n/{vectors,generator} + regen.sh
└── scripts/                          # upstream-diff.sh + golden + manifest(重锚官方 v0.21.0)
```

构建目标(与 ref 侧同构,alias 前缀 `bcosevmref::` → `bcosevm::`):

- `bcosevm::eth`(`bcos-evm-eth`,已有)← 并入 `eth/utils/` 源与 `StateRootCompute.cpp`
- `bcosevm::opstack`(`bcos-evm-opstack`)→ 链 `bcosevm::eth`
- `bcos-evm-opstack-tests`(GTest)→ 链 `bcosevm::opstack` + `GTest::gtest` + `nlohmann_json`;源含 ref 的 `test/main.cpp` 与 `statetest_loader.cpp`(测试侧源)

不变式:

- `ports/` 目录 diff 为空,port-version 停在 7
- 命名空间沿用 `bcos::evmref`
- 根 CMake 沿用 FULLNODE 下 `add_subdirectory(bcos-evm)`;standalone 构建模式保留
- 库目标(`bcosevm::eth`/`bcosevm::opstack`)不得链接 gtest/nlohmann

include 改写规则(全模块仅三条,机械可脚本化;vendored utils 内部互引同样适用):

1. `<test/state/X>` → `<bcos-evm/eth/state/X>`
2. `<test/utils/X>` → `<bcos-evm/eth/utils/X>`
3. `<bcos-evm-ref/Y>` → `<bcos-evm/Y>`

## 4. 组件移植映射

### 4.1 源码层(基准 = §2 固化 SHA)

| ref 侧 | 目标 | 处理 |
|---|---|---|
| `opstack/` 17 文件(~1920 行) | `bcos-evm/bcos-evm/opstack/` | 原样 + include 三规则改写 |
| `adapter/StateDiffSanitize.h`、`StateDiffWriteback.h` | `bcos-evm/bcos-evm/adapter/` | 原样 + 改写 |
| `adapter/StateRootCompute.{h,cpp}` | 同上 | 原样 + 改写;`.cpp` 并入 `bcos-evm-eth` |
| `adapter/StateViewAdapter.h` | 已存在 | 取 ref 版本对齐一次 |
| `eth/EthTransition.{h,cpp}` | 已存在 | 取 ref 版本(含 sanitize 接线)再改 include |

### 4.2 vendor 层(官方 `ipsilon/evmone v0.21.0` `test/utils/` → `eth/utils/`)

起始清单:`mpt.{hpp,cpp}`、`mpt_hash.{hpp,cpp}`、`rlp.hpp`、`rlp_encode.{hpp,cpp}`、
`test_state.{hpp,cpp}`、`statetest.hpp`、`utils.{hpp,cpp}`、`stdx/`。
`statetest_loader.cpp` 拆入测试目标(nlohmann 不渗入库目标)。
**排除**:`blockchaintest*`、`statetest_export`/runner 类纯 EEST 工具;`bytecode.hpp`
等仅在链接器要求时补入。闭包判据:`bcos-evm-opstack-tests` 链接成功,多退少补,
最终清单回填本节。vendored 源沿用既有惯例:上游原样不改逻辑、
`-Wno-missing-field-initializers` 抑制。

vendor 基准必须与 `eth/state/` 既有 vendor 同源(官方 v0.21.0),不从 ywy2090 fork 取。

### 4.3 测试层

| ref 侧 | 目标 | 处理 |
|---|---|---|
| `test/opstack/` 24 个 `.cpp` + helper | `bcos-evm/test/opstack/` | 仅 include 三规则改写 |
| `t8n/vectors/`(432K,31 向量 + manifest + DIVERGENCES.md) | 同构搬运 | **逐字节不动** |
| `t8n/generator/` + `regen.sh` | 同构搬运 | 只搬不跑;README 路径引用改写 |
| `test/main.cpp` | `test/opstack/` | 原样沿用(零改写原则),不换 `gtest_main` |

### 4.4 依赖变更

`vcpkg.json` 新增 `gtest`、`nlohmann-json`。均为叶子依赖;与 baseline 解析冲突则 pin version。

### 4.5 明确不搬

`bcos-evm-ref/` 模块壳与 CMakeLists、fork portfile(16.5KB,`EVMONE_STATE` 导出机制)、
EEST 双套件与 blockchaintest loader、`spike/`(M3.5 读放大)、iwyu 工装(可后补)。

## 5. 数据流与 ETH 侧欠账接线

块级执行数据流(移植后与 ref 侧完全同构):

```
向量 pre (JSON) ──statetest loader──▶ TestState 播种 InMemoryStateView
                                        │
processOpBlock(view, block, txs, cfg, vm)
  ├─ 逐 tx: opValidate → opTransition / runDeposit
  │    ├─ evmone::state::transition(…, vm)   # vm 携带 hash_fn(当前底座)
  │    ├─ sanitizeStateDiff(view, diff)      # 出口消毒(FINDING-1)
  │    └─ OpTxReceipt{receipt, meta}
  ├─ applyStateDiffStrict(state, diff)       # 写回 + tripwire
  └─ finalize → sanitizeStateDiff
        │
sealOpBlock(…) → receiptsRoot/txRoot/stateRoot(mpt_hash)+ logsBloom
        │
header 六字段 + 逐 receipt + postState ⇄ op-geth 产出逐位比对(gate)
```

**ETH 侧欠账(必补,否则 access_list 向量翻红)**:当前 `EthTransition.cpp` 停在
FINDING-1 修复前。按 ref 版本对齐两出口:`runTransaction` 的 `transition` 返回值过
`sanitizeStateDiff`;`finalize` 出口同样包裹。六出口的其余四个在 opstack 层文件内,
随整层移植自然带入。

**StateView 适配器契约**(随移植原文保留,StateDiffSanitize.h 头注):
`get_account(addr).has_value()` ⇔ 账户存在于账本承诺表示,"存在但空"不得折叠为
`nullopt`。`TestState` 满足;将来接真账本视图(M3.5 P2,不在本次)必须遵守。

**hash_fn/VM 交互**:ref 侧 gate 绿灯在 fork evmone(旧 SM3 形态)取得;本次底座为
官方 v0.21.0 + hash_fn-on-VM patch。OP 回放纯 keccak 语义,patch 不碰 `test/`,预期
零行为差;31 向量 gate 复跑即此等价性的实证,不另设专门测试。

**错误处理不变式**(随源码移植,不改):`opValidate` 失败→致命(G-1);
`applyStateDiffStrict` tripwire →测试失败而非静默;回放分歧→仅 `DIVERGENCES.md`
签核豁免可非致命,gate 从不自授豁免。

## 6. Gate 复活与 upstream-diff 重锚

### 6.1 t8n 差分 gate

- `OpT8nReplay.Vectors` 进 ctest 默认套件(同 ref 侧)。回放器向量目录定位机制保持
  ref 侧不变,只改路径常量。
- 验收判据同口径:opstack 123/123(21 suites)、31/31 向量、`known_diverges=0`、
  比对计数 ~3401 次量级吻合。向量翻红按 DIVERGENCES 三选一纪律归因,不许改向量变绿。
- generator/`regen.sh` 只搬不跑:本次不改向量,不触发再生成仪式。pin 校验、干净树契约
  原样保留。

### 6.2 upstream-diff 重锚(本次唯一需重新生成基准的资产)

- `EVMONE_GIT` 从 fork `3585c2cb` 改指官方 `ipsilon/evmone v0.21.0` 检出(与 vendor 同源)。
- 步骤:官方检出上跑 `upstream-diff.sh` → 零漂移则仅更新 manifest 的 REF 记载;
  行号级漂移则 `--regenerate-goldens` 并在 commit message 写明漂移内容。
- **STOP 条款**:漂移超出行号级(fork 曾改 `test/state` 语义)即设计前提被证伪
  (vendor 基准与 gate 绿灯基准不同源),停止并上报,不得静默重生成。
- `manifest.tsv` 路径 `bcos-evm-ref/...` → `bcos-evm/...`。

### 6.3 CI

`bcos-evm-opstack-tests` 随 FULLNODE 构建进 ctest(与 `BcosEvmEthTests` 并列)。
upstream-diff.sh 需外部 evmone 检出,只做本地/手动检查,不进 CI(同 ref 侧现状)。

## 7. 实施切分

每步可独立编译验证,顺序即依赖序:

1. **基准固化**:worktree 侧提交译英批次(单独 commit);SHA 回填 §2。
2. **vendor 补齐**:官方 v0.21.0 取 `test/utils` 最小集入 `eth/utils/`,并入
   `bcos-evm-eth`(statetest_loader 除外);编译绿。
3. **ETH 欠账**:StateDiffSanitize/Writeback/StateRootCompute 三件 + EthTransition
   两出口接线;现有 Boost.Test 目标回归绿。
4. **opstack 整层**:17 文件 + include 改写 + `bcos-evm-opstack` 目标;编译绿。
5. **测试复活**:vcpkg 加 gtest/nlohmann-json → 24 测试文件 + t8n 资产搬运 →
   `bcos-evm-opstack-tests` 全绿(123/123 + 31/31,`known_diverges=0`)。
6. **upstream-diff 重锚**:按 §6.2;manifest/golden 落账。
7. **文档回填**:README 移植说明(E-b park 限定原文保留)、DIVERGENCES 头注路径更新。

## 8. 验收清单

- [ ] `bcos-evm-opstack-tests` 123/123(21 suites),ctest 默认套件
- [ ] `OpT8nReplay.Vectors` 31/31,`known_diverges=0`,比对计数 ~3401
- [ ] 现有 `BcosEvmEthTests`(Boost.Test)与全仓 FULLNODE 构建零回归
- [ ] `git diff --exit-code -- test/opstack/t8n/vectors/` 为空(向量未被手改)
- [ ] `upstream-diff.sh` 对官方 v0.21.0 检出通过
- [ ] `ports/` 目录 diff 为空;库目标不链接 gtest/nlohmann

## 9. 风险与预案

| 风险 | 概率 | 预案 |
|---|---|---|
| fork@3585c2cb 曾改 `test/state`/`test/utils` 语义,vendor 与 gate 绿灯基准不同源 | 低 | §6.2 STOP 条款;gate 复跑 + upstream-diff 双证 |
| statetest loader 依赖闭包比预期大 | 中 | 闭包判据=链接成功;多 vendor 无害,守住库目标不进 nlohmann |
| gtest/nlohmann 与 vcpkg baseline 版本解析冲突 | 低 | 叶子依赖;冲突则 pin version |
| hash_fn-on-VM 底座引入未预期行为差 | 低 | 31 向量 gate 即判别器,翻红即定位 |

## 10. 边界重申

本移植交付 **reference gate 层面等价**;`pre` 播种 `InMemoryStateView` 不经真实账本,
**E-b park 限定不解除**(ref README spec §1.1 R2),不得据此宣称 OP 路径生产可用或
op-geth 生产等价。M3.5 P2 真账本桥接、Karst 真适配(现仅 Jovian 别名占位)均不在本次
范围。
