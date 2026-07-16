# M3.5 Phase 1 — evmone `StateView` 读放大测量

对应 spec §7.2 的 go/no-go 第 1 步。工具：`ReadAmplification.cpp`（`bcos-evm-ref-read-amplification`）。

> ## ⚠️ 范围声明（2026-07-09 合规审计后补，必读）
>
> spec rev.4 对 M3.5 的原始要求是**「把 `StateViewAdapter` 接一次真实账本（或其协程存储的最小切片），
> 度量 ①同步 noexcept 桥接开销 ②每 tx 重建 `State` 开销 ③code 按值返回开销」**。
>
> **本文记录的 Phase 1 没有做这件事**：它包装的是纯内存的 `test::TestState`，**未接触任何账本或协程
> 存储**；成本模型是**阅读** `LedgerStateView.h` 源码后人工推算的常量，不是**运行**它实测的延迟。
> 上述三项开销**一项都未测**。
>
> 因此 Phase 1 证明的是：**接口宽度（3 方法 vs 7 方法）不构成障碍**——该结论有效，算术已被独立复算与
> 实机重跑验证。它**不能**证明「桥接开销可接受」；后者目前只有一条佐证：`LedgerStateView` 在生产里跑
> 着（存在性），而非开销实测。
>
> **本文的「判定 GO」应读作暂定。** 解冻 M4/M5 需要 Phase 2 真正接一次账本并测出那三项的绝对值。

```bash
EVM_REF_EEST_ROOT=<fixtures> ./build/bcos-evm-ref-read-amplification [max_files]
```

## 前提：桥接不是未知数，它已在生产

`bcos-evm/storage/LedgerStateView.h` 就是 "Production storage read adapter: StateView backed by
`ledger::EVMAccount`"——每个读方法内 `task::syncWait(...)` 协程存储。配套 `storage/StateDiffApplier.h`
是协程写回。

> **更正（2026-07-09，spec 提交 `66ca1015c`）**：本文最初写"两侧 `State` 架构同构"，**这是错的**。
> evmone 的 `State::find()` 命中 view 后**写入 `m_modified` 缓存**（读穿缓存），粗粒度 `get_account`
> 只在每 (tx, 地址) 付一次；而 `bcos-evm` 的 `State::find()`（`eth/state/State.cpp:43`）在
> `m_accounts` 未命中时**直接返回 `m_baseStateView->get_account()`、不写入缓存**——未修改的账户每次
> 访问都回账本。这正是它必须把 StateView 加宽到 7 个窄读方法的根因（粗粒度回落 = 每个 opcode 5 次读）。
> **结论方向不变但更强：evmone 的读路径设计在这一点上优于现状，下文实测的 1.16x 放大低估了它的优势。**

所以"同步 noexcept 接口能否接协程账本"**已经被回答：能，今天就在跑**。真正未知的是：evmone 的
StateView 只有 3 个方法（`get_account` 粗粒度 / `get_account_code` / `get_storage`），而 bcos-evm 把
自己的 StateView 加宽到 7 个窄读方法，理由写在 `LedgerStateView.h:139-142`：回落到粗粒度 `get_account`
意味着"a five-read full account load per lookup"。**读放大有多大？**

## 读代价模型（逐条对照 LedgerStateView.h 的 lambda）

每个 lambda 都先 `syncWait(account.exists())` 并在为假时**提前返回**：

| StateView 方法 | 命中 | 未命中 |
|---|---|---|
| `get_account` | 4（exists+balance+nonce+codeHash）+1 若需探测 `has_storage` | **1**（仅 exists） |
| `get_account_code` | 2（exists+code） | 1 |
| `get_storage` | 2（exists+storage） | 1 |

**为什么 `get_account` 记 4 读，而 `LedgerStateView.h:139-142` 自己说「five-read」？**（合规审计指出的
内部矛盾，此处澄清）bcos-evm 的 `m_accountRead` lambda 实测有 **5 次** `syncWait`——exists + balance +
nonce + **code** + codeHash——因为 bcos-evm 自己的 `Account` 结构**带 code 字段**。而 **evmone 的
`StateView::Account` 没有 code 字段**（`{nonce, balance, code_hash, has_storage}`），code 由独立的
`get_account_code` 单独计费。所以一个 evmone 形状的 `LedgerStateView` 适配器，`get_account` 只需 4 读。
模型取 4 是正确的，但此前未写明理由。

**`get_account_code` / `get_storage` 的「未命中」列在本工作负载下不会发生**：evmone 只在
`code_hash != EMPTY` 时调 `get_code`、只在账户已存在（`State::get()` 断言）后调 `get_storage`，
两者恒为命中，故模型统一按 2 计费与真实行为一致。

`has_storage` 的唯一消费者是 evmone `host.cpp:91` 的 EIP-7610 CREATE 碰撞检查，且被
`nonce != 0 || code_hash != EMPTY` 短路——只有 nonce=0 且无 code 的账户才真需探测。

## 结果（EEST v5.4.0 state fixtures，Cancun+，2723 文件 / 53,131 笔成功 tx，16 秒）

```
kind            txs    acct/tx   code/tx     slot/tx    acctRd/tx     stoRd/tx    totRd/tx    storage%    amplif
transfer       2251       6.83      0.09        7.47        23.55        14.94       38.49      38.82%      1.28x
call          31691      36.19      1.52        3.28        53.65         6.56       60.20      10.89%      1.16x
create        19189       9.86      2.23       20.96        30.92        41.92       72.85      57.55%      1.15x
ALL           53131      25.44      1.72        9.84        44.16        19.69       63.85      30.83%      1.16x
```

## 三个结论

**1. 读放大只有 1.16x——粗粒度 `get_account` 不是问题。**
`get_account` 调用 1,351,512 次，其中 **83% 是 miss**（1,121,541），而 miss 在两种接口下都只花 1 次
`exists()` 读。命中的 17% 才付 4 读，且被 evmone 的 `m_modified` 缓存为每 (tx, 地址) 一次。
bcos-evm 当初遭遇的"5 读全账户加载"痛点，在 evmone 的 State 缓存 + 高 miss 率下被大幅稀释。

**2. 真正的浪费在别处，且更容易修：不存在账户的负查询不被缓存。**
evmone `state.cpp:249` 有个上游 TODO——`State::find()` 命中 `m_modified` 或调 `get_account()`，但
**返回 nullopt 时不插入缓存**，于是同一 tx 内对同一个不存在地址反复查询。实测：

- 946,042 次冗余重复查询 = **全部 `get_account` 调用的 70%**
- 折合 **全部账本读的 27.9%** 是纯粹浪费（每次一个 `syncWait(exists())` 往返）

| 优化路径 | 节省 | 代价 |
|---|---|---|
| **A) 适配器加负缓存**（per-tx 记住"这个地址不存在"） | **27.9%** | ~5 行，**纯适配器侧，不碰 evmone** |
| B) 把 StateView 加宽为窄读 | 13.6%（上界） | 需 fork evmone 的公开接口 |

**便宜的那条路收益是贵的那条的两倍。**

**3. `has_storage` 是伪问题。** 只有 9.1% 的命中需要真实探测；即便无条件探测每个命中账户，总读也只
+3.2%。条件化（仅 nonce=0 且无 code 时探测）几乎免费。

## 对 §7.2 的影响

对抗性审查当初把"同步 StateView vs 协程账本"列为可能否决替换的风险（挑战 7c）。**该风险被高估了**：
桥接已在生产验证（`LedgerStateView` + `StateDiffApplier`），粗粒度接口的实测放大仅 1.16x，且最大的
一笔浪费有一个不碰 evmone 的 5 行解法。（注意：两侧 `State` 缓存策略**并不相同**，见上方更正框——
evmone 是读穿缓存，`bcos-evm` 无读缓存，这一差异使 1.16x 成为 evmone 优势的下界而非上界。）

**Phase 1 判定：GO。** 剩余工作：
- **Phase 2**：测 `ledger::EVMAccount` 单次读的真实延迟，× 63.85 读/tx（或加负缓存后的 46.05）得到
  绝对开销，与 bcos-evm 生产路径对比。
- **Phase 3（仅当 Phase 2 超标）**：块级缓存适配器——evmone 的 `State` 是每 tx 重建的，同一区块内
  同一账户会被反复冷读；块级缓存同时消掉这一项与冷填成本。同样是适配器侧。

## 已知偏差

EEST fixtures 是对抗性用例，不代表真实流量（`call` 桶 36 次 `get_account`/tx 远高于一次 ERC-20 转账）。
**绝对数字偏保守（偏高）**；但比值（1.16x）与负缓存收益（27.9%）是结构性的，不随流量分布改变。
