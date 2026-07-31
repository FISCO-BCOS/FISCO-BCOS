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
