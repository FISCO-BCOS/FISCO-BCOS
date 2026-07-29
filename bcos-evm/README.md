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
- `engine/`:验证者模式的 engine 侧调度组件(`OpSchedulerImpl.h` 双签名 + `OpReceiptMap.h` +
  `OpEngineSeam.h`,纯模板头,design
  `docs/superpowers/specs/2026-07-28-op-validator-minimal-loop-design.md`)——依赖
  `bcos-framework`,构建边界同 `ledger/Storage2Ledger.h`,见"条件编译边界"。闭环状态见下方
  "engine 验证者闭环状态"一节
- `test/`(`eth/` + `opstack/`)：op-validator-loop 收尾后分支实况——in-tree `build/`
  `bcos-evm-opstack-tests` **206** 用例、standalone `bcos-evm/build/` 同名目标 131 用例
  (双路口径同下方"条件编译边界"一节,差值 **75** = `Storage2LedgerTest`/`LedgerRootTest`/
  `EbT8nReplayTest` 三个既有守卫文件 25 例 + op-validator-loop 新增的 5 个 engine/编解码测试
  文件 50 例,全部仅 in-tree 编译;数字随后续任务增测浮动,不作为硬编码基线),含
  `OpT8nReplay.Vectors`
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
一致)。op-validator-loop 的 5 个新测试文件(`EngineNewPayloadGateTest.cpp` /
`EngineOpBranchTest.cpp` / `EngineVersionGateTest.cpp` / `EthBlockHeaderTest.cpp` /
`OpSchedulerImplTest.cpp`)一并落在同一 `if(TARGET bcos-framework)` 守卫块内,理由相同
(`OpSchedulerImpl.h` 与被测的 `engine/bcos-engine/EngineServiceImpl.h` 同样直接引用
`bcos-framework`)。实测(2026-07-29,op-validator-loop 收尾后):in-tree `build/` **206** 个
`bcos-evm-opstack-tests` 用例;standalone `bcos-evm/build/` **131** 个(与本闭环开工前
**逐条相同**——新增 50 例全部位于守卫块内,通用件改动零外溢);差值 **75**,双路 `ctest`
均全绿。

## engine 验证者闭环状态(op-validator-loop,2026-07-29)

design:`docs/superpowers/specs/2026-07-28-op-validator-minimal-loop-design.md`(rev.3.1)。
**已交付的,仅限 engine 语义层的等价证据**:

- `EngineServiceImpl::newPayload` 的 **OP 分支**(版本闸 → 静态校验 + blockHash 重组 →
  storage 侧 parentKnown → 父子块号连续性 → 执行 → **六项比对面** → 块登记),执行经
  `OpSchedulerImpl`(scheduler_v1 形态的调度组件)走真桥 `Storage2Ledger` 链路;
- 判据是**离线 op-geth 金值**(pinned v1.101702.2,`test/opstack/t8n/golden/engine/`),
  不是自算自验:`payload.blockHash` 一律取 `golden.blockHash`,`result.txRoot` 对
  `golden.transactionsRoot`;
- 实测(in-tree `build/bcos-evm-opstack-tests`):金向量 gate **33/33 VALID**、两块链式对
  绿(parent-known 经块登记因果成立)、变异矩阵 **13 类 18 例** 全绿、engine/编解码新增测试
  合计 **50/50**、全量 opstack **206/206**、standalone **131/131**、engine Boost
  `test-bcos-engine` **11/11**;五探针翻红复绿留痕
  `.superpowers/sdd/probe-op-validator-gate-report.md`;
- 通用组合根**行为零漂移**:版本上界是构造参数(默认 V3),只有 OP 组合根放宽到 V4;
  探针⑤ 反证(把默认值改成 4 → 三条通用闸测试立刻翻红)。

**明确不构成的宣称(design §10 边界 + §6.4 欠账台账,逐条不得省略)**:

- **不构成与真实 op-node 互操作可用**。`engine_newPayloadV4` / `engine_getPayloadV4` 的
  **RPC 端点注册本期整体豁免**——`bcos-rpc` / `EngineEndpoint` **零改动**,V4 只在
  `EngineServiceImpl` 层生效,gate 与单测均**直调**该类,不经 RPC 解析/分发路径;
- OP 异常类型上标注的 -38005 / -38003 / -32603 **是意图文档,不是线上 JSON-RPC 码**:
  异常类型 → 错误码的映射在本仓尚未实现(既有通用异常同样如此),测试断言的是**异常类型**;
- **attributes 构块(FCU attrs + getPayloadV4)未做**:OP 模式收到 attributes 直接拒绝
  (forkchoice 状态照常推进,仅不开启 build);
- **链头进度表不写**:FCU 保持只读 + 内存态,`SYS_CURRENT_STATE` 的 current number 推进
  在欠账台账上;
- **`mergeBackStorage()` 永不调用**:每接受一块只 `pushView`,不可变层无界增长 + 读放大随
  已接受块数线性增长——最小闭环规模下是伸缩性问题,生产接入前必须解决;
- **`executionRequests` 校验真空成立**:`NewPayloadRequest` 没有该成员,约束当前无载体可查
  (以 `static_assert` 钉住,载体一旦加入立刻翻红);
- **extraData 形状校验、Holocene EIP-1559 baseFee 父子一致性校验未做**:真实 op-geth 会拒绝的
  baseFee 错块,本验证者放行;
- SYNCING 完整语义(缓存回填 / 侧链 ACCEPTED)、JWT、重组窗口、增量 stateRoot 均未做。

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

op-validator-loop(2026-07-29)在此之上又把桥接进了 **engine `newPayload` 的 OP 分支**并以离线
op-geth 金值验收——但那是 **engine 语义层的等价证据**,**不解除本节任何一条 park**:RPC 端点
未注册、attributes 构块未做、链头进度表不写、`mergeBackStorage` 永不调用,逐条见上方
"engine 验证者闭环状态"与 design §6.4 欠账台账。

**park 未完全解除,仍不得宣称 OP 路径生产可用**:编排/调度接入(把桥接进真实执行流程)未做、
生产 `HostContext::exists()` 目前仍是恒真桩(带 TODO,桥未替换它)——且这两项并非穷尽清单。
**Karst 真适配(现仅 Jovian 别名占位)是独立于真账本桥的另一生产缺口**,与桥接进度无关,
park 完全解除须以全部缺口清账为准,不因本轮而一并解除。
向量再生成纪律与 DIVERGENCES 豁免流程见
`test/opstack/t8n/generator/README.md` 与 vectors/DIVERGENCES.md(出处
记录中的 `bcos-evm-ref` 路径为历史原貌,有意保留)。
