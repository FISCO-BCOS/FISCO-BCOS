# bcos-evm

复用 evmone(官方 `ipsilon/evmone` v0.21.0 + `ports/evmone/fisco-sm3.patch`,
hash_fn 挂 VM)的 ETH + OP Stack 执行参考模块。自 `bcos-evm-ref`
(分支 `feat-evm-mb1-block-execution`,基准 1cec91b27639cab7037bcf344d4109fd19334fff)
全保真移植,底座由 vcpkg 导出 `evmone::state` 改为 vendored 源
(`bcos-evm/eth/state/`、`bcos-evm/eth/utils/`,取自官方 v0.21.0,仅 include 改写)。

- `eth/`:ETH 状态转换内核(EthTransition + vendored state/utils)
- `adapter/`:StateDiff 消毒/严格写回/stateRootOf/StateView 适配
- `opstack/`:OP 薄层(processOpBlock/sealOpBlock/deposit/fee/receipt)
- `ledger/`:双后端账本抽象(真账本桥,design
  `docs/superpowers/specs/2026-07-27-real-ledger-bridge-design.md`)——`MemoryLedger`
  自研内存账本(C 路线 step 1 账本本体件)与 `Storage2Ledger`(storage2 真桥,同步直读 +
  块级读缓存 + 毒旗错误通道,纯模板头)共享 `StateView`/`applyDiff`/`visitAccounts` 抽象,
  `LedgerSeed.h` 提供统一播种。`Storage2Ledger` 依赖 `bcos-framework`(storage2/EVMAccount/
  task),仅在 in-tree 构建(`bcos-framework` 目标存在)参与编译;standalone 构建只交付
  `MemoryLedger` 部分,见下方"条件编译边界"。
- `test/`(`eth/` + `opstack/`)：终审修复后分支实况 27 测试文件——in-tree `build/`
  `bcos-evm-opstack-tests` 156 用例、standalone `bcos-evm/build/` 同名目标 131 用例
  (双路口径同下方"条件编译边界"一节,差值 25 = `Storage2LedgerTest`/`LedgerRootTest`/
  `EbT8nReplayTest` 三个仅 in-tree 编译的守卫文件用例数;数字随后续任务增测浮动,不作为
  硬编码基线),含 `OpT8nReplay.Vectors`
  块级 op-geth(pinned v1.101702.2)差分 gate(33 向量,ctest 常驻)。真账本桥新增三腿回放
  互为对照:`OpT8nReplay`(TestState,既有不动)/`MemoryLedgerT8nReplay`(33 向量)/
  `EbT8nReplay`(33 向量,storage2 真桥,仅 in-tree),`Storage2LedgerTest`/`LedgerRootTest`
  两组专项单测同一构建边界。
- `scripts/upstream-diff.sh`:照抄面静态护栏(EVMONE_GIT 指官方 v0.21.0 检出)

## 条件编译边界(standalone vs in-tree)

`Storage2Ledger.h` 是纯模板头,直接引用 `bcos-framework`(`storage2`/`ledger::EVMAccount`/
`task::syncWait`)的公开头——单向依赖(框架不回依赖 `bcos-evm`),但这意味着它只能在
`bcos-framework` 目标可见处编译。`bcos-evm/test/CMakeLists.txt` 用
`if(TARGET bcos-framework)` 守卫把 `Storage2LedgerTest.cpp`/`LedgerRootTest.cpp`/
`EbT8nReplayTest.cpp` 三个源码只并入 in-tree 构建(根仓库 `add_subdirectory(bcos-framework)`
早于 `add_subdirectory(bcos-evm)` 的那条路径)。standalone 构建(本文件"Build(standalone)"
一节的独立 vcpkg.json,不含 tbb/magic-enum/proxy/wedprcrypto 等 `bcos-framework` 传递依赖)
不满足该条件,三个守卫源码**不参与编译**,`Storage2Ledger`/`Storage2Backend`/`EbT8nReplay`
在 standalone 产物中不存在——standalone 交付的是账本抽象的 `MemoryLedger` 部分(`ledger/`
目录本体、`MemoryLedgerT8nReplayTest.cpp`、`MemoryLedgerTest.cpp` 均无条件编译,两路构建
一致)。实测(终审修复后):in-tree `build/` 156 个 `bcos-evm-opstack-tests` 用例
(含终审 I-1 新增的 `Storage2Ledger.HasStorageFalseAfterLogicalDeleteOfOnlySlot` 墓碑
变体回归测试);standalone `bcos-evm/build/` 131 个(不变——新增测试位于三个守卫文件
之一 `Storage2LedgerTest.cpp`,不参与 standalone 编译);差值 25 = 上述三个守卫文件的
用例数,双路 `ctest` 均全绿。

## Build(standalone)

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
    cmake --build build
    ctest --test-dir build --output-on-failure

## 边界(继承 ref spec §1.1 R2,E-b park 部分解除)

`OpT8nReplay.Vectors` 腿的 `pre` 仍播种 `InMemoryStateView`(TestState),不经真实账本,
该腿本身不动。但真账本桥(`ledger/`,design
`docs/superpowers/specs/2026-07-27-real-ledger-bridge-design.md`)已交付并接线验证:
`EbT8nReplay.Vectors` 把同一 33 向量经 `LedgerSeed`→`Storage2Ledger` 复播于内存版 storage2
（`MultiLayerStorage`/`StateKey`/`EVMAccount` 键空间）之上,33/33、`known_diverges=0`,
storage2 实际读写调用计数常驻断言 `>0`(接线完整性探针,证明 gate 真实依赖 storage2 路径而
非假后端,留痕见 `.superpowers/sdd/probe-ledger-bridge-report.md`)——**park 中"真账本桥
缺失"这一层因此解除**。

**park 未完全解除,仍不得宣称 OP 路径生产可用**:编排/调度接入(把桥接进真实执行流程)未做、
生产 `HostContext::exists()` 目前仍是恒真桩(带 TODO,桥未替换它)——且这两项并非穷尽清单。
**Karst 真适配(现仅 Jovian 别名占位)是独立于真账本桥的另一生产缺口**,与桥接进度无关,
park 完全解除须以全部缺口清账为准,不因本轮而一并解除。
向量再生成纪律与 DIVERGENCES 豁免流程见
`test/opstack/t8n/generator/README.md` 与 vectors/DIVERGENCES.md(出处
记录中的 `bcos-evm-ref` 路径为历史原貌,有意保留)。
