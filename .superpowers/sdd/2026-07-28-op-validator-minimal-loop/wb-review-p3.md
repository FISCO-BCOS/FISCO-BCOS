# 视角 3 · 正确性:存储、账本桥、生命周期与并发

**先读** `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-review-context.md`。
**报告写** `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-review-p3-report.md`。

## 你的范围

- `bcos-evm/bcos-evm/ledger/Storage2Ledger.h`(+53/-15)—— evmone `StateView` ↔ FISCO 存储的桥
- `EngineServiceImpl.h` 中的存储交互面:`registerOpBlock`、`fork()` / `newMutable()` / MultiLayerStorage 的层管理、以及所有 `storage2::readOne` / `writeOne` 调用点
- 上下游参考(不在改动范围,但你要判断本实现是否误用):`transaction-scheduler/` 的 MultiLayerStorage、`bcos-ledger/bcos-ledger/LedgerMethods.h`、`bcos-storage/` 的 RocksDBStorage

## 你的问题

你负责回答"**这条链跑一万个块之后还对不对、还跑得动**",以及"**有没有 UB**"。

逐项核对:

1. **KEEP 契约**:桥的核心语义是"存在但为空 ≠ 不存在(nullopt)"。逐个读取点核对:账户不存在、账户存在但 code 为空、storage slot 存在但值为 0 —— 这三种在 evmone 侧的语义分别是什么,桥返回的是不是对的?特别是 `has_storage`(它喂给 `state.cpp` 的 `has_initial_storage`,进而决定 **EIP-161/7610 空账户清除与 CREATE 地址碰撞判定**——错了就是共识分歧)。
2. **缓存正确性**:`m_accountCache` 等缓存的填充点与失效点。**未命中的结果绝不能进缓存**(否则后续写入后再读会读到陈旧的"不存在")。跨块是否复用?fork 出来的新层是否继承了上一层的缓存?
3. **MultiLayerStorage 的层生命周期**:`pushView` 之后是否有对应的 `mergeBackStorage`。如果没有,层栈会**无界增长**——`fork()` 复制整个 deque,`readOneRaw` 线性扫描所有层,复杂度随块高线性劣化。给出你读到的实际情况与量化后果。
4. **`stateRootOf` 的开销**:每块是否重建全量 trie?`visitAccounts` 的迭代器构造次数与账户数/槽数的关系。这是"跑得动"的主要风险点。
5. **异常安全与资源**:所有 `throw` 路径上,已经 push 的层、已经打开的迭代器、已经部分写入的表分别处于什么状态?有没有需要 RAII 但用了裸配对调用的地方?
6. **协程生命周期(重点,已记账两条 pre-existing 缺陷,请独立核实并判断本分支代码是否踩到同一个坑)**:
   - `bcos-ledger/bcos-ledger/LedgerMethods.h:233-235` 未 `has_value()` 即解引用 optional
   - `bcos-storage/.../RocksDBStorage.cpp:228-233` 成功回调写在 `try` 块内 → 回调若抛异常会走进 catch 再回调一次 → **同一个协程 handle `resume()` 两次 = UB**
   核实这两条是否属实(给出你读到的行号与代码),并回答:**本分支新增的代码路径会不会实际触发它们**?(即这两条是"存在但不可达"还是"我们刚刚让它可达了")
7. **`s_eth_hash_2_rawtx` / `s_eth_block_header` 等新表**:表名常量放在哪、有没有与既有表名冲突的可能、键的构造(是否有前缀/编码不一致)、值的编码是否自描述(将来读侧能否判断版本)。
8. **并发**:Engine API 的调用是串行的吗?`newPayload` 与 `forkchoiceUpdated` 之间共享的成员(如 forkchoice head、`maxEngineVersion`)有没有数据竞争?协程挂起点(`co_await`)前后对成员的假设是否仍然成立?

## 交付

按共享上下文的报告格式。第 6 点的两条必须给出**独立核实结论**(属实/不属实/部分属实),不要转述我的描述。
