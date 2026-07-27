# 真账本 StateView 桥 + 双后端账本抽象设计(E-b 解冻前置,C 路线 step 1 合并)

日期:2026-07-27
状态:已评审(设计六节逐节用户确认)
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
   "自研账本替 TestState"——一个抽象、两个后端,两笔债一次清。

### 1.1 勘察锚定的三个关键事实

- `evmone::state::StateView`(`bcos-evm/eth/state/state_view.hpp:17-32`)是**同步
  noexcept** 接口;storage2 全部读写是 `task::Task` 协程(`Storage.h` tag_invoke 族)
  ——桥接核心矛盾;
- 账户存在性判据:`SYS_TABLES`("s_tables")中以账户表路径 `/apps/<hex(addr)>` 为键
  的标记行(`EVMAccount::exists():27-31`/`create():33-37`);生产 `HostContext::exists()`
  目前是恒真桩(`HostContext.h:365-369`,带 TODO),桥不照抄桩,以 `SYS_TABLES` 为准;
- 新旧两执行栈并存,`MultiVersionScheduler` 按运行时 `executor.version`(genesis 默认
  0=老栈)切换;storage2 世界仅 version≥1 生效。

## 2. 已定决策

| 决策点 | 结论 |
|---|---|
| 目标存储 | **新栈 storage2**(`MultiLayerStorage`/`StateKey`/`EVMAccount` 键空间);OP 链要求 `executor.version>=1` |
| E-b 判据环境 | **内存版 storage2**(`MemoryStorage`+`MultiLayerStorage`,仿 `transaction-scheduler/tests/testMultiLayerStorage.cpp:20-37` fixture),零 IO,ctest 常驻 |
| 范围边界 | 只交付账本抽象双后端 + E-b gate 复播;**不碰**:`HostContext::exists()` 桩修复、BaselineScheduler/编排接入、增量 stateRoot(TA-1d~1f) |
| 桥接方案 | **方案 A**:同步直读桥 + 块级读缓存(含负缓存)+ 毒旗错误通道 |
| 抽象锚点 | 不发明新接口:读 = `evmone::state::StateView`,写 = `applyDiff` 三契约,遍历 = `visitAccounts`;C 路线去 evmone 类型的接口演进留后续步骤 |

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
  `bcos-framework`(storage2/EVMAccount/task)的 PUBLIC 头依赖——本模块首次依赖
  FISCO 框架层,**单向**(框架不回依赖 bcos-evm),编译期隔离原则不破。
- `adapter/StateViewAdapter.h` 的 v1 占位由本期落地取代:**删除该头**,其占位注释
  中仍有效的契约说明迁入 `Storage2Ledger.h` 头注,不留双份。

**与 TestState 的过渡策略**:`MemoryLedger` 本期接管 E-b 新 gate 与新增测试;既有
33 向量 TestState gate **原样保留**(等价性证据链不动,双 gate 并行互为对照)。
15 个存量测试文件迁 `MemoryLedger` 为**末位可裁剪任务**:完成则 C 路线 step 1
全额清账;裁剪则降级为后续批次,不影响 E-b 验收。

## 4. Storage2Ledger 读桥(方案 A 四机制)

三个只读方法各走同一条流水:**查缓存 → syncWait 读 storage2 → 归一化 → 回填缓存**。

1. **协程落地**:每读一次 `task::syncWait` 驱动协程。内存后端同步完成零阻塞;
   将来 RocksDB 后端为线程内阻塞,对块级串行执行可接受。**契约:禁止在协程
   上下文内调桥**(桥内单层 syncWait,嵌套即栈陷阱),E-b 为全同步环境。
2. **块级读缓存(含负缓存)**:三张表,生命周期一个块——
   账户 `address → optional<LedgerAccount>`(**nullopt 也缓存**,消掉 M3.5 P1 spike
   定位的负查询浪费,占账本读 27.9%);槽 `(address, slot) → bytes32`(零值也缓存);
   code `address → bytes`。写回**写穿**缓存(改值更新、删除写负缓存),块内已写回
   中间态与底层永远一致,无脏读窗口。
3. **毒旗错误通道**:读方法 noexcept,内部 catch 全部异常→置 `m_poisoned`(记首个
   错误)并返回 `nullopt`/零值。**消费方契约**:块执行结束必须检查 `poisoned()`,
   置位即整块失败——存储错误绝不静默降级为"账户不存在"(KEEP 契约在错误路径
   的延伸;忘查毒旗 = 使用错误,测试有探针)。
4. **存在性判据(KEEP 落地)**:`get_account(addr).has_value()` ⇔ `SYS_TABLES` 存在
   `/apps/<hex(addr)>` 行。"存在但空"返回 `Account{nonce=0, balance=0, 空码}` 而非
   `nullopt`。字段缺省归一化:balance/nonce 缺省→0,code 缺省→空,code_hash 用
   keccak(空) 常量;storage_root 不依赖账户内嵌值(建根走 §6 遍历)。

## 5. 写回与播种

`Storage2Ledger::applyDiff(const StateDiff&)`:**序列化格式不自造,逐字段以
`EVMAccount`(`bcos-framework/ledger/EVMAccount.h`)读写对为准**,往返测试硬判据
(桥写→`EVMAccount` 读逐字段相等,反向亦然)。

写回三契约(承接 `adapter/StateDiffWriteback.h`)的键空间落地:

| 契约 | storage2 动作 |
|---|---|
| ① `deleted_accounts` 必须删除 | 删 `SYS_TABLES` 标记行 + 账户表全部字段行 + **range 扫删全部存量槽**(逻辑删除);strict tripwire 保留(deleted 项必须在 view 中存在) |
| ② 槽值为 0 = 删槽 | `removeOne(StateKey{表, slot原始32字节})`,不写零值 |
| ③ code 仅 `has_value()` 时覆写 | 写 `CODE_HASH` 字段 + 内容寻址写 `SYS_CODE_BINARY`(同 `EVMAccount::setCode` 构型),已存在的 code 行不重写 |

新建账户:先写 `SYS_TABLES` 标记行(等价 `EVMAccount::create()`)再写字段——KEEP
判据写侧成立。

**播种**:`LedgerSeed` 把向量 `pre` 合成创世 diff,走**同一条 `applyDiff` 路径**;
不存在第二套播种代码,写回路径在每条向量回放开始即被检验。

**事务性边界**:桥持 `Storage&` 引用,不拥有分层;层叠/丢弃归消费方
(`MultiLayerStorage` 的 `fork()/newMutable()`,失败弃 view 即块级回滚)。桥内零
补偿逻辑。

## 6. 建根与全量导出

- **遍历接口(抽象第三面)**:两后端各提供 `visitAccounts(AccountVisitor)`——
  访问者收到地址、四元组(nonce/balance/code/codeHash)与该账户槽遍历器。
- **建根**:`adapter/StateRootCompute` 增加基于遍历的 `stateRootOf` 重载,用已
  vendored 的 `MPT`+`rlp` 原语自建 secure-trie 账户树(键 `keccak(addr)`,叶值
  `rlp(nonce, balance, storageRoot, codeHash)`;每账户存储树同 `opStorageRoot`
  逻辑)。对齐锚写注释,金标准判据是 E-b 的 stateRoot 比对,非人工目检。既有
  `stateRootOf(TestState&)` 保留标 deprecated,随 C 路线退役。
- **Storage2Ledger 遍历落地**:range 扫 `SYS_TABLES` 的 `/apps/` 前缀枚举账户 →
  按 `ACCOUNT_TABLE_FIELDS` 全集读字段 → range 扫账户表,**键判别规则显式化**:
  命中已知字段名=字段,32 字节原始键=槽,**其余未知键→毒旗**(不猜测);零值槽
  防御性跳过并计数,计数非零=写回有漏,进毒旗。
- **性能边界**:全量重建=正确性版,与现状同档;TA-1d~1f 不在本期。

## 7. E-b gate 与测试

- **E-b gate**:新文件 `test/opstack/EbT8nReplayTest.cpp`,复用回放器骨架、同一
  33 向量与 DIVERGENCES 纪律,底座换 `LedgerSeed`→`Storage2Ledger`(内存版
  storage2 fixture)。既有 TestState gate 不动,双 gate 互为对照。
- **单元测试**(并入现有 `bcos-evm-opstack-tests` GTest 目标,不新建测试目标):
  `MemoryLedger`(StateView 语义/三契约/KEEP);`Storage2Ledger`
  (存在性判据、空账户归一化、负缓存命中计数断言、毒旗注入、删除 range 扫删
  完整性);**往返判据**(vs `EVMAccount` 双向逐字段);**三后端同根**
  (TestState == MemoryLedger == Storage2Ledger)。
- **翻红探针**(防真空绿惯例):毒旗探针(坏 storage 必翻红)、KEEP 探针(空账户
  折叠 nullopt 必致消毒误删翻红)、建根探针(单字段篡改必变根)。

## 8. 验收清单

- [ ] E-b gate 33/33,`known_diverges=0`
- [ ] 既有全部套件零回归(124 用例 + TestState gate)
- [ ] 往返测试(vs EVMAccount 双向)绿
- [ ] 三后端同根测试绿
- [ ] 翻红探针逐个按预期翻红并恢复
- [ ] `ports/` 零改动;库目标纯净约束不变(gtest/nlohmann 不入库)

## 9. 风险与预案

| 风险 | 预案 |
|---|---|
| 序列化与 `EVMAccount` 不对齐 | 不自造格式 + 往返测试硬判据 |
| 槽/字段键判别遗漏(ALIVE/FROZEN/SHARD 等) | 枚举 `ACCOUNT_TABLE_FIELDS` 全集;未知非 32 字节键→毒旗 |
| syncWait 嵌套协程栈陷阱 | 桥内单层 syncWait;"禁止协程上下文内调桥"写成契约 |
| `bcos-framework` 头依赖负担 | 仅模板头引用;单向依赖已核实无环 |
| 15 测试迁移 churn | 末位可裁剪任务,降级不影响 E-b 验收 |

## 10. 非目标与诚实边界

**非目标**:`HostContext::exists()` 恒真桩修复、编排/调度接入(第 2 层)、增量
stateRoot(TA-1d~1f)、RocksDB 后端测试、老执行栈兼容、EEST 套件。

**E-b park 边界**:本期绿灯解除 park 中"真账本桥缺失"一层;**仍不得宣称 OP 路径
生产可用**——编排未接入、生产 host `exists()` 桩未修,park 完全解除以该两项
立项交付为准。
