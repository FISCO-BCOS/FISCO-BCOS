# V3 对抗性复核报告 —— 表 2 前 4 条(生产阻塞)+ 5 条文档失实

被复核对象:`.superpowers/sdd/2026-07-28-op-validator-minimal-loop/state-divergence-audit-report.md`
基准 op-geth:`e8800cffe`(`/Users/octopus/octo/code/blockchain-impl/op-geth`,已 `git log` 确认 HEAD,worktree clean)。
本报告**只读**:未构建、未跑测试、未改任何源码/测试文件。

---

## 复核 2-1(表 2 #1 / 分歧 6-1):stateRoot 收录每一个存活账户,不做 EIP-161 空账户过滤

原结论:"接生产账本当天每块 stateRoot 都对不上 → 节点对每个块判 INVALID",全部审计里最重的一条。

### A. 我方代码 —— 引文属实,但只属实一半

`bcos-evm/bcos-evm/adapter/StateRootCompute.h:82-93` 现文确为:

```cpp
template <class Ledger>
[[nodiscard]] evmone::hash256 stateRootOf(const Ledger& ledger)
{
    evmone::state::MPT trie;
    ledger.visitAccounts([&](const auto& account) {
        trie.insert(evmone::keccak256(account.addr),
            evmone::rlp::encode_tuple(account.nonce, account.balance,
                accountStorageRoot(account.storage), account.codeHash));
        return true;
    });
    return trie.hash();
}
```

无任何 `nonce==0 && balance==0 && codeHash==EMPTY → skip`。**A 项属实。**

上游 `Storage2Ledger.h:679-720 visitAccountsImpl` 同样无空账户过滤,过滤只有两条(`/apps/` 前缀区间、
`liveContent` 墓碑)。**A 项属实。**

但审计把"无过滤"直接等同于"与 op-geth 分歧",这一步是错的 —— 见 B。

### B. op-geth —— 审计的关键前提被证伪

审计原文(报告 :598-601):

> "从未被 touch 的空账户在规范链上**根本不可能**存在于 trie 中。"

**这句话在 `e8800cffe` 上是假的。** 证据:

`core/genesis.go:180`(`hashAlloc`)与 `:219`(`flushAlloc` 的 `CommitWithUpdate`)建创世状态用的是:

```go
stateRoot, err := statedb.Commit(0, false, false)
```

`core/state/statedb.go:1445` 的签名 `Commit(block uint64, deleteEmptyObjects bool, noStorageWiping bool)`
—— 第二参 `deleteEmptyObjects = **false**`。而 `hashAlloc` 对每个 alloc 条目都无条件调
`statedb.SetCode(addr, account.Code, …)` 与 `statedb.SetNonce(addr, account.Nonce, …)`
(`core/genesis.go:173-174`),两者都经 `getOrNewStateObject` → `createObject`
(`statedb.go:658-673`)把对象放进 `journal.dirties`。于是 `Finalise(false)`
(`statedb.go:834` 的 `deleteEmptyObjects && obj.empty()` 短路为 false)走 `else` 分支
`obj.finalise(); s.markUpdate(addr)` —— **一个 `{"balance":"0x0"}` 形状的 alloc 条目会被写进
创世 trie 并永久留在那里**(此后没人 touch 它,`Finalise(true)` 的 `journal.dirties` 永远不含它)。

`{"addr": {"balance": "0x0"}}` 恰恰是 hardhat / foundry / op-deployer 生成的 genesis 里最常见的形状。
所以 op-geth 的 trie **完全可能且经常**含有从未被 touch 的空账户。审计的 B 项前提不成立。

### C. 可达性 —— 审计自己点名的那条来源上,两边是**一致**的

审计说触发来源是"通用执行器(transaction-executor / **bcos-ledger 创世 alloc**)没有 EIP-158 清除语义"。
逐条查这条来源:

`bcos-ledger/bcos-ledger/Ledger.cpp:1792-1876 importGenesisState`:

```cpp
account::EVMAccount account(storage, address, features.get(Features::Flag::feature_raw_address));
co_await account.create();                                   // :1810 无条件
if (!importAccount.code.empty())   { … setCode(…);   }       // :1812
if (!importAccount.nonce.empty())  { … setNonce(…);  }       // :1822
if (importAccount.balance > 0)     { … setBalance(…);}       // :1827
```

一个 `balance=0 / 无 code / 无 nonce / 无 storage` 的 alloc 条目,在 FISCO 账本里落成
**只有 SYS_TABLES 标记行、账户表内一行字段都没有**的账户。
`EVMAccount::create()`(`bcos-framework/bcos-framework/ledger/EVMAccount.h:33-37`)写的就是
`storage::Entry{"value"}` 这一个标记行,不写任何字段。

我方 `Storage2Ledger::fetchAccount`(:762-…)对它:`existsOne` → true;
`code_hash = keccak256({})`(:772);balance/nonce 由 `Account` 成员默认初始化为 0。
→ 叶 = `rlp(0, 0, EMPTY_MPT_HASH, EMPTY_CODE_HASH)`,**进 stateRoot**。

op-geth 对同一条 alloc:见 B,**同样进 trie,同样的叶**。

**两边一致。** 审计点名的这条来源不产生分歧。

再看 `bcos-ledger/bcos-ledger/GenesisStateRoot.cpp:70-98 computeGenesisStateRoot`:它对
`genesis.m_allocs` 逐条无条件建叶(`nonce/balance/storageRoot/codeHash`),同样不过滤空账户 ——
**与 op-geth `Genesis.ToBlock()` 的 `Commit(0,false,false)` 行为对齐**,这是有意的对齐而不是遗漏。
若真按审计的建议给 `stateRootOf` 加一个 EIP-161 过滤,反而会让 `stateRootOf` 与
`computeGenesisStateRoot` / op-geth 三方**同时**对不上 —— 建议本身是有害的。

我方对"被 touch 的空账户"的 EIP-161 删除语义是**已实现且与 geth 对齐**的:
`bcos-evm/bcos-evm/eth/state/state.cpp:214-219`(`erase_if_empty && rev>=SPURIOUS_DRAGON && is_empty()`
→ `deleted_accounts`,`just_created` 不上报)→ `StateDiffSanitize.h:31-36`(view 中存在者的删除项保留)
→ `applyDeletedEntry`。这一点审计自己在"一致项"表里也承认了(报告 :672),却在 6-1 里当作缺失重复计了一次。

### D. 触发构造 —— 真正能构造出分歧的输入是**另外三条**,都不是"空账户过滤"

要产生分歧,需要"账户在我方账本里存在,而在 op-geth 的 trie 里不存在(或叶不同)"。空账户本身不满足这个条件。
真正满足的是:

**(D1) 两侧 genesis 配置不同。** 这是部署配置问题,不是代码缺陷。

**(D2) 同一 FISCO 账本上跑过非 OP 的通用 FISCO 交易。** 这时任何 FISCO 原生合约账户
(`/apps/<40hex>`,有 code,**非空**)都会进 OP 的 stateRoot 而 op-geth 从未听说过它 ——
分歧与"空不空"完全无关,空账户过滤一点用没有。而且这条路上还有一个**先一步炸掉的**问题,见本报告末节
「审计漏掉的 · N-1」:根本走不到比根,`visitAccounts` 先毒旗。

**(D3) 分歧 6-3 那条路径**(默认 `get_or_insert` 插入、最终为空、非 `just_created` →
落 `modified_accounts` → `applyModifiedEntry` 无条件 `create()` 标记行)。这才是唯一一条
"**我方凭空造出一个 op-geth 没有的空账户**"的机制。审计把它排在表 2 #6 并标注"今天没有已知可达路径"。
**6-1 的全部真实危害其实寄生在 6-3 上**,而 6-1 自身的表述(以及它的"最小验证步骤")指向的是一个不存在的分歧。

### 派单点名必答:FISCO 里存在的账户,在以太坊定义下必然非空吗?

**确定裁决:不必然 —— 而且这不是缺陷,是桥有意的归一化。**

- FISCO 的"存在"= `SYS_TABLES` 里有一条 `/apps/<hex>` 标记行(`EVMAccount::exists()` 就是
  `existsOne(SYS_TABLES, tableName)`)。`create()` 只写标记行,**零个字段**。
- 以太坊的"空"= nonce==0 且 balance==0 且 codeHash==keccak(空)。
- `ACCOUNT_TABLE_FIELDS`(`bcos-framework/bcos-framework/ledger/LedgerTypeDef.h:113-123`)有 8 个字段:
  `CODE_HASH / CODE / BALANCE / ABI / NONCE / ALIVE / FROZEN / SHARD`。
  `Storage2Ledger::fetchAccount` 只读其中三个语义字段(BALANCE / NONCE / CODE_HASH),
  `ABI / ALIVE / FROZEN / SHARD` **结构上不参与叶**(`StateRootCompute.h:88-89` 的叶只有四元组)。
- 因此:**一个只带 `alive` / `frozen` / `shard` / `abi` 而没有 balance/nonce/code 的 FISCO 账户,
  在以太坊定义下就是精确的空账户**,并且我方建出的叶与 op-geth 对同一空账户建出的叶**逐字节相同**。
- 同理 `setBalance(0)` 写的是字符串 `"0"`(`EVMAccount.h` 的 `balance.str({},{})`),
  `fetchAccount` 用 `intx::from_string` 读回 0 —— 仍是空账户,仍与 geth 同叶。

也就是说:FISCO 的额外字段**不会**把一个以太坊空账户"变成非空",反过来也不会。"存在但空"这一状态在
两边是同构的,`StateDiffSanitize.h:24-30` 的 KEEP 契约注释正是为此写的。

**结论:审计假设的"FISCO 侧空账户 ≠ 以太坊侧空账户,所以两边叶不同"这一潜台词不成立;
两边对同一个空账户建出的是同一个叶。**

### E. 裁决:**降级** —— 从「表 2 头号生产阻塞:每块 stateRoot 对不上」降为「文档/记账问题 + 归并进 6-3」

理由汇总:

1. B 项前提(op-geth trie 不可能含未 touch 的空账户)**被证伪**:`core/genesis.go:180` /
   `statedb.go:1445` 的 `Commit(0, **false**, false)`。
2. 审计点名的主来源(bcos-ledger 创世 alloc)上,两边**行为一致**
   (`Ledger.cpp:1810` 无条件 `create()` ↔ geth `hashAlloc` 无条件 `getOrNewStateObject`;
   两边的创世根算法 `GenesisStateRoot.cpp:70-98` ↔ `hashAlloc` 也都不过滤)。
3. 被 touch 的空账户的 EIP-161 删除,我方**已实现且已对齐**(`state.cpp:214-219` + sanitize + applyDeletedEntry),
   审计自己在一致项里也这么写。
4. 唯一真实机制是 6-3(表 2 #6),它今天没有可达路径。
5. 审计给的"最小验证步骤"(手工塞一个只有标记行的账户,比对含/不含时的根)**必然得到"根不同"**,
   但这只证明"trie 里多一个叶则根不同"这一同义反复,**不能**证明与 op-geth 分歧 —— 该步骤无判别力。

**不构成"接生产账本当天全线崩"。** 批次划分不应把它排在最高优先级。

**保留的真实欠账(降级后的记账)**:
- `stateRootOf` 与 `computeGenesisStateRoot` / op-geth genesis 的"都不过滤空账户"是一条
  **三方隐式契约**,今天没有任何注释或断言承载它。任何未来"顺手加个 EIP-161 过滤"的改动会同时打破三方。
  建议在 `StateRootCompute.h` 的模板注释里写明"**故意不过滤空账户**,锚 `core/genesis.go:180`
  `Commit(0,false,false)` + `GenesisStateRoot.cpp:70-98`"。这条比审计原来的建议方向**相反**。
- 6-3 的守护值得做(审计给的最小步骤是对的):在 `applyModifiedEntry` 入口断言 entry 非 EIP-161 空。

**`[需验证]`(不阻塞裁决)**:上述"两边同叶"的字节级确证需要一次实验 ——
最小步骤:在 `EbT8nReplayTest` 的 `LedgerSeed` 里塞一个只有标记行的 `/apps/<addr>`,
同时给 op-geth 的等价 t8n 输入 alloc 加 `{"addr":{"balance":"0x0"}}`,比对两侧 stateRoot。
预期相同。**V3 只读,未执行。**

---

## 复核 2-2(表 2 #2 / 分歧 5-1):Jovian `daFootprintGasScalar` 取自不同的源

### A. 我方代码 —— 属实

`bcos-evm/bcos-evm/opstack/OpFeeParams.cpp:33`:
`.da_footprint_gas_scalar = static_cast<uint16_t>(readBE(slot8, 18, 2))`,
`:43-45 loadOpFeeParams` 读 `OP_L1_BLOCK` 的槽 1/3/7/8。
`OpFeeParams.h:25` 的注释就是 `// slot 8 bytes[18,20)`,**没有任何来源锚**(既没有 op-geth 位置,
也没有 L1Block.sol 的存储布局出处)——全仓 grep `da_footprint_gas_scalar` 只有这 5 处,无一条溯源。

`OpBlockExecute.cpp:66-73`:惰性在**第一笔非 deposit 交易**处加载(即所有 deposit 执行完之后)。
`OpReceiptMeta.cpp:27-32` 用它算 `da_footprint`;`OpBlockSeal.cpp:74-80` 只累加
`std::get_if<OpTxReceipt>` 的收据(即排除 deposit)→ `seal.blobGasUsed`。

### B. op-geth —— 属实,且比审计说的更强

`core/types/rollup_cost.go:547-557 ExtractDAFootprintGasScalar` 与 `:563-591 CalcDAFootprint`
在 `e8800cffe` 上与审计引文逐字一致。补充两点审计没写的:

1. **op-geth 的 DA 校验在 `ValidateBody` 里,是执行前的块体校验**
   (`core/block_validator.go:119-134`),不是执行后比对:
   ```go
   if v.config.IsJovian(header.Time) {
       if header.BlobGasUsed == nil { return errors.New("nil blob gas used …") }
       daFootprint, err := types.CalcDAFootprint(block.Transactions())
       if err != nil { return fmt.Errorf("failed to calculate DA footprint: %w", err) }
       else if blobGasUsed != daFootprint { … }
       if daFootprint > block.GasLimit() { … }
   }
   ```
   所以选择器/长度不合法时 op-geth **根本不执行这个块**。我方是执行完再算。
2. **op-geth 确实从槽 8 读东西 —— 但只读 operator fee**
   (`rollup_cost.go:84 OperatorFeeParamsSlot = 8`,`:228 statedb.GetState(L1BlockAddr, OperatorFeeParamsSlot)`,
   字节 [20:24) scalar / [24:32) constant)。**全仓没有任何 DA-footprint 的 slot 偏移常量**
   (grep `DAFootprint` 只命中 `ExtractDAFootprintGasScalar`/`CalcDAFootprint` 与 `miner/worker.go:422`、
   `core/types/receipt_opstack.go:26`,三个消费方**全部**走 attributes calldata)。
   → 我方的 `readBE(slot8, 18, 2)` 在 op-geth 侧**没有对应物**,这是一个纯粹自造的取值点。

### C. 可达性 + D. 触发构造 —— 审计只给了一条,实际有三条,其中一条**与存储布局完全无关**

审计只列了 (a) 选择器/长度不校验 与 (b) "槽 8 [18:20] 是否恒等于 calldata[176:178](取决于 L1Block 存储布局)"。
补两条 (b) 之外的、**不依赖布局假设**的触发路径:

**(b1) L1 attributes deposit 执行失败。** deposit 失败在两边都是"回执 status=0、块仍有效"
(我方 `OpDepositTx.cpp` 失败回执语义、op-geth Regolith 语义,审计第 7 层已确认一致)。
此时 L1Block 的槽 8 **没有被本块的 attributes 更新**,我方 `loadOpFeeParams` 读到的是
**上一块的** scalar;op-geth 读的是**本块 calldata** 里的。两值不同 ⇒ `blobGasUsed` 不同 ⇒
我方判 INVALID 而 op-geth 判 VALID。触发只需要一笔 gas 不足 / 会 revert 的 attributes deposit。
**这条不需要任何关于 L1Block.sol 布局的假设,`[需验证]` 只剩"deposit 失败后槽确实不更新",
而那是 EVM 回滚语义的直接推论。**

**(b2) 本块有第二笔 deposit 覆写槽 8。** attributes 之后的任一 deposit(depositor 权限)
再调一次 setter,我方读到的是**最后一次写入**的值,op-geth 读的仍是 `txs[0].Data()`。
需要特权 deposit,可达性低,但机制上成立。

**(b3) 纯 deposit 块 / 无非 deposit 交易的 Jovian 块。** 我方 `feeLoaded` 永不置位 ⇒
`fee` 保持默认 ⇒ scalar=0 ⇒ blobGasUsed=0;op-geth 的 `CalcDAFootprint` 循环里 deposit 全部
`continue` ⇒ 也是 0。**数值上一致**,但 op-geth **仍然会先跑一遍 `ExtractDAFootprintGasScalar`
的长度/选择器校验**并因不合法而拒块(`:577` 的注释 "ExtractDAFootprintGasScalar catches all
invalid lengths")。所以 (a) 在纯 deposit 块上同样成立 —— 审计把 (a) 归到"表 1 #6 构造 attributes
calldata",成本描述是准确的。

### E. 裁决:**CONFIRMED**(并**上调**)

- (a) 长度/选择器零校验:**CONFIRMED**,当前可构造,方向"我方接受 op-geth 拒绝的"。
- (b) 取值源不同:**CONFIRMED**,且比审计写的更硬 —— 不是"取决于 L1Block 布局",
  而是 **op-geth 侧根本不存在这个槽读取点**,我方的槽偏移是无出处的自造常量;
  另有 (b1) 一条**与布局无关**的可达路径。
- 建议的修法方向也随之变化:审计的"最小验证步骤"(去查 L1Block.sol 布局)只能验证 (b) 的一半;
  **正解是改为与 op-geth 同源 —— 从 `txs[0]` 的 calldata[176:178] 取值,并同时补上
  178 长度 + `0x3db6be2b` 选择器校验**,一次修掉 (a)(b)(b1)(b2) 四条。
- 保留 `[需验证]`:"槽 8 [18:20] 在规范 Jovian L1Block 预部署里到底是不是 daFootprintGasScalar"
  仍未证实(本仓无出处、op-geth 无对应常量)。若它其实**不是**,那今天连正常块的
  `blobGasUsed` 都是错的 —— 这会把本条从"生产接入才可达"直接变成"表 1 级别"。
  最小步骤:取一条真实 Jovian 版 L1Block 预部署字节码,跑一次 `setL1BlockValuesJovian`,
  dump 槽 8,与 calldata[176:178] 比对。**V3 只读,未执行。**

---

## 复核 2-3(表 2 #3 / 分歧 3-1 + 3-2):`execute_system_call` 绕过 `Host::call`;Release 下 assert 被编译掉

派单点名要分别裁决,并特别核实 evmone 的 `system_call_block_start` 是否已经处理了 REVERT。

### 3-1(不回滚)

**A.** `bcos-evm/bcos-evm/eth/state/system_contracts.cpp:65-81`:
```cpp
const Transaction empty_tx{};
Host host{rev, vm, state, block, block_hashes, empty_tx};
return vm.execute(host, rev, msg, code.data(), code.size());
```
构造了 `Host`,但**直接 `vm.execute`,不经 `Host::call`**。`:83-103 system_call_block_start`
外层持有 `State state{state_view}`,按引用传给每次调用,末尾 `:106 state.build_diff(rev)`。

**派单点名的核实:evmone 侧没有任何 REVERT 处理。** `system_call_block_start` 的循环体
(`:96-103`)只有三步:取 code、空则 continue、`execute_system_call` + `assert`。
`:103` 之后没有任何按 `status_code` 分支的代码,没有 checkpoint/rollback。
回滚只存在于 `host.cpp:383-397` 的 `Host::call`(`m_state.checkpoint()` / 非 SUCCESS 时
`m_state.rollback(...)`),而这条路径**不经过它**。**审计没有漏看 evmone 侧的处理 —— 那处理不存在。**

**B.** op-geth `core/state_processor.go:262-282 ProcessBeaconBlockRoot` / `:286-308
ProcessParentBlockHash` 在 `e8800cffe` 上与审计引文一致:两者都走 `evm.Call(...)`
(内部 snapshot/revert),2935 侧另有 `:305-307 if err != nil { panic(err) }`,
两者结尾都补一次 `evm.StateDB.Finalise(true)`。方向确认:**op-geth 回滚,我方不回滚**。

**C/D.** 可达性:需要 `0x000F3df6…Beac02` / `0x0000F908…002935` 上的代码在 SSTORE 之后
REVERT 或耗尽 30M gas。规范 4788/2935 预部署的 set 分支只在 `calldatasize != 32` 时 revert,
而我方输入恒为 32 字节 —— **规范链上不可达**,只有非规范创世/升级能触发。审计的定性("需生产账本")准确。

**E. 裁决:CONFIRMED,但可达性维持在"需非规范预部署"。**
两点补充:
- 这是**上游 evmone 的行为**(vendored 文件,`scripts/upstream-diff/manifest.tsv` 里没有
  `system_contract*` 条目 = 本仓未改过它)。修它等于对上游打补丁,应记为 upstream 分歧而不是本仓缺陷,
  但**对 OP 验证者而言仍然是我方的责任面**。
- 修法只需一行量级:在 `execute_system_call` 外包一层
  `const auto cp = state.checkpoint(); … if (res.status_code != EVMC_SUCCESS) state.rollback(cp);`
  —— 与 `Host::call` 同一对原语。

### 3-2(assert 被编译掉)

**A.** `system_contracts.cpp:103 assert(res.status_code == EVMC_SUCCESS);` 属实,
且**比审计说的更严重**:`cmake/Options.cmake:41-43` 的默认构建类型是 **`RelWithDebInfo`**,
`cmake/CompilerSettings.cmake:84` 的 `CMAKE_CXX_FLAGS_RELWITHDEBINFO = "-O2 -g -DNDEBUG"` ——
**不显式指定 `-DCMAKE_BUILD_TYPE=Debug` 的构建(含开发者的默认构建)全部带 NDEBUG**,
这条 assert 在绝大多数构建里根本不存在。审计写的"生产构建(NDEBUG)下"低估了覆盖面。

**B.** op-geth:4788 侧同样忽略返回(`_, _, _ = evm.Call`),但 snapshot 保证"失败 ⇒ 无状态变化";
2935 侧 `panic(err)`。审计引文准确。

**E. 裁决:CONFIRMED(且事实加强)。** 它本身不是独立分歧,是 3-1 的失败模式选择器:
Debug 下 abort(可用性事故),NDEBUG 下静默状态分歧(共识事故)。两者都不是"判块 INVALID"。
与 3-1 同一处修复即可消除(rollback 之后 assert 就不再承担唯一失败处理)。

---

## 复核 2-4(表 2 #4 / 分歧 8-1):MessagePasser 账户不存在时 withdrawalsRoot 不同

**A.** `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:877-887`:
```cpp
std::map<evmc::bytes32, evmc::bytes32> messagePasserStorage;
bridge.visitAccounts([&](const auto& accountView) {
    if (accountView.addr == bcos::evm::opstack::OP_L2_TO_L1_MESSAGE_PASSER)
    { messagePasserStorage = accountView.storage; return false; }
    return true;
});
```
账户不存在 ⇒ map 保持空 ⇒ `OpBlockSeal.cpp:62 opStorageRoot({})` = `EMPTY_MPT_HASH`。
**代码无法区分"账户不存在"与"账户存在但无非零槽"** —— 两者都给空 map。属实。

**B.** `core/state/statedb.go:347-353`:
```go
func (s *StateDB) GetStorageRoot(addr common.Address) common.Hash {
    stateObject := s.getStateObject(addr)
    if stateObject != nil { return stateObject.Root() }
    return common.Hash{}
}
```
**不存在 ⇒ 全零哈希**;存在但存储为空 ⇒ `stateObject.Root()` = `types.EmptyRootHash`。
`core/block_validator.go:190-197` 拿它与 `*header.WithdrawalsHash` 比。审计引文与行号在 `e8800cffe` 上属实。

所以分歧只在"账户完全不存在"这一格:op-geth 期望 `0x00…00`,我方给 `0x56e8…b421`。
"存在但空存储"那一格两边一致。审计的表述准确。

**C. 可达性 —— 尝试证伪,失败,但可达性比审计写的更低**
- 规范 OP 链创世必含 `0x4200…0016` 预部署(带 code),不可能不存在。
- 能否被删掉?SELFDESTRUCT 在 Cancun+ 只对**同笔创建**的账户真正销毁
  (`host.cpp:137-148` 的 `m_rev >= EVMC_CANCUN && !acc.just_created` 分支只转余额),
  MessagePasser 是创世部署的,永远 `just_created == false` ⇒ **销毁路径不可达**。
- 因此实际触发只剩"创世里就没部署它"这一条,即**非规范创世配置**。

**D.** 触发输入确实存在(一份不含 `0x4200…0016` 的 genesis alloc),但它同时意味着这条链不是
规范 OP 链 —— 而在这种链上 op-node 也不会正常工作。

**E. 裁决:CONFIRMED(事实),但**降级**为「防御性欠账 / 鲁棒性缺口」,不是"生产接入才可达的分歧"。**
理由:唯一触发面是非规范创世,而非规范创世本身就不在被支持的输入集里。
建议(低成本、值得做):在 `visitAccounts` 回调里同时记一个 `bool found`,
未找到时**毒旗或抛块级错误**,而不是静默地给出空 MPT 根 —— 因为"MessagePasser 不存在"永远是
一个配置事故,静默地给一个看起来合理的根是最坏的处理方式。

**顺带(不是分歧,记一笔)**:这一步为了找 1 个账户做了一次**全账本 `visitAccounts`**
(每个账户还要 `fetchAllStorage` 全量扫),紧接着 `stateRootOf(bridge)` 又扫**第二遍**。
两遍全表扫描,且中间没有任何缓存复用。正确性无碍,但接生产账本后这是每块两次 O(全状态) 的开销。

---
