# Task 4 报告:Types.h 载体 + OpSchedulerImpl 双签名 + fork 阈值注入 + 单测

## 状态:**未编译验证**

按用户执行协议:开发期跳过 FISCO 编译/测试运行——代码与测试全部照写照提交,不
`cmake --build`、不 `ctest`。自审改为"逐 API 对照仓内已编译通过的用法先例静态走查",
见下文"API 先例映射表"(本任务无编译条件下能拿到的最强正确性证据)。

## 实现要点

### 1. `bcos-framework/bcos-framework/engine/Types.h`

`ExecutionPayload` 增两个可选字段:`rawTransactions`(`std::optional<std::vector<bytes>>`)
与 `withdrawalsRoot`(`std::optional<h256>`)。通用路径(现有 `transactions` 字段/所有既有
调用点)零改动、零读取——纯新增可选字段,不触碰任何既有成员。

### 2. `bcos-evm/bcos-evm/opstack/OpForkSchedule.{h,cpp}`

新增 `OpForkTimestamps{ uint64_t isthmusTime; uint64_t jovianTime; }` 与自由函数
`configAt(uint64_t timestamp, const OpForkTimestamps&) noexcept`。判定规则(裁定 A5):
`timestamp ∈ [isthmusTime, jovianTime)` → `isthmusConfig()`;`∈ [jovianTime, +∞)` →
`jovianConfig()`;`< isthmusTime` 同样落 Isthmus(本闭环 Isthmus+ only,无更早档可退,brief
Step 1(e) 两个测试分区未覆盖该分支,已在注释中显式声明为"未覆盖但有意"而非遗漏)。既有
7 个具名工厂函数(`ecotoneConfig()` 等)原样不动。

### 3. `bcos-evm/bcos-evm/engine/OpReceiptMap.h`(新建)

`mapOpReceipt(const evmone::state::TransactionReceipt&, const
protocol::TransactionReceiptFactory::Ptr&) -> protocol::TransactionReceipt::Ptr`。只映射
status(EVMC_SUCCESS→0,否则→1,FISCO 0=成功惯例)/gasUsed/logs 三项(brief 明示范围);
contractAddress/output/blockNumber 留工厂默认值——OP meta 字段(deposit_nonce/l1_fee/
operator_fee/da_footprint)不在六项比对面内,本任务不映射(design §2 非目标清单)。

### 4. `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h`(新建,核心交付)

- `OpConsensusError`/`OpStorageError`:`std::runtime_error` 派生,继承其构造函数。
- `OpBlockEnv`:9 字段,类型对齐 `Types.h::ExecutionPayload`(`bcos::h256`/`Address`/
  `u256`/`bytes`/`uint64_t`),`fiscoHeader` 为 `const protocol::BlockHeader&`。
- `OpExecuteBlockResult`:`receipts` + `seal`(`OpBlockSeal` 原样)+ `stateRoot`/`gasUsed`/
  `txRoot`(三个独立成员,不并入 seal)。
- `detail` 子命名空间:bcos::↔evmc:: 定长转换、`narrowU256ToU64`(显式越界检查窄化,不用
  裸 `convert_to`/`static_cast`)、`ParentOnlyBlockHashes`、`toBlockInfo`、**原始交易字节
  解码**(deposit 0x7E / eip1559 0x02 / setcode 0x04,三个类型均是 processOpBlock 已理解的
  `OpBlockTx` variant 成员——见下"六步落位"与"API 先例映射表")。
- `OpSchedulerImpl<Storage>`:构造注入 `receiptFactory`/`chainId`/`OpForkTimestamps`;
  `m_vm` 成员 `evmc::VM{evmc_create_evmone()}`;哑 `executeBlock`(立即 throw,消息含
  "OP mode")+ 真 `executeOpBlock`(六步,见下)。

### 5. `bcos-evm/test/opstack/OpSchedulerImplTest.cpp`(新建)

套件名 `OpSchedulerImpl`,5 个 `TEST` + 1 个 `static_assert`(详见"测试清单")。

### 6. `bcos-evm/test/CMakeLists.txt`

`if(TARGET bcos-framework)` 门控块内追加 `OpSchedulerImplTest.cpp` + 链接
`protocol-tars`(具体 `TransactionReceiptFactory`/`BlockHeader` 实现)+ `bcos-crypto`
(`CryptoSuite`/`Keccak256`)。`codec` 链接边复用(Task 3 已加,本任务的
`RLPDecode.h` 依赖同一条边)。

## 六步落位(design §4.3,`executeOpBlock` 内联注释同口径)

1. **分拣**:`for (rawTxBytes) txs.push_back(detail::decodeOneRawTx(...))`——按
   EIP-2718 类型字节分派 deposit/eip1559/setcode 三种解码函数。
2. **桥一块一实例**:`Storage2Ledger<Storage> bridge(storage)`,函数作用域内唯一实例。
3. **processOpBlock**:`bridge` 同时充当 `StateView` 与 `applyDiff` 回调落点
   (`[&bridge](diff){ bridge.applyDiff(diff); }`)。
4. **毒旗/异常分类**:`catch` 块内先查 `bridge.poisoned()`→`OpStorageError`,否则
   `OpConsensusError(e.what())`;`try` 块外**再次**无条件查一次 poisoned()(覆盖
   processOpBlock 正常返回但静默吞掉毒旗错误的路径——Storage2Ledger 的读方法全 noexcept,
   下毒后返回"安全值"而非上抛,`processOpBlock` 可能因此正常返回一个基于错误默认值的
   结果而不自知)。
5. **seal + stateRoot**:`bridge.visitAccounts` 找 MessagePasser 存储快照(`return
   false` 提前结束,不视为毒旗,按 Storage2Ledger.h 文档契约)→ `sealOpBlock` →
   `bcos::evm::stateRootOf(bridge)`,全程桥未销毁;再查一次 poisoned()。
6. **txRoot + gasUsed**:对调用方原始 `rawTxBytes`(不是分拣后的解码结果)建
   `evmone::state::MPT`,key = `evmone::rlp::encode(index)`(op-geth `DeriveSha`
   规约:key 是索引的规范 RLP 编码,不是裸索引字节),value = 原始字节本身;
   `gasUsed = static_cast<uint64_t>(result.gasUsed)`。receipts 经 `std::visit` 取两个
   variant 分支共有的 `.receipt` 字段,喂 `mapOpReceipt`。`co_return
   OpExecuteBlockResult{...}`。

`executeOpBlock`/`executeBlock` 内部均无 `co_await`(processOpBlock/sealOpBlock/
stateRootOf 都是同步调用,异步性完全封装在 Storage2Ledger 内部的嵌套 syncWait 里)——这
正是 design §4.4 显式许可的"外层 Task 协程 → 内层嵌套 syncWait"拓扑,不是遗漏。

## API 先例映射表(无编译条件下的强制交叉核验)

| 本任务调用的 API | 先例(已在仓内编译/使用) |
|---|---|
| `bcos::codec::rlp::decodeHeader`/`decode(bytesRef&, T&)` 变长参数族 | `bcos-rpc/web3jsonrpc/model/Web3Transaction.cpp::decodeTransaction`(EIP-1559 解码,`to` 可空处理 `in[0]==BYTES_HEAD_BASE` 判据逐字复用) |
| `decode(bytesRef&, bcos::Address&)` | 同上 Web3Transaction.cpp:453-457/511-516(`Address addr{}; decode(in, addr);`) |
| `bcos::codec::rlp::encode`/`RLPEncode.h` 系列(Task 3 侧,佐证 `UnsignedByte`/`UnsignedIntegral` 概念覆盖 u256/bool 边界) | `bcos-codec/bcos-codec/rlp/OpDepositEncode.cpp`(`isSystemTransactionByte` 走 UnsignedByte 路径,不走裸 `encode(bool)`——本任务据此纠正了 decode 侧对应字段的做法,见下"自审发现") |
| `evmone::rlp::encode_tuple(...)` 构建签名前摘要 | `bcos-evm/bcos-evm/opstack/OpTransition.cpp::recoverAuthority`(0x05 magic + `encode_tuple(chain_id, addr, nonce)`)、`bcos-evm/test/opstack/T8nReplayHarness.h::replayRecoverAuthority`(同构) |
| `evmmax::secp256k1::ecrecover(hash, r, s, yParity!=0)` | 同上两处,调用形状(`std::span<const uint8_t,32>` 三元组 + bool)逐字复刻 |
| `evmone::state::rlp_encode(const Transaction&)` 各分支字段顺序 | `bcos-evm/bcos-evm/eth/utils/rlp_encode.cpp:38-71`(eip1559/set_code 分支字段顺序是本任务签名前摘要重建的唯一依据源) |
| `evmone::state::rlp_encode(const Authorization&)` | 同上 `rlp_encode.cpp:96-100`(authorization_list 元组顺序) |
| `Storage2Ledger<Storage>` 构造/`applyDiff`/`visitAccounts`/`poisoned`/`firstError` | `bcos-evm/test/opstack/EbT8nReplayTest.cpp::Storage2Backend`、`Storage2LedgerTest.cpp` 全套 |
| `bcos::evm::stateRootOf(ledger)` | `StateRootCompute.h` 自身文档 + `EbT8nReplayTest.cpp::Storage2Backend::stateRoot` |
| `processOpBlock`/`sealOpBlock` 调用形状(参数顺序、`applyDiff` lambda 签名) | `bcos-evm/test/opstack/T8nReplayHarness.h::replayVector`(逐参数对照,见实现注释内联行号引用) |
| `evmone::state::MPT::insert(key, value)` 语义(key 不二次哈希,value 是 trie 叶子原样) | `bcos-evm/bcos-evm/adapter/StateRootCompute.h::stateRootOf<Ledger>`(账户树 `trie.insert(keccak256(addr), encode_tuple(...))`——本任务 txRoot 树刻意**不**对 key 做 keccak,矛盾点已在实现注释中说明:账户树是 secure-trie 需要 hash key,tx 树是 op-geth `DeriveSha` 的普通 trie,key 就是索引的 RLP 编码本身) |
| `evmc::VM{evmc_create_evmone()}` | `bcos-evm/test/opstack/T8nReplayHarness.h::replayAllVectors`(`auto vm = evmc::VM{evmc_create_evmone()};`) |
| `bcos::h256`/`Address` 定长转换(`FixedBytes(byte const*, size_t)`) | `Storage2Ledger.h::applyModifiedEntry`(codeHash 转换)、`addressFromTableName`(memcpy 反向) |
| `bcos::u256` 越界窄化前先显式 `> max` 检查 | `Storage2Ledger.h::fetchAccount` nonce 处理(MEMORY `costofprecompiled-int64-overflow` 同一纪律) |
| `protocol::TransactionReceiptFactory::createReceipt(u256, string, vector\<LogEntry\>, int32_t, bytesConstRef, BlockNumber)` 调用形状 | `bcos-scheduler/src/BlockExecutive.cpp::onTxFinish`(`createReceipt(txGasUsed, contractAddress, logEntries, status, output, number())`) |
| `protocol::LogEntry(bytes, h256s, bytes)` 构造 | `bcos-framework/protocol/LogEntry.h` 自身构造函数签名(唯一签名,无歧义) |
| `bcostars::protocol::TransactionReceiptFactoryImpl(CryptoSuite::Ptr)`/`BlockHeaderImpl` | `bcos-tars-protocol/bcos-tars-protocol/protocol/{TransactionReceiptFactoryImpl,BlockHeaderImpl}.h` 自身构造/setter 签名 |
| `CryptoSuite(Keccak256, nullptr, nullptr)` 构造 | `transaction-scheduler/tests/testSchedulerSerial.cpp:59-60` |
| `StubExecutor`(`createExecuteContext`/`executeTransaction` 形状) | `engine/test/unittests/engine/EngineServiceTest.cpp:147-169`(逐字段复刻;**未**复制该文件同段的 `StubScheduler`/`BloomScheduler` 悬垂工厂模式,按 brief 告诫) |
| `scheduler_v1::TransactionScheduler` concept 的 `requires` 子句形状 | `bcos-framework/bcos-framework/transaction-scheduler/TransactionScheduler.h` 自身定义(唯一权威源) |
| `task::Task<T>` 协程"body 不 co_await 直接 throw/co_return"模式的安全性依据 | `libtask/bcos-task/Task.h:55`(`initial_suspend()` 返回 `std::suspend_always`,懒启动)+ `:63-71`(`unhandled_exception` 标准传播路径) |
| `MultiLayerStorage`/`ViewType`/`TrivialCheckpointStorage` fixture 构造(`fork()`→`newMutable()`) | `bcos-evm/test/opstack/EbT8nReplayTest.cpp::Storage2Backend::Impl` |
| `ThrowingStorage<Storage>` 用法(读全抛、写直通,先建正常桥落种子再换毒 storage) | `bcos-evm/test/opstack/Storage2LedgerTest.cpp::PoisonOnInjectedStorageException` |
| `evmc::address`/`bytes32`/`BloomFilter` 的隐式 `operator bytes_view()` | `evmc/evmc.hpp`(address/bytes32)+ `bcos-evm/bcos-evm/eth/state/bloom_filter.hpp:21`(BloomFilter);后者被 `T8nReplayHarness.h::hexBytes(evmc::bytes_view(seal.logsBloom))` 实际使用,本任务测试断言处照抄同一转换手法(未误用 `.begin()/.end()`) |
| `asH256`/`asAddress`/`asU256`/`asU64`/`asBytes` 十六进制转换辅助 | `bcos-evm/test/opstack/EthBlockHeaderTest.cpp:82-101`(同任务族先例,同一组金值来源) |

## 自审发现并修正的一处实质问题

`bcos::codec::rlp::decode(bytesRef&, bool&)`(RLPDecode.h)要求 `payloadLength==1`,会拒绝
RLP 空串(payloadLength=0)编码的 `false`。但 op-geth 对 Go `bool` 字段(`deposit
tx.IsSystemTransaction`)的真实编码是"false=空串/true=0x01"(Go rlp 包的原生 bool 编码
规则),Task 3 的 `OpDepositEncode.cpp` 编码侧也是按这个规则走 `UnsignedByte` 通用标量路径
(`isSystemTransactionByte ? 1 : 0`),而不是走一个不存在的 `encode(bytes&, bool&)`
重载。若解码侧直接调用 `decode(bytesRef&, bool&)`,对现实中 `isSystemTransaction=false`
(空串编码)的向量会误判"长度错误"炸掉——这是一处会在**真实金向量**(所有非系统
deposit,即全部 `isSystemTransaction=false` 的样本)上必现的解码错误。已改为
`decodeU64Scalar(in) != 0`(该函数走的是能正确处理 payloadLength==0→0 的通用整数标量
解码路径),与编码侧保持字节对称。

## 测试清单(**未编译验证**)

文件:`bcos-evm/test/opstack/OpSchedulerImplTest.cpp`,套件 `OpSchedulerImpl`(5 个
`TEST`)+ 1 个文件作用域 `static_assert`:

1. `ExecuteBlockThrowsInOpMode` —— Step 1(a):哑 `executeBlock` 经 `syncWait` 调用,
   捕获异常断言消息含 "OP mode"。
2. `ExecuteOpBlockSixWayComparisonSurface` —— Step 1(b) 全链路:`isthmus_transfer_basic`
   (1 deposit + 1 eip1559,brief 建议的"最简向量"),`pre` 经 `LedgerSeed` 播种到
   `MultiLayerStorage::ViewType`,`golden.rawTransactions` 直喂 `executeOpBlock`;断言
   六项比对面(`seal.{receiptsRoot,logsBloom,withdrawalsRoot}` + `result.{stateRoot,
   gasUsed,txRoot}`)== 向量 `_op_expected.header.*` + `golden.transactionsRoot`。
3. `FirstTxNotAttributesDepositIsConsensusError` —— Step 1(c):仅取
   `golden.rawTransactions[1]`(eip1559,跳过首笔 deposit)喂入,断言
   `OpConsensusError`(经 `processOpBlock` 自身的"首笔非 attributes deposit"语义检查)。
4. `ThrowingStorageIsStorageError` —— Step 1(d):`ThrowingStorage<MutableStorage>` 包裹
   全新(故意不播种,读全抛这条路径无需种子即可触发)storage,断言 `OpStorageError`
   而非裸异常穿透或误判 `OpConsensusError`。
5. `ConfigAtThresholds` —— Step 1(e):`configAt` 四点判定
   (`isthmusTime`/`jovianTime-1`/`jovianTime`/远大值)对 Isthmus/Jovian 两档。
6. 文件作用域 `static_assert(scheduler_v1::TransactionScheduler<OpSchedulerImpl<ViewType>,
   ViewType, StubExecutor, std::vector<protocol::Transaction::Ptr>>)` —— Step 3,`Storage`
   实参钉死为 `MLS::ViewType`(brief 硬性要求,非 `GlobalStateStorage`)。

## Commit

```
f067d23fe feat(bcos-evm): OpSchedulerImpl 双签名调度组件(executeOpBlock 走真桥链路,六项比对面)+ Types.h OP 载体字段 + fork 阈值注入
```

精确路径(7 个文件,`rtk git add` 逐路径,无 `add -A`/`add .`):

- `bcos-framework/bcos-framework/engine/Types.h`(modified)
- `bcos-evm/bcos-evm/opstack/OpForkSchedule.h`(modified)
- `bcos-evm/bcos-evm/opstack/OpForkSchedule.cpp`(modified)
- `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h`(new)
- `bcos-evm/bcos-evm/engine/OpReceiptMap.h`(new)
- `bcos-evm/test/opstack/OpSchedulerImplTest.cpp`(new)
- `bcos-evm/test/CMakeLists.txt`(modified)

`clang-format -style=file:.clang-format --dry-run --Werror` 对全部 6 个新增/改动的
`.h/.cpp` 文件确认 CLEAN(先 `-i` 原地格式化一轮,再 dry-run 复核)。`ports/`、
`bcos-evm/test/opstack/t8n/vectors/`、`transaction-scheduler/` 零触碰
(`git status` 核对提交前后均无宽路径误吸)。

## 已知风险 / 留给 T5b-T7 的关注点

- **最高风险区**:`OpSchedulerImpl.h::detail` 命名空间内的原始交易字节解码器
  (deposit/eip1559/setcode 三路 RLP 解码 + ecrecover 签名恢复)是本任务净新增的、此前仓内
  不存在的能力(bcos-evm 生产库此前从未需要"从原始字节反解交易"——桥测试链路一律走
  "JSON 向量结构化字段直接构造 `OpBlockTx`"路径,`T8nReplayHarness.h` 也是如此)。虽然
  字段顺序/签名摘要构造逐行对照了 `rlp_encode.cpp` 的编码侧实现、解码原语复用了
  `Web3Transaction.cpp` 的生产先例,但这是一段**没有任何仓内先例覆盖其"反向"正确性**的
  新代码,编译期 concept/模板实例化问题(尤其是 `bcos::codec::rlp::decode` 对
  `FixedBytes<32>`/`h256` 实参的具体实例化路径此前仅在 encode 侧+Address 实参上有先例,
  h256 实参是本任务新增的实例化点)与运行期字节级正确性都需要 T5b 集成时的首次真实编译
  + 本任务测试 2/3 的实际 ctest 结果来验证。
- test (2)/(3) 是这段解码器唯一的字节级验证点(经真实 golden 向量的 rawTransactions
  往返);如果 T5/T6/T7 阶段发现这两个测试翻红,优先怀疑解码字段顺序/符号扩展/
  `expectExhausted` 误报,而非 `processOpBlock`/`sealOpBlock`/`stateRootOf` 本身
  (这三者是既有、已被 33 向量 gate 验证过的代码路径,未被本任务触碰)。
- `OpForkSchedule::configAt` 对 `timestamp < isthmusTime` 的回退语义(仍判 Isthmus)未被
  brief 的两个测试分区覆盖,是本任务在文档中显式声明、但代码层面无测试钉住的一处次要
  空白——若后续 spec 修订引入更早的 fork,需要重新审视这条回退分支是否仍然合适。

---

## 审查修复(协调者审查,3 项必修)

协调者对"最高风险区"(原始交易字节解码器)做了独立复核,发现 3 项问题(2 Critical +
1 Important),全部命中且确认为真实缺陷,已逐条修复。改动全部限于
`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h`(未新增文件、未改测试文件——三处修复对
`OpSchedulerImplTest.cpp` 的测试 (b)/(c) 期望行为透明,原测试断言无需改动)。

### C1(Critical):三路解码器漏消费外层 RLP list 头

**根因**:typed envelope 的线格式是 `typeByte ‖ rlp([fields...])`——type 字节之后紧跟一个
**完整的 RLP list 编码**(自带 list 头,例如长 list 是 `0xf7+lenOfLen` 前缀 + 大端长度字节
+ payload)。原实现 `decodeDepositTx`/`decodeEip1559Tx`/`decodeSetCodeTx` 里
`bcos::bytesRef body(rawEntry.data()+1, ...)` 之后**直接**对 `body` 逐字段解码,把
list 头误当成第一个字段的起始字节——`sourceHash`/`chainId` 等字段的
`decode(bytesRef&, T&)` 会在 `decodeHeader` 判定 `header.isList==true` 时因"期望标量却
遇到 list"直接报错(RLPDecode.h 的标量/固定长度分支统一拒绝 list),必现失败。

同一文件里 `decodeAccessList`/`decodeAuthorizationList` 处理**嵌套**列表时其实已经在用
`enterList()`(先 `decodeHeader` 消费 list 头、判定 `isList`、返回 scoped payload
view),只是漏在了三个顶层解码函数的**外层**入口。

**修复**:三处均在 `bcos::bytesRef body(rawEntry.data()+1, ...)` 之后插入
`auto listBody = enterList(body);`,后续全部字段改从 `listBody` 读;结尾双重收尾——
`expectExhausted(listBody, "...envelope fields")`(list 内部字段是否读满)+
`expectExhausted(body, "...(trailing bytes after the field list)")`(整个 envelope 除
type 字节+这一个 list 外是否还有多余字节,理论上不应该有,但保留这层防御,不静默吞掉
畸形 envelope)。

### C2(Critical):setcode 签名摘要构造会硬编译错误

**根因**:`decodeSetCodeTx` 里 `evmone::rlp::encode_tuple(..., tx.authorization_list)`
中 `tx.authorization_list` 类型是 `evmone::state::AuthorizationList =
vector<Authorization>`;`encode(vector<T>)` 逐元素调用
`encode(const T&) -> decltype(rlp_encode(std::declval<T>()))`(rlp.hpp:54-58 的模板,靠
ADL 找 `rlp_encode(const Authorization&)`)。该函数**声明**在
`bcos-evm/bcos-evm/eth/utils/rlp_encode.hpp`(定义在同名 .cpp,链接边已通过
`bcosevm::opstack → bcosevm::eth` 现有依赖满足),但本文件此前只 include 了
`transaction.hpp`(定义 `Authorization` 类型本身)而没有 include `rlp_encode.hpp`(声明
`rlp_encode(const Authorization&)`)——声明不可见,ADL 找不到重载,模板替换失败,是一处
必现的编译错误,不是运行期风险。access_list 侧的 `encode(pair<address,vector<bytes32>>)`
不受影响,因为 `pair` 的 `encode` 重载是 `rlp.hpp` 自身直接定义的(非 ADL 依赖)。

**修复**:在 include 块补一行 `#include <bcos-evm/eth/utils/rlp_encode.hpp>`。

### I1(Important):yParity 未校验 ≤1

**根因**:`decodeEip1559Tx`/`decodeSetCodeTx` 解出 `yParity`(`intx::uint256`)后直接
`tx.v = static_cast<uint8_t>(yParity)` 窄化、并传给 `recoverTxSender` 做
`yParity != 0` 判定。op-geth 对 EIP-1559/EIP-7702 交易的 yParity 字段有严格校验(必须
∈{0,1},否则 invalid y parity → 该 payload 应被判 INVALID/拒收),原实现对 >1 的值既不
拒绝也不会在 `static_cast<uint8_t>` 处必然出错(截断到 [0,255],且 `!=0` 判据会把任何
非零值——包括被截断后恰好落在 0 的 256、512...——都当作 parity=1 处理),把一个应该在
解码阶段就判 `OpConsensusError` 的畸形输入悄悄放过给 ecrecover,产出一个"看起来合法但
签名验证语义已经跑偏"的 sender。

**修复**:两处解码函数在 `decodeU256Scalar` 拿到 `yParity` 后、`static_cast`/`ecrecover`
之前,插入 `if (yParity > 1) throw OpConsensusError(...)`。

### 字节级复核记录(手工静态走查,替代编译验证)

对 `isthmus_transfer_basic.golden.json` 的两条 `rawTransactions`(与 T4 原报告"六项比对面"
happy-path 测试用的同一向量)逐字节手算 RLP 解码,验证修复后的路径(`enterList` +
逐字段读取 + 双重 `expectExhausted`)在真实金值上跑通、无遗漏无越界:

**deposit(`rawTransactions[0]`,264 字节,`0x7e f9 01 04 ...`)**:
- type=`0x7e`;`body`(263 字节)首字节 `0xf9`→长 list 头(`lenOfLen=2`),长度字节
  `01 04`=260,`enterList` 消费 3 字节头,`listBody`=260 字节负载。
- 逐字段手算:`sourceHash`(33B,`a0`+32B)→`0x6ab967d...45d7`,与向量
  `_op_deposit.source_hash` 一致;`from`(21B,`94`+20B)→
  `0xdeaddead...dead0001`,与 `_op_deposit.from` 一致;`to`(21B)→
  `0x4200...0015`,与 `_op_deposit.to` 一致;`mint`(1B,`80`=空串→0,向量无 `mint`
  键,隐式 0,一致);`value`(1B,`80`→0,向量无 `value` 键,一致);`gas`(4B,
  `83 0f 42 40`→`0xf4240`)与 `_op_deposit.gas` 一致;`isSystemTransaction`(1B,
  `80`=空串→**false**,验证了 I1 之外的另一处早前修正——空串编码 false 而非单字节
  `0x00`——与 `_op_deposit.is_system_tx:false` 一致);`data`(178B,`b8 b0`长串头+
  176B 负载)与向量 `data` 字段(176 字节 L1Block calldata)一致。
- 累加消耗:33+21+21+1+1+4+1+178=260,**恰好**等于 `listBody` 声明长度 260 →
  `expectExhausted(listBody,...)` 通过;`enterList` 已让 `body` 前进
  3(头)+260(负载)=263 字节,与 `body` 初始长度 263 相等 → `expectExhausted(body,...)`
  通过。**零遗漏、零越界。**

**eip1559(`rawTransactions[1]`,119 字节,`0x02 f8 74 ...`)**:
- type=`0x02`;`body`(118 字节)首字节 `0xf8`→长 list 头(`lenOfLen=1`),长度字节
  `74`=116,`enterList` 消费 2 字节头,`listBody`=116 字节负载。
- 逐字段手算:`chainId`(3B,`82 21 05`→`0x2105`)与向量 `chainId` 一致;`nonce`
  (1B,`80`→0)与向量 `nonce:"0x0"` 一致;`maxPriorityFeePerGas`(5B,`84 05 f5 e1
  00`→`0x5f5e100`)一致;`maxFeePerGas`(5B,`84 77 35 94 00`→`0x77359400`)一致;
  `gasLimit`(3B,`82 52 08`→`0x5208`)一致;`to`(21B,`94`+20B→
  `0xb0b0...0001`)一致;`value`(9B,`88`+8B→`0xde0b6b3a7640000`=1 ETH)一致;
  `data`(1B,`80`→空,一致);`accessList`(1B,`c0`=空 list,`enterList` 返回空
  `listBody`,while 循环零迭代,一致);`yParity`(1B,单字节 `01`<0x80,RLP
  单字节自编码规则,值=1,**通过 I1 新增的 `>1` 检查**,不误杀合法值);`r`(33B,
  `a0`+32B)、`s`(33B,`a0`+32B)与原始签名字节吻合。
- 累加消耗:3+1+5+5+3+21+9+1+1+1+33+33=116,**恰好**等于 `listBody` 声明长度 116 →
  两处 `expectExhausted` 均通过。**零遗漏、零越界。**

两条向量的手算结果与向量 JSON 的结构化字段(`env`/`block.transactions[]`/
`_op_deposit`)逐项吻合,证明 C1 修复后的字段边界切分正确;`isSystemTransaction`=false
走空串分支的观察进一步印证了原报告"自审发现并修正的一处实质问题"节(decodeBoolField 改
`decodeU64Scalar!=0`)在这条真实向量上确实会被触发,而非纸面假设。

### 未动范围确认

Minor M1-M5 本轮不动(按协调者指示:M1/M2 留 T6,M4 留 T5b 知悉,M3/M5 记账,均不在本次
diff 内)。`OpSchedulerImplTest.cpp`/`OpReceiptMap.h`/`Types.h`/`OpForkSchedule.{h,cpp}`/
`test/CMakeLists.txt` 本轮零改动——修复完全限定在 brief 指出的
`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h` 一个文件内。

### Fix Commit

```
5c69034cc fix(bcos-evm): OpSchedulerImpl 原始交易解码器修复(漏消费外层 list 头/setcode ADL 缺失 include/yParity 未校验)
```

精确路径 `git add` 仅 `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h` 一个文件
(60 insertions/36 deletions)。`clang-format -style=file:.clang-format --dry-run
--Werror` 复核 CLEAN(先 `-i` 原地格式化,再 dry-run 确认)。

---

## 首次真实编译 + ctest 暴露的第二轮问题(诊断 → 修复 → 重跑验证)

统一编译验证首次跑通(`cmake --build`),`bcos-evm-opstack-tests` 全量 ctest 结果:
50 例中 48 通过,2 例失败,全部集中在 `OpSchedulerImplTest.cpp`:

```
OpSchedulerImplTest.cpp:402  FirstTxNotAttributesDepositIsConsensusError
  Expected: throws OpConsensusError.  Actual: it throws a different type.
OpSchedulerImplTest.cpp:428  ThrowingStorageIsStorageError
  Expected: throws OpStorageError.  Actual: it throws a different type.
```

值得注意:**test (b)(`ExecuteOpBlockSixWayComparisonSurface`,全链路六项比对面)首次编译
即绿**——这是对 C1(list 头修复)/C2(rlp_encode.hpp include)/I1(yParity 校验)三处上一轮
修复以及整个原始交易解码器(deposit+eip1559 分支、ecrecover、txRoot 建树)最有力的运行期
证据:手工字节级复核的结论被真实编译+执行验证坐实,而非停留在纸面。

### 诊断过程(先诊断再改,按协调者要求)

在 `OpSchedulerImplTest.cpp` 的 (c)/(d) 两处临时把 `EXPECT_THROW` 换成手工
`try { ... } catch(OpConsensusError&) / catch(OpStorageError&) / catch(const
std::exception&) / catch(...)` 多级捕获,`catch(...)` 分支内用
`abi::__cxa_current_exception_type()`(T8nReplayHarness.h 已有的同一诊断手法)打印真实
动态类型名,重新编译并单独跑这两例:

```
[ RUN      ] OpSchedulerImpl.FirstTxNotAttributesDepositIsConsensusError
[DIAG c] caught non-std exception, dynamic type: St13runtime_error
[ RUN      ] OpSchedulerImpl.ThrowingStorageIsStorageError
[DIAG d] caught non-std exception, dynamic type: St13runtime_error
```

**实测证据**:两例的真实异常都是 `std::runtime_error`(mangled `St13runtime_error`),但
**都逃过了 `catch (const std::exception& e)`**——落进最后的 `catch (...)` 兜底。这不是
"抛错类型不对",而是**类型完全对,但这条编译单元里的 typed catch 判定失败**。

### 归属判定:实现分类缺陷,非测试构造错误

排除测试构造错误的依据:
- test (c) 复用的 env/pre/rawTxBytes 与 test (b)(已验证全绿)完全同源,只是故意跳过首笔
  deposit——`processOpBlock` 按其自身文档确实会为此抛 `std::runtime_error("op block: first
  tx is not the L1 attributes deposit")`(OpBlockExecute.cpp),这正是测试想触发的路径,
  向量/env 构造无误。
- test (d) 的 `ThrowingStorage` 精确复刻 `Storage2LedgerTest.cpp::PoisonOnInjectedStorageException`
  先例的用法(读全抛),异常经 `Storage2Ledger::applyDiff`(非 noexcept 的写路径,内部
  `account.exists()` 是读操作)不受阻拦地传播,也是设计预期路径。

真实原因是:**本仓已有文档记录的 typed-catch RTTI 旁路问题**
(T8nReplayHarness.h:625-636 引用的 `docs/audits/2026-07-12-typed-catch-rtti-investigation.md`——
`libevmone.a` 以 `-fno-rtti` 编译,给 `std::exception` 带入一份隐藏的非唯一 typeinfo 副本,
导致本二进制内 `catch(const std::exception&)` 对相当一部分(实测覆盖:evmone/opstack 内部抛出
的 + 本任务自己测试基础设施 `ThrowingStorage` 抛出的,两个不同源头)`std::runtime_error`
判型失败、漏接。T8nReplayHarness.h 早在（本任务之前）就已经因为同一现象加了
`catch(...)` 兜底(见其源码 §"typed-catch RTTI 兜底"注释)——这是**已知的、项目级**问题,
不是本任务新引入的孤立 bug,但 `OpSchedulerImpl.h`(本任务新增的生产代码)当时**没有**照抄
这条既有防御纪律,只写了单一 `catch (const std::exception&)`。

**这是 Important 级实现缺陷,不是测试断言错误**:spec §4.3 的错误分类(③ OpConsensusError
→ INVALID / ⑤ OpStorageError → -32603)是 T5b(newPayload OP 分支)、T6(变异矩阵 18 例、
`-32603` vs `INVALID` 分档探针)据以判定响应类型的唯一依据。若 `catch(const std::exception&)`
在生产路径里同样漏接(而不仅是本任务两个单测碰巧触发),`processOpBlock` 的全部 block-level
语义错误(空块、首笔非 deposit、gas 池溢出、is_system_tx、setcode 校验……)都会以**未分类的
裸异常**逃出 `executeOpBlock`,T5b 的调用方拿不到 `OpConsensusError`/`OpStorageError` 去
匹配 INVALID/-32603,分档基础被击穿。修复必须落在生产代码 `OpSchedulerImpl.h`,不能只在
测试侧掩盖。

### 修复

`executeOpBlock` 内 `processOpBlock` 调用的 `try/catch` 块,在既有
`catch (const std::exception& e)` 之后追加 `catch (...)`,复用**同一套** poisoned()-优先
分类逻辑(先查 `bridge.poisoned()` → `OpStorageError`,否则 → `OpConsensusError`)——不依赖
typeid 匹配,直接兜底任何逃过 typed catch 的异常。因为 `catch(...)` 拿不到类型化句柄,
无法调用 `.what()`,原始消息在这条路径上不可恢复,改用固定诊断文案说明"typed catch 被
已知 RTTI 问题旁路"(代码内联注释同口径,便于日后排查命中这条分支)。

未改动:`throwOnDecodeError`/`enterList` 等解码期错误路径不受影响(它们本就不是
`catch(const std::exception&)` 语义,是 `bcos::Error::UniquePtr` 返回值判空后主动
`throw OpConsensusError`,不涉及 typed-catch 问题);`Storage2Ledger.h` 自身的
`noexcept` 读方法早已有 `catch(...)` 兜底(既有代码,未受影响,也未受本次改动触碰)。

### 重跑验证证据

```
$ ./build/bcos-evm/test/bcos-evm-opstack-tests --gtest_filter='OpSchedulerImpl.*'
[==========] 5 tests from 1 test suite ran. (3 ms total)
[  PASSED  ] 5 tests.

$ ./build/bcos-evm/test/bcos-evm-opstack-tests \
    --gtest_filter='Engine*:OpSchedulerImpl*:EthBlockHeader*:OpDepositEncode*'
[==========] 50 tests from 7 test suites ran. (64 ms total)
[  PASSED  ] 50 tests.

$ ./build/bcos-evm/test/bcos-evm-opstack-tests   # 全量 opstack 套件,零过滤
[==========] 206 tests from 33 test suites ran. (171 ms total)
[  PASSED  ] 206 tests.
```

三层验证(本任务 5 例 → 协调者点名的 50 例交集 → 全量 opstack 206 例)均 100% 通过,零回归、
零放宽断言(EXPECT_THROW 的目标异常类型 `OpConsensusError`/`OpStorageError` 逐字未改)。

### 第二轮 Fix Commit

```
fe2a40c29 fix(bcos-evm): OpSchedulerImpl processOpBlock 错误分类补 catch(...) 兜底(typed-catch RTTI 旁路,实测确认)
```

精确路径 `git add` 仅 `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h` 一个文件
(24 insertions)。诊断过程中在 `OpSchedulerImplTest.cpp` 加过的临时诊断代码(`try/catch` +
`abi::__cxa_current_exception_type`)在确认根因后已**全部还原**为原有的 `EXPECT_THROW`
断言(逐字不变,`git diff` 对该文件为空),未提交任何诊断脚手架。
