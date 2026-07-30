# 视角 4 · 代码组织合理性 —— 复审报告

**范围**:14 个生产文件 + `bcos-evm/test/CMakeLists.txt` + `bcos-codec/CMakeLists.txt`
**方法**:只读。未构建、未跑测试、未修改任何文件。所有需要构建才能证实的推论标 `[需验证]` 并附最小验证步骤。
**判据(自我约束)**:每条发现必须能说出「什么样的后续改动会因此出错」。说不出的已被我删掉,不在本报告里。

---

## 0. 一句话结论

代码本身是可读的——注释密度远超本仓平均(`EngineServiceImpl.h` 1493 行里 530 行注释),
几乎每个非显然决策都留了理由和出处。**问题不在可读性,在可改性**:engine 与 OP 实现之间
的接缝有 **11 条要求**,其中只有 **1 条**被编译期探测、**4 条**在 `OpEngineSeam.h` 里被公开
声明,其余 **7 条**只以「`SchedulerType::` 依赖名的使用点」形式散落在一个 1493 行模板体内部。
其中两条(`OpBlockCommitments` 加字段、`OpBlockEnv` 加字段)的漂移是**静默的**——不报错、
不翻红、后果是错判 VALID。这是本视角最重要的一条。

---

## 1. 文件尺寸与职责(问题 1)

### 1.1 实测数据(不是印象)

```
engine/bcos-engine/EngineServiceImpl.h    1493 行(注释 530 / 空 96 / 代码 867)
bcos-evm/bcos-evm/engine/OpSchedulerImpl.h 937 行(注释 331 / 空 67 / 代码 539)
bcos-evm/bcos-evm/engine/OpEngineSeam.h     187 行(注释 105 / 空 11 / 代码 71)
```

本仓既有同类文件(`bcos-evm/`、`transaction-scheduler/`、`bcos-executor/src/`、`engine/`
下全部 `.h`/`.hpp`,按行数降序):

```
1493  engine/bcos-engine/EngineServiceImpl.h        ← 本分支
 937  bcos-evm/bcos-evm/engine/OpSchedulerImpl.h    ← 本分支
 841  transaction-scheduler/.../BaselineScheduler.h ← 既有最大
 766  bcos-executor/src/executor/SwitchExecutorManager.h
 742  bcos-evm/bcos-evm/ledger/Storage2Ledger.h
 398  bcos-executor/src/Common.h
 361  transaction-scheduler/.../SchedulerParallelImpl.h
```

**诚实读数**:`EngineServiceImpl.h` 是全仓最大的头,是既有最大值的 1.8×;但按**代码行**
(867)算,它约等于 `BaselineScheduler.h` 的**总行数**。所以「体量超标」这个论断只在总行数
口径上成立,在代码口径上它落在既有分布的顶端而非之外。头文件里放模板实现是本仓一贯做法
(`BaselineScheduler.h`/`Storage2Ledger.h`/`SchedulerParallelImpl.h` 全是),这一点没有偏离惯例。

### 1.2 `EngineServiceImpl.h` 现在承担几件事

七件:

1. 11 个异常类型(6 个通用 + 5 个 OP 专用,`:62-95`);
2. `detail` 命名空间 11 个自由函数声明(5 通用 + 6 OP,`:97-142`);
3. `c_opMode` SFINAE 探针 + 其 40 行论证(`:155-186`);
4. 通用 Engine API(FCU 状态机 / getPayload / newPayload,`:230-660`);
5. 通用**出块**路径(`buildPayload` 163 行 + `calculateStateRoot`,`:1257-1450`);
6. OP newPayload 分支(`handleOpNewPayload` + `runOpNewPayloadSteps`,**368 行**,`:677-1119`);
7. OP 块登记(`registerOpBlock`,**113 行**,`:1140-1253`)。

「这个文件负责什么」现在只能答成一句复合句:*「Engine API 的通用实现,外加一个编译期分流
出去的、完全独立的 OP 验证者闭环,外加一个 OP 模式下不可达的出块器」*。

### 1.3 该拆吗 —— 结论:**该拆,但不该现在拆**

**具体缝**(唯一一条,不是三条):OP 分支整体外移。

```
engine/bcos-engine/
├── EngineServiceImpl.h      (保留 ~950 行:通用异常 / 通用 detail / 类模板 /
│                             c_opMode / 6 处 if constexpr 分流点)
├── EngineServiceImpl.cpp    (保留通用 detail 实现)
├── OpSchedulerSeam.h        (新:concept OpSchedulerSeam —— 见 §2.3,~60 行)
├── OpNewPayload.h           (新:5 个 OP 异常 + 6 个 OP detail 声明 +
│                             3 个自由函数模板 handleOpNewPayload /
│                             runOpNewPayloadSteps / registerOpBlock,~500 行)
└── OpNewPayload.cpp         (新:validateOpNewPayloadRequest / rebuildOpEthHeader /
                              toEthLogsBloom / narrowU256ToU64 + 3 个协议常量)
```

自由函数模板需要类的 4 个成员(`x_state` / `m_globalStateStorage` / `m_scheduler` /
`m_blockFactory`),打成一个 `OpNewPayloadDeps` 引用聚合传入即可。

**拆分的代价 —— 逐条诚实评估**:

| 代价项 | 实际情况 |
|---|---|
| 模板实现必须在头里 | **不构成代价**。移出去的仍是头文件,TU 集合不变。 |
| `c_opMode` 探针依赖完整类型 | **不构成代价**。探针留在 `EngineServiceImpl.h`,只探 `SchedulerType`,与 OP 函数在哪个文件无关。 |
| 编译期开销 | **不变**。同一批 TU 仍要包含同样多的模板代码;头拆分不减少实例化。 |
| 成员函数声明随类实例化的陷阱 | **拆分后变好,不是变差**。自由函数模板只在被调用时实例化,`registerOpBlock` 之所以要写成成员模板(`:1140`,那段 10 行论证)恰恰是因为它是**成员**;搬成自由函数后这个陷阱在结构上消失。 |
| `makeStatus` 是 private static | 需下沉到 `detail`。~5 行机械改动。 |
| bcos-evm 测试 CMake | 需把 `OpNewPayload.cpp` 加进「编入源码」清单——**这会让 §4.4 的护栏更脆**(要同步的文件从 1 个变 2 个)。所以 §4.4 的 CMake 断言必须先落地。 |
| 复审成本 | 500 行零行为变更的搬移,在终审阶段引入。**这是唯一真实的、也是决定性的代价。** |

**我的建议**:**不要在本轮做这次搬移**。理由不是「拆分没价值」,而是本轮已到终审,一次
500 行搬移的复审成本高于它在本轮能兑现的收益;而它带来的收益(可读性)恰恰是这份代码
目前**最不缺**的东西。真正缺的是 §2 的三条防漂移绊线,它们合计 ~70 行、可独立落地、
且每一条都把一种**静默失效**变成**编译期诊断**。优先级排序应当是:先绊线,后搬移。

---

## 2. `c_opMode` 双模式的组织代价(问题 2)

### 2.1 实测计数

`if constexpr (c_opMode)` / `if constexpr (!c_opMode)` 共 **6 处**,分布在 **4 个函数**:

| 位置 | 函数 | 极性 |
|---|---|---|
| `EngineServiceImpl.h:220` | `exchangeCapabilities` | `c_opMode` |
| `EngineServiceImpl.h:248` | `updateForkchoice`(attributes 预校验跳过) | `!c_opMode` |
| `EngineServiceImpl.h:362` | `updateForkchoice`(-38003 拒绝) | `c_opMode` |
| `EngineServiceImpl.h:494` | `handleGetPayload` | `c_opMode` |
| `EngineServiceImpl.h:561` | `handleNewPayload`(V4 硬拒护栏,B4-3) | `!c_opMode` |
| `EngineServiceImpl.h:574` | `handleNewPayload`(OP 分支分流) | `c_opMode` |

### 2.2 接缝的真实规模 —— 11 条要求,只探 1 条

`grep -n "SchedulerType::\|m_scheduler.get()\." engine/bcos-engine/EngineServiceImpl.h` 的结果,
去掉注释后,engine 对 `SchedulerType` 的**全部**要求:

| # | 要求 | 位置 | `c_opMode` 探测? | `OpEngineSeam.h` 公开? |
|---|---|---|---|---|
| 1 | `executeOpBlock(view, env, range)` | `:989` | **✔ 是** | ✘(只公开了 `OpBlockCommitments`/`computeOpTxRoot`) |
| 2 | `isIsthmusActiveAt(uint64_t)` | `:692` | ✘ | ✘ |
| 3 | `isJovianActiveAt(uint64_t)` | `:762` | ✘ | ✘ |
| 4 | `static computeTxRoot(range)` | `:771` | ✘ | 间接(`computeOpTxRoot`) |
| 5 | `static constexpr c_ethBlockHeaderTable` | `:870`, `:1166` | ✘ | ✔(`SYS_ETH_BLOCK_HEADER`) |
| 6 | `static constexpr c_ethRawTxTable` | `:1250` | ✘ | ✔(`SYS_ETH_HASH_2_RAWTX`) |
| 7 | `BlockEnv`(**9 个具名字段**) | `:974-984` | ✘ | ✘ |
| 8 | `ExecuteResult`(需有 `.receipts`) | `:986`, `:1213`, `:1222` | ✘ | ✘ |
| 9 | `ConsensusError`(可 catch,带 `.what()`) | `:994` | ✘ | ✘ |
| 10 | `StorageError`(可 catch,带 `.what()`) | `:1000` | ✘ | ✘ |
| 11 | `static commitmentsOf(ExecuteResult)`(返回值需有 **8 个具名成员**) | `:1050-1093` | ✘ | ✔(结构体) |

`OpEngineSeam.h` 的自我定位是「engine 面向的公开接缝面」(文件头注释 `:5-26`),
**但它只覆盖了 11 条中的 4 条**。

### 2.3 失效模式 —— 分两个方向,只有一个方向有护栏

**方向 A(探针塌成 false)**:已被充分处理。
- 护栏 1:`EngineServiceImpl.h:561-570` —— 非 OP 构建对 `version >= 4` 直接拒绝(B4-3)。
- 护栏 2:`EngineOpBranchTest.cpp:374/375/596/1519`、`EngineNewPayloadGateTest.cpp:359/360/836`、
  `EngineVersionGateTest.cpp:269/301` 的 `static_assert`,以及 `EngineOpBranchTest.cpp:1492-1523`
  的 `DriftedEngineService` 反向用例。
- 已记账:§6.4 条目 (i)②(签名漂移静默退化)与 (m)(生产组合根缺 `static_assert`)。
- **我的独立判断:该裁定仍然成立,优先级没有被低估。**

**方向 B(探针保持 true,其余 10 条中某条漂移)**:**无任何护栏,且其中两条是静默的。**

这个方向不在 §6.4 台账上——(i)② 只描述了 A 方向。这是本报告的核心新发现。

---

## 3. 【Important-1,新发现】接缝的两条**静默**加宽路径

> 后果等级 = 错判 VALID(即 Critical 类后果),但当前不可达,故按共享上下文的分级规则记为 Important。

### 3.1 `OpBlockCommitments` 加字段 → engine 静默不比对

**位置**:`bcos-evm/bcos-evm/engine/OpEngineSeam.h:112-122`(结构体定义)
        ↔ `engine/bcos-engine/EngineServiceImpl.h:1050-1093`(比对链)

engine 侧用**具名成员访问**逐个比对 8 个成员:

```
:1052  if (commitments.receiptsRoot   != payload.receiptsRoot)
:1056  else if (commitments.logsBloom != detail::toEthLogsBloom(payload.logsBloom))
:1060  else if (commitments.withdrawalsRoot != *payload.withdrawalsRoot)
:1064  else if (commitments.stateRoot != payload.stateRoot)
:1068  else if (commitments.gasUsed   != payload.gasUsed)
:1072  else if (commitments.txRoot    != transactionsRoot)
:1080  else if (commitments.blobGasUsed.has_value() && ...)
:1087  else if (commitments.requestsHash.has_value() && ...)
```

**会出错的后续改动**:任何人在 `OpEngineSeam.h:112` 的 `OpBlockCommitments` 里追加一个
成员——例如 Jovian 之后把 DA footprint 拆成独立字段、或按 §6.4 (e) 补 Holocene baseFee
承诺——`commitmentsOf`(`OpEngineSeam.h:127-145`)会填它,`sealOpBlock` 会算它,
**而 engine 的这条 `else if` 链一个字都不用改就继续编译通过**。结果:执行侧已经承诺的
一个字段,验证侧从不比对 → 该字段被篡改的 payload 被判 **VALID**,而 op-geth 拒绝。
不会有任何测试翻红,因为没有测试断言「比对链是穷尽的」——`EngineOpBranchTest.cpp:1000+`
的 `EachComparisonSurfaceFieldMismatchIsNamed` 是逐字段罗列的 8 行表,新字段同样不会
自动进表。

**可强制执行的修法(1 行,零成本)**:把成员访问换成对整个聚合的结构化绑定。

```cpp
// EngineServiceImpl.h:1050 附近
const auto commitments = SchedulerType::commitmentsOf(*executeResult);
// ↓ 加这一行:聚合元数变化时,此处直接编译错误("cannot decompose ... into N names")
auto const& [cReceiptsRoot, cLogsBloom, cWithdrawalsRoot, cStateRoot,
             cGasUsed, cTxRoot, cBlobGasUsed, cRequestsHash] = commitments;
```

结构化绑定要求名字个数与聚合成员数**精确相等**,加一个成员立刻在**比对链所在的那一行**
报错。依赖类型上的结构化绑定在模板里合法,不需要 engine 认识 `bcos-evm` 的任何名字
(仍是依赖名,实例化期解析),不破坏「库纯净」约束。

`[需验证]`:最小验证步骤 = 在 `OpEngineSeam.h:121` 后临时加一个 `bcos::h256 probe;` 成员,
只构建 `bcos-evm-opstack-tests`。**期待现象**:加绊线前编译通过(证明漏洞存在);
加绊线后在 `EngineServiceImpl.h` 结构化绑定那一行报 `cannot decompose ... into 8 names`。

### 3.2 `OpBlockEnv` 加字段 → engine 静默传 0

**位置**:`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:94-109`(9 个成员)
        ↔ `engine/bcos-engine/EngineServiceImpl.h:974-984`(指派初始化,9 个)

```cpp
typename SchedulerType::BlockEnv blockEnv{
    .fiscoHeader = *fiscoHeader,  .parentHash = ...,  .prevRandao = ...,
    .baseFeePerGas = ...,  .feeRecipient = ...,  .parentBeaconBlockRoot = ...,
    .gasLimit = ...,  .extraData = ...,  .blobGasUsed = ...,
};
```

**会出错的后续改动**:在 `OpBlockEnv` 末尾追加一个非引用成员(例如 `uint64_t excessBlobGas;`,
或未来 Holocene 的 `uint64_t minBaseFee;`)。C++ 聚合初始化允许省略尾部成员——被省略的成员
按空初始化列表**值初始化为 0**。于是 engine **静默地**用 `0` 填了这个新的执行环境字段,
`toBlockInfo`(`OpSchedulerImpl.h:232-245`)照单全收,整块按错误的执行环境跑完,
六项比对面全部对不上 → 好块被判 **INVALID**(或更坏:如果新字段只影响某个 fork 的边缘
路径,则是错判 VALID)。同样没有任何诊断。

注:唯一会**响亮**失败的情形是新成员是引用类型(如 `fiscoHeader`)——那才 ill-formed。
不能指望这一点。

**可强制执行的修法(~10 行)**:让 OP 侧拥有构造权,把 9 个字段变成一个具名参数列表:

```cpp
// OpSchedulerImpl.h 的 "engine-facing seam surface" 块里
static BlockEnv makeBlockEnv(bcos::protocol::BlockHeader const& fiscoHeader,
    bcos::h256 parentHash, bcos::h256 prevRandao, bcos::u256 baseFeePerGas,
    bcos::Address feeRecipient, bcos::h256 parentBeaconBlockRoot,
    uint64_t gasLimit, bcos::bytes extraData, uint64_t blobGasUsed);
```

engine 侧改成 `auto blockEnv = SchedulerType::makeBlockEnv(...);`(一行)。
此后给 `OpBlockEnv` 加字段就必须动 `makeBlockEnv` 的签名(或**显式**给它一个默认值——
那时至少是一个有意识的决定),engine 的调用点会立刻报参数个数不匹配。

### 3.3 其余 8 条要求 —— 漂移是响亮的,但落点很差

改名 `isJovianActiveAt` / `commitmentsOf` / `c_ethRawTxTable`,或把 `ExecuteResult::receipts`
改名,都会产生编译错误——**但错误落在 `EngineServiceImpl.h` 第 762 / 1050 / 1250 / 1213 行**,
即一个 1493 行模板体的深处,报错文本是「`SchedulerType` 没有成员 X」而不是「你破坏了 OP 接缝
契约」。而且:`engine` 库本身照常编译通过(OP 分支不在 `engine` 里实例化),**只有 bcos-evm
测试二进制会红**。也就是说,一个只构建 `engine` 的人看不到任何问题。

**可强制执行的修法(~60 行,新文件 `engine/bcos-engine/OpSchedulerSeam.h`)**:把 11 条
要求写成一个 concept。关键点:**它完全可以只用 `bcos::` 类型表达,不需要 engine 依赖
bcos-evm** —— `BlockEnv` 的 9 个字段全是 `bcos::h256/Address/u256/bytes/uint64_t` +
`protocol::BlockHeader`(bcos-framework),`OpBlockCommitments` 的 8 个成员同理,
`ExecuteResult::receipts` 是 `protocol::TransactionReceipt::Ptr` 的 range(bcos-framework)。

```cpp
namespace bcos::engine {
template <class S, class ViewType>
concept OpSchedulerSeam = requires(S& s, const S& cs, ViewType& view,
                                   typename S::BlockEnv const& env,
                                   typename S::ExecuteResult const& r,
                                   std::vector<bcos::bytes> const& raws, uint64_t ts) {
    // 1. 执行入口(= 现 c_opMode 探针)
    { &S::template executeOpBlock<std::vector<bcos::bytes>> };
    // 2-3. fork 判定
    { cs.isIsthmusActiveAt(ts) } -> std::same_as<bool>;
    { cs.isJovianActiveAt(ts) }  -> std::same_as<bool>;
    // 4. txRoot
    { S::computeTxRoot(raws) } -> std::same_as<bcos::h256>;
    // 5-6. 表名
    { S::c_ethBlockHeaderTable } -> std::convertible_to<std::string_view>;
    { S::c_ethRawTxTable }       -> std::convertible_to<std::string_view>;
    // 7. BlockEnv 的 9 个字段(逐个 requires,类型全是 bcos::)
    { env.fiscoHeader } -> std::convertible_to<bcos::protocol::BlockHeader const&>;
    { env.parentHash }  -> std::convertible_to<bcos::h256>;
    /* ... prevRandao / baseFeePerGas / feeRecipient / parentBeaconBlockRoot
           / gasLimit / extraData / blobGasUsed ... */
    // 8. ExecuteResult
    { r.receipts.size() } -> std::convertible_to<std::size_t>;
    // 9-10. 错误分类(可 catch 且带 what())
    requires std::derived_from<typename S::ConsensusError, std::exception>;
    requires std::derived_from<typename S::StorageError,  std::exception>;
    // 11. 比对面
    { S::commitmentsOf(r).receiptsRoot } -> std::convertible_to<bcos::h256>;
    /* ... 其余 7 个成员 ... */
};
}  // namespace bcos::engine
```

然后 `static constexpr bool c_opMode = OpSchedulerSeam<SchedulerType, ViewType>;`。

**收益**:11 条要求里的 8 条从「1000 行外的晚期实例化错误」变成「类模板 requires 子句
上的一次诊断」;并且**接缝第一次可以不读 `OpSchedulerImpl.h` 就读懂**——这正是问题 3 的判据。

**迁移成本(诚实评估)**:
- `OpSchedulerImpl` 今天已经满足全部 11 条,**不需要改一行 OP 实现**;
- 新文件只依赖 bcos-framework/bcos-utilities/`<concepts>`,不新增任何 CMake 链接边;
- 风险点:concept 里的 `typename S::BlockEnv` 等在**通用**组合根(`SchedulerSerialImpl`)上
  求值时必须**软失败**而非硬错。`requires` 表达式内部的替换失败是 SFINAE-friendly 的,
  所以 `c_opMode == false` 会正常得出;**但这一点必须实测**,因为 `requires std::derived_from<typename S::ConsensusError, ...>`
  这类嵌套 requires 的软失败边界比 `{ expr }` 形式更微妙。
- `[需验证]`:最小验证步骤 = 落地该 concept 后构建 `bcos-evm-opstack-tests`,
  **期待现象**:`EngineOpBranchTest.cpp:375` 的 `static_assert(!GenericEngineService::c_opMode)`
  与 `:374` 的 `static_assert(OpEngineService::c_opMode)` **同时**通过,
  且 `EngineVersionGateTest.cpp:269/301` 亦然。若通用侧变成硬错,方案需退化为
  「探针保持现状 + concept 只作为 `if constexpr (c_opMode)` 内部的 `static_assert`」。
  这个退化版本仍然拿到全部收益(把错误提前到分支入口),只是不再驱动探针本身。

### 3.4 我评估过并**否决**的两个替代方案

- **独立的 OP 派生类**:`EngineServiceImpl` 是四参数类模板,派生要么 CRTP(把 `if constexpr`
  换成同等复杂的静态多态)、要么复制全部通用状态(`m_payloadCache`/`m_forkchoiceState`/
  `x_state`…)。而 6 处 `if constexpr` 里有 4 处是**纯粹的短路**(拒绝/换 capability 列表),
  只有 2 处是真分支。为 2 处真分支引入一层继承不划算。
- **策略类特化**:`OpPolicy<SchedulerType>` 把 6 处分流搬进特化。问题在于两个真分支
  (`handleOpNewPayload` 与通用 `handleNewPayload` 体)**共享 4 个成员状态**,策略类要么
  持有 `EngineServiceImpl&` 反向引用(循环)、要么接收 4 个参数——那就等价于 §1.3 的自由
  函数模板方案,但多了一层类型。**自由函数模板更简单,收益相同。**

---

## 4. 重复(问题 4)

### 4.1 【Important-2,新发现】`decodeEip1559Tx` / `decodeSetCodeTx` 是 40 行克隆

**位置**:`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:557-599` ↔ `:606-646`

13 个字段里 **11 个**逐字相同、顺序相同;5 处校验各写两遍:

| 校验 | eip1559 | setcode |
|---|---|---|
| chainId 比对(C2) | `:566-567` | `:615-616` |
| gasLimit int64 收窄(C4) | `:571-572` | `:620-621` |
| **`yParity > 1`(I1)** | `:583-584` | `:630-631` |
| `expectExhausted(listBody)` | `:587` | `:634` |
| `expectExhausted(body)` | `:588` | `:635` |
| `tx.v = static_cast<uint8_t>(yParity)` | `:589` | `:636` |
| `recoverTxSender(preimage, yParity, r, s)` | `:596` | `:643` |

**这正是本仓已实证过的那条**:提交 `a47b00e78` 的标题即「I-2 setcode yParity 测试盲区」——
校验在两处都写了,但**测试只覆盖了一处**,复审时才发现。结构没变,下一次照样会发生。

**会出错的后续改动 —— 具体且已经在门口**:op-geth 的 `ErrTipAboveFeeCap`
(`maxPriorityFeePerGas > maxFeePerGas` → 整块无效,`core/types/transaction.go` /
`txpool` 校验链)在**两个解码器里都不存在**。谁去补这条 —— 那是迟早的事,因为它是
op-geth 会拒而本实现放行的一类块 —— 极大概率补在 `decodeEip1559Tx` 上(它排在前面,
也是更常见的类型),然后 `decodeSetCodeTx` 保持放行。后果:一笔 tip > feeCap 的 **0x04**
交易组成的块,op-geth 判无效,本验证者判 VALID。同一类型的第二次犯同一个错。

**合并方案(具体,含风险)**:字段顺序**不允许**朴素合并——`authorizationList` 夹在
`accessList` 与 `yParity` 之间,把 `yParity/r/s` 提前会破坏 RLP 顺序。必须切成
「前缀 + 可选中段 + 后缀」三段:

```cpp
// detail 内新增
inline void decodeTypedTxPrefix(bcos::bytesRef& listBody, evmone::state::Transaction& tx,
    uint64_t chainId, const char* kind);            // chainId..accessList(含全部 5 处校验)
inline intx::uint256 decodeTypedTxSignature(bcos::bytesRef& listBody,
    evmone::state::Transaction& tx, const char* kind);  // yParity(含 >1 校验)/r/s,返回 yParity
```

`decodeSetCodeTx` 在两者之间插一行 `tx.authorization_list = decodeAuthorizationList(listBody);`。
**残留的、必须仍然分开的部分**:signingPreimage 的 `encode_tuple` 实参个数不同
(`:592-595` 9 个 vs `:639-642` 10 个)——这一段不能合,也**不应该**合(它就是两种类型
真正的区别)。

**合并的风险**:`kind` 字符串参数是为了保住 `narrowGasLimit(..., "eip1559.gasLimit")` /
`"setcode.gasLimit"` 的区分——`OpSchedulerImplTest.cpp` 的 I-1 消息断言依赖这个区分
(见 `OpSchedulerImpl.h:179-182` 的注释)。合并时必须把它作为参数透传,否则那批断言退化
成「两处都能过」。这是一个**已知的、必须在同一次改动里处理的**约束。

### 4.2 【Important-3,新发现】测试 fixture 的跨 TU 复制

**位置**(全部在 `bcos-evm/test/opstack/`):

| 名字 | 出现的 TU | 形态 |
|---|---|---|
| `registerVerifiedBlock` | `EngineOpBranchTest.cpp:158`、`EngineNewPayloadGateTest.cpp:189` | **函数体逐字相同**,只有注释不同 |
| `StorageFixture` | `EngineVersionGateTest.cpp:133`、`EngineOpBranchTest.cpp:141`、`EngineNewPayloadGateTest.cpp:172`、`OpSchedulerImplTest.cpp:169` | **两种不同形态**(带/不带 `view` 成员) |
| `StubExecutor` / `StubMemPool` / `MockExecutorSerial` / `TrivialCheckpointStorage` | 3-4 个 TU 各一份 | 匿名命名空间同名 |
| `GoldenSample` | `EngineNewPayloadGateTest.cpp`、`EthBlockHeaderTest.cpp` | 同名不同结构 |

**会出错的后续改动**:`registerVerifiedBlock` 把 `SYS_HASH_2_NUMBER` 的键值编码
(键 = 哈希裸 32 字节,值 = 十进制字符串)**复刻**了生产实现 `registerOpBlock`
(`EngineServiceImpl.h:1152-1157`)。一旦生产编码变更(例如值改成大端 u64——这在
`SYS_CURRENT_STATE` 接入、§6.4 (b) 落地时是个真实候选),改的人会搜到**一个**
`registerVerifiedBlock` 并修好,另一个 TU 的种子照旧写老编码 → 那个 TU 里所有
「预登记 parent」的用例开始返回 **SYNCING** 而非 VALID。而这类失败在测试里的表现是
「一片红,但不是编译错误」,最容易被当成「新编码有 bug」而回滚掉正确的改动。

同样,`StorageFixture` 有两种形态但同名,任何一处对种子布局的修正(例如补一行
`SYS_TABLES`)只会作用于一个 TU。

**修法**:抽 `bcos-evm/test/opstack/EngineTestFixtures.h`(测试专用头,不进生产库),
把 5 个 stub + `registerVerifiedBlock` + `StorageFixture` 收进具名命名空间
`bcos::evm::test::engine`。**顺带解决 §6.4 (i)③**——匿名命名空间同名类型正是
`UNITY_BUILD` 打不开的原因。

**对 §6.4 (i)③ 的独立判断**:该条已记账,**但计数偏低**——它写的是「三个 engine 测试 TU」,
实测是 **五个**(还有 `OpSchedulerImplTest.cpp` 与 `EthBlockHeaderTest.cpp`)。裁定
(「当前无 ODR 问题」)仍然成立,但它把这条框成了「UNITY_BUILD 的阻碍」,漏掉了更常发生的
那一半:**fixture 语义漂移**。优先级被低估。

### 4.3 【Minor-1】两套彼此独立的「严格规范 RLP」解码器

**位置**:`bcos-codec/bcos-codec/rlp/EthBlockHeader.cpp:28-133`(匿名命名空间:
`expectString` / `decodeFixed` / `decodeScalarBytes`)
↔ `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:300-335`(`readCanonicalScalar` / `readFixedWidth`)

同一批规则的两份实现:①拒绝 list;②定宽字段必须精确等长;③标量不得有前导零;④标量不得
超宽。两者都在同一批复审(B4-1 / B4-2)里、出于同一个理由被写出来,只是错误通道不同
(`Error::UniquePtr` vs `throw OpConsensusError`)。**当前无语义分歧**(我逐条比对过)。

**会出错的后续改动**:§6.4 条目 (n) 把「`computeOpTxRoot` ↔ op-geth `DeriveSha` 的等价性」
钉在解码器严格性上——但它只钉了 **`OpSchedulerImpl.h` 那一份**。如果有人为「兼容性」放宽
`EthBlockHeader.cpp` 那一份(它读的是**本节点自己写的**头,看起来放宽是安全的),
后果是 step 3a 的父头读路(`EngineServiceImpl.h:869-891`)开始接受一个 `encode()` 不能
逐字节还原的头 —— 那个头的 keccak 不等于它被存进去时的块哈希,而 timestamp 单调校验
正是拿它做基准。已记账条目 (n) 覆盖不到这一份。

**修法**:不建议合并(错误通道不同、跨库)。建议把 (n) 的台账措辞扩成「**两处**严格层」
并在 `EthBlockHeader.cpp:30-33` 的注释里互指 `OpSchedulerImpl.h:275-296`——后者已经写了
很完整的理由,前者只说了「比 RLPDecode.h 更严格」,没说**为什么不能放宽**。

### 4.4 【Minor-2】`if (bridge.poisoned()) throw OpStorageError(...)` × 5

**位置**:`OpSchedulerImpl.h:841-842`、`:867-868`、`:874-875`、`:888-889`、`:893-894` —— 逐字相同的两行。

**会出错的后续改动**:在 `executeOpBlock` 里插入任何一个新的「读桥」步骤(最可能的候选:
§6.4 (e) 的 Holocene baseFee 需要读父头、或未来的增量 stateRoot)而忘记补这一对。
`Storage2Ledger` 的读方法是 `noexcept` 的、把失败吞进毒旗(`Storage2Ledger.h` 的
「毒旗错误通道」契约),所以漏检的后果**不是抛异常**,是拿到一个默认值继续算 ——
stateRoot 算错 → 好块被判 INVALID(存储故障被当成了对区块的裁决,恰恰是 §4.3 明令禁止的)。

**修法**:`inline void throwIfPoisoned(auto& bridge)` 一个 helper,把两行压成一行。
**诚实说明**:helper **不能**阻止遗漏,它只降低成本。真正能阻止遗漏的做法是让每个步骤
返回时强制过一次检查(例如把 `bridge` 的读接口包一层返回 `expected<T>` 的门面),
那是超出本轮的改造。所以我把它记为 Minor 而不是 Important。

### 4.5 我核查过并**判定不是问题**的重复

- **`narrowU256ToU64` 两份**(`EngineServiceImpl.cpp:166` 返回 `optional` vs
  `OpSchedulerImpl.h:154` 抛异常)。跨库、错误通道不同、两处都有交叉引用注释。合并会强制
  引入库依赖边。**不合并是对的。**
- **三个协议常量重复**(`c_emptyOmmersHash`/`c_posNonce`/`c_opEmptyRequestsHash`,
  `EngineServiceImpl.cpp:44-48`,与 `bcos-evm/eth/state/hash_utils.hpp:27`、
  `OpBlockSeal.h:19`、`EthBlockHeaderTest.cpp:66` 重复)。**已被金值 gate 锚死**:
  `EngineNewPayloadGateTest.cpp:885-895` 断言 `rebuildOpEthHeader().encode() ==
  golden.encodedHeaderHex`,而 `payload.blockHash` 来自 op-geth 金值而非自算
  (该文件 `:22-25` 与 `:535` 明确写了这一点)。任一常量打错 → 33 条金值全红。
  `requestsHash` 还额外有运行时交叉比对(`EngineServiceImpl.h:1087-1093`)。
  **这是本分支里做得最扎实的一处防重复漂移,不构成发现。**
- **`decodeDepositTx` ↔ `encodeDepositEnvelope`** 是一对 encode/decode,分居两库、无
  round-trip 测试 —— 但**两侧各自被同一批 op-geth 金值独立锚死**
  (`EthBlockHeaderTest.cpp:358` 的 33 向量 deposit 全量 + gate 的 txRoot 比对),
  金值锚比 round-trip 更强。**不构成发现。**

---

## 5. 依赖方向(问题 5)

### 5.1 核对结果:方向干净,零反向依赖

`grep -rn "bcos-evm" engine/`(排除 `engine/test`)的**全部**命中都是注释文本,没有一条
`#include`。`bcos-codec/rlp/{EthBlockHeader,OpDepositEncode}.h` 只包含
`bcos-utilities/{Common,Error,FixedBytes}.h` + 标准库,不碰 bcos-evm、不碰 bcos-framework。
`bcos-evm/bcos-evm/` 下没有任何文件包含 `engine/bcos-engine/`。
`engine/ → bcos-evm/ → bcos-codec/ → bcos-framework/` 这条声明的方向**没有被违反**。

接缝走「`SchedulerType` 依赖名」而非 `#include`,这是本分支最好的一个设计决定——
它让方向在**物理上**无法被违反。

### 5.2 关于「`EthBlockHeader::decode` 靠测试二进制传递链接」这条记账 —— **已过时,不再属实**

原记账文本在 `.superpowers/sdd/.../progress.md:123`:
> ⚠️ engine target 未链 codec,EthBlockHeader::decode 靠测试二进制传递链接,生产组合根接入时须处理。

**核实结果(三步)**:

1. `bcos-codec/CMakeLists.txt:24` —— 本分支新增 `aux_source_directory(bcos-codec/rlp SRC_LIST)`。
   基线 `42e62fcef` 的 `bcos-codec/bcos-codec/rlp/` 只有 3 个 `.h`(`Common.h`/`RLPDecode.h`/
   `RLPEncode.h`),**没有任何 `.cpp`**,所以基线不需要这一行。本分支新增的
   `EthBlockHeader.cpp` / `OpDepositEncode.cpp` 因此**已经编进 `codec` 库**。
2. `bcos-ledger/CMakeLists.txt:27` —— `target_link_libraries(ledger PUBLIC tool protocol-tars codec ...)`,
   `codec` 是 **PUBLIC**。
3. `engine/CMakeLists.txt:14` —— `target_link_libraries(engine PUBLIC bcos-framework bcos-task bcos-utilities ledger)`。

结论:`engine` **传递地**拿到了 `codec` 的定义与 PUBLIC include 目录
(`codec` 的 `target_include_directories(... PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)`
正是 `<bcos-codec/rlp/EthBlockHeader.h>` 能解析的原因)。
`EngineServiceImpl.h:42-45` 的注释描述的就是这个事实,**注释是对的**。

**所以:这不是一颗会在集成时爆炸的定时炸弹。** 该记账条目应当改写或撤销
(它现在会误导集成者去解决一个已经解决的问题)。

### 5.3 【Minor-3】但确实有一条**较弱**的隐患:`engine` 没有具名 `codec`

`engine/bcos-engine/EngineServiceImpl.h:46` 直接 `#include <bcos-codec/rlp/EthBlockHeader.h>`,
而 `engine/CMakeLists.txt` 的 `target_link_libraries` 里**没有 `codec`**——依赖完全建立在
「`ledger` 恰好 PUBLIC 链了 `codec`」之上。

**会出错的后续改动**:任何人把 `bcos-ledger/CMakeLists.txt:27` 的 `codec` 从 PUBLIC 改成
PRIVATE(一次完全合理的依赖收紧清理,`ledger` 的公开头是否真的暴露 codec 类型是可以质疑的),
`engine` 立刻**编译失败**:`fatal error: bcos-codec/rlp/EthBlockHeader.h: No such file or directory`。
改 `ledger` 的人不会预期自己弄坏了 `engine`。

**修法**:`engine/CMakeLists.txt:14` 加一个词 —— `... bcos-utilities ledger codec)`。
一行,零风险(`codec` 已在链接闭包里,不新增任何实际依赖),把隐式契约变成显式声明。

---

## 6. 命名(问题 6)

### 6.1 `ValidPayloadRegistersAllFourTables` 现在写五张表 —— 【Minor-4,取舍不成立】

**位置**:`bcos-evm/test/opstack/EngineOpBranchTest.cpp:941`,自辩注释在 `:934-940`:
> 测试 NAME 保持原样是刻意的:批 3 的报告按名字引用它作为回执写入变异实验的红色见证,
> 改名会破坏该可追溯性。名字陈旧,覆盖不陈旧。

**我的判断:这个取舍不成立。** 三点:

1. **代价被高估**。可追溯性的载体是**批 3 报告**——那是一份历史文档,它引用的是一个历史
   时刻的测试名。给测试改名并**在测试注释里写一行**「(原名 `ValidPayloadRegistersAllFourTables`,
   批 3 报告按此名引用)」,可追溯性一分不少。这正是本分支在别处反复用的手法
   (`OpSchedulerImpl.h:146-148` 就是这么处理 `toBcosH256` 搬家的)。
2. **收益被低估,且有具体失效场景**。§6.4 (b) 落地时要写 `SYS_CURRENT_STATE`,那就是**第六张表**。
   实施者会去找「钉住登记清单的那个测试」,搜到 `AllFourTables`,面对一个名字说四、
   注释说五、实际断言四的测试。两个后果都真实发生过的:要么他判定这个测试已经陈旧、
   另起一个新测试(于是清单钉子变成两颗,谁都不完整);要么他信名字,认为登记面只有四张表,
   把第六张表的断言写进别处 —— 而 `RawTransactionEnvelopesAreRegisteredUnderEthTxHash`
   这个第五张表的断言散在另一个测试里,已经开了这个头。
3. **代价为零**。GTest 测试名不是 API,没有外部消费者。

**修法**:改名为 `ValidPayloadRegistersLedgerAndHeaderTables`,或干脆把第五张表的断言并回来
改成 `ValidPayloadRegistersAllFiveTables`。

### 6.2 【Minor-5,新发现】`SYS_ETH_BLOCK_HEADER` / `SYS_ETH_HASH_2_RAWTX` 的 `SYS_` 前缀名不副实

**位置**:`bcos-evm/bcos-evm/engine/OpEngineSeam.h:51` 与 `:74`。

本仓的 `SYS_*` 常量**无一例外**住在 `bcos-framework/.../LedgerTypeDef.h`,并且**定义**了
每个 FISCO 节点都要有的账本 schema。这两个常量是 OP 验证者模式**专有**的表
(裁定 B5 明确要求它们**不进** `LedgerTypeDef.h`),却沿用了同一个前缀。

**会出错的后续改动**:①做 schema 审计的人 `grep SYS_ LedgerTypeDef.h`,得到一份完整清单,
据此断定「OP 模式不额外写表」——漏掉两张;②或者反过来:某个「一致性整理」的人看到
`SYS_*` 却不在 `LedgerTypeDef.h`,认为这是疏漏,把它们搬进去 —— 直接违反裁定 B5,
并且改动 `LedgerTypeDef.h` 触碰了本分支的零触碰硬约束。两个方向都是由名字诱发的。

**修法**:重命名 C++ 标识符为 `OP_ETH_BLOCK_HEADER_TABLE` / `OP_ETH_HASH_2_RAWTX_TABLE`。
**字符串字面值 `"s_eth_block_header"` / `"s_eth_hash_2_rawtx"` 必须保持不变**——那是落盘的
表名,改了就是数据不兼容。改动面:`OpEngineSeam.h:51/74`、`OpSchedulerImpl.h:715/717`、
`EngineOpBranchTest.cpp` 的引用点。纯标识符重命名,零行为变化。

### 6.3 【Minor-6】`runOpNewPayloadSteps` 的文档注释与实际内容不符

**位置**:`EngineServiceImpl.h:747` —— `/// Design §6.1 steps 2-6.`

函数体实际包含 step 2、3、**3a**、**3b**、**3c**、4、5、6。3a/3b/3c 不是 §6.1 的步骤,
是批 2/批 4 复审新增的检查(注释里各自都诚实说明了「批 X 新增」)。

**会出错的后续改动**:对着 spec §6.1 逐条核对实现的人,按「steps 2-6」的承诺读这个函数,
会把 3a/3b/3c 当成 §6.1 的一部分去 spec 里找对应条款,找不到 → 要么以为 spec 缺失、
要么以为实现越权。实际两者都不是。**修法**:注释改成
`/// Design §6.1 steps 2-6, plus the review-added parent-consistency checks 3a/3b/3c.`
(一行)。

### 6.4 【Minor-7】`lookupBlockNumberByHash` 是死代码,且是**危险的**死代码

**位置**:`EngineServiceImpl.h:1453-1467`。全仓 `grep` 只有定义,**零调用点**。

**是既有代码**(基线 `42e62fcef` 的 `:641` 已存在),不是本分支引入。但本分支让它变得
危险了:它按哈希查块号的方式是**读内存态**(`m_blockHashToPayloadId` + `m_payloadCache`),
而 OP 分支的 step 3(`EngineServiceImpl.h:790-802`)刻意**不用**内存态、改走
`SYS_HASH_2_NUMBER` 存储查询,并且 OP 分支**从不填充**这两个容器
(`:793-795` 的注释明确说了这一点)。

**会出错的后续改动**:§6.4 (b)「`SYS_CURRENT_STATE` 随 FCU head 推进」的实施者需要
「按哈希查块号」这个能力,在类里搜到一个现成的私有方法 `lookupBlockNumberByHash`,
用它 —— 在 OP 模式下它**永远返回 `nullopt`**,因为那两个容器在 OP 路径上恒空。
后果是 FCU head 永远推不动,而且没有任何报错。

**修法**:删掉它(它零调用),或至少加一行 `/// NOT usable in OP mode: reads the generic
path's in-memory cache, which the OP branch never populates. Use ledger::getBlockNumber(..., fromStorage).`

### 6.5 【Minor-8】`encodeDepositEnvelope` 是零生产消费者的生产库代码

`bcos-codec/bcos-codec/rlp/OpDepositEncode.{h,cpp}`(133 行)的唯一调用者是
`bcos-evm/test/opstack/EthBlockHeaderTest.cpp`。它现在被编进 `codec` 库、并随
`bcos-codec/CMakeLists.txt:50` 的 `install(DIRECTORY "bcos-codec" ...)` 装进对外头文件。

**会出错的后续改动**:任何一个「清理未使用代码」的自动化或人工审查会把它标为死代码并删除
—— 而它其实是 33 条金值向量里 deposit 部分的**唯一**字节重建路径(spec §7.1:
「deposit envelope 由 `OpDepositEncode` 从结构化字段重建」),删掉会让金值 gate 无法重建。
**修法**:在 `OpDepositEncode.h` 的文件头加一行说明它是金值仪式的组成部分、生产路径不消费它。
(不建议搬进 test/:它按 §3「模块布局」明确规划在 bcos-codec,搬动会与 spec 不符。)

---

## 7. CMake(问题 7)

### 7.1 「编入源码 vs 链 engine 库二选一」的护栏 —— **靠注释,不靠断言**【Important-4】

**位置**:`bcos-evm/test/CMakeLists.txt:82-102`。注释本身是诚实的(`:89-96` 明确
承认「the guardrail is a CONVENTION, not something the build enforces」,并纠正了早先
「会 duplicate symbol」的错误说法)。**但护栏本身仍然只是一段注释。**

我核对了注释的技术论断,基本正确但**方向偏乐观**:
- 论断「`engine` 是 STATIC + UNITY_BUILD ON,即一个 archive member;链接器只在需要解析
  未定义符号时才抽取,所以会静默链过」—— 在**当前**条件下成立;
- **注释没说的那一半**:正因为 UNITY_BUILD 让 `engine` 只有一个 archive member,
  一旦这个测试二进制将来需要 `engine` 里**任何别的**符号(例如未来 engine 新增一个
  `.cpp` 提供的 helper),链接器就会抽取**整个** member —— 那个 member 同时定义了
  `bcos::engine::detail::validateOpNewPayloadRequest` 等,与本二进制已编入的同名符号
  **重复定义,链接失败**。所以失败形态不是恒定静默,而是「先静默、在某个不相关的
  后续改动上突然爆炸,且错误信息完全不指向真正的原因」。

**会出错的后续改动(具体)**:有人为了复用 engine 的某个新 helper,在 `:151` 之后加一行
`target_link_libraries(bcos-evm-opstack-tests PRIVATE engine)`,并**保留** `:102` 的源码条目
(注释叫他删,但注释在 20 行之上、且没有任何机制提醒)。当下:链接通过,但 engine 库里
那份 `EngineServiceImpl.cpp` 的目标码**从未被使用** —— 于是一个只改 `EngineServiceImpl.cpp`
的人重新构建 `engine` 后跑测试,看到的仍是**编入的那份旧代码的行为**。这就是注释说的
「stale/duplicated definition nobody notices」,而它**不会**在下一次构建时自愈。

**修法(4 行,真正可执行的断言)**,放在 `bcos-evm/test/CMakeLists.txt:152` 的 `endif()` 之前:

```cmake
    # 护栏(裁定 B2)的可执行形式:编入 EngineServiceImpl.cpp 与链 engine 库互斥。
    get_target_property(_opstack_test_libs bcos-evm-opstack-tests LINK_LIBRARIES)
    if(_opstack_test_libs AND "engine" IN_LIST _opstack_test_libs)
        message(FATAL_ERROR
            "bcos-evm-opstack-tests 同时链接了 `engine` 并编入 "
            "engine/bcos-engine/EngineServiceImpl.cpp(见本文件 §编入与链 engine 库二选一)。"
            "二者互斥:请删除 target_sources 里的 EngineServiceImpl.cpp 条目。")
    endif()
```

`[需验证]`:最小验证步骤 = 临时在 `:151` 后加 `target_link_libraries(bcos-evm-opstack-tests
PRIVATE engine)`,只跑 `cmake -S . -B <某个新目录>`(**不构建**,避免占用批 6 复审者的
构建目录)。**期待现象**:configure 阶段直接 FATAL_ERROR 并打印上述文本;移除后 configure 正常。

### 7.2 `UNITY_BUILD` 与匿名命名空间的冲突风险

**核对结果**:
- `engine` target:`UNITY_BUILD ON`(`engine/CMakeLists.txt:15`),但
  `engine/bcos-engine/` 下**只有一个** `.cpp`(`EngineServiceImpl.cpp`),
  其匿名命名空间的 5 个常量(`c_hashBytes`/`c_payloadIdBytes`/`c_emptyOmmersHash`/
  `c_posNonce`/`c_opEmptyRequestsHash`)无冲突对象。**当前无风险。**
  **未来风险**:engine 增加第二个 `.cpp` 且复用 `c_hashBytes` 这种通用名 → unity 硬冲突。
- `codec` target:`UNITY_BUILD ON`(`bcos-codec/CMakeLists.txt:34`)。本分支新增的
  `EthBlockHeader.cpp:28` 有匿名命名空间(`expectString`/`decodeFixed`/`decodeScalarBytes`/
  `decodeU64`/`decodeU256`/`decodeByteString`);我扫过 `bcos-codec/bcos-codec/{abi,scale,rlp}/*.cpp`,
  **没有第二个匿名命名空间**,无冲突。`OpDepositEncode.cpp` 无匿名命名空间。
  **注:这是一次真实的、只在本分支才出现的暴露** —— 基线的 `rlp/` 目录没有 `.cpp`,
  是 `bcos-codec/CMakeLists.txt:24` 这一行把它们拉进了 unity 构建。当前干净,但
  `decodeU64` / `decodeU256` / `expectString` 都是极易撞名的名字;后续任何人在
  `abi/` 或 `scale/` 的 `.cpp` 里开一个匿名命名空间用同名函数,就是 unity 硬冲突。
- `bcos-evm-opstack-tests`:**没有** `UNITY_BUILD` 设置(我 grep 了根 `CMakeLists.txt`、
  `bcos-evm/CMakeLists.txt`、`bcos-evm/test/CMakeLists.txt`,无 `CMAKE_UNITY_BUILD`
  全局开关)。所以 §4.2 列出的 5 个 TU 同名类型**当前不冲突**,但这个 target 因此
  **永久无法打开 UNITY_BUILD**,除非先做 §4.2 的 fixture 提取。这与 §6.4 (i)③ 的记账
  一致(但计数应从「三个」更正为「五个」)。

### 7.3 `${CMAKE_SOURCE_DIR}` 的作用范围 —— 两个问题

**范围核对**:`target_include_directories(bcos-evm-opstack-tests PRIVATE ${CMAKE_SOURCE_DIR}
${CMAKE_SOURCE_DIR}/bcos-ledger ${CMAKE_SOURCE_DIR}/transaction-scheduler)`(`:128-131`)
是 **PRIVATE**,不传播给任何下游;且整段在 `if(TARGET bcos-framework)` 内,standalone 构建
不生效。**这两点注释说得对。** 但它作用于该 target 的**全部 ~25 个源文件**,不止三个 engine
测试 —— 这一点 §6.4 (i)④ 已记账,注释里「exposure is also bounded」的措辞确实弱化了范围。

**【Important-5,新发现】注释与 spec 共同断言的一个事实是错的**:

`bcos-evm/test/CMakeLists.txt:119-127` 与 §6.4 (i)④ 都以「仓库根**无无扩展名文件**,
同名目录项被跳过」作为「无遮蔽」的论据。**实测:仓库根有 `LICENSE`,一个无扩展名文件。**
(`ls -p | grep -v /` 的完整结果:`ChangeLog.md`、`CMakeLists.txt`、`codecov.yml`、
`LICENSE`、`README.md`、`vcpkg-configuration.json`、`vcpkg.json`。)

`LICENSE` 本身无害(没有任何标准头叫这个名字),所以**当前没有实际遮蔽**——结论是对的,
**论据是错的**。这不是吹毛求疵,因为论据错了会让后续判断失效:

**会出错的后续改动(具体且高度可信)**:有人在仓库根加一个名为 `version` 的无扩展名文件
(版本号文件是极其常见的仓库根产物)。`<version>` 是一个**真实的 C++20 标准头**,
被 libstdc++ / range-v3 / fmt 等广泛间接包含。该 target 的 ~25 个 TU 里任何一个的
包含链一旦触及 `<version>`,就会解析到仓库根那个文本文件 —— 编译错误诡异到几乎不可能
被归因到这条 include 路径上。而做这次改动的人,如果按注释和台账的说法「仓库根无无扩展名
文件」去自查,会**确认自己是第一个**,从而放心加下去 —— 恰恰是最坏的情况。

**修法(两选一)**:
- (a) 把注释与 §6.4 (i)④ 的论据改成事实:「仓库根现有一个无扩展名文件 `LICENSE`,
  与任何标准/第三方头名均不冲突;**新增仓库根无扩展名文件前必须核对它是否与某个标准头同名
  (`version`、`filesystem`、`memory`、`ranges`… 均为真实标准头名)**」;
- (b) 更彻底:把 `${CMAKE_SOURCE_DIR}` 收窄。三个 engine 测试需要的只是能解析
  `#include "engine/bcos-engine/EngineServiceImpl.h"` —— 那是**引号包含**,可以直接
  改成相对路径或改用 `${CMAKE_SOURCE_DIR}/engine` + `#include <bcos-engine/EngineServiceImpl.h>`,
  从而完全不需要把仓库根放上去。注释 `:126-127` 自己也提到了「the fix is to narrow these
  to the specific subdirectories actually needed」——建议现在就做,不要等到「a top-level
  *file* ever collide」发生之后。

### 7.4 `bcos-codec/CMakeLists.txt` 的一行改动 —— 正确,但值得留一句

`:24` `aux_source_directory(bcos-codec/rlp SRC_LIST)` 与既有的 `abi`/`scale` 两行同构,
风格一致,是必要的(否则新增的两个 `.cpp` 根本不会被编译)。**没有问题。**
唯一值得注意的是 §7.2 提到的副作用:它把 `rlp/` 的 `.cpp` 拉进了 `codec` 的 unity 构建。

---

## 8. 发现汇总

| # | 级别 | 标题 | 位置 | 台账状态 |
|---|---|---|---|---|
| I-1 | Important | 接缝 11 条要求只探 1 条;`OpBlockCommitments` / `OpBlockEnv` 加字段**静默**漂移 → 错判 VALID / 好块判 INVALID | `OpEngineSeam.h:112-122` ↔ `EngineServiceImpl.h:1050-1093`;`OpSchedulerImpl.h:94-109` ↔ `EngineServiceImpl.h:974-984` | **新发现**(§6.4 (i)② 只覆盖反方向) |
| I-2 | Important | `decodeEip1559Tx`/`decodeSetCodeTx` 40 行克隆,5 处校验各写两遍;`ErrTipAboveFeeCap` 是下一个会只补一边的 | `OpSchedulerImpl.h:557-599` ↔ `:606-646` | **新发现** |
| I-3 | Important | 测试 fixture 跨 5 个 TU 复制(`registerVerifiedBlock` 逐字重复;`StorageFixture` 两种形态同名) | `EngineOpBranchTest.cpp:158/141`、`EngineNewPayloadGateTest.cpp:189/172`、`EngineVersionGateTest.cpp:133`、`OpSchedulerImplTest.cpp:169` | 部分已记账 §6.4 (i)③,**计数偏低(三→五)且漏了 fixture 漂移这一半** |
| I-4 | Important | 「编入 vs 链 engine 二选一」护栏只是注释;可用 4 行 CMake 断言真正强制 | `bcos-evm/test/CMakeLists.txt:82-102` | **新发现** |
| I-5 | Important | 「仓库根无无扩展名文件」是**错的**(`LICENSE`);论据错会让下一个加根级 `version` 文件的人误判 | `bcos-evm/test/CMakeLists.txt:119-127` + §6.4 (i)④ | **新发现(证伪一条已记账论据)** |
| M-1 | Minor | 两套独立的严格规范 RLP 解码器;§6.4 (n) 只钉住其中一份 | `EthBlockHeader.cpp:28-133` ↔ `OpSchedulerImpl.h:300-335` | 部分已记账 §6.4 (n) |
| M-2 | Minor | `if (bridge.poisoned()) throw ...` 逐字重复 5 次,无 helper | `OpSchedulerImpl.h:841/867/874/888/893` | **新发现** |
| M-3 | Minor | `ValidPayloadRegistersAllFourTables` 写五张表;保名取舍不成立 | `EngineOpBranchTest.cpp:941` | 已知(实施者主动披露) |
| M-4 | Minor | `SYS_ETH_*` 前缀名不副实(OP 专用表却用 FISCO schema 前缀) | `OpEngineSeam.h:51/74` | **新发现** |
| M-5 | Minor | `engine` target 未具名 `codec`,靠 `ledger` PUBLIC 传递 | `engine/CMakeLists.txt:14` | 记账文本**已过时**(见 §5.2) |
| M-6 | Minor | `runOpNewPayloadSteps` 注释说 "steps 2-6",实含 3a/3b/3c | `EngineServiceImpl.h:747` | **新发现** |
| M-7 | Minor | `lookupBlockNumberByHash` 死代码,且在 OP 模式下恒返回 `nullopt` | `EngineServiceImpl.h:1453-1467` | **新发现**(既有代码,本分支使其变危险) |
| M-8 | Minor | `encodeDepositEnvelope` 零生产消费者却在生产库并被安装 | `bcos-codec/bcos-codec/rlp/OpDepositEncode.{h,cpp}` | **新发现** |

**Critical:0 条。** 我没有找到组织层面会**当下**导致共识分歧/UB/数据损坏的问题;
I-1 的后果等级是 Critical,但按共享上下文的分级规则(「正确性风险但当前不可达」)记为 Important。

---

## 9. 建议的落地顺序(如果协调者只做一部分)

1. **I-1 的两条绊线**(结构化绑定 1 行 + `makeBlockEnv` ~10 行)—— 投入最小、把两种
   **静默**失效变成编译期诊断,收益/成本比远高于其余全部。
2. **I-4 的 CMake 断言**(4 行)—— 纯 configure 期,零构建风险。
3. **I-5(a)** 改注释与台账论据(纯文本)。
4. M-5(`engine` 加 `codec`,1 行)、M-6/M-7(注释/删死代码)、M-3/M-4(改名)。
5. **I-1 的 concept**(~60 行,需实测通用侧软失败)。
6. **I-2 / I-3 的去重**(各 ~1 小时,需跑测试验证零行为变化)。
7. **§1.3 的文件拆分** —— 明确建议**不在本轮做**。

---

## 10. 我无法判定、需要协调者裁定的

1. **I-1 的 concept 方案是否会在通用组合根上硬错**。`requires std::derived_from<typename
   S::ConsensusError, std::exception>` 这类**嵌套 requires** 在 `SchedulerSerialImpl`
   (无 `ConsensusError` 成员)上的替换失败是否 SFINAE-friendly,我有把握说「应该是」,
   但这正是本分支已经踩过一次的那类坑(`c0288b8b0`:`registerOpBlock` 签名里的 OP 依赖名
   让通用组合根无法实例化)。**必须实测**,不能推理。若硬错,退化为「探针不变 +
   `if constexpr (c_opMode)` 内首行 `static_assert(OpSchedulerSeam<...>)`」,收益基本保留。
2. **I-3 的 fixture 提取会不会与「零触碰 `vectors/`/`golden/`」冲突**。提取只动
   `test/opstack/*.cpp` 与新增一个 `.h`,按我的读解不触碰受保护目录;但本轮对测试资产
   的红线很严,请协调者确认。
3. **M-3 改名是否真的会破坏批 3 报告的可追溯性**。我判断不会(报告是历史文档,
   加一行「原名」注释即可),但作出「保名」决定的是实施者,他可能掌握我不知道的
   引用关系(例如某个自动化脚本按测试名过滤)。
4. **§1.3 的拆分是否应该进入下一轮的正式任务**,还是永久接受当前形态。我给的是
   「本轮不做」,但没有立场判断它是「延后」还是「不做」——这取决于这份代码的预期寿命
   和后续 OP 工作量(§6.4 还有 19 条欠账,其中 (k)/(l)/(q) 都要动 OP 分支)。
   如果 (k)/(l) 真要落地,OP 分支还会再涨几百行,那时 `EngineServiceImpl.h` 会逼近 2000 行。
