# 分歧审计结论 · 对抗性复核(证伪导向)

工作目录:`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`,分支 `feat-op-validator-loop`,HEAD 250/250。
基准:op-geth **`e8800cffe`**,本机 `/Users/octopus/octo/code/blockchain-impl/op-geth`。

被复核对象:`.superpowers/sdd/2026-07-28-op-validator-minimal-loop/state-divergence-audit-report.md`(967 行,九层差分,单人只读产出)。

---

## 你的立场:证伪,不是复述

审计是**一个人、只读、没跑过任何测试**得出的。它的每一条都可能错在:

- 读错了我方代码(漏看了某个上游校验、某个 `if constexpr` 分支、某个调用点)
- 读错了 op-geth(版本不对、看了不在执行路径上的函数、混淆了 EL 与 CL 的职责)
- **路径不可达**(结论在代码上成立,但没有任何真实调用路径能走到)
- **已被别处堵住**(另一层的检查先一步拒绝了该输入,分歧永不显现)
- 方向搞反(把"我方更严"写成"我方更松",或反之)

**默认假设每条都是错的,由你去推翻这个默认。** 一条findings只有在你**主动尝试证伪并失败**之后,才算 CONFIRMED。

本闭环已有多次"看似成立的结论被实验推翻"的先例:测试注释自称的机制被证伪、"穿协程边界"的归因被证伪、一条 codec 链接欠账被证伪、Karst 缺口被证伪为无害。**证伪一条比确认十条更有价值。**

## 判据(每条 findings 必须逐项回答)

```
【复核 N】<审计的原结论,一句话>
  A. 我方代码:审计引的 file:line 现在还对吗?它读对了吗?(自己打开确认,不要采信转述)
  B. op-geth:审计引的 file:line 在 e8800cffe 上还对吗?那段代码在执行路径上吗?
  C. 可达性:有没有一条真实的调用路径能走到?被上游哪个检查堵住了吗?
  D. 触发构造:审计说的触发成本属实吗?你能写出具体的输入吗?
  E. 裁决:CONFIRMED / REFUTED / 降级(说明降到什么程度) / 需实验(给最小步骤)
```

**没做 A-D 就直接给 E 的,该条作废。**

## 纪律

- 用 `rtk git`。每复核完一组就 `git add -f` 提交一次报告增量——本任务链已发生 6 次断流,分层提交是唯一有效的保险。
- 报告写各自指定路径。**结论用中文,file:line 用原文。**
- 只有 V1 可以构建/跑测试(它独占构建目录);V2/V3 **只读**,禁止 `cmake --build`、禁止改任何文件。
- 若你的复核推翻了审计,**要给出推翻的证据**(源码引用或实验输出),不能只说"我认为不成立"。

---

# V1(可构建,独占构建目录)· 表 1 的两条"日常流量即触发"

这两条如果成立,是"接上 op-node 当天就废"的级别;如果不成立,批次划分要重排。**它们值得用真实测试去证,而不是读代码。**

### 复核 1-1:legacy / 0x01 一律拒块

审计称 `OpSchedulerImpl.h:660-676` 的 `decodeOneRawTx` 只认 `0x7E/0x02/0x04`,任何含 legacy 或 access-list 交易的块直接 INVALID,对照 op-geth `core/types/transaction.go:191-234`。

除 A-D 外必答:
- **这是刻意的范围裁剪还是遗漏?** 去翻 spec、翻 `git log`/`git blame` 该段,看有没有明文裁定。审计把它当缺陷,五视角复审的 C6 也是——但如果有明文裁定说"本期只支持这三型",那它是**已声明的偏离**,性质不同(仍要改文档措辞)。
- **写一条测试实证**:构造一个含 legacy 交易的 payload 走真调度器,断言实际返回什么。**这是本次复核唯一必须落地的测试**(可临时写、验证后还原,也可留下——你判断)。

### 复核 1-2:`c_systemTxsAddress` → 毒旗 → -32603

审计称向 `0x…1000` 等 8 个地址转 1 wei 就让节点在该块上永久卡死。

除 A-D 外必答:
- 这 8 个地址**具体是哪 8 个**?逐个列出。它们在 OP 主网/测试网上是否已有真实用途或已被占用?
- 触发路径是"转账"还是"该地址被写入状态"?**一笔转到该地址的普通交易,真的会让桥走到毒旗吗?**——注意批 9 改过 `applyDiff` 的分类,现在毒旗置位后是 -32603,但**这条路径是读路还是写路**?
- **实测它**:构造该场景跑一次,记录实际返回码与 `firstError()`。

**收尾**:若有注入/临时测试,必须还原 + 重建 + 确认 250/250 复绿,`git status` 干净。
报告写 `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/audit-refute-v1-report.md`。

---

# V2(只读)· 表 1 剩余 6 条 + 表 2 后 4 条

**表 1**:#3(空块/首笔 attrs/deposit 后置三条更严准入)、#4(extraData OP 形状零校验 + 与 C4 复合)、#5(Jovian daFootprint > gasLimit)、#6(Jovian attributes 长度/选择器)、#7(Jovian 激活块不得含用户交易)、#8(首块 timestamp 豁免 + `configAt` 吞 pre-Isthmus)。

**表 2**:#5(父头按高度读)、#6(默认 `get_or_insert` 空账户落账)、#7(Karst 别名)、#8(七个 fork config 五个不可达)。

重点关注:

- **#3 的定性**:审计自己承认 `OpBlockExecute.cpp:17-19` 的注释准确标明了"更严"方向并指出 op-geth 行为,所以这是**已声明的偏离**。那么把它列进"分歧表"是否恰当?它与 #1(未声明的缺陷)混在一张表里会不会误导批次划分?给你的判断。
- **#4 的复合论断**:"extraData 是下一块 baseFee 的参数源,与 C4 复合后 baseFee 完全失守"——**这条复合推理链最长,最可能出错**。逐环节验证:extraData 里的 denominator/elasticity 真的进了 baseFee 计算吗?我方**算不算**下一块 baseFee(还是直接用 payload 给的)?若我方根本不算 baseFee,这条复合就不成立。
- **#7/#8 是"证伪型"结论**(审计说 Karst 无害、五个 config 是死代码)。**证伪型结论同样要复核** —— 它们会让人删掉欠账、降低警戒。确认 op-geth 的 `IsKarst` 在 `e8800cffe` 上真的没有执行侧调用方。
- **#6 的"侥幸"判断**:审计称"今天所有此类路径都 bump nonce"所以不触发。**穷举那些路径,确认没有漏**。

报告写 `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/audit-refute-v2-report.md`。

---

# V3(只读)· 表 2 前 4 条(生产阻塞)+ 5 条文档失实

**表 2**:#1(stateRoot 无 EIP-161 空账户过滤)、#2(DA scalar 槽 8 vs calldata)、#3(`execute_system_call` 绕过 `Host::call` + Release 下 assert 被编译掉)、#4(MessagePasser 缺席时 withdrawalsRoot 不同)。

**文档失实 1-5**(见报告末节)。

重点关注:

- **#1 是全部结论里最重的一条**(审计称"接生产账本当天每块 stateRoot 都对不上")。逐环节拆:
  - 我方 `StateRootCompute.h:82-93` 的 `visitAccounts` 真的无过滤吗?
  - `visitAccounts` 的**上游**(`Storage2Ledger::visitAccountsImpl`)有没有过滤空账户?
  - op-geth 的 `Finalise(deleteEmptyObjects)` 删的是**被 touch 的**空账户(`journal.dirties`),**不是** trie 里所有空账户。那么 op-geth 的 trie 里会不会也存在从未被 touch 的空账户?**若会,两边可能是同类行为,分歧不成立。**
  - 关键问题:**FISCO 通用执行器写出的"空账户"在以太坊定义下真的空吗**(nonce=0 且 balance=0 且无 code)?FISCO 账户表有 `alive`/`frozen`/`abi` 等字段,一个"FISCO 里存在"的账户在以太坊定义下是否必然非空?
  这条必须给出确定的裁决,它决定批次划分。
- **#3 的两半**:"REVERT 不回滚"与"assert 被编译掉"是两条独立论断,分别裁决。特别核实:evmone 的 `system_call_block_start` 内部**是否已经处理了** REVERT(审计可能漏看了 evmone 侧的处理)。
- **文档失实 1** 是全部失实里最要紧的(它把一个被证伪的断言当作 txRoot 设计的正当性论据)。**确认那段注释的原文与位置,并判断建议的改法是否足够** —— 只改措辞够不够,还是这个设计本身需要重新论证?

报告写 `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/audit-refute-v3-report.md`。

---

## 三份报告的共同收尾

每份最后给一张裁决表:

| 原编号 | 原结论 | 裁决 | 依据 |
|---|---|---|---|

裁决只能是:**CONFIRMED**(已尝试证伪并失败)/ **REFUTED**(附证据)/ **降级为 X** / **需实验**(附最小步骤)。

并在末尾单列一节:**「审计漏掉的」** —— 你在复核过程中发现的、审计没提的新问题。
