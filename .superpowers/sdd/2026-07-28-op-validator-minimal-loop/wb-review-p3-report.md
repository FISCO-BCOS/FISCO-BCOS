# 视角 3 复审报告 · 存储、账本桥、生命周期与并发

分支 `feat-op-validator-loop`,范围 `42e62fcef..HEAD`,工作目录
`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`。

**本次复审全程只读**:未修改任何文件,未执行 `cmake --build`,未跑测试。凡需构建/运行证实的,
标 `[需验证]` 并给出最小验证步骤。

---

## 0. 结论摘要

| 级别 | 数 | 一句话 |
|---|---|---|
| Critical | 0 | 没有找到在**本期 E-b 世界内**会导致错误 VALID/INVALID 判定或 UB 的缺陷。异常安全与协程生命周期在本分支路径上是干净的(见 §5/§6)。 |
| Important | 6 | 三条是"生产接入即变 Critical"的地雷(I-1 零值槽 → EIP-7610 误判;I-2 非地址 `/apps/` 表名 → 全量 -32603;I-3 层栈无界 + 每块两次全量遍历 → Θ(A·S·N²));两条是新发现的浪费/记账缺口(I-4 MessagePasser 全遍历;I-5 `SYS_NUMBER_2_BLOCK_HEADER`/`SYS_NUMBER_2_TXS` 也不写);一条是文档误导(I-6,§6.4 条目 r/s 的可达性与 UB 结论被高估)。 |
| Minor | 4 | 协程引用形参、未加锁的 `m_nextPayloadSequence`、`bad_lexical_cast` 逃逸、TOCTOU 的 FCU 号码窗口。 |

**"跑一万个块之后还对不对、还跑得动"的直接回答**:
- **还对**——在"本桥是底层存储唯一写者"这个前提成立的前提下,正确性没有随块高退化的机制(桥一块一实例,缓存不跨块,层间遮蔽语义正确)。
- **跑不动**——见 I-3,这是**确定性**的 Θ(N²) 时间 + Θ(N) 不可回收内存,一万个块之前就已不可用。§6.4 条目 (a) 把它定性为"伸缩性问题而非正确性问题",我不同意这个优先级(理由见 I-3)。

---

## 1. KEEP 契约逐点核对(问题 1)

`Storage2Ledger.h` 三个读方法的三态语义,逐个对照 evmone 消费侧:

| 场景 | 桥的返回 | evmone 侧语义 | 判定 |
|---|---|---|---|
| 账户不存在(SYS_TABLES 无标记行) | `get_account` → `nullopt`(`Storage2Ledger.h:593-595`) | `State::find` 返回 `nullptr`(`eth/state/state.cpp:249-260`) | ✅ |
| 账户存在但全空 | `Account{nonce=0,balance=0,code_hash=keccak(∅),has_storage=probe}`(`:597-641`) | `find` 插入一条 `is_empty()` 的账户,EIP-161/7610 可见它"存在" | ✅ 未折叠成 nullopt |
| code 为空 | `get_account_code` → 空 `evmc::bytes`(`:696-702`) | `get_code_size`=0,`code_hash` 仍取自 Account | ✅ |
| slot 存在但值为 0 | `get_storage` → 全零 `bytes32`(`:727`) | 与"槽不存在"同义 | ✅(桥的写侧从不写零值槽,`:306-321`) |

`has_storage` 的消费链我亲自跟到了底:
`Storage2Ledger::probeHasStorage` → `Account::has_storage` → `eth/state/state.cpp:259`
`.has_initial_storage = cacc->has_storage` → `eth/state/host.cpp:81-97` `is_create_collision()`。
即**它直接决定 EIP-7610 的 CREATE 碰撞判定**——错了就是共识分歧。视角提示是准确的。

### I-1(Important;生产接入即 Critical)`probeHasStorage` 把零值槽行算作"有存储"

`bcos-evm/bcos-evm/ledger/Storage2Ledger.h:670-688`,核心一行:

```cpp
if (fieldKey.size() == kStorageSlotKeySize && liveContent(rawValue).has_value())
    co_return true;                                    // :682
```

判据只有两条:键长 32 字节 + 非墓碑。**不看值是不是零**。

而同一文件 `:485-494` 的 `fetchAllStorage` 对零值槽是 `throw`,且注释自己写明了这条规则的边界:

> 此规则仅在桥自写的 E-b 世界成立……**该规则不得被继承到编排接入层(生产 `HostContext::set`
> 对零值照写不删,真实链账户表必然含零值槽行)**。

两处从同一份事实出发,却得出了相反的鲁棒性:`fetchAllStorage` 对零值槽**响亮失败**(至少不会静默错),
`probeHasStorage` 对零值槽**静默答 true**,而且这条不对称在文件里没有任何记载。

**失效场景(生产接入后)**:账户 X 由通用 FISCO 执行器写过一个槽后又被置零,账户表留下一行
32 字节键、值全零。此时 X 的 nonce=0、code 为空。合约对 X 做 `CREATE2`:
- op-geth:X 的 storage trie 为空(零值槽不入 trie)⇒ `is_create_collision` 为 false ⇒ **创建成功**;
- 本桥:`probeHasStorage` 命中那一行 ⇒ `has_storage=true` ⇒ `host.cpp:88` 命中 ⇒ **INVALID / 创建失败**。

⇒ 共识分歧。今日不可达(E-b 世界桥是唯一写者,零值槽不存在),但这正是"编排接入层"要走的那条路。

**建议修法**:`probeHasStorage` 在 `liveContent` 之后追加 `!evmc::is_zero(slotValue)` 判据,
使其与 `fetchAllStorage` 的"活槽"定义完全同源;并把"零值槽 = 无存储"这条写进文件头的
`has_storage 判据` 段。

`[需验证]` 最小验证步骤:在 `bcos-evm/test/opstack/Storage2LedgerTest.cpp` 加一例——直接用
`storage2::writeOne` 往账户表写一行 32 字节键 + 32 字节全零值(绕过 `applyDiff`,模拟通用执行器的写法),
然后 `bridge.get_account(addr)->has_storage`;跑 `Storage2LedgerTest`。当前期待 `true`(即暴露缺陷),
修好后期待 `false`。

### I-2(Important;生产接入即 Critical)`/apps/` 下任何非"20 字节 hex"表名 ⇒ 整块 -32603

`Storage2Ledger.h:423-437` `addressFromTableName`:

```cpp
boost::algorithm::unhex(hexView.begin(), hexView.end(), std::back_inserter(decoded));
if (decoded.size() != sizeof(addr.bytes))
    throw std::length_error(... "/apps/ table name is not a 20-byte hex-encoded address" ...);
```

`visitAccountsImpl`(`:507-548`)对 `SYS_TABLES` 里 `/apps/` 前缀的**每一行**无条件调用它。
非 hex 字符 ⇒ `boost::algorithm::non_hex_input`(派生自 `std::exception`);长度不对 ⇒ `length_error`。
两者都被 `visitAccounts`(`:250-265`)接住 → `poison()` → `executeOpBlock` 的
`OpSchedulerImpl.h:888/893` 检查 → `OpStorageError` → engine `-32603`。

`visitAccountsImpl:501-506` 的注释把"`/apps/` 下只有 hex 地址表"称作"an E-b precondition,
not a runtime check"。这是对的描述,但没有说明后果的量级:**一条不满足前提的表名,会让此后每一个
OP 块都拿到 -32603**(遍历在 `stateRootOf` 路径上,每块必走)。FISCO 的 BFS 会在 `/apps/` 下建
以合约名为键的目录/链接项,所以一条既有链上大概率不满足这个前提。

这不是"跑不动",是**一条已有链上 OP 验证者根本起不来**,且失败面是全局的、不可恢复的(不删表就永远 -32603)。

**建议**:`visitAccountsImpl` 对无法解析为 20 字节地址的 `/apps/` 项**跳过**(它必然不是 EVM 账户表),
而不是毒旗;把"跳过"记进 §6.4 并配一条守护用例。若坚持毒旗,则必须在 §6.4 里把它记成
"接入既有链的硬阻塞",而不是一句 precondition。

`[需验证]` 最小验证步骤:`Storage2LedgerTest.cpp` 里往 `SYS_TABLES` 插一行键为 `/apps/HelloWorld`
的标记行,再调 `bcos::evm::stateRootOf(bridge)`;跑 `Storage2LedgerTest`。期待 `bridge.poisoned()==true`
(暴露缺陷)。

---

## 2. 缓存正确性(问题 2)

**填充点**:`get_account:123` / `get_account_code:154` / `get_storage:187`(均为 `emplace`,含
nullopt / 空 code / 零值的负缓存);`applyModifiedEntry:327-333`(写穿,账户与 code 走**权威重读**
`fetchAccount`/`fetchCode`,槽按写入值直填)。
**失效点**:`applyDeletedEntry:381-384`(账户/ code 置负,槽 `erase_if` 按地址全清)。

逐点核对结论:

1. **"未命中的结果绝不能进缓存"**——这句在本桥里的准确形态是"**缺 `has_storage` 的 Account 绝不能进缓存**"。
   `fetchAccountForVisit`(`:655-663`)在缓存未命中时用 `computeHasStorage=false` 读,并**刻意不回写**。
   我核对了这条不变式的守护:`bcos-evm/test/opstack/Storage2LedgerTest.cpp:706`
   `TEST(Storage2Ledger, VisitAccountsMissDoesNotPoisonAccountCache)` 确实存在(§6.4 条目 o 已记账)。
   **该裁定仍然成立**,窗口窄的判断也准确:`executeOpBlock` 在 `visitAccounts`(:880/:892)之后不再
   `get_account`。
2. **负缓存与 `visitAccounts` 的交互**:`visitAccountsImpl:532-534` 缓存命中拿到 `nullopt` 就 `continue`,
   即**该账户不进 stateRoot**。这在"桥是唯一写者"下不可能与"SYS_TABLES 标记行存活"共存
   (`applyDeletedEntry` 两者同时改,`applyModifiedEntry` 两者同时改)。✅ 一致。
   但这条一致性**完全依赖唯一写者不变式**——越过桥写一行标记行(生产接入的常见形态)就会让该账户
   从 stateRoot 里静默消失。文件头 `:40-42` 记了唯一写者不变式,但没有点名这个具体后果(静默漏账户,
   而不仅仅是"读缓存失真")。建议在 §6.4 记一笔。
3. **跨块复用**:不复用。桥一块一实例(`OpSchedulerImpl.h:818` `Storage2Ledger<Storage> bridge(storage);`
   是 `executeOpBlock` 的局部变量),缓存随实例销毁。✅
4. **fork 出来的新层是否继承上一层缓存**:不适用——缓存在桥里,不在 `View` 里;`MultiLayerStorage::fork()`
   (`MultiLayerStorage.h:526-541`)只拷贝层指针 deque,不带任何桥状态。✅
5. **写穿顺序**:`applyModifiedEntry` 先写槽(`:308-321`)**再**刷新账户缓存(`:327`),所以重算的
   `has_storage` 已经包含本轮槽变更。✅ 顺序是对的。

**未发现缓存污染缺陷。**

---

## 3. MultiLayerStorage 层生命周期(问题 3)—— I-3

### 事实

- `runOpNewPayloadSteps` 每接受一个块执行 `m_globalStateStorage.get().pushView(std::move(view))`
  (`EngineServiceImpl.h:1117`),**从不调用 `mergeBackStorage()`**。代码注释 `:1108-1116` 自认此事,
  §6.4 条目 (a) 已记账。
- `pushView`(`MultiLayerStorage.h:543-551`)`push_front` 进 `m_storages`;`popFrontStorage`/`mergeBackStorage`
  是唯一两个出栈点,OP 路径都不调。⇒ **B 个已接受块 ⇒ `m_storages.size() == B`,单调增长,无回收。**

### 量化后果(我按源码逐条推的,不是转述注释)

设已接受块数 B、账户数 A、平均活槽数 S。

| 操作 | 复杂度 | 依据 |
|---|---|---|
| `fork()` | O(B),B 次 shared_ptr 原子自增 | `MultiLayerStorage.h:532/538` `view.m_immutableStorages = m_storages;`(整 deque 拷贝) |
| `View::readOneRaw(key)` | 最坏 B+2 层探测 | `MultiLayerStorage.h:238-245` 线性 for |
| `View::range(...)` 构造 | B+2 个子迭代器 + 一次全量 forward | `MultiLayerStorage.h:389-407` `Iterator::init` |
| `Iterator::next()` 每一步 | O(B) 线性求最小 | `MultiLayerStorage.h:414-448` |

单块 stateRoot 段的代价:`executeOpBlock` 每块跑**两次** `visitAccounts`
(`OpSchedulerImpl.h:880` MessagePasser + `:892` `stateRootOf` → `StateRootCompute.h:86`),
每次遍历里每个账户还要开**一次独立的 range**(`Storage2Ledger.h:536` `fetchAllStorage`):

```
单块代价 ≈ 2 · A · (S + c) · Θ(B)
N 个块累计 ≈ Θ(A · S · N²)
```

代入一组保守数字(A=10⁴,S=8,N=10⁴):**第 10 000 块单块就要约 3×10⁹ 次层探测**。
"跑一万个块"在这个实现下不是慢,是不会返回。

### 独立判断:§6.4 (a) 的定性被低估

台账把它写成"**伸缩性问题而非正确性问题**"。我认为这个定性掩盖了两件事:

1. **内存永不回收**。每个已接受块的 mutable 层(整个写集)被 `m_storages` 持有到进程结束。这不是
   "读放大",是**单调泄漏**。一个长跑的验证者会 OOM——那是可用性缺陷,不是伸缩性偏好。
2. **它落在本期唯一的核心用例上**。本期交付物就是"逐块收 payload 并比对",而这条路径的每块成本
   与已接受块数成正比。"最小闭环块数量级小"是对测试夹具说的,不是对交付语义说的。

**建议**:把 (a) 从"伸缩性"改记为"**生产接入的功能性阻塞**",与 (k)/(l)/(q) 同列。修法方向不必现在定
(merge 时机与重组窗口耦合,台账已说清),但定性要改。

---

## 4. `stateRootOf` 的开销(问题 4)—— I-4(新发现)

`StateRootCompute.h:82-93`:每次调用**重建全量 trie**,注释自认("rebuilds the whole trie on every call")。
这一条已入档,我不重复。

我要报的是**没入档的那一半**:

### I-4(Important)`executeOpBlock` 为了取一个已知地址的存储,做了一次全量 `visitAccounts`

`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:879-887`:

```cpp
std::map<evmc::bytes32, evmc::bytes32> messagePasserStorage;
bridge.visitAccounts([&](const auto& accountView) {
    if (accountView.addr == bcos::evm::opstack::OP_L2_TO_L1_MESSAGE_PASSER)
    {
        messagePasserStorage = accountView.storage;
        return false;   // 找到即停
    }
    return true;
});
```

`visitAccountsImpl`(`Storage2Ledger.h:507-548`)对**每一个**被访问到的账户,在调用 visitor **之前**就
无条件跑完 `fetchAccountForVisit` + `fetchAllStorage`(`:532/:536`)——也就是把该账户的**整张活槽表
物化成一个 `std::map`**。visitor 返回 `false` 只能停在"已经付完这一个账户的全部代价"之后。

`OP_L2_TO_L1_MESSAGE_PASSER = 0x4200…0016`,表名 `/apps/42000000…16`,在 `/apps/` 的字典序里大约
落在 26% 处。⇒ **每块要白白物化并丢弃约 0.26·A 个账户的完整槽表**,只为读其中一个账户的槽。

这是纯浪费,且与 I-3 相乘(每次 `fetchAllStorage` 都是一次 O(B) 的 range 构造)。它**不在 §6.4 任何
条目里**——(a) 说的是层数,(o) 说的是缓存不变式,都不是这条。

**建议修法**:给 `Storage2Ledger` 加一个 `task::Task<std::map<...>> storageOf(const evmc::address&)`
式的按址取槽接口(`fetchAllStorage` 已经是私有实现,包一层即可),`executeOpBlock` step 5 直接调它;
或者让 `visitAccountsImpl` 接受一个可选的地址过滤器,在 `fetchAllStorage` **之前**短路。

`[需验证]` 最小验证步骤:该改动是纯性能改动,正确性由既有 33 条金值向量 gate 覆盖
(`withdrawalsRoot` 是六项比对面之一,MessagePasser 槽错了必翻红)。改完跑 standalone
`bcos-evm/build` 的 131 例即可。

---

## 5. 异常安全与资源(问题 5)—— 结论:干净

逐条走了所有 `throw` 路径:

| 路径 | 已 push 的层 | 已开的迭代器 | 已部分写入的表 | 判定 |
|---|---|---|---|---|
| `executeOpBlock` 抛(任意一步) | 无——`pushView` 在 `EngineServiceImpl.h:1117`,在 `co_await registerOpBlock` **之后**;异常沿 `runOpNewPayloadSteps` 传出时 `view` 是局部量,被析构 ⇒ **可变层整体丢弃** | `Iterator` 是 `View::range` 的返回值,协程帧局部量,栈展开自动析构 | 部分写入只存在于被丢弃的可变层里 | ✅ 原子:失败块不留任何持久痕迹 |
| 步骤 5 六项比对失配 → `co_return makeStatus(Invalid…)`(`:1096`) | 同上,`view` 析构,层丢弃 | — | — | ✅ |
| `registerOpBlock` 中途抛(收据数不符 `:1213`、null 收据 `:1225`、`writeOne` 抛) | `pushView` 未执行 ⇒ 层丢弃 | — | 部分写入在被丢弃的层里 | ✅ |
| `applyDeletedEntry` 收键后逐个 `removeOne` 中途抛(`:370-372`) | 同上 | range 已在 `:369` 的作用域结束时析构(实现者刻意"先收键再删",`:354-355` 注释) | 半删账户只存在于被丢弃的层里 | ✅ |
| `applyModifiedEntry` 在槽循环里抛(`:308-321`) | 同上 | — | 三张读缓存此时相对存储是**陈旧的**(刷新在 `:327-333`,在循环之后) | ✅ 无后果:异常一路传到 `executeOpBlock` 的 catch,桥与层一起丢弃,没有"catch 后继续用同一个桥"的调用点 |

**没有需要 RAII 却用裸配对调用的地方。** `pushView` + `mergeBackStorage` 那对(`EngineServiceImpl.h:646-654`,
带 TODO)只在**通用**路径上,OP 路径不走。

一个值得点名的正面判断:`view.newMutable()`(`:967`)在执行**之前**、`pushView`(`:1117`)在
**全部成功之后**,中间没有任何提前发布点。这是本分支存储侧最重要的一条正确性性质,它成立。

`applyDiff` 的抛出可达性我也核了:`processOpBlock`(`OpBlockExecute.cpp:27`)本身不是 `noexcept`,
`applyDiff` 在 `:31/:57/:84/:94` 被**直接**调用(不经 evmone 的 noexcept 回调帧),所以存储写回失败
是正常传播而不是 `terminate`。这与 MEMORY 里 "evmone noexcept 论断被证伪 / 回调间帧仍 noexcept" 的
警告不冲突——本桥的**写**侧不在那些帧里,**读**侧才在,而读侧三个方法都是 `noexcept` + 毒旗。✅ 设计一致。

---

## 6. 协程生命周期:两条 pre-existing 缺陷的独立核实(问题 6,重点)

### 6.1 `bcos-ledger/bcos-ledger/LedgerMethods.h:233-235` —— **属实**,但**本分支不使其可达**

我读到的代码(行号与描述一致):

```cpp
228:                auto transactions = co_await storage2::readSome(
229:                    storage, hashes | ::ranges::views::transform([](auto& hash) {
230:                        return executor_v1::StateKeyView{
231:                            SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(hash)};
232:                    }));
233:                for (auto& txEntry : transactions)
234:                {
235:                    auto field = txEntry->get();      // ← 未判 has_value()
```

`readSome` 的返回类型是 `std::vector<std::optional<Entry>>`(`MultiLayerStorage.h:287-298`,
`readSome` 对缺行 `return {}` 即 `nullopt`)。所以缺行 ⇒ 空 optional 解引用 ⇒ **UB。属实。**
(同一函数 `:250` 的收据循环是同样的形状。)

**但"本分支让它可达了吗?"—— 没有,而且是反向的。** 判据在同一函数上方:

```cpp
217:        if (auto txsEntry = co_await storage2::readOne(
218:                storage, executor_v1::StateKeyView{SYS_NUMBER_2_TXS, blockNumberStr}))
219:        {                                             // ← 整个 tx/receipt 段的守门
```

进入 `:233` 的**前提**是 `SYS_NUMBER_2_TXS` 有这一行。而 `registerOpBlock`
(`EngineServiceImpl.h:1141-1252`)只写五张表:`SYS_NUMBER_2_HASH` / `SYS_HASH_2_NUMBER` /
`s_eth_block_header` / `SYS_HASH_2_RECEIPT` / `s_eth_hash_2_rawtx`。**`SYS_NUMBER_2_TXS` 也不写。**
于是对 OP 块,`:218` 为 nullopt,整段被跳过,`:235` **结构上不可达**。

更前面还有一道:`:199-212` 的 HEADER 分支读 `SYS_NUMBER_2_BLOCK_HEADER`——OP 块同样不写——
缺行时 `:211` `BOOST_THROW_EXCEPTION(NotFoundBlockHeader{})`,**响亮失败**,连 tx 段都到不了。

⇒ **核实结论:缺陷属实(是既有 UB),但它对 OP 块是"存在且更不可达",不是"我们刚刚让它可达了"。**

### 6.2 `bcos-storage/.../RocksDBStorage.cpp:228-233` —— **部分属实**

代码形态我逐行确认了,与描述一致:

```cpp
227:        STORAGE_ROCKSDB_LOG(TRACE) << LOG_DESC("asyncGetRows") ...
228:        _callback(nullptr, std::move(entries));      // 成功回调在 try 内
229:    }
230:    catch (const std::exception& e)
231:    {
232:        _callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(UnknownEntryType, "Get rows failed! ", e), {});
233:    }
```

**"成功回调写在 try 块内、catch 会再调一次同一个 `_callback`"——属实,确凿。**
(`asyncGetRow` 在 `:151` 是同一形状,`asyncSetRow` 亦然。)

**"同一个协程 handle `resume()` 两次 = UB"——这一步我判定为高估**,理由是我把整条链跟到了底:

1. 回调把协程唤醒的地方是 `bcos-framework/bcos-framework/storage/LegacyStorageMethods.h:38-51`:
   lambda 里 `m_result.emplace(...)` 后 `handle.resume();`。
2. 要触发第二次 `_callback`,必须有异常从 `:228` 抛出。`:228` 之后 try 里没有别的语句,所以异常只能
   **从 `handle.resume()` 内部逃逸**。
3. 而 `handle.resume()` 内部的异常能否逃逸,取决于 `libtask/bcos-task/Task.h:62-70`:
   ```cpp
   void unhandled_exception() {
       auto exception = std::current_exception();
       if (m_continuation == nullptr) { std::rethrow_exception(exception); }
       m_continuation->value.template emplace<std::exception_ptr>(exception);
   }
   ```
   只有在 `m_continuation == nullptr`(即该 Task 是被 `.start()` 直接启动、无人 `co_await` 它)时才重抛。
   而 `task::syncWait`(`libtask/bcos-task/Wait.h:56-91`)的根任务把整个 `co_await` 包在
   `try { ... } catch (...) { result.emplace<exception_ptr>(...); }` 里,内层任务全部有 continuation。
   ⇒ **沿 `syncWait` 驱动的存储读路径,异常不会逃逸 `resume()`,第二次 `_callback` 不会发生。**
4. §6.4 条目 (s) 给出的具体触发链我也核了,并且**该链推不出 double-resume**:
   `Ledger.cpp:1416-1465` 的 lambda 若在 `:1441` `createTransaction` 抛出,此时它**还没有**调用外层
   `callback`(外层 callback 在 `:1464`),而 `handle.resume()` 恰恰在外层 callback 里
   (`LedgerMethods.cpp:556-569`)。⇒ 第一次没 resume,第二次走 `:1418` error 分支调一次 ⇒
   **总共 resume 一次**。结果是"一次成功读被错报成 error",是错误分类,**不是 UB**。
   要构成 double-resume,异常必须发生在 `:1464` 的 `callback(...)` **之内**,即已经 resume 之后,
   并且要能穿透第 3 点的 promise 捕获——这两个条件叠加,我在本仓找不到成立的路径。

⇒ **核实结论:代码缺陷形态属实(双重回调是真实的、应该修的);"同一 handle resume() 两次 = UB"
在本仓当前 `bcos::task` promise 设计下不成立于任何 `syncWait` 驱动的路径。**

### 6.3 本分支是否踩到同一个坑?—— 没有,且今天连边都碰不到

- **本分支的桥不走 `LegacyStorageMethods`/`StorageInterface`**:`Storage2Ledger` 的所有读写都打在
  `View`(`MultiLayerStorage.h:227-351`)上,`View` 有自己的 `readOne`/`writeOne`/`range`,是纯协程、
  纯同步完成,不经回调。
- **OP 组合根在生产里不存在**:`libinitializer/EngineServiceInitializer.h:27-28` 是唯一的生产组合根,
  它不传 `maxEngineVersion`(默认 3),也没有任何地方用 `OpSchedulerImpl` 实例化它。三个 OP 测试
  (`bcos-evm/test/opstack/Engine*Test.cpp`)用的是内存 `MultiLayerStorage`
  (`EngineOpBranchTest.cpp:132-138`),**根本没有 RocksDB**。这也与 §6.4 条目 (q) 一致。
- 所以 6.1 / 6.2 两条对本分支都是"**存在但不可达**",而不是"我们刚刚让它可达了"。

### I-6(Important:会误导读者的文档)§6.4 条目 (r)/(s) 的表述需要修正

- **(r)** 写的是"**会先由 OP 块暴露**——因为条目 (f) 裁定 OP 块不写该表"。
  按 §6.1 的核实,这个因果是**反的**:`LedgerMethods.h:217-218` 的守门读的是 `SYS_NUMBER_2_TXS`,
  OP 块**连它也不写**,所以整段被跳过;更早的 HEADER 分支还会先抛 `NotFoundBlockHeader`。
  OP 块让这条缺陷**更不可能**被触发。按现在的写法,读者会以为"接上 OP 就会踩 UB",从而误配优先级。
- **(s)** 的结论句"**即 UB/崩溃而非干净的 RPC 错误**"按 §6.2 的核实过强;它描述的那条链
  (`:1441` 抛出)只 resume 一次。建议改成:"双重回调形态属实;沿该链的实际后果是把成功读错报为
  error,double-resume 需要异常从 `handle.resume()` 内逃逸,而 `Task.h:62-70` 在有 continuation 时
  已捕获,故当前不可达。"
- 同一处 `EngineServiceImpl.h:1194-1200` 的注释复述了 (r) 的同一说法("leaving the row absent is not
  perfectly clean either — `LedgerMethods.h:233-235` … so a missing row is UB on that path"),
  同样需要按上面修正,否则两处互相印证一个不准确的判断。

---

## 7. 新表:表名、键、值编码(问题 7)—— I-5(部分新发现)

### 表名常量位置与冲突

`bcos-evm/bcos-evm/engine/OpEngineSeam.h:51/74`:

```cpp
inline constexpr std::string_view SYS_ETH_BLOCK_HEADER{"s_eth_block_header"};
inline constexpr std::string_view SYS_ETH_HASH_2_RAWTX{"s_eth_hash_2_rawtx"};
```

- 位置符合硬约束 3(未动 `LedgerTypeDef.h`),经 `OpSchedulerImpl.h:715/717`
  (`c_ethBlockHeaderTable` / `c_ethRawTxTable`)以依赖名再发布给 engine。✅
- 与既有表名冲突:既有系统表统一 `s_` 前缀(`s_hash_2_number` 等),这两个名字落在同一命名空间里但
  不与任何既有名重合。✅ 无冲突。

### 键构造

| 表 | 键 | 与谁对齐 | 判定 |
|---|---|---|---|
| `SYS_NUMBER_2_HASH` | `lexical_cast<string>(payload.blockNumber)`(`EngineServiceImpl.h:1144/1149`) | `BaselineScheduler.h:207-220` | ✅ |
| `SYS_HASH_2_NUMBER` | 哈希裸 32 字节(`:1156`) | 读侧 `LedgerMethods.h:435-437` 同构造 | ✅ 我逐字核对了读写两侧 |
| `s_eth_block_header` | 同 `SYS_NUMBER_2_HASH` 的十进制串(`:1166`) | 读侧 `:868-870` 用 `lexical_cast<string>(*parentBlockNumber)`,而 `parentBlockNumber` 来自 `SYS_HASH_2_NUMBER` 的十进制串解析 | ✅ 闭环 |
| `SYS_HASH_2_RECEIPT` | `keccak(raw envelope)`(`:1230/1236`) | 既有收据表键约定是 tx hash | ⚠️ 见下 |
| `s_eth_hash_2_rawtx` | 同上(`:1250`) | 刻意与收据同键 | ✅ 设计意图明确 |

⚠️ `SYS_HASH_2_RECEIPT` 的键在通用世界是 `bcos::protocol::Transaction::hash()`(tars 对象哈希),
在 OP 世界是 `keccak(EIP-2718 envelope)`。**同一张表里出现两种哈希口径**。这不会碰撞(哈希空间),
但意味着"按 tx hash 查收据"在混链上的语义取决于块的来源。§6.4 (f) 详细论证了为什么**不写**
`SYS_HASH_2_TX`,却没有对**写** `SYS_HASH_2_RECEIPT` 做同等的口径分析。建议在 (f) 里补一句。

### 值编码是否自描述

**不自描述,三张表都不带版本/魔数**:
- `s_eth_block_header` = `EthBlockHeader::encode()` 裸 RLP(`:1164`)。21 字段 RLP 的字段数是隐式的
  (Isthmus 21 字段,未来分叉加字段就变 22)。读侧 `:875-877` 直接 `parentHeader.decode(...)`;
  若将来头字段数变了,旧行会解码失败 ⇒ `:881-884` 抛 `OpExecutionInternalError`(-32603),
  **不是静默错答**。✅ 失败形态是安全的,但没有版本位意味着**无法平滑升级**(必须重建索引)。
- `s_eth_hash_2_rawtx` = 原始 envelope,首字节即 EIP-2718 type,**自带类型判别**。✅ 这条实际上是三张里
  最自描述的。
- `SYS_HASH_2_RECEIPT` = `receipt->encode()`,沿用既有编码。

**建议**:在 `OpEngineSeam.h` 的两条常量注释里明确写下"**值不带版本前缀,升级路径 = 重建表**",
免得将来有人以为可以原地改格式。

### I-5(Important,新发现)`SYS_NUMBER_2_BLOCK_HEADER` / `SYS_NUMBER_2_TXS` 也从不写,§6.4 只记了 `SYS_HASH_2_TX`

`registerOpBlock` 写五张表,通用账本读侧依赖的另外两张——`SYS_NUMBER_2_BLOCK_HEADER` 与
`SYS_NUMBER_2_TXS`——**一张都没写**。直接后果(我沿 `LedgerMethods.h:197-260` 逐行推的):

- 任何对 OP 块调 `getBlockData(..., HEADER)` 的消费者(含 `BaselineScheduler.h:742`)会拿到
  `NotFoundBlockHeader` 异常,而不是块;
- `getBlockData(..., TRANSACTIONS|RECEIPTS)` 对 OP 块**全部返回空**(守门行不存在);
- 于是链上出现一种"`SYS_HASH_2_NUMBER`/`SYS_NUMBER_2_HASH` 说这个高度有块,但通用账本取不出块"的
  **索引与内容不一致**状态。

§6.4 条目 (f) 只讨论了 `SYS_HASH_2_TX`,把结论收在"通用'按哈希查交易'接口对 OP 块仍无数据"。
实际范围比这大一圈:**通用"按块号取块"接口对 OP 块是抛异常**。这一条不在台账任何位置。
它今天无消费者(与 (f) 同理由),但它是 (k)/(l)/(q) 那一族"接入前置"里应该并列的一项。

**建议**:在 §6.4 把 (f) 的标题从"交易本体"扩为"**块登记只覆盖索引面,不覆盖通用块内容面**",
并列出三张未写表与各自的失败形态(抛异常 / 静默空)。

---

## 8. 并发(问题 8)

### 调用是否串行?—— **没有任何机制保证**

engine 没有 RPC 入口(§6.4 条目 q),`AnyEngineService` 也不做串行化。所以必须按"可并发调用"审。

### 共享成员的保护面

`x_state`(`EngineServiceImpl.h:1476`,`std::shared_mutex`,`mutable`)覆盖:

| 成员 | 写点 | 读点 | 判定 |
|---|---|---|---|
| `m_forkchoiceState` / `m_trackedHeadBlock` / `m_safeBlockNumber` / `m_finalizedBlockNumber` | `:345-350`(unique) | `:436/:442`(shared)、`:612`(unique) | ✅ 全覆盖 |
| `m_payloadCache` / `m_blockHashToPayloadId` | `:415-416`、`:624`、`:656`(unique) | `:510`(shared)、`:613/:619/:649` | ✅ |
| `m_maxEngineVersion` | 构造时定死,之后只读(`:470-474`) | 无锁读 | ✅ 常量语义,安全 |
| `m_nextPayloadSequence` | `:1255` `m_nextPayloadSequence++` | — | ⚠️ 见 M-2 |

`newPayload` 与 `forkchoiceUpdated` 之间共享的 head / `maxEngineVersion`:**没有数据竞争**。

### 持锁跨 `co_await`

`runOpNewPayloadSteps:788` 取 `std::unique_lock lock(x_state)`,一直持到函数返回(含 `:1101`
`registerOpBlock` 与 `:1117` `pushView`),中间跨了 `getBlockNumber`、`readOne`、`executeOpBlock` 等
多个 `co_await`。这是 design §4.4 明文批准的例外,前提是"所有 storage2 后端在 co_await 处线程内同步完成"。

我独立核了这个前提在**当前**配置下成立:
- 内存 `MemoryStorage`:`readOneRaw`/`range` 是同步返回的 Task,`co_await` 走对称转移,不换线程;
- RocksDB 经 `LegacyStorageMethods.h:38-51`:`asyncGetRow` 的 `_callback` 在调用线程内联执行
  (`RocksDBStorage.cpp:228` 就在同一函数体里),`handle.resume()` 也在同一线程;
- ⇒ `std::unique_lock` 不会在另一个线程上解锁,不构成 UB。✅

**一条前瞻性警告(不是当前缺陷)**:`MultiLayerStorage::mergeBackStorage`(`:562-601`)在
`withCacheStorage` 分支用 `tbb::parallel_invoke` + `task::tbb::syncWait`(`:576-588`),而生产
`GlobalStateStorage`(`libinitializer/GlobalStateStorageInitializer.h:19-31`)**恰好带 cache storage**。
今天 OP 路径从不调 `mergeBackStorage`(正是 I-3 的成因),所以不触发;但**修 I-3 的那次改动会把
`mergeBackStorage` 引进持 `x_state` 的段里**,届时必须重新走一遍 §4.4 的失效判据——
`tbb::parallel_invoke` 本身不换回调线程(两个 lambda 都在其内部完成后才返回),但它会在持
`x_state` 时向 TBB 提交并行任务,窃取语义下的死锁面需要单独论证。建议把这条写进 §6.4 (a) 的修法约束里。

### 协程挂起点前后的成员假设

`runOpNewPayloadSteps` 在 `:788` 拿锁**之前**已经用 `payload` 做完 step 2 的全部静态校验
(`:761-778`)——那些只读请求对象,不碰成员。拿锁之后到函数结束,成员只被 `pushView` 一处间接触及。
`view` 是局部量,不是成员。⇒ **挂起点前后没有"再确认一次成员"的假设需要维持**。✅

---

## 9. Minor

- **M-1 协程形参取引用**:`Storage2Ledger::fetchAccountForVisit(const evmc::address&, const std::string&)`
  (`:655-656`)、`applyModifiedEntry(const StateDiff::Entry&)`(`:268`)、`applyDeletedEntry(const evmc::address&)`
  (`:336`)、`visitAccountsImpl(Visitor&)`(`:508`)都是**引用形参的协程**——经典悬垂形态。
  当前全部安全(调用点的实参要么是同一协程帧里的局部量 `:529-532`,要么由 `syncWait` 同步完成 `:216-218`),
  但同文件里 `fetchAccount(std::string)` / `fetchAllStorage(std::string)` 却刻意按值传。
  这种**同一文件内两种约定并存**正是后续重排里会被踩的坑。建议统一按值传,或在每个引用形参上加一行
  "调用者必须保证实参活过整个 `syncWait`"的注释。
- **M-2 `m_nextPayloadSequence` 无锁自增**:`nextPayloadID()`(`:1255`)在 `updateForkchoice:395` 是
  **锁外**调用(锁在 `:351` 已释放),而在 `handleNewPayload:623` 是**锁内**调用 ⇒ 数据竞争。
  OP 模式不可达(`:362-379` 的 `if constexpr (c_opMode)` 在 `:395` 之前就抛了),是通用路径的既有问题。
- **M-3 `bad_lexical_cast` 逃逸 `updateForkchoice`**:`LedgerMethods.h:439`
  `boost::lexical_cast<protocol::BlockNumber>(blockNumberStr)` 对损坏的 `SYS_HASH_2_NUMBER` 值会抛;
  `updateForkchoice:265-270` 的三次 `getBlockNumber` 不在任何 try 内,异常会以未分类形态逃出。
  OP 的 newPayload 侧没有这个问题(`:724-744` 的分类屏障覆盖了 `:797/:915`)。建议 FCU 侧比照补屏障。
- **M-4 FCU 号码的 TOCTOU**:`updateForkchoice:264-270` 在**锁外**用一个 `fork()` 出来的快照算出
  三个块号,再到 `:315` 拿锁做单调性判断并写 `m_trackedHeadBlock`。并发的 newPayload 可能在这两步之间
  `pushView` 了新块。我推演了几种交错,结果都收敛到"提前返回 VALID"或"正常 +1",**没有**能构造出
  错误 `InvalidForkchoiceState` 的交错,所以只记 Minor。若将来 FCU 侧要写存储,这个窗口必须收进锁内。

---

## 10. 已记账条目的独立复核结论

| 条目 | 我的独立判断 |
|---|---|
| (a) `mergeBackStorage()` 永不调用 | 事实属实,**定性被低估**。见 I-3:内存单调不可回收 + 本期核心用例上的 Θ(N²),应从"伸缩性"改记为"接入功能性阻塞"。 |
| (o) `visitAccountsImpl` 未命中不入缓存 | 裁定仍成立,守护断言 `Storage2LedgerTest.cpp:706` 确实存在。"窗口窄"的判断准确(`executeOpBlock` 在 `visitAccounts` 之后不再 `get_account`)。 |
| (p) `s_eth_block_header` 按块号取 vs op-geth 按 hash 取 | 属实,且 `EngineServiceImpl.h:841-849` 的注释把依赖关系写得很清楚。**优先级不低估**。 |
| (f) OP 块不写 `SYS_HASH_2_TX` | 裁定本身我同意(假交易比查不到坏一个数量级)。但**覆盖面不足**:见 I-5,`SYS_NUMBER_2_BLOCK_HEADER`/`SYS_NUMBER_2_TXS` 同样不写,后果比 (f) 描述的更大。 |
| (r) `LedgerMethods.h:233-235` | 缺陷属实,**可达性描述反了**。见 I-6。 |
| (s) 双重回调 → double resume | 代码形态属实,**UB 结论对该链不成立**。见 §6.2 与 I-6。 |
| (q) 生产无 OP RPC 入口 | 我从 `libinitializer/EngineServiceInitializer.h:27-28` 独立确认了"生产组合根不存在 OP 实例化",与该条一致。这也是 6.1/6.2 两条 pre-existing 缺陷今天完全不可达的根本原因。 |

---

## 11. 无法判定 / 需协调者裁定

1. **I-1(零值槽)与 I-2(非地址 `/apps/` 表名)是否算本期缺陷?**
   两者在 E-b 世界内都不可达,严格按"当前不可达 ⇒ Important"的分级规则我记为 Important。
   但它们的共同点是:**桥的 KEEP 契约在"桥是唯一写者"这个前提外就不再成立**,而生产接入必然要读
   通用执行器写的账本。是否要在本期就把这两条修掉(都是几行的改动),还是记进 §6.4 与
   (k)/(l)/(q) 同列,需要裁定。我的倾向是**I-1 现在就修**(三行,且能配一条会翻红的断言,符合
   §11 的通则),I-2 记账。
2. **I-3 的定性变更是否采纳?** 把 §6.4 (a) 从"伸缩性问题而非正确性问题"改为"接入功能性阻塞"。
   这会改变台账的优先级排序(它会插到 (k)/(l) 附近)。
3. **I-6 的两处文档修正是否要在本期落地?** 涉及 §6.4 条目 (r)/(s) 与
   `EngineServiceImpl.h:1194-1200` 的源码注释,都属"只改文字"。但按纪律本次复审只读,需协调者决定
   是否并入批 6 之后的统一修复。
