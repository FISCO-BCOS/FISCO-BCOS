# 全分支复审 · 合并裁定

范围 `42e62fcef..HEAD`(30 提交;prod +2906/-128 / 14 文件,test +6805,docs +5152)。
5 个只读视角 + 控制器独立核实。`[需验证]` 实验由 `wb-verify-report.md` 统一执行。

原始报告:`wb-review-p{1..5}-report.md`。本文件只做**去重、交叉核对、分级裁定**,不重复原文。

---

## 一、总体判断

**这个分支的执行主干是对的,漏洞集中在"边缘输入"与"跨层契约"两处。**

五个视角一致地没有在**正常路径**上找到缺陷:33 条金值向量的执行结果、六项比对、块登记的原子性、桥的写回一致性、异常安全,全部经独立核对成立。P5 逐条核对后明确排除了"gate 是同义反复"这个最大的系统性怀疑——四条断言的右侧全部来自 op-geth,`makeGoldenRequest` 无自算回填。

问题全部落在金值语料**结构上到不了**的地方:非规范编码、超范围分叉、本地故障、生产账本的真实形状、以及"将来有人加一个字段"。这与之前的判断一致,但这次给出了 6 条**具体可构造**的分歧。

---

## 二、Critical(共识分歧,按可达性排序)

### C1 · RLP 长形态长度前缀的前导零未拒绝 —— **当前即可构造**
`bcos-codec/bcos-codec/rlp/RLPDecode.h:92-113` 只查 `payloadLength < 56`,不查长度字节自身前导零。
op-geth `rlp/decode.go:1098-1114` 的 `readUint`:`if buffer[start] == 0 → ErrCanonSize`。**控制器已打开两侧原文核对。**

失效场景:`0x7e f9 01 04 …` → `0x7e fa 00 01 04 …`(payload 一字不动)。本实现解出**完全相同**的 `DepositTx`;而 `computeOpTxRoot`(`OpEngineSeam.h:172-185`)对**原始线上字节**建根 —— 攻击者令 `payload.transactionsRoot` 与非规范字节自洽即可:**本实现 VALID,op-geth INVALID。**

连带证伪:spec §6.4 (n) 与 `OpEngineSeam.h` 两处"已拒绝 Go 会拒的一切非规范编码"的断言。
测试为何打不到:`OpSchedulerImplTest.cpp:425-476` 两个手写 writer 的注释**明写**"non-canonicality 只放 payload 不放 framing"——界当时就划了,没人回头问为什么。

修复位置需裁定(P2 CONCERNS 2):改共享的 `RLPDecode.h::decodeHeader` 会碰到本闭环之外的既有以太坊消费者(P2 倾向此项,认为对它们是修正而非回归);在 OP 侧再复制一层则把"严格层两份"变三份。**控制器倾向 P2 方案**——Go 的行为就是这一条,共享层修正才是根治。

### C2 · EIP-7702 授权项 `yParity` 位宽错 —— **当前即可构造**
`OpSchedulerImpl.h:477` `auth.v = decodeU256Scalar(...)`,op-geth `core/types/tx_setcode.go` 是 **`uint8`**。
`yParity` 编成 `0x82 0x01 0x00` 时:本实现在 `OpTransition.cpp:67` `continue` 跳过该授权、块可 VALID;op-geth 报 `rlp: input string too long for uint8`,整笔解不出 → INVALID。

### C3 · -38005 闸放行 pre-Isthmus V3 后必判 INVALID —— **spec 自相矛盾**
**控制器独立核实,结论强于 P1 原报**:
- spec:64 目标范围 = "newPayloadV4 + FCU V3(**Isthmus/Jovian**)" → pre-Isthmus **不在范围内**;
- spec:219 却把闸写成双向"Isthmus+ 禁 V3,**pre-Isthmus 禁 V4**" → 闸**故意放行** pre-Isthmus V3;
- 代码忠实实现 :219(`EngineServiceImpl.h:694` `isthmusActive != (version==4)`,pre-Isthmus+V3 → `false != false` → 放行),随后撞上 `.cpp:219-225` **无条件**要求 `withdrawalsRoot` → INVALID;
- op-geth `api_optimism.go` 反向:pre-Isthmus **必须** `withdrawalsRoot` 为 nil。

**要害是桶错了**:超出支持范围应答"我处理不了"(-38005),而不是"这个块是坏的"——与 §4.3"存储故障绝不能报 INVALID"同一条纪律。最小修法约 3 行:闸对 pre-Isthmus 一律 -38005。

### C4 · `baseFeePerGas` 两侧各有一个缺陷 —— **两个视角结论相反,控制器核实后判定互补**
- **欠拒绝(P1 C1)**:`Types.h:92` 声明为 `u256`,`EngineServiceImpl.cpp:334` 与 `.h:978` **原样透传**,静态校验(`.cpp:185-302`)与六项比对面(`.h:1044-1098`)**全文无该字段**。op-geth `consensus/misc/eip1559/eip1559.go:53-56` 强制 `expectedBaseFee` 与父块公式相等 → **放行 op-geth 会拒的块**。
- **过拒绝(P5 I-7)**:`OpSchedulerImpl.h:238` `narrowU256ToU64(env.baseFeePerGas, ...)` 把 `≥2^64` 拒掉,而 op-geth `beacon/engine/types.go:301` 只限 **256 位** → **拒掉 op-geth 会接受的块**(实战不可达)。

两条同时成立,方向相反,分别落在 engine 层与 scheduler 层——**这正是把正确性拆成三个视角的收益,任一单独视角只会看到一半**。
零测试覆盖:gate 测试 `EngineNewPayloadGateTest.cpp:534` 直取向量 baseFee,变异矩阵无该项 → "33/33 全绿"对此**零信息**。
P1 已拆掉当初"缺前置条件"的免责:批 4 打通父头读路后(`EngineServiceImpl.h:875-885`),`CalcBaseFee` 所需五字段已备齐。

### C5 · 本地故障被改写成共识拒绝
`bad_alloc` 等被 `OpSchedulerImpl.h:839-873` 的 `catch(...)` 改写成 `ConsensusError` → engine 判 **INVALID**。
engine 层那两道 -32603 防线**对该路径是死代码**。违反 §4.3 最核心的一条纪律:本地故障绝不能让本节点对区块投反对票。
修复点在视角 2/3 的文件,engine 层单独修不了。

### C6 · legacy / 0x01 交易硬拒
op-geth 正常处理的块本实现判 INVALID。**若认定属"最小闭环既定范围"则可降级为记账**,但 spec:311 的"唯二 op-geth 能处理而本实现硬拒"**不实**(实为三条,且第三条触发频率最高)——**这条措辞必须改,无论定性如何**。

---

## 三、生产接入即触发(E-b 测试世界内不可达)

| # | 位置 | 后果 |
|---|---|---|
| P1 | `Storage2Ledger.h:682` `probeHasStorage` **不判零值** | 同文件 `:486-489` 自陈"生产 `HostContext::set` 对零值照写不删,真实链账户表必然含零值槽行" → `has_storage` 误真 → `state.cpp:259 .has_initial_storage` → `host.cpp:88 is_create_collision` → **同一 CREATE2 在 op-geth 成功、在本桥 INVALID** |
| P2 | `/apps/` 下任一非 20 字节 hex 表名(BFS 建的表) | **每个 OP 块永久 -32603** |
| P3 | `SYS_NUMBER_2_BLOCK_HEADER` / `SYS_NUMBER_2_TXS` 同样不写 | 通用"按块号取块"对 OP 块**抛异常**;§6.4 (f) 只记了 `SYS_HASH_2_TX` |
| P4 | 两张 OP 专用表不在 `Ledger.cpp:2001-2012` 创世表清单(批 6 复审 M-6) | `storageTool` / `archiveTool` 对其不可见 |
| P5 | `EngineEndpoint.cpp:164` + `rawTransactions` **全仓零生产赋值点**(§6.4 条目 q) | op-node 通路**生产上完全断开**——实连第一道墙 |

P3 判定 `probeHasStorage` 三行可修且能配翻红断言,建议本期修;其余记账。

---

## 四、静默漂移(将来有人改动才触发,但无任何诊断)

**P4 I-1 是本轮组织类唯一的结构性发现**:engine 对 `SchedulerType` 有 **11 条要求,只有 1 条被 `c_opMode` 编译期探测**(`EngineServiceImpl.h:186` 只探 `executeOpBlock`),`OpEngineSeam.h:5-26` 自称"公开接缝面"却只公开 3 条。

- 给 `OpBlockCommitments` 加第 9 个成员 → `EngineServiceImpl.h:1052-1093` 的 else-if 链**一字不改继续编译通过,新承诺字段永不比对 → 错判 VALID**;
- 给 `OpBlockEnv` 加字段 → engine 静默传 0 → **好块判 INVALID**。
- §6.4 (i)② 只覆盖了反方向。

**P4 I-2 / P2 Imp-3 是同一个病的两处**:`decodeEip1559Tx`/`decodeSetCodeTx` 是 40 行克隆、5 处校验各写两遍;规范性严格层在 `OpSchedulerImpl.h` 与 `EthBlockHeader.cpp` 被复制成两份独立实现,六条严格性用例只测了其中一份。**这正是 `a47b00e78` yParity 事故的同一结构**;P4 预言下一个只补一边的会是 op-geth 的 `ErrTipAboveFeeCap`。

**P4 I-1 的修法待实测**:`requires std::derived_from<typename S::ConsensusError, std::exception>` 这类嵌套 requires 在无该成员的 `SchedulerSerialImpl` 上是软失败还是硬错——本分支在 `c0288b8b0` 踩过同类坑,必须实测。

---

## 五、测试判别力(P5)

- **I-1 两个字段的映射零守护**:全 35 份金值文档里 `env.currentRandom` **恒为 `0x0`**、`env.currentCoinbase` **恒为 `0x4200…0011`**。唯一映射断言是 `encode()==golden.encodedHeaderHex`;`EngineOpBranchTest.cpp:428-433` **明文声明自己拦不住映射错误**;`EthBlockHeaderTest` 根本不经过 `rebuildOpEthHeader`。→ `EngineServiceImpl.cpp:321/:333` 换成硬编码常量,**239 例应全绿**。`prevRandao` 连执行腿也零守护(无向量读 `PREVRANDAO`)。**这正是批 5 语料重生成要解决的,现在有了精确的量化。**
- **I-2 静态校验 14 条拒绝中 8 条零覆盖**,其中 **4 条回归后是空 optional 解引用(UB)而非错判**——因为用的是 `operator*` 而非 `.value()`,分类屏障接不住。
- **I-3 链式对承载全套最独特的覆盖**(`number`/`timestamp`/`baseFee` 的唯一支点 + timestamp 单调的唯一金值锚)**却无任何 provenance**,生成器显式丢弃 `opGethCommit`。
- **I-5 §6.4 (j) 那类信息湮灭还有第二、三处且未记账**,三个测试已自证不可区分。

---

## 六、文档失实(双向)

**本轮最值得记的元结论:文档在两个方向上都不准 —— 既宣称了不存在的覆盖,也记了不存在的欠账。**

宣称了不存在的覆盖:
- §6.4 (n) "已拒绝 Go 会拒的一切非规范编码" → 被 C1 证伪
- spec:311 "唯二 op-geth 能处理而本实现硬拒" → 被证伪两次(C6、P5 I-7)
- §7.5 "子串匹配已全部移除" → 假(批 4 新增两处)
- §8 验收数字仍 225/225(实为 239/239)
- CMake 注释与 §6.4 (i)④ 共同断言的"仓库根无无扩展名文件" → 被 `LICENSE` 证伪(**结论对、论据错**;论据错会让下一个在根加 `version`(真实 C++20 标准头名)的人误判)

记了不存在的欠账(**四条应删/下修**):
- "`EthBlockHeader::decode` 靠测试二进制传递链接、生产接入会爆炸" → **不成立**。基线 `42e62fcef` 的 `rlp/` 只有 3 个 `.h` 无 `.cpp`;本分支新增 `bcos-codec/CMakeLists.txt:24` 已把它编进 `codec`,`bcos-ledger/CMakeLists.txt:27` 链 codec 为 PUBLIC、`engine/CMakeLists.txt:14` 链 ledger 为 PUBLIC → 传递可得。**控制器复核确认。** 残留仅"engine 未具名 codec"这条弱隐患(一行可修)。
- "gasLimit 变化率未校验" → **不适用**。op-geth `eip1559.go:37-42` 的 `if !config.IsOptimism()` 明确放行 OP 链瞬时调整,本实现只查 `≤2^63-1` 是**正确对齐**。
- "excessBlobGas 恒钉 0 是简化" → **正确**。`CalcExcessBlobGas` 对 OP 短路 `return 0`,Jovian 也不例外。
- 条目 (r)/(s) 后果被高估 → (r) `LedgerMethods.h:235` 确为未判 `has_value` 的解引用,但守门是 `:217-218` 的 `SYS_HASH_2_TXS` 行,而 `registerOpBlock` 连 `SYS_NUMBER_2_TXS` 都不写 → 对 OP 块**结构性不可达**,台账"会先由 OP 块暴露"的**因果是反的**;(s) 双回调形态确凿,但 double-resume 需异常从 `handle.resume()` 逃逸,而 `bcos-task/Task.h:62-70` 有 continuation 时已捕获、`syncWait`(`Wait.h:56-91`)根任务全包 try/catch → 后果是**错误分类而非 UB**。

依赖方向零违反:`engine/` 内所有 `bcos-evm` 命中均为注释,无一条 `#include`。

---

## 七、建议的修复批次

| 批 | 内容 | 依据 |
|---|---|---|
| **7** | C1 + C2 + C3 + C5(四条可构造的共识分歧)+ C4 两侧 | 全部有明确 op-geth 出处与最小修法;C1 需先裁定修共享层还是 OP 层 |
| **8** | P4 I-1 接缝探测收紧 + I-2/Imp-3 去重(解码器与严格层各一份) | 需先实测嵌套 requires 是否软失败 |
| **9** | `probeHasStorage` 零值(三行 + 翻红断言);其余生产接入项记账 | P3 判定本期可修 |
| **5**(既有,阻塞) | 语料重生成:`currentRandom`/`currentCoinbase` 多取值 + 链式对 provenance + 刷新 `SHA256SUMS` | P5 I-1/I-3 把它从"建议"变成"有量化依据的必需" |
| **文档** | 上述六节的失实逐条修正;§11 追加批 6 复审的三句;删除四条不成立的欠账 | 与代码批次分开,零生产改动 |

**§11 待追加三句(批 6 复审提出,控制器采纳)**:(a) 报告必须点名**哪个目录是红绿见证**;(b) **依赖图为空的目录属空真,不得计入证据**;(c) **每批至少一个目录构成红绿见证**。
