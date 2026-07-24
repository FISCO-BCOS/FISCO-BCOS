# OP Stack 块级执行移植 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `feat-evm-mb1-block-execution` 分支 `bcos-evm-ref` 的 OP Stack 块级执行(源码 + 21 测试文件 + 31 向量 t8n gate + upstream-diff 护栏)全保真移植到当前 vendored 底座的 `bcos-evm` 模块。

**Architecture:** 方案 A 单模块扩展——opstack 作为 `bcos-evm` 第二子库(`bcosevm::opstack` 链 `bcosevm::eth`),evmone `test/utils` 最小集 vendor 进 `eth/utils/`,`ports/` 零改动。验证资产(GTest 套件 + t8n 向量 gate)原样复活,验收与 ref 侧同口径。

**Tech Stack:** C++20、CMake ≥3.25、vcpkg(官方 evmone v0.21.0 + fisco-sm3.patch,port-version 7)、GTest、nlohmann-json、Boost.Test(既有 eth 测试)。

**Spec:** `docs/superpowers/specs/2026-07-24-opstack-block-execution-port-design.md`

## Global Constraints

- `ports/` 目录 diff 必须为空;evmone port-version 停在 **7**。
- 命名空间沿用 `bcos::evmref`;alias 前缀 `bcosevm::`。
- include 改写仅三条规则(对移植文件与新 vendored 文件同等适用):
  1. `<test/state/X>` → `<bcos-evm/eth/state/X>`
  2. `<test/utils/X>` → `<bcos-evm/eth/utils/X>`
  3. `<bcos-evm-ref/Y>` → `<bcos-evm/Y>`
- vendored 源(`eth/state/`、`eth/utils/`)不改逻辑,仅 include 改写;编译告警用 `-Wno-missing-field-initializers` 抑制,不改上游代码。
- 库目标 `bcosevm::eth`/`bcosevm::opstack` 不得链接 gtest/nlohmann(nlohmann 只许进测试目标)。
- `test/opstack/t8n/vectors/` 全目录(向量 JSON、manifest.txt、DIVERGENCES.md)**逐字节不动**;测试代码除 include 三规则外零改写。
- vendor 与 upstream-diff 基准同源:官方 `ipsilon/evmone` tag `v0.21.0`,不从 ywy2090 fork 取。
- macOS/zsh 环境:`sed -i ''`(BSD sed);git/构建命令一律 `rtk` 前缀(如 `rtk git add`)。
- 环境变量约定(每个任务开头重新导出,子代理不共享 shell):

```bash
export FB=/Users/octopus/octo/code/FISCO-BCOS
export REF=/Users/octopus/octo/code/blockchain-impl/FISCO-BCOS/.claude/worktrees/mb1-block-execution
export SCRATCH=/private/tmp/claude-502/-Users-octopus-octo-code-FISCO-BCOS/916d54b9-ac0d-407e-a7cd-587f6e393af6/scratchpad
export EVMONE=$SCRATCH/evmone-v0.21.0
```

---

### Task 1: 基准固化与分支创建

**Files:**
- Modify(worktree 侧): `$REF/bcos-evm-ref/**`(提交 30 个未提交译英文件)
- Modify: `$FB/docs/superpowers/specs/2026-07-24-opstack-block-execution-port-design.md:§2`(回填 SHA)

**Interfaces:**
- Produces: 基准 commit `$REF_SHA`(后续所有拷贝的唯一来源);分支 `feat-evm-opstack-port`。

- [ ] **Step 1: worktree 侧提交译英批次**

```bash
export REF=/Users/octopus/octo/code/blockchain-impl/FISCO-BCOS/.claude/worktrees/mb1-block-execution
cd $REF && rtk git status   # 预期:仅 bcos-evm-ref/ 下 30 个 M 文件,无其他改动
rtk git add bcos-evm-ref && rtk git commit -m "docs(evm-ref): 注释中译英批次(30 文件,非功能变更)——opstack 移植基准固化"
export REF_SHA=$(git -C $REF rev-parse HEAD) && echo $REF_SHA
```

预期:commit 成功,`REF_SHA` 为新 HEAD。若 `git status` 出现 bcos-evm-ref 之外的改动:STOP,上报后再继续。

- [ ] **Step 2: 目标仓创建移植分支**

```bash
export FB=/Users/octopus/octo/code/FISCO-BCOS
cd $FB && rtk git switch -c feat-evm-opstack-port
rtk git log --oneline -1   # 预期:HEAD 含本 plan/spec 提交,分支名 feat-evm-opstack-port
```

- [ ] **Step 3: 回填 spec §2 基准 SHA**

编辑 spec §2 决策表"移植基准"行,把 `efb6fd42e + worktree 未提交译英批次(搬运前先在 worktree 侧提交固化,SHA 落账于此)` 改为 `` `<REF_SHA 实值>`(= efb6fd42e + 译英批次) ``。

- [ ] **Step 4: Commit**

```bash
rtk git add docs/superpowers/specs/2026-07-24-opstack-block-execution-port-design.md
rtk git commit -m "docs(spec): 回填移植基准 SHA"
```

---

### Task 2: vendor 补齐(eth/utils)+ standalone 清单

**Files:**
- Create: `bcos-evm/bcos-evm/eth/utils/{mpt.hpp,mpt.cpp,mpt_hash.hpp,mpt_hash.cpp,rlp.hpp,rlp_encode.hpp,test_state.hpp,test_state.cpp,statetest.hpp,utils.hpp,utils.cpp}`(以官方源实况为准,多退少补)
- Create: `bcos-evm/vcpkg.json`、`bcos-evm/vcpkg-configuration.json`
- Modify: `bcos-evm/CMakeLists.txt`(bcos-evm-eth 源列表 + 告警抑制)

**Interfaces:**
- Consumes: 官方 evmone v0.21.0 检出 `$EVMONE`。
- Produces: `<bcos-evm/eth/utils/mpt_hash.hpp>`(`evmone::state::mpt_hash`)、`<bcos-evm/eth/utils/test_state.hpp>`(`evmone::test::TestState`)、`<bcos-evm/eth/utils/rlp.hpp>`(`evmone::rlp`)可被 Task 3/4/5 include;`statetest.hpp` 仅供测试目标(Task 5)。standalone 构建管线(`bcos-evm/build`)。

- [ ] **Step 1: 克隆官方 evmone v0.21.0**

```bash
export SCRATCH=/private/tmp/claude-502/-Users-octopus-octo-code-FISCO-BCOS/916d54b9-ac0d-407e-a7cd-587f6e393af6/scratchpad
export EVMONE=$SCRATCH/evmone-v0.21.0
git clone --depth 1 --branch v0.21.0 https://github.com/ipsilon/evmone $EVMONE
ls $EVMONE/test/utils/    # 记录实际文件清单(本任务与 Task 5 的拷贝依据)
cat $EVMONE/test/utils/CMakeLists.txt   # 确认 testutils 库的源文件集合
```

- [ ] **Step 2: 拷贝最小集并做 include 改写**

```bash
cd $FB/bcos-evm/bcos-evm/eth/utils
for f in mpt.hpp mpt.cpp mpt_hash.hpp mpt_hash.cpp rlp.hpp rlp_encode.hpp \
         test_state.hpp test_state.cpp statetest.hpp utils.hpp utils.cpp; do
  [ -f $EVMONE/test/utils/$f ] && cp $EVMONE/test/utils/$f . || echo "SKIP(不存在): $f"
done
# 若 Step 1 的 CMakeLists 显示 rlp_encode.cpp 等额外源存在且被 testutils 编译,一并拷入
LC_ALL=C sed -i '' -e 's|<test/state/|<bcos-evm/eth/state/|g' -e 's|<test/utils/|<bcos-evm/eth/utils/|g' *.hpp *.cpp
```

注意:`statetest_loader.cpp` **不在**本步(测试侧源,Task 5 处理);`blockchaintest*`、`statetest_export*` 不拷。

- [ ] **Step 3: 检查库源无 nlohmann 渗入**

```bash
rg -l "nlohmann" $FB/bcos-evm/bcos-evm/ | rg -v statetest.hpp ; echo "exit=$?"
```

预期:除 `statetest.hpp`(纯头,只被测试 TU include)外无命中(exit=1)。若 `test_state.cpp`/`utils.cpp` 命中:把该 `.cpp` 从 Step 4 的库源列表移到 Task 5 的测试目标源列表。

- [ ] **Step 4: CMake 并入库源**

`bcos-evm/CMakeLists.txt` 的 `add_library(bcos-evm-eth STATIC ...)` 源列表追加(以 Step 2 实拷为准):

```cmake
    bcos-evm/eth/utils/mpt.cpp
    bcos-evm/eth/utils/mpt_hash.cpp
    bcos-evm/eth/utils/test_state.cpp
    bcos-evm/eth/utils/utils.cpp
```

同文件既有 `set_source_files_properties(... -Wno-missing-field-initializers)` 列表追加同上四个(或实拷集合的).cpp 路径。

- [ ] **Step 5: 建 standalone vcpkg 清单**

`bcos-evm/vcpkg.json`:

```json
{
    "name": "bcos-evm",
    "version-string": "0.1.0",
    "builtin-baseline": "f6729a3ac3bfdefc999aa8e3664f8014370886b8",
    "dependencies": [
        "boost-test",
        "evmone",
        "gtest",
        "nlohmann-json"
    ]
}
```

`bcos-evm/vcpkg-configuration.json`:

```json
{
    "overlay-ports": [
        "../ports/evmone",
        "../ports/intx",
        "../ports/blst"
    ]
}
```

- [ ] **Step 6: standalone 配置并构建**

```bash
cd $FB
cmake -B bcos-evm/build -S bcos-evm -DCMAKE_TOOLCHAIN_FILE=$FB/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build bcos-evm/build --target bcos-evm-eth -j8
```

预期:配置与编译成功(首次配置会 vcpkg 装 evmone/gtest/nlohmann-json)。若 vcpkg 报 baseline 不可用:`builtin-baseline` 改为 `git -C $FB/vcpkg rev-parse HEAD` 的输出重试。

- [ ] **Step 7: 既有 Boost 测试回归**

```bash
cmake --build bcos-evm/build --target bcos-evm-eth-tests -j8
ctest --test-dir bcos-evm/build -R BcosEvmEthTests --output-on-failure
```

预期:PASS(vendor 追加不得破坏既有 eth 目标)。

- [ ] **Step 8: Commit**

```bash
cd $FB && rtk git add bcos-evm && rtk git commit -m "build(bcos-evm): vendor evmone v0.21.0 test/utils 最小集入 eth/utils + standalone vcpkg 清单"
```

---

### Task 3: ETH 侧欠账(adapter 三件 + EthTransition sanitize 接线)

**Files:**
- Create: `bcos-evm/bcos-evm/adapter/{StateDiffSanitize.h,StateDiffWriteback.h,StateRootCompute.h,StateRootCompute.cpp}`
- Modify(以 ref 版覆盖): `bcos-evm/bcos-evm/adapter/StateViewAdapter.h`、`bcos-evm/bcos-evm/eth/EthTransition.h`、`bcos-evm/bcos-evm/eth/EthTransition.cpp`
- Modify: `bcos-evm/CMakeLists.txt`(库源 + `StateRootCompute.cpp`)

**Interfaces:**
- Consumes: Task 2 的 `<bcos-evm/eth/utils/mpt_hash.hpp>`、`<bcos-evm/eth/utils/test_state.hpp>`。
- Produces: `bcos::evmref::sanitizeStateDiff(const StateView&, StateDiff)`、`bcos::evmref::applyStateDiffStrict`、`bcos::evmref::stateRootOf`(签名以 ref 头文件为准,Task 4/5 依赖);`runTransaction`/`finalize` 出口已消毒。

- [ ] **Step 1: 拷贝并改写**

```bash
cd $FB/bcos-evm/bcos-evm
cp $REF/bcos-evm-ref/bcos-evm-ref/adapter/StateDiffSanitize.h   adapter/
cp $REF/bcos-evm-ref/bcos-evm-ref/adapter/StateDiffWriteback.h  adapter/
cp $REF/bcos-evm-ref/bcos-evm-ref/adapter/StateRootCompute.h    adapter/
cp $REF/bcos-evm-ref/bcos-evm-ref/adapter/StateRootCompute.cpp  adapter/
cp $REF/bcos-evm-ref/bcos-evm-ref/adapter/StateViewAdapter.h    adapter/
cp $REF/bcos-evm-ref/bcos-evm-ref/eth/EthTransition.h           eth/
cp $REF/bcos-evm-ref/bcos-evm-ref/eth/EthTransition.cpp         eth/
LC_ALL=C sed -i '' -e 's|<test/state/|<bcos-evm/eth/state/|g' -e 's|<test/utils/|<bcos-evm/eth/utils/|g' -e 's|<bcos-evm-ref/|<bcos-evm/|g' \
  adapter/StateDiffSanitize.h adapter/StateDiffWriteback.h adapter/StateRootCompute.h adapter/StateRootCompute.cpp adapter/StateViewAdapter.h eth/EthTransition.h eth/EthTransition.cpp
```

- [ ] **Step 2: CMake 并入 StateRootCompute.cpp**

`add_library(bcos-evm-eth STATIC ...)` 源列表追加:

```cmake
    bcos-evm/adapter/StateRootCompute.cpp
```

(FISCO 自有代码,不进告警抑制列表。)

- [ ] **Step 3: 构建 + 既有测试回归**

```bash
cmake --build bcos-evm/build --target bcos-evm-eth bcos-evm-eth-tests -j8
ctest --test-dir bcos-evm/build -R BcosEvmEthTests --output-on-failure
```

预期:PASS。**若 EthTransitionTest 因 sanitize 后 state_diff 内容变化而 FAIL**:以 `$REF/bcos-evm-ref/test/eth/EthTransitionTest.cpp`(FINDING-1 修复后语义)为金标准对齐 Boost 版断言——只改断言期望,不改库代码;若分歧超出断言期望层面,STOP 上报。

- [ ] **Step 4: Commit**

```bash
cd $FB && rtk git add bcos-evm && rtk git commit -m "feat(bcos-evm): StateDiff 消毒/严格写回/stateRootOf 三件 + EthTransition 两出口接线(FINDING-1 对齐)"
```

---

### Task 4: opstack 整层移植

**Files:**
- Create: `bcos-evm/bcos-evm/opstack/`(ref `opstack/` 全部 30 个文件,15 头 + 15 源)
- Modify: `bcos-evm/CMakeLists.txt`(新目标 `bcos-evm-opstack`)

**Interfaces:**
- Consumes: Task 3 的 adapter 三件与 `bcosevm::eth`。
- Produces: `bcosevm::opstack` 目标;`processOpBlock`/`sealOpBlock`/`opValidate`/`opTransition`/`runDeposit` 等 API(签名以 ref 头文件为准),Task 5 测试直接消费。

- [ ] **Step 1: 整层拷贝并改写**

```bash
mkdir -p $FB/bcos-evm/bcos-evm/opstack
cp $REF/bcos-evm-ref/bcos-evm-ref/opstack/*.h $REF/bcos-evm-ref/bcos-evm-ref/opstack/*.cpp $FB/bcos-evm/bcos-evm/opstack/
cd $FB/bcos-evm/bcos-evm/opstack
LC_ALL=C sed -i '' -e 's|<test/state/|<bcos-evm/eth/state/|g' -e 's|<test/utils/|<bcos-evm/eth/utils/|g' -e 's|<bcos-evm-ref/|<bcos-evm/|g' *.h *.cpp
ls | wc -l   # 预期:30(15 头 + 15 源)
```

- [ ] **Step 2: CMake 新目标**

`bcos-evm/CMakeLists.txt` 在 `bcos-evm-eth` 块之后追加(源列表照抄 ref `CMakeLists.txt` 的 15 个 .cpp):

```cmake
add_library(bcos-evm-opstack STATIC
    bcos-evm/opstack/OpForkSchedule.cpp
    bcos-evm/opstack/OpHost.cpp
    bcos-evm/opstack/OpPrecompiles.cpp
    bcos-evm/opstack/OpFeeParams.cpp
    bcos-evm/opstack/OpPredeploys.cpp
    bcos-evm/opstack/RollupCost.cpp
    bcos-evm/opstack/OpValidate.cpp
    bcos-evm/opstack/OpTransition.cpp
    bcos-evm/opstack/OpExecCommon.cpp
    bcos-evm/opstack/OpDepositTx.cpp
    bcos-evm/opstack/OpReceiptMeta.cpp
    bcos-evm/opstack/OpBlockFinalize.cpp
    bcos-evm/opstack/OpBlockExecute.cpp
    bcos-evm/opstack/OpReceiptEncode.cpp
    bcos-evm/opstack/OpBlockSeal.cpp
)
add_library(bcosevm::opstack ALIAS bcos-evm-opstack)
target_include_directories(bcos-evm-opstack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bcos-evm-opstack PUBLIC bcosevm::eth)
```

- [ ] **Step 3: 构建**

```bash
cmake --build $FB/bcos-evm/build --target bcos-evm-opstack -j8
```

预期:编译链接成功。**若 in-tree/-Werror 场景报上游照抄段告警**:对触发的具体 .cpp 追加 `set_source_files_properties(... PROPERTIES COMPILE_OPTIONS "-Wno-<具体告警>")`,不改源码。

- [ ] **Step 4: Commit**

```bash
cd $FB && rtk git add bcos-evm && rtk git commit -m "feat(bcos-evm): opstack 块级执行整层移植(processOpBlock/sealOpBlock/deposit/fee/receipt)"
```

---

### Task 5: 测试复活(GTest 套件 + t8n gate)

**Files:**
- Modify: `vcpkg.json`(根清单,dependencies 数组加 `"gtest"`、`"nlohmann-json"`)
- Create: `bcos-evm/test/opstack/`(整目录:21 个 `.cpp` + `OpL1AttributesTestHelpers.h` + `scripts/gen_7702_vectors.py` + `t8n/{cases,generator,vectors}`)
- Create: `bcos-evm/test/fixtures/opstack/`(bin 语料)
- Create: `bcos-evm/test/support/statetest_loader.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `bcosevm::opstack`(Task 4)、`statetest.hpp`(Task 2)。
- Produces: ctest 目标 `BcosEvmOpstackTests`(123 用例含 `OpT8nReplay.Vectors`)。

- [ ] **Step 1: 根 vcpkg.json 加测试依赖**

在根 `vcpkg.json` 顶层 `"dependencies"` 数组中(字母序插入)追加两个条目:

```json
    "gtest",
    "nlohmann-json",
```

- [ ] **Step 2: 拷贝测试资产**

```bash
mkdir -p $FB/bcos-evm/test/fixtures $FB/bcos-evm/test/support
cp -R $REF/bcos-evm-ref/test/opstack $FB/bcos-evm/test/   # 整目录:21 .cpp + helper .h + scripts/gen_7702_vectors.py + t8n/{cases,generator,vectors}
cp -R $REF/bcos-evm-ref/test/fixtures/opstack $FB/bcos-evm/test/fixtures/
cd $FB/bcos-evm/test/opstack
LC_ALL=C sed -i '' -e 's|<test/state/|<bcos-evm/eth/state/|g' -e 's|<test/utils/|<bcos-evm/eth/utils/|g' -e 's|<bcos-evm-ref/|<bcos-evm/|g' *.cpp *.h
```

注意:sed 只作用于 `test/opstack/` 顶层 `.cpp/.h`,**不得**触碰 `t8n/` 子树。

- [ ] **Step 3: 向量逐字节校验**

```bash
diff -r $REF/bcos-evm-ref/test/opstack/t8n/vectors $FB/bcos-evm/test/opstack/t8n/vectors && echo BYTE-IDENTICAL
```

预期:输出 `BYTE-IDENTICAL`。

- [ ] **Step 4: vendored 测试侧 loader**

```bash
cp $EVMONE/test/utils/statetest_loader.cpp $FB/bcos-evm/test/support/
LC_ALL=C sed -i '' -e 's|<test/state/|<bcos-evm/eth/state/|g' -e 's|<test/utils/|<bcos-evm/eth/utils/|g' $FB/bcos-evm/test/support/statetest_loader.cpp
```

若 loader 在官方树的实际位置/文件名不同(以 Task 2 Step 1 的 `ls`/CMakeLists 记录为准),按实际路径拷贝;若其 include 闭包缺头(如 `blob_schedule.hpp`),把缺的头从 `$EVMONE/test/utils/` 补拷进 `eth/utils/` 并同样 sed 改写。

- [ ] **Step 5: 测试 CMake**

`bcos-evm/test/CMakeLists.txt` 追加(测试文件列表照抄 ref,不增不减):

```cmake
find_package(GTest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(bcos-evm-opstack-tests
    support/statetest_loader.cpp
    opstack/OpForkScheduleTest.cpp
    opstack/OpHostTest.cpp
    opstack/OpPrecompilesTest.cpp
    opstack/OpFeeParamsTest.cpp
    opstack/OpDepositTxTest.cpp
    opstack/OpPredeploysTest.cpp
    opstack/RollupCostTest.cpp
    opstack/OpValidateTest.cpp
    opstack/OpTransitionTest.cpp
    opstack/OpDepositTest.cpp
    opstack/OpBlockHarnessTest.cpp
    opstack/OpZeroDiffTest.cpp
    opstack/OpReceiptMetaTest.cpp
    opstack/Op7702Test.cpp
    opstack/OpFloorGasTest.cpp
    opstack/OpBlockFinalizeTest.cpp
    opstack/OpBlockExecuteTest.cpp
    opstack/OpStateDiffSanitizeTest.cpp
    opstack/OpReceiptEncodeTest.cpp
    opstack/OpBlockSealTest.cpp
    opstack/OpT8nReplayTest.cpp
)
target_compile_definitions(bcos-evm-opstack-tests PRIVATE
    EVM_REF_OPSTACK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/opstack"
    OP_T8N_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/opstack/t8n/vectors"
)
target_link_libraries(bcos-evm-opstack-tests PRIVATE
    bcosevm::opstack
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)
add_test(NAME BcosEvmOpstackTests COMMAND bcos-evm-opstack-tests)
```

- [ ] **Step 6: 构建并跑全套**

```bash
cmake --build $FB/bcos-evm/build --target bcos-evm-opstack-tests -j8
ctest --test-dir $FB/bcos-evm/build -R BcosEvmOpstackTests --output-on-failure
```

预期:`[  PASSED  ] 123 tests`(21 suites)。链接若报 undefined symbol 且符号属 `$EVMONE/test/utils` 某未拷源文件:补拷该 `.cpp` 入 `eth/utils/`(sed 改写 + CMake 库源追加,Task 2 惯例),重来本步;最终实拷清单留待 Task 7 回填 spec。

- [ ] **Step 7: gate 单独复核**

```bash
$FB/bcos-evm/build/test/bcos-evm-opstack-tests --gtest_filter='OpT8nReplay.Vectors' 2>&1 | tail -20
```

预期:31/31 向量回放通过、`known_diverges=0`、比对计数 ~3401 量级。**任何向量翻红:按 DIVERGENCES 三选一纪律归因,禁止改向量变绿**;若归因指向底座差异(hash_fn/官方 v0.21.0 vs fork),STOP 上报(spec §6.2 前提证伪)。

- [ ] **Step 8: 库目标纯净复查**

```bash
rg -l "nlohmann|gtest" $FB/bcos-evm/bcos-evm/ | rg -v statetest.hpp ; echo "exit=$?"
```

预期:exit=1(无命中)。

- [ ] **Step 9: Commit(两笔)**

```bash
cd $FB
rtk git add vcpkg.json && rtk git commit -m "build: 根清单加 gtest + nlohmann-json(测试期依赖)"
rtk git add bcos-evm && rtk git commit -m "test(bcos-evm): opstack GTest 套件 + t8n 向量 gate 全保真复活(123 用例,31/31 向量)"
```

(若 Step 6 曾补拷 utils 源,`bcos-evm` 一并入第二笔。)

---

### Task 6: upstream-diff 照抄面护栏重锚

**Files:**
- Create: `bcos-evm/scripts/upstream-diff.sh`、`bcos-evm/scripts/upstream-diff/{manifest.tsv,golden/*.patch}`
- Modify: `manifest.tsv`(路径列 `bcos-evm-ref/` → `bcos-evm/`)、脚本内 pinned REF

**Interfaces:**
- Consumes: `$EVMONE`(Task 2 的官方 v0.21.0 检出,含 .git)。
- Produces: 对官方基准可通过的 `./bcos-evm/scripts/upstream-diff.sh`。

- [ ] **Step 1: 拷贝并改路径**

```bash
mkdir -p $FB/bcos-evm/scripts
cp $REF/bcos-evm-ref/scripts/upstream-diff.sh $FB/bcos-evm/scripts/
cp -R $REF/bcos-evm-ref/scripts/upstream-diff $FB/bcos-evm/scripts/
LC_ALL=C sed -i '' 's|bcos-evm-ref/bcos-evm-ref/|bcos-evm/bcos-evm/|g' $FB/bcos-evm/scripts/upstream-diff/manifest.tsv $FB/bcos-evm/scripts/upstream-diff.sh
```

再人工核对脚本内相对路径推导(脚本可能以自身位置推 repo 根),确保指向 `bcos-evm/`。

- [ ] **Step 2: 更新 pinned REF**

```bash
export OFFICIAL_SHA=$(git -C $EVMONE rev-parse HEAD) && echo $OFFICIAL_SHA
rg -n "3585c2cb" $FB/bcos-evm/scripts/
```

把脚本/manifest 中所有 `3585c2cb...` 引用替换为 `$OFFICIAL_SHA`(注释中的历史叙述保留)。

- [ ] **Step 3: 对官方基准运行**

```bash
cd $FB && EVMONE_GIT=$EVMONE ./bcos-evm/scripts/upstream-diff.sh; echo "exit=$?"
```

预期:exit=0(golden 零漂移)。若非零:`./bcos-evm/scripts/upstream-diff.sh --show <segment>` 逐段核查——**仅行号/空白级漂移** → `--regenerate-goldens` 并在 commit message 写明;**内容级漂移(fork 曾改 test/state 语义)** → STOP 上报,spec §6.2 前提证伪,不得静默重生成。

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/scripts && rtk git commit -m "chore(bcos-evm): upstream-diff 护栏重锚官方 evmone v0.21.0(REF ${OFFICIAL_SHA:0:9})"
```

---

### Task 7: 文档回填与全量验收

**Files:**
- Create: `bcos-evm/README.md`
- Modify: `docs/superpowers/specs/2026-07-24-opstack-block-execution-port-design.md`(§4.2 最终 vendor 清单回填)

**Interfaces:**
- Consumes: Task 1–6 全部产出。
- Produces: 可交付的移植分支(验收清单全绿)。

- [ ] **Step 1: 写 README**

`bcos-evm/README.md`:

```markdown
# bcos-evm

复用 evmone(官方 `ipsilon/evmone` v0.21.0 + `ports/evmone/fisco-sm3.patch`,
hash_fn 挂 VM)的 ETH + OP Stack 执行参考模块。自 `bcos-evm-ref`
(分支 `feat-evm-mb1-block-execution`,基准 <REF_SHA>)全保真移植,
底座由 vcpkg 导出 `evmone::state` 改为 vendored 源
(`bcos-evm/eth/state/`、`bcos-evm/eth/utils/`,取自官方 v0.21.0,仅 include 改写)。

- `eth/`:ETH 状态转换内核(EthTransition + vendored state/utils)
- `adapter/`:StateDiff 消毒/严格写回/stateRootOf/StateView 适配
- `opstack/`:OP 薄层(processOpBlock/sealOpBlock/deposit/fee/receipt)
- `test/opstack/`:21 测试文件 123 用例,含 `OpT8nReplay.Vectors`
  块级 op-geth(pinned v1.101702.2)差分 gate(31 向量,ctest 常驻)
- `scripts/upstream-diff.sh`:照抄面静态护栏(EVMONE_GIT 指官方 v0.21.0 检出)

## Build(standalone)

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
    cmake --build build
    ctest --test-dir build --output-on-failure

## 边界(继承 ref spec §1.1 R2,E-b park 不解除)

t8n gate 的 `pre` 播种 `InMemoryStateView`,不经真实账本;本模块绿灯
**不构成** OP 路径生产可用或 op-geth 生产等价的宣称依据。M3.5 P2
真账本桥接、Karst 真适配(现仅 Jovian 别名占位)均未做。
向量再生成纪律与 DIVERGENCES 豁免流程见
`test/opstack/t8n/generator/README.md` 与 vectors/DIVERGENCES.md(出处
记录中的 `bcos-evm-ref` 路径为历史原貌,有意保留)。
```

`<REF_SHA>` 用 Task 1 实值替换。

- [ ] **Step 2: 回填 spec §4.2 最终 vendor 清单**

按 `ls bcos-evm/bcos-evm/eth/utils/` 与 `test/support/` 实况,把 spec §4.2 的"起始清单"段落改为"最终清单"并如实列出(含 Task 5 Step 6 补拷项)。

- [ ] **Step 3: 全量验收清单核验**

```bash
cd $FB
ctest --test-dir bcos-evm/build --output-on-failure          # BcosEvmEthTests + BcosEvmOpstackTests 全 PASS
rtk git diff refactor-evmone-vm-hash-fn..HEAD --stat -- ports/  # 预期:空(ports 零改动)
diff -r $REF/bcos-evm-ref/test/opstack/t8n/vectors bcos-evm/test/opstack/t8n/vectors  # 预期:无输出
EVMONE_GIT=$EVMONE ./bcos-evm/scripts/upstream-diff.sh          # 预期:exit=0
```

- [ ] **Step 4: 全仓 in-tree 构建回归**

```bash
# 若根 build/ 已配置:
cmake --build $FB/build -j8
# 否则(耗时,FULLNODE 全量):
cmake -B $FB/build -S $FB -DCMAKE_TOOLCHAIN_FILE=$FB/vcpkg/scripts/buildsystems/vcpkg.cmake -DFULLNODE=ON -DTESTS=ON && cmake --build $FB/build -j8
```

预期:构建成功(bcos-evm 子目录随 FULLNODE 编译,含测试目标)。本地资源不足时:如实记录"待 CI 验证",不得宣称已验。

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/README.md docs/superpowers/specs/2026-07-24-opstack-block-execution-port-design.md
rtk git commit -m "docs(bcos-evm): 移植 README(E-b park 限定保留)+ spec vendor 清单回填"
```

---

## 验收清单(全部满足才算完,来自 spec §8)

- [ ] `bcos-evm-opstack-tests` 123/123(21 suites),ctest 默认套件
- [ ] `OpT8nReplay.Vectors` 31/31,`known_diverges=0`,比对计数 ~3401
- [ ] 既有 `BcosEvmEthTests`(Boost.Test)与全仓 FULLNODE 构建零回归(本地或 CI)
- [ ] vectors 目录与 ref 基准 `diff -r` 为空
- [ ] `upstream-diff.sh` 对官方 v0.21.0 检出 exit=0
- [ ] `ports/` diff 为空;库目标源无 nlohmann/gtest 引用
