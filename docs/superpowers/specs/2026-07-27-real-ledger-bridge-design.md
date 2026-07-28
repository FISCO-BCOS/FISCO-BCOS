# 真账本 StateView 桥 + 双后端账本抽象设计(E-b 解冻前置,C 路线 step 1 合并)

日期:2026-07-27
状态:**rev.2**——rev.1 经 4 视角并行审查(事实核查/契约一致性/设计质量/测试完备性,
42 项发现全数裁定采纳)后修订;事实核查确认 rev.1 全部源码引用属实(行号级)。
分支基座:`feat-evm-opstack-port`(PR #5361 之上)
前置文档:`2026-07-24-opstack-block-execution-port-design.md`(移植 spec,§4.6 偏离台账)、
`2026-07-27-eth-utils-removal-c-route-todo.md`(C 路线清单,本设计承接其 §6 第 1 步)

## 1. 背景与目标

OP 块级执行现状是 **reference-gate 层面等价**:33 向量 t8n gate 的 `pre` 播种
`InMemoryStateView`(evmone `TestState`),不经真实账本——E-b park 因此成立。
本设计交付两件事:

1. **真账本桥**:在 FISCO 新执行栈 storage2 世界(`MultiLayerStorage`/`StateKey`/
   `EVMAccount` 键空间)之上实现 `evmone::state::StateView`,并把 33 向量 gate
   复播于其上(E-b gate);
2. **双后端账本抽象**:同时交付自研内存账本 `MemoryLedger`,承接 C 路线第 1 步
   "自研账本替 TestState"——一个抽象、两个后端。桥接一笔债本期清账;C 路线一笔
   以 15 个存量测试迁移是否完成为准(§3 过渡策略),迁移裁剪则该笔延后,不得
   宣称"两笔全清"。

### 1.1 勘察锚定的关键事实(rev.1 审查逐条核验属实)

- `evmone::state::StateView`(`bcos-evm/eth/state/state_view.hpp:17-32`)是**同步
  noexcept** 接口,`Account` 含第四字段 **`has_storage`**(EIP-7610 create 碰撞判定
  的输入,`host.cpp:81-98`;TestState 口径 = `!storage.empty()`);storage2 全部读写
  是 `task::Task` 协程——桥接核心矛盾;
- 账户存在性判据:`SYS_TABLES`("s_tables")中以账户表路径为键的标记行
  (`EVMAccount::exists():27-31`/`create():33-37`)。表路径 `/apps/<hex(addr)>`
  **以 `feature_raw_address` 未开启为前提**(创世默认;开启后表键为原始 20 字节,
  `EVMAccount.h:234-263`);`c_systemTxsAddress` 集合地址路由到 `/sys/` 而非 `/apps/`;
- 生产 `HostContext::exists()` 目前是恒真桩(`HostContext.h:365-369`,带 TODO),
  桥不照抄桩,以 `SYS_TABLES` 为准;
- storage2 为**逻辑删除**:`removeOne` 写 `DELETED_TYPE` 墓碑;range 归并迭代
  **原样返回值变体不过滤墓碑**(`MultiLayerStorage.h:359-450`,点查路径才过滤)——
  遍历侧必须自行判别;
- 新旧两执行栈并存,`MultiVersionScheduler` 按运行时 `executor.version`(genesis
  默认 0=老栈)切换;storage2 世界仅 version≥1 生效。

## 2. 已定决策

| 决策点 | 结论 |
|---|---|
| 目标存储 | **新栈 storage2**;OP 链要求 `executor.version>=1` |
| E-b 判据环境 | **内存版 storage2**(`MemoryStorage`+`MultiLayerStorage`,仿 `testMultiLayerStorage.cpp:20-37` fixture),零 IO,ctest 常驻 |
| 范围边界 | 只交付账本抽象双后端 + gate 复播;**不碰**:`HostContext::exists()` 桩修复、编排接入、增量 stateRoot(TA-1d~1f) |
| 桥接方案 | **方案 A**:同步直读桥 + 块级读缓存(含负缓存)+ 毒旗错误通道 |
| 抽象锚点 | 读 = `evmone::state::StateView`,写 = `applyDiff`(strict 单形态,§5),遍历 = `visitAccounts`(§6);接口演进留 C 路线后续 |
| 环境前置(E-b) | `feature_raw_address` 关闭;向量地址不含 `c_systemTxsAddress` 集合;单线程串行调用 |

### 2.1 条件编译边界(实施实况,Task 7 回填)

`Storage2Ledger.h` 是纯模板头,直接引用 `bcos-framework`(`storage2`/`ledger::EVMAccount`/
`task::syncWait`)的公开头——单向依赖已核实(框架不回依赖 `bcos-evm`),但代价是它只能在
`bcos-framework` 目标可见处编译。`bcos-evm/test/CMakeLists.txt` 用
`if(TARGET bcos-framework)` 把 `Storage2LedgerTest.cpp`/`LedgerRootTest.cpp`/
`EbT8nReplayTest.cpp` 三个源码只并入 in-tree 构建(根仓库 `add_subdirectory(bcos-framework)`
早于 `add_subdirectory(bcos-evm)` 的那条路径)。**standalone 构建**(`bcos-evm/build/`,独立
`vcpkg.json`,不含 tbb/magic-enum/proxy/wedprcrypto 等 `bcos-framework` 传递依赖)不满足该
条件,三个守卫源码不参与编译——`Storage2Ledger`/`Storage2Backend`/`EbT8nReplay` 在
standalone 产物中不存在,standalone 只交付账本抽象的 `MemoryLedger` 部分(`ledger/` 目录
本体、`MemoryLedgerT8nReplayTest.cpp`、`MemoryLedgerTest.cpp` 均无条件编译,两路构建一致)。
这条边界从 Task 1 起即成立(`MemoryLedger` 不依赖 `bcos-framework`),Task 3 引入
`Storage2Ledger` 起该边界开始产生实际的"两路构建用例数不同"效果,Task 7 在此实测验证并
回填:in-tree `build/` 155 个 `bcos-evm-opstack-tests` 用例,standalone `bcos-evm/build/`
131 个(差值 24 = 三个守卫文件的用例数:`Storage2LedgerTest.cpp` 20 + `LedgerRootTest.cpp`
3 + `EbT8nReplayTest.cpp` 1),双路 `ctest` 均全绿。

## 3. 总体架构

```
bcos-evm/bcos-evm/ledger/
├── MemoryLedger.{h,cpp}      # 后端一:自研内存账本(C 路线 step 1)
│     StateView + map<address, LedgerAccount> + applyDiff + visitAccounts
├── Storage2Ledger.h          # 后端二:storage2 真桥(E-b 主角,纯模板头)
│     template<Storage> StateView(syncWait+缓存+毒旗)+ applyDiff(EVMAccount
│     键空间)+ visitAccounts(range 扫描)
└── LedgerSeed.h              # 向量 pre 播种:复用 applyDiff 路径
```

- 命名空间 `bcos::evm::ledger`。
- 构建:`MemoryLedger.cpp` 进 `bcosevm::eth`;`Storage2Ledger.h` 纯模板头,引入对
  `bcos-framework`(storage2/EVMAccount/task)的 PUBLIC 头依赖——单向(框架不回
  依赖 bcos-evm,已核实零反向引用),编译期隔离原则不破。
- `adapter/StateViewAdapter.h` 删除,其占位注释中仍有效的契约说明迁入
  `Storage2Ledger.h` 头注。
- **MemoryLedger 的 KEEP 落地**:map 中存在键 ⇔ 账户存在(含空账户值);语义对齐
  TestState 口径(`get_account` 四元组含 `has_storage = !storage.empty()`)。

**三腿回放(rev.2 升格)**:33 向量回放跑三个后端——TestState(既有 gate,不动)、
**MemoryLedger(rev.2 新增必选**,参数化复用回放器;否则 C 路线替身只有单测背书,
证据弱于另两后端)、Storage2Ledger(E-b gate)。三腿互为对照。

**内存态退役终态**(过渡期三套并存的收敛路径):C 路线后续步骤中 TestState 随
vendored utils 退役 → `MemoryLedger` 成为唯一内存后端;15 个存量测试文件迁
`MemoryLedger` 为本期**末位可裁剪任务**(完成则 C 路线 step 1 全额清账;裁剪则
该笔债延后,§10 边界如实记载)。

## 4. Storage2Ledger 读桥(方案 A 四机制 + rev.2 补强)

三个只读方法各走同一条流水:**查缓存 → syncWait 读 storage2 → 归一化 → 回填缓存**。

### 4.1 协程落地与线程契约

- 每读一次 `task::syncWait` 驱动协程(`Wait.h:42-121`,异常在调用点重抛——毒旗
  catch 的必要性来源)。内存后端同步完成零阻塞;RocksDB 后端为线程内阻塞,可接受。
- **契约(头注明文)**:禁止在协程上下文内调桥(单层 syncWait,嵌套即栈陷阱);
  **桥实例单线程使用**——const 读方法回填缓存/置毒旗依赖 `mutable` 成员,无锁,
  并发共享即数据竞争(将来并行调度接入时的第一个重新设计点)。

### 4.2 块级读缓存与归属

- 三张缓存表:账户 `address → optional<LedgerAccount>`(**nullopt 也缓存**,消掉
  M3.5 P1 spike 的负查询浪费,占账本读 27.9%);槽 `(address, slot) → bytes32`
  (零值也缓存);code `address → bytes`。
- **归属**:桥对象生命周期 = 一个块,每块新建,**不提供 reset()**(可忘调用的 API
  即陷阱)。
- **唯一写者不变式(头注明文)**:桥存续期内,底层 storage2 的唯一写入路径是本桥
  的 `applyDiff`;越过桥直写底层即缓存静默失真,属使用错误。view 与 `applyDiff`
  必须作用于同一桥实例。
- **写穿**:`applyDiff` 同步更新缓存(改值更新、删除写负缓存);**删除账户 ⇒ 三张
  表对该地址全量失效/置负**(含全部已缓存槽与 code——EIP-6780 同 tx 删除后
  CREATE2 同址重生场景,漏清槽缓存即静默脏读)。

### 4.3 毒旗错误通道

- 读方法 noexcept,内部 catch 全部异常→置 `m_poisoned`(记首个错误)并返回安全值。
- **消费方契约**:块执行结束必须检查 `poisoned()`,置位即整块失败——存储错误绝不
  静默降级为"账户不存在"。忘查毒旗 = 使用错误,探针兜底(§7)。
- 毒旗生命周期随桥实例(一块一实例,天然复位);探针须断言新实例不受前一被下毒
  实例影响。
- **显式毒旗点**(catch 兜不住的静默错误):nonce 账本值为十进制 u256 字符串,
  `StateView::Account::nonce` 为 uint64——converto 溢出是**静默截断**,必须显式
  range check → 毒旗(本仓有 convert_to 溢出前科,见 costOfPrecompiled 台账)。

### 4.4 存在性判据与归一化(KEEP 落地)

- `get_account(addr).has_value()` ⇔ `SYS_TABLES` 存在该账户表路径行(判据锚
  `EVMAccount` 的同一路径构造逻辑,**不复制路由规则**;E-b 前置:
  `feature_raw_address=off`,故为 `/apps/<hex(addr)>`)。遇 `c_systemTxsAddress`
  集合地址(`/sys/` 路由)→ **毒旗,不猜测**。
- "存在但空"返回 `Account{nonce=0, balance=0, 空码, has_storage=false}` 而非
  `nullopt`。字段缺省归一化:balance/nonce 缺省→0,code 缺省→空,code_hash 用
  keccak(空) 常量。
- **`has_storage`(rev.2 补,Critical)**:判据 = 账户表存在至少一个 32 字节槽键行。
  冷读时一次 range seek 探测,结果并入账户缓存;写穿维护——首个槽写入→true,
  契约①删账户/契约②删至最后一个槽→false(动态口径,对齐 TestState
  `!storage.empty()`)。漏掉此字段 = EIP-7610 create 碰撞漏判,直接错执行。

## 5. 写回与播种

`Storage2Ledger::applyDiff(const StateDiff&)`:**序列化格式不自造,逐字段以
`EVMAccount` 读写对为准**,往返测试硬判据。**单一 strict 形态**:tripwire(deleted
项必须在 view 中存在)内置,不提供 raw 版——TestState 侧的 raw/strict 二态是历史
过渡,桥不继承。

写回三契约的键空间落地:

| 契约 | storage2 动作 |
|---|---|
| ① `deleted_accounts` 必须删除 | 删 `SYS_TABLES` 标记行 + 账户表全部字段行 + range 扫删全部存量槽(逻辑删除)。**`SYS_CODE_BINARY`/`SYS_CONTRACT_ABI` 行永不删除**(内容寻址、可多账户共享,删即误伤;残留行对 stateRoot(账户叶只含 codeHash)与 postState(账户驱动比对)均不可见,与 geth 一致;同块"删除→同 hash 重建"恰因残留行 + ③"已存在不重写"而正确) |
| ② 槽值为 0 = 删槽 | `removeOne(StateKey{表, slot原始32字节})`,不写零值 |
| ③ code 仅 `has_value()` 时覆写 | 写 `CODE_HASH` 字段 + 内容寻址写 `SYS_CODE_BINARY`;**codeHash 由 applyDiff 自行 keccak(code)**(StateDiff 无 code_hash 字段);**不写 ABI 行**(StateDiff 无 ABI 概念;往返判据字段清单显式豁免 abi,见 §7) |

**账户 ensure-exists(rev.2 补)**:对**每个** modified entry 无条件确保 `SYS_TABLES`
标记行存在(存在性探测走桥账户缓存,廉价)——**不得**优化为"无字段可写则跳过":
pre 中完全空账户(EIP-161 touch-delete 向量的前置)正依赖此落账。已知事实:evmone
`build_diff` 把只读 touched 账户也放进 modified(`state.cpp:221-226`),对其重写
nonce/balance 为同值,无害;不得把"出现在 modified"当作"新建"或"被改"的判据。

**播种**:`LedgerSeed` 把向量 `pre` 合成创世 diff,走同一条 `applyDiff` 路径
(StateDiff::Entry 完整覆盖播种需求,含空账户;显式零值槽经契约②落为不写,与
trie "0≡缺席"规约一致)。

**事务性边界**:桥持 `Storage&` 引用,不拥有分层;层叠/丢弃归消费方。桥内零补偿。

## 6. 建根与全量导出

- **AccountVisitor 签名(rev.2 钉死)**:回调式;**返回 bool 可中止**;账户载荷为
  nonce/balance/codeHash + **code 惰性 getter**(建根叶值不需要 code 字节,避免
  白读 `SYS_CODE_BINARY`)+ 槽遍历器。`visitAccounts` 本身 **noexcept + 毒旗**
  (不抛);毒旗置位 ⇒ 遍历尽早短路,**本次遍历产物全部作废**,消费方查
  `poisoned()`。`MemoryLedger` 侧 `poisoned()` 恒 false(抽象层统一提供该查询,
  后端不对称由此收敛)。
- **建根**:`adapter/StateRootCompute` 增加基于遍历的 `stateRootOf` 重载,用
  vendored `MPT`+`rlp` 原语自建 secure-trie(键 `keccak(addr)`,叶值
  `rlp(nonce, balance, storageRoot, codeHash)`——与 vendored `mpt_hash.cpp:27-36`
  逐字段一致;每账户存储树同 `opStorageRoot` 先例)。既有 `stateRootOf(TestState&)`
  保留标 deprecated。
- **Storage2Ledger 遍历落地**:range 扫 `SYS_TABLES` 的 `/apps/` 前缀枚举账户
  (`/sys/` 不扫;E-b 前置保证系统地址不出现,出现即毒旗)→ 按
  `ACCOUNT_TABLE_FIELDS` 全集(`CODE_HASH/CODE/BALANCE/ABI/NONCE/ALIVE/FROZEN/
  SHARD`)读字段 → range 扫账户表,键判别:已知字段名=字段,32 字节原始键=槽,
  **其余未知键→毒旗**。
- **值变体判别(rev.2 补,Critical)**:storage2 逻辑删除,range 归并**不过滤墓碑**
  ——遍历必须自行跳过 `DELETED_TYPE`/`NOT_EXISTS_TYPE` 变体,否则块内刚删除的
  槽/账户还魂进 stateRoot。墓碑 ≠ 零值:零值槽计数规则只统计**实值为零**的行。
- **零值槽毒旗的适用域(rev.2 限定)**:"零值槽计数非零=写回有漏→毒旗"**仅在
  桥自写的 E-b 世界成立**(生产 `HostContext::set` 对零值照写不删,真实链账户表
  必然含零值槽行)——此规则不得被继承到编排接入层。
- **性能边界**:全量重建=正确性版;TA-1d~1f 不在本期。

## 7. 测试与探针

**三腿回放**:TestState gate(既有,不动)/ `MemoryLedgerT8nReplay`(33 向量,
参数化回放器)/ `EbT8nReplayTest`(33 向量,`LedgerSeed`→`Storage2Ledger`,内存版
storage2 fixture)。同一 DIVERGENCES 纪律;每向量比对计数>0、`known_diverges` 走
`RecordProperty`(不硬编数字)。

**单元测试**(并入 `bcos-evm-opstack-tests`,rev.2 逐项点名):
- `MemoryLedger`:StateView 语义(含 has_storage 动态口径)/三契约/KEEP;
- `Storage2Ledger`:存在性判据、空账户归一化、`has_storage`(含删至零槽翻 false)、
  负缓存命中计数断言、**写穿失效**(负缓存写穿:读不存在→applyDiff 建账户/写槽→
  立即再读得新值;正缓存失效:读→覆写/删除→再读得新值/空值)、删除的三表全失效
  (CREATE2 同址重生场景)、毒旗注入(读路径)、nonce 溢出→毒旗、range 扫删
  完整性(**含带存量槽账户的删除**——三腿向量结构性测不到该路径,单测兜底)、
  **遍历键分类**(注入 ALIVE/FROZEN/SHARD/ABI 行,断言不误判为槽、不误报毒旗)、
  **墓碑跳过**(块内删后遍历建根不含已删实体)、**协程重入契约**(协程上下文内
  调桥可判定失败)、**大规模边界**(O(1000+) 槽 + 大 code 账户:遍历完整、删除
  完整、建根与逐槽读回交叉一致,不依赖 op-geth 金标准);
- **往返判据**:vs `EVMAccount` 双向逐字段;字段清单 = 存在性/nonce/balance/code/
  codeHash,**abi 显式豁免**;
- **三后端同根**:同一状态,TestState == MemoryLedger == Storage2Ledger。

**翻红探针**(五个,均须常驻或留痕):
1. 毒旗探针:坏 storage 注入必翻红;新桥实例不受前一被下毒实例影响;
2. KEEP 探针:空账户折叠 nullopt 必致消毒误删翻红;
3. **空账户播种探针**(rev.2 新增):seed 侧跳过空账户 ensure-exists 必翻红;
4. 建根探针(rev.2 扩展):**逐字段矩阵**(nonce/balance/codeHash/单槽各一例),
  固化为常驻 gtest;
5. **接线完整性探针(rev.2 新增,wiring probe)**:E-b gate 换假后端(恒 nullopt/
  误接 MemoryLedger)必**批量**翻红,证明 gate 真实依赖 storage2 路径;并以
  `RecordProperty` 记录 storage2 实际读写调用计数、断言 >0,作为常驻机器判据。

**探针留痕纪律**(比照 DIVERGENCES"11 项变异全部翻红"先例):每个探针的注入点、
翻红实际输出、回退复绿确认,记入任务报告/PR 描述,供审查复核——不许无痕勾选。

## 8. 验收清单(rev.2 改相对基线口径,附执行命令;实测值 Task 7 回填)

- [x] 三腿回放全绿:`ctest -R BcosEvmOpstackTests`(含既有 gate)+
      `--gtest_filter='MemoryLedgerT8nReplay*'` + `--gtest_filter='EbT8nReplay*'`,
      各 33/33、`known_diverges=0`、每向量比对计数>0 ——
      **实测**:`--gtest_filter='OpT8nReplay.*:MemoryLedgerT8nReplay.*:EbT8nReplay.*'` +
      `--gtest_output=xml` 解析确认三腿各自 33 条 `*.comparisons` 属性全部存在且值>0
      (zero_valued_comparisons=0)、`known_diverges=0`;`ctest -R BcosEvmOpstackTests`
      PASS(in-tree)。
- [x] 既有套件零回归:**合并前捕获基线用例清单 N0**(`--gtest_list_tests`),合并后
      N0 全部通过;新增用例数量与名单在 PR 描述如实记录(不硬编总数)——
      **实测**:N0(Task 6 末态)= 154;Task 7 新增 1 条
      (`OpBlockSeal.AccountStorageRootMatchesOpStorageRootOnSameMap`,漂移防线单测,见下)
      → N1 = 155,154 条基线用例逐一比对姓名确认全部仍在且全部 PASS(`--gtest_list_tests`
      diff 仅新增一行)。
- [x] `Storage2Ledger`/`MemoryLedger` 专项单测全绿(§7 点名清单逐项对应,独立行,
      不折叠进"零回归")—— **实测**:`Storage2Ledger` 20/20、`MemoryLedger` 5/5、
      `LedgerRoot` 3/3,均 PASS(既随 N1 全量 155/155 一次跑出,也逐类目
      `--gtest_filter` 单独确认)。
- [x] 往返测试(vs EVMAccount 双向,abi 豁免口径)绿 —— **实测**:
      `Storage2Ledger.RoundTripFields`/`RoundTripWriteDirectionVsEVMAccount` PASS。
- [x] 三后端同根测试绿 —— **实测**:
      `LedgerRoot.ThreeBackendsSameRootWithCodeAndSlots` PASS(TestState/MemoryLedger/
      Storage2Ledger 三根逐字节相等,含带码带槽账户/多槽账户/完全空账户三类混合状态)。
- [x] 五个翻红探针逐个按预期翻红并复绿,**留痕记录在案** —— 详见
      `.superpowers/sdd/probe-ledger-bridge-report.md`(每探针的注入 diff、翻红实际输出、
      回退命令、复绿输出、`git status` 全程干净)。
- [x] 接线完整性:storage2 读写计数 `RecordProperty` >0(常驻断言)—— **实测**:
      `EbT8nReplay.Vectors` XML `storage2_reads`=188/`storage2_writes`=36(末次记录的
      `jovian_tx_reverted` 向量值,`ASSERT_GT(reads,0U)` 对 33 个向量均非平凡通过);
      接线完整性探针(假后端注入)复现见探针留痕文档。
- [x] `ports/` 零改动;库目标纯净约束不变(gtest/nlohmann 不入库)—— **实测**:
      `git diff --stat 8c8dbd7 -- ports/` 空输出;`bcos-evm/test/opstack/t8n/vectors/`
      同样零改动;`grep -rl "nlohmann\|gtest" bcos-evm/bcos-evm/ | grep -v statetest.hpp`
      空输出(唯一命中是 vendored 测试装载器 `eth/utils/statetest.hpp`,非库产物)。

## 9. 风险与预案

| 风险 | 预案 |
|---|---|
| 序列化与 `EVMAccount` 不对齐 | 不自造格式 + 往返测试硬判据 |
| `has_storage` 维护遗漏(EIP-7610 漏判) | §4.4 判据+写穿维护规则明文;单测含删至零槽翻 false |
| 墓碑误判(已删数据还魂进根) | §6 值变体判别明文;墓碑跳过单测 + 块内删后建根用例 |
| 槽/字段键判别遗漏 | 枚举 `ACCOUNT_TABLE_FIELDS` 全集;未知键→毒旗;注入单测 |
| nonce u64 静默截断 | 显式 range check→毒旗(catch 兜不住) |
| 写穿失效漏做(tx N+1 读旧值) | 写穿失效单测(正/负两向)+ 删除三表失效单测 |
| E-b gate 接线假绿 | wiring probe + storage2 调用计数常驻断言 |
| syncWait 嵌套/并发误用 | 单层 syncWait + 单线程契约头注明文;协程重入单测 |
| 地址路由分叉(raw_address//sys/) | E-b 前置明文;系统地址→毒旗;判据锚 EVMAccount 同源逻辑 |
| `bcos-framework` 依赖负担 | 仅模板头引用;单向无环已核实 |
| 15 测试迁移 churn | 末位可裁剪;裁剪后果如实进 §10 |

## 10. 非目标与诚实边界

**非目标**:`HostContext::exists()` 恒真桩修复、编排/调度接入(第 2 层)、增量
stateRoot(TA-1d~1f)、RocksDB 后端测试、老执行栈兼容、EEST 套件、**并发访问**
(本期全部单线程串行;并发语义留编排接入时重新设计)、`feature_raw_address`
开启形态与 `/sys/` 系统地址(桥遇之毒旗)。

**E-b park 边界**:本期绿灯解除 park 中"真账本桥缺失"一层;**仍不得宣称 OP 路径
生产可用**——编排未接入、生产 host `exists()` 桩未修;且该两项**并非穷尽清单**:
**Karst 真适配(现仅 Jovian 别名占位)是独立于本轮的另一生产缺口**(移植 spec §10
与 README 边界章节在案),park 完全解除须以全部缺口清账为准。

**C 路线 step 1 口径**:15 测试迁移完成则全额清账;裁剪则 MemoryLedger 的落地
仅覆盖回放腿与单测,迁移债务延后并如实记账。

### 10.1 实施期偏离与发现(Task 7 回填)

- **gitignore `build**` 误伤 golden**:`.gitignore` 的 `build**` 反选规则会连带匹配
  `bcos-evm/scripts/upstream-diff/golden/build_message.patch`(路径含 `build` 子串),
  致该 golden 文件曾静默逃逸出提交历史而不自知。Task 5 审查发现,本分支已修复
  (commit `f8656949f`,加一条 `!**/upstream-diff/golden/*.patch` 反选例外 + 补交丢失的
  golden 文件)。**同一缺口在 `feat-evm-opstack-port` 主分支上原样存在**(PR #5361 引入
  `upstream-diff` 护栏时未预见 `build_message.patch` 这个文件名与 `build**` 规则的
  冲突)——本次修复只落在本桥接分支,未回合并进主分支,如需消除该缺口需另行处理。
- **`pushView` 有意省略**(Task 6 审查 Minor):`EbT8nReplayTest.cpp` 的 `Storage2Backend`
  fixture(每向量独立 `MultiLayerStorage`)只调用 `multiLayerStorage.fork()` +
  `view.newMutable()` 取一枚可写 view,全程不调用 `MultiLayerStorage::pushView()`(把
  forked view 的写入合并回底层/跨 checkpoint 持久化的操作)。这不是遗漏,而是本 fixture
  的隔离架构决定的:每个向量的账本状态只在该向量的作用域内存在(design §7"33 向量互不
  共享底层存储",与"一块一实例"的桥生命周期契约一致),向量结束后整个 `Impl`
  (含 `multiLayerStorage`/`view`/`bridge`)连同其状态一并析构丢弃,没有下一层需要把
  写入合并进去——`pushView` 要解决的"view 内写入需要对其他 view/下一个区块可见"的问题在
  这个单向量、用后即弃的测试拓扑里不存在。brief 中"失败弃 view(天然回滚),不 pushView"
  一句描述的正是同一件事:测试对失败向量的处理不需要显式回滚逻辑,因为**从不曾提交**。
  这条边界仅对本测试 fixture 成立;真实编排接入(design §10 非目标)把桥接进跨块状态时,
  `pushView` 的调用时机与失败回滚策略需要重新设计,不能照搬本 fixture 的"从不调用"。
- **visitor 异常边界契约(不对称,Task 5 审查 Minor)**:`MemoryLedger::visitAccounts` 的
  函数签名是字面 `noexcept`(`MemoryLedger.h:77`)——它本身没有任何会抛异常的内部路径
  (`poisoned()` 恒 false),但如果调用方传入的 **visitor 回调本身抛出异常**,该异常会在一个
  `noexcept` 函数内传播且找不到匹配的 catch,依 C++ 标准触发 `std::terminate()`,没有任何
  优雅降级路径。`Storage2Ledger::visitAccounts` 表面签名同样是 `noexcept`,但实现上用
  `try/catch` 包住了整个 `syncWait(visitAccountsImpl(visitor))`(含 `visitor(accountView)`
  调用本身),因此 visitor 抛出的异常会被这层 catch 接住转成 `poison()`,不会 terminate——
  两个后端对"visitor 抛异常"这一事件的实际后果并不对称,尽管公开签名看起来一致。这是**调用
  方义务**:传给 `visitAccounts` 的 visitor 必须自身不抛(或自行吞掉全部异常),不能依赖
  `MemoryLedger` 侧提供任何异常安全网——本设计与既有 33 向量回放腿代码的 visitor(仅做字段
  搬运/比对,不抛)都遵守这条隐性契约,但契约本身此前未在文档中明文,Task 7 在此补记。
