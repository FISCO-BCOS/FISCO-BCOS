# OP Stack 块级执行移植设计(bcos-evm-ref → bcos-evm vendored 底座)

日期:2026-07-24
状态:已评审(设计五节逐节用户确认)
来源分支:`feat-evm-mb1-block-execution`(worktree,挂本仓 .git,merge-base `6dcbcd07b`)
目标分支:`feat-evm-opstack-port`(自 `refactor-evmone-vm-hash-fn` HEAD 开出)

## 1. 背景与目标

`feat-evm-mb1-block-execution` 分支上的 `bcos-evm-ref` 模块已完成 OP Stack 块级执行全套:
`processOpBlock`/`sealOpBlock`、receipt/tx/state 三根建根、op-geth `v1.101702.2` 块级 t8n
差分 gate(33 向量、124 测试全绿、FINDING-1 已归零)。其底座是 vcpkg fork port
(`ywy2090/evmone@3585c2cb`,导出 `evmone::state`/`evmone::testutils` 导入目标)。

当前分支 `refactor-evmone-vm-hash-fn` 已将同一模块的 ETH 侧移植为 `bcos-evm`,换用
**vendored 底座**:evmone `test/state` 源码进树(`bcos-evm/eth/state/`),port 换官方
`ipsilon/evmone v0.21.0` 压缩包 + `fisco-sm3.patch`(hash_fn 挂 VM,port-version 7,
patch 只碰 `lib/evmone/*`,不碰 `test/`)。

本设计把 opstack 层(源码 + 全部验证资产)移植到 vendored 底座上,验收与 ref 侧同口径。

## 2. 已定决策

| 决策点 | 结论 |
|---|---|
| 验收范围 | **全保真**:源码 + 21 个测试文件(124 用例)+ 33 向量 t8n gate + upstream-diff 重锚 |
| 测试框架 | **加 gtest + nlohmann-json**(vcpkg,仅测试目标链接),测试代码零改写 |
| 移植基准 | `1cec91b27639cab7037bcf344d4109fd19334fff`(= efb6fd42e + 译英批次) |
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
│   └── opstack/                      # 30 文件整层移植(15 头 + 15 源)
├── test/
│   ├── eth/                          # 已有 Boost.Test 目标,不动
│   └── opstack/                      # GTest 目标 + t8n/{vectors,generator} + regen.sh
└── scripts/                          # upstream-diff.sh + golden + manifest(重锚官方 v0.21.0)
```

构建目标(与 ref 侧同构,alias 前缀 `bcosevmref::` → `bcosevm::`):

- `bcosevm::eth`(`bcos-evm-eth`,已有)← 并入 `eth/utils/` 源与 `StateRootCompute.cpp`
- `bcosevm::opstack`(`bcos-evm-opstack`)→ 链 `bcosevm::eth`
- `bcos-evm-opstack-tests`(GTest)→ 链 `bcosevm::opstack` + `GTest::gtest_main` + `nlohmann_json`;源另含 `statetest_loader.cpp`(测试侧 vendored loader)。ref 的 `test/main.cpp` 未被任何目标引用,不搬。编译期宏 `EVM_REF_OPSTACK_FIXTURES_DIR`/`OP_T8N_VECTORS_DIR` 沿用 ref 机制,指向移植后的源内路径

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
| `opstack/` 30 文件(15 头 + 15 源,~1920 行) | `bcos-evm/bcos-evm/opstack/` | 原样 + include 三规则改写 |
| `adapter/StateDiffSanitize.h`、`StateDiffWriteback.h` | `bcos-evm/bcos-evm/adapter/` | 原样 + 改写 |
| `adapter/StateRootCompute.{h,cpp}` | 同上 | 原样 + 改写;`.cpp` 并入 `bcos-evm-eth` |
| `adapter/StateViewAdapter.h` | 已存在 | 取 ref 版本对齐一次 |
| `eth/EthTransition.{h,cpp}` | 已存在 | 取 ref 版本(含 sanitize 接线)再改 include |

### 4.2 vendor 层(官方 `ipsilon/evmone v0.21.0` `test/utils/` → `eth/utils/`,最终清单)

最终落地清单(`bcos-evm/bcos-evm/eth/utils/` 实况核对,库目标):
`mpt.{hpp,cpp}`、`mpt_hash.{hpp,cpp}`、`rlp.hpp`、`rlp_encode.{hpp,cpp}`、
`test_state.{hpp,cpp}`、`statetest.hpp`、`utils.{hpp,cpp}`、`blob_schedule.{hpp,cpp}`、
`stdx/utility.hpp`。其中 `blob_schedule.{hpp,cpp}` 起始清单未列出,系 Task 5 Step 6
（测试复活阶段补齐依赖闭包时)补拷入库,已并入 `bcos-evm-eth` 库目标源
(`CMakeLists.txt` add_library 列表)。

测试侧(不进库目标,`nlohmann` 不渗入库):`bcos-evm/test/support/statetest_loader.cpp`,
按闭包判据"链接成功"拆出,单独编入 `bcos-evm-opstack-tests` 目标源列表。

**排除**(与起始清单一致,实况确认未引入):`blockchaintest*`、
`statetest_export`/runner 类纯 EEST 工具;`bytecode.hpp` 等未触发链接器要求,未补入。

vendored 源沿用既有惯例:上游原样不改逻辑,`eth/state/` 与 `eth/utils/` 全部
vendored `.cpp` 统一 `-Wno-missing-field-initializers` 抑制
(`bcos-evm/CMakeLists.txt` `set_source_files_properties`)。

vendor 基准必须与 `eth/state/` 既有 vendor 同源(官方 v0.21.0),不从 ywy2090 fork 取。

### 4.3 测试层

| ref 侧 | 目标 | 处理 |
|---|---|---|
| `test/opstack/` 整目录:21 个 `.cpp` + `OpL1AttributesTestHelpers.h` + `scripts/gen_7702_vectors.py`(7702 金值出处)+ `t8n/{cases,generator,vectors}` | `bcos-evm/test/opstack/` | 整目录搬运;仅顶层 `.cpp/.h` 做 include 三规则改写 |
| `test/fixtures/opstack/`(bin 语料) | `bcos-evm/test/fixtures/opstack/` | 逐字节搬运 |
| `t8n/vectors/`(432K,33 向量 JSON + manifest.txt + DIVERGENCES.md) | 同构搬运 | **逐字节不动**(含 DIVERGENCES.md;其中 `bcos-evm-ref` 路径引用视为出处记录,不改写) |
| `t8n/generator/`(含 `regen.sh`) | 同构搬运 | 只搬不跑;generator README 路径引用改写 |

### 4.4 依赖变更

- 根 `vcpkg.json` 新增 `gtest`、`nlohmann-json`(in-tree `TESTS=ON` 构建需要)。均为叶子依赖;与 baseline 解析冲突则 pin version。
- `bcos-evm` 当前**无** standalone vcpkg 清单;本次照 ref 模块补建 `bcos-evm/vcpkg.json`(deps: `boost-test`/`evmone`/`gtest`/`nlohmann-json`——`boost-test` 为既有 Boost.Test eth 测试目标 standalone 构建所需;builtin-baseline 沿用 ref 值)与 `bcos-evm/vcpkg-configuration.json`(overlay `../ports/{evmone,intx,blst}`),使 standalone 迭代构建可用。

### 4.5 明确不搬

`bcos-evm-ref/` 模块壳与 CMakeLists、fork portfile(16.5KB,`EVMONE_STATE` 导出机制)、
EEST 双套件与 blockchaintest loader、`spike/`(M3.5 读放大)、iwyu 工装(可后补)。

### 4.6 移植偏离台账

移植过程中(Task 1-7)出现的、与"sed 三规则改写、逐字节不动"字面约束有偏离但已
逐笔核实为语义等价/零风险的改动,统一在此登记,供后续核对与(如适用)回传
`bcos-evm-ref`:

**(a) clang-format pre-commit hook 触发的 include 重排(纯格式化,零语义)**

sed 把 `<bcos-evm-ref/...>`/`<test/state/...>`/`<test/utils/...>` 改写为
`<bcos-evm/...>` 后,字符串按字母序落到不同的 include 分组位置,触发本仓
clang-format 的 include 排序规则在提交时(pre-commit hook)自动重排。涉及
15 个文件,均只调整 `#include` 顺序,不增删行、不改变符号可见性或逻辑:

- `eth/EthTransition.h`(Task 3):1 处,`<bcos-evm/eth/state/state.hpp>` 从
  `<system_error>`/`<variant>` 之间移到 include 组最前。
- `opstack/` 11 个文件(Task 4,28 行):`OpBlockExecute.cpp`、`OpBlockSeal.cpp`、
  `OpBlockSeal.h`、`OpExecCommon.h`、`OpFeeParams.cpp`、`OpHost.cpp`、`OpHost.h`、
  `OpReceiptMeta.h`、`OpTransition.cpp`、`OpTransition.h`、`OpValidate.h`。
- 测试侧 3 个文件(Task 5):`test/opstack/Op7702Test.cpp`、
  `test/opstack/OpT8nReplayTest.cpp`、`test/support/statetest_loader.cpp`。

控制器已就 Task 3 的首次出现裁定等价口径:"sed 改写 + clang-format 归一化"——
约束本意是不改逻辑,非逐字节不可变;后续同类重排均按此口径记录,不手工改回。

**(b) `-Werror` 两处两行零行为修复(Task 7 Step 4 发现,建议回传 ref)**

standalone 构建(`BCOS_EVM_STANDALONE`)不继承根 `cmake/CompilerSettings.cmake`
的 `-Werror -Wall -Wextra -pedantic`,故 Task 2-6 从未在这组严格告警集下编译过
opstack 层;Task 7 Step 4 首次执行 in-tree(`add_subdirectory(bcos-evm)`,
FULLNODE)构建时触发并定位:

- `opstack/OpPredeploys.h:7` 前向声明 `struct TestState;` 与
  `eth/utils/test_state.hpp`(官方 v0.21.0 vendor)的 `class TestState` 定义
  不一致(`-Wmismatched-tags`)→ 改为 `class TestState;`。
- `opstack/OpTransition.cpp:193` 聚合初始化 `evmone::state::TransactionReceipt`
  缺第 8 个字段 `post_state`(`-Wmissing-field-initializers`)→ 补尾随 `{}`。

两处均非 `manifest.tsv` 追踪的"上游照抄段"(`OpPredeploys.h` 完全不在 manifest
范围;`OpTransition.cpp:193` 不落在该文件已追踪的三段 24-29/82-135/155-165 内),
属 FISCO 自有的 OP Stack 胶水代码,按 `bcos-evm/CMakeLists.txt` 政策("FISCO
自有代码保持全 `-Werror`")直接对齐声明/补全字段修复,不做文件级告警抑制。
`bcos-evm-ref` 若曾/将在同一严格告警集下构建,预期会遇到同一对告警,建议
回传对齐。详见 `.superpowers/sdd/task-7-report.md`"审查整改"节。

**(c) upstream-diff 5 段 golden 重生成(ref 侧欠账,已签核,ref 侧待补 regen)**

Task 6(upstream-diff 基准重锚官方 v0.21.0)首次对官方检出跑护栏时,
`apply_call_value`/`call_depth_guard`/`call_03_quirk`/`op_storage_root`/
`receipts_root_loop` 5 段 FAIL。经排查:该 5 段失败在 `$REF`
(`bcos-evm-ref`,HEAD `1cec91b27639`)用其自身原始 fork pin 独立复现,逐字节
相同——与本次基准重锚(fork→官方)无因果关系,根因是 `$REF` 侧
`885be8d76`(D-12:OpHost precompile override 分派重写)与
`5b91bfde6`/`9ef2fe6b1`(M-B2:OpBlockSeal receipts-root/withdrawals-root
落地)改动了照抄面之后,golden(`35283af1b` 固化)未同步重新生成的旧账。
用户复核归因证据后签核:视为"记录 ref 源码现实状态",授权
`--regenerate-goldens`,已在 Task 6 提交 `abd35de7e`。`$REF` 仓库自身的
`scripts/upstream-diff/golden/*.patch` 尚未同步这 5 段的重生成,仍是
`35283af1b` 时代的旧版本;若后续仍以 `$REF` 作为其他任务的搬运源,建议在
`$REF` 侧同步执行一次 `--regenerate-goldens` 并提交。详见
`.superpowers/sdd/task-6-report.md`"裁定与重生成"节。

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
零行为差;33 向量 gate 复跑即此等价性的实证,不另设专门测试。

**错误处理不变式**(随源码移植,不改):`opValidate` 失败→致命(G-1);
`applyStateDiffStrict` tripwire →测试失败而非静默;回放分歧→仅 `DIVERGENCES.md`
签核豁免可非致命,gate 从不自授豁免。

## 6. Gate 复活与 upstream-diff 重锚

### 6.1 t8n 差分 gate

- `OpT8nReplay.Vectors` 进 ctest 默认套件(同 ref 侧)。回放器向量目录定位机制保持
  ref 侧不变,只改路径常量。
- 验收判据同口径:opstack 124/124(21 suites)、33/33 向量、`known_diverges=0`、
  比对计数 3558 次(实测)。向量翻红按 DIVERGENCES 三选一纪律归因,不许改向量变绿。
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
   `bcos-evm-opstack-tests` 全绿(124/124 + 33/33,`known_diverges=0`)。
6. **upstream-diff 重锚**:按 §6.2;manifest/golden 落账。
7. **文档回填**:新建 `bcos-evm/README.md` 移植说明(E-b park 限定原文保留);DIVERGENCES.md 逐字节保留不改写;spec §2 基准 SHA 与 §4.2 最终 vendor 清单回填。

## 8. 验收清单

- [ ] `bcos-evm-opstack-tests` 124/124(21 suites),ctest 默认套件
- [ ] `OpT8nReplay.Vectors` 33/33,`known_diverges=0`,比对计数 3558
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
| hash_fn-on-VM 底座引入未预期行为差 | 低 | 33 向量 gate 即判别器,翻红即定位 |

## 10. 边界重申

本移植交付 **reference gate 层面等价**;`pre` 播种 `InMemoryStateView` 不经真实账本,
**E-b park 限定不解除**(ref README spec §1.1 R2),不得据此宣称 OP 路径生产可用或
op-geth 生产等价。M3.5 P2 真账本桥接、Karst 真适配(现仅 Jovian 别名占位)均不在本次
范围。
