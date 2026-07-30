# 视角 2 复审报告 · 交易解码、区块执行与编解码

分支 `feat-op-validator-loop`,范围 `42e62fcef..HEAD`。**本次复审只读**:未构建、未跑测试、未修改任何生产/测试文件。所有需要构建才能确认的项都标了 `[需验证]` 并附最小验证步骤。

对照基准:op-geth `/Users/octopus/octo/code/blockchain-impl/op-geth`(`rlp/decode.go`、`core/types/{block,tx_setcode,deposit_tx,transaction_marshalling}.go`、`consensus/beacon/consensus.go`、`params/protocol_params.go`),均已逐段打开核对。

---

## 结论摘要

| 级别 | 数量 | 一句话 |
|---|---|---|
| Critical | 3 | 规范性严格层漏了一整类(RLP 长度前缀前导零),op-geth 判 INVALID 的块本实现判 VALID;7702 授权项 `yParity` 宽度不匹配,同类分歧;legacy/0x01 交易被硬拒 —— op-geth 正常处理的块本实现判 INVALID |
| Important | 3 | 块 gas 池窄化的守卫跨模块(调度器自身不设防);回执映射丢字段但已落账本;规范性严格层被复制粘贴成两份互不相干的实现 |
| Minor | 3 | 类型字节错误消息用十进制冒充十六进制;deposit 回执 nonce/version 无分叉门控(当前范围内正确);`OpFork::Karst` 无护栏(但不可达) |

**正面核实**(排除了两条视角问题清单点名怀疑的假绿):

- **块头 21 字段没有"读反+写反互相抵消"**。`EthBlockHeader::decode` 不是只跟 `encode()` 对拍:`bcos-evm/test/opstack/EthBlockHeaderTest.cpp:231/246/269` 把 **op-geth 产出的 golden `encodedHeaderHex`** 同时喂给 `decode()` 与 `encode()`,并断言 `decode()->encode()` 恒等 + `hash() == golden.blockHash`,33 条向量全覆盖。字段顺序我又独立对了一遍 op-geth `core/types/block.go` 的 `Header`:parentHash/ommersHash/coinbase/root/txHash/receiptHash/bloom/difficulty/number/gasLimit/gasUsed/time/extra/mixDigest/nonce/baseFee/withdrawalsHash/blobGasUsed/excessBlobGas/parentBeaconRoot/requestsHash —— 与 `EthBlockHeader.h:37-71` 逐字段一致。顺序是外部锚定的,不是自洽的。
- **签名规则没有第二份复制**。`requireLowSSignature`(`OpSchedulerImpl.h:207-215`)是外层交易签名的唯一实现,两个 typed 解码器都经 `recoverTxSender`(`:497-511`)进入它,只有一处。7702 授权项的 `s <= n/2`/`v <= 1` 是另一套规则、另一条码路(`OpTransition.cpp:50-90`),两者不是同一规则的两份拷贝。(真正的复制粘贴在别处,见 Imp-3。)
- **`setcode` 的 `to == nil`、空 `authorizationList`**:本实现在解码期放行、在 `validate_transaction`(`bcos-evm/bcos-evm/eth/state/state.cpp:449/451-452`)拒绝;op-geth 在解码期拒绝(`SetCodeTx.To` 是非指针 `common.Address`)。**阶段不同,判决相同(都 INVALID)**,不是分歧。
- **deposit 的 `mint` nil vs 0**:RLP 上不可区分,本实现一律解成 `optional(0)`;`runDeposit` 无条件 `get_or_insert(dep.from)` 已经 touch 了账户,与 op-geth `AddBalance(0)` 的 touch 效果一致。不是分歧。
- **gas 池不会走负**:`processOpBlock` 把 `blockGasLeft` 传给 `validate_transaction`,超额返回 `GAS_LIMIT_REACHED`(`OpBlockExecute.cpp:56/76`,`OpDepositTx.cpp:88/95`),配合 `narrowGasLimit` 挡住负 `gas_limit`,`cumulative` 因此恒非负,`static_cast<uint64_t>(result.gasUsed)`(`OpSchedulerImpl.h:925`)安全。
- **`configAt` 用 `>=`**,`[jovianTime, +inf) -> Jovian`(`OpForkSchedule.cpp:107-109`),与 op-geth `isTimestampForked` 的 `s <= head` 语义一致;`isIsthmusActiveAt`/`isJovianActiveAt` 同样 `>=`。边界时间戳归属正确。`configAt` 的 sub-isthmus 分支被 `handleOpNewPayload` 的 -38005 门(`EngineServiceImpl.h:692-702`,`isthmusActive != (version == 4)` 双向)封死,不可达。

---

## Critical

### C-1 RLP 长度前缀的前导零未被拒绝 —— 规范性严格层漏了一整类,`computeOpTxRoot ≡ DeriveSha` 的等价性今天不成立

**位置**:`bcos-codec/bcos-codec/rlp/RLPDecode.h:65-84`(长字符串 0xB8–0xBF)与 `:92-113`(长列表 0xF8–0xFF)
**波及**:`OpSchedulerImpl.h` 的全部解码入口(`readCanonicalScalar:300`、`readFixedWidth:322`、`enterList:423`、`decodeBytesField:400`)、`EthBlockHeader.cpp` 的全部字段读取器(`:35-132`)
**已记账?** 否 —— 这是新发现。spec §6.4 条目 (n)(第 319 行)把"规范性严格层"逐条列成 **「禁前导零 / u64 ≤8 字节 / address·hash 等长 / bool 仅空串与 `0x01`」**,**这个枚举本身是不完整的**,漏掉的正是本条。

**op-geth 怎么做的**(`rlp/decode.go` `readKind`/`readUint`,我已打开原文):

```go
case b < 0xC0:   // 长字符串
    size, err = s.readUint(b - 0xB7)
    if err == nil && size < 56 { err = ErrCanonSize }
default:         // 长列表
    size, err = s.readUint(b - 0xF7)
    if err == nil && size < 56 { err = ErrCanonSize }

func (s *Stream) readUint(size byte) (uint64, error) {
    ...
    if buffer[start] == 0 {
        return 0, ErrCanonSize      // ← 长度字节自身的前导零
    }
```

**本实现怎么做的**(`RLPDecode.h:92-113`,列表分支,字符串分支同形):

```cpp
const auto lenOfLen{byte - LONG_LIST_HEAD_BASE};
auto payloadSize = fromBigEndian<uint64_t, bcos::bytesConstRef>(from.getCroppedData(0, lenOfLen));
header.payloadLength = payloadSize;
from = from.getCroppedData(lenOfLen);
if (header.payloadLength < 56)          // ← 只有这一条
    return {... NonCanonicalSize ...};
```

`payloadLength < 56` 有,`长度字节前导零` **没有**。

**具体失效场景(逐字节)**。取本仓自己的金值向量 `bcos-evm/test/opstack/t8n/golden/engine/isthmus_transfer_basic.golden.json` 的第 0 笔(L1 attributes deposit),其规范字节以 `0x7e f9 01 04 a0 6a b9 67 df …` 开头(`0xf9` = 长列表,lenOfLen=2,payload 长 0x0104 = 260)。

把外层列表头改写成 **`0xfa 0x00 0x01 0x04`**(lenOfLen=3,长度字节带一个前导零),其余 260 字节一字不动:

| | 本实现 | op-geth |
|---|---|---|
| 解码 | `decodeHeader`:`0xfa > 0xf7` → lenOfLen=3 → `fromBigEndian(00 01 04)=260` → `260 >= 56` → **接受**;字段逐个解出,与规范形态**完全相同**的 `DepositTx` | `readKind` → `readUint(3)` → `buffer[start]==0` → **`ErrCanonSize`**;`Transaction.UnmarshalBinary` 失败 |
| 执行 | 正常执行,产出正常回执 | 从不执行 |
| txRoot | `computeOpTxRoot`(`OpEngineSeam.h:172-185`)对**线上原始 265 字节**建 trie → 与规范形态的 264 字节**不同的 txRoot** | `DeriveSha` 从解析后结构体**重新规范编码** → 只可能得到规范形态的 txRoot |
| 块头 | `rebuildOpEthHeader` 用上面那个 txRoot 重建 → payload 若按同一口径算出 `blockHash`,step 2 的 `ethHeader.hash() != payload.blockHash` **通过** | payload 直接被拒 |
| **判决** | **VALID** | **INVALID** |

即:同一串 payload 字节,两个实现给出相反判决 —— 共识分歧。攻击面是任何能影响 batch/payload 字节的角色(恶意 sequencer/batcher)。同样的改写可施加在**每一个** ≥56 字节的嵌套字段上(deposit 的 `data`、1559 的 `data`/`accessList`、setcode 的 `authorizationList`),不止外层信封。

**为什么现有测试打不到**:`OpSchedulerImplTest.cpp:425-476` 的两个手写 RLP writer(`rlpString`/`rlpList`)在注释里明确写了 **「the non-canonicality lives in the payload the test hands them … never in the framing itself」** —— 用例 (l)–(q)(`:966`–`:1016`)全部是**字段载荷**层面的非规范,**框架层(长度前缀)非规范被结构性排除在外**。33 条金值向量更打不到(共享上下文已说明:可信生成器只产规范字节)。

**连带的文档不实**:`OpEngineSeam.h:156-163` 与 `OpSchedulerImpl.h:900-908` 两处互指的注释都断言「解码器 reject every non-canonical encoding Go's `rlp` rejects」,spec §6.4 (n) 同文。**该断言今天为假**,并且它正是 (n) 所说"放宽解码层会无声破坏 blockHash 一致性"的既成事实版本 —— 不是被放宽,是从来没写全。

**修法(最小)**:在 `RLPDecode.h` 两个长形态分支里、`fromBigEndian` 之前加

```cpp
if (from[0] == 0) return {BCOS_ERROR_UNIQUE_PTR(NonCanonicalSize, "NonCanonicalSize"), Header()};
```

(`lenOfLen >= 1` 且已校验 `lenOfLen <= from.size()`,取 `from[0]` 安全。)注意 `decodeHeader` 是**通用**基础设施,还有 `Web3Transaction` 等既有消费者;若担心向前兼容,替代方案是在 `OpSchedulerImpl.h` 新增一个 OP 专用的 `decodeHeaderStrict` 并让 `readCanonicalScalar`/`readFixedWidth`/`enterList`/`decodeBytesField` 全部改走它 —— 但那会把 Imp-3 的复制问题再放大一层。**建议改共享层**:Go 的行为就是这一条,放宽它对任何以太坊语义的消费者都是错的。

**`[需验证]` 最小验证步骤**:
1. 在 `bcos-evm/test/opstack/OpSchedulerImplTest.cpp` 的 `DepositFieldEncodings::envelope()` 旁加一个变体,把 `rlpList` 产出的 `0xf8 LL` 改写成 `0xf9 0x00 LL`(payload 不变);
2. `cmake --build <build> --target bcos-evm-test`,跑 `--gtest_filter=OpSchedulerImpl.*`;
3. **期待现象(修复前)**:该用例**不抛异常**、解码成功 —— 即证实缺口。修复 `RLPDecode.h` 后同一用例应抛 `OpConsensusError`,且既有 33/33 金值 gate 与 `EthBlockHeaderTest` 全部保持绿(规范输入不受影响)。

---

### C-2 EIP-7702 授权项 `yParity` 宽度与 op-geth 不匹配 —— 本实现"跳过该授权后 VALID",op-geth"整笔交易解不出 → INVALID"

**位置**:`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:477`
**已记账?** 否 —— 新发现。

```cpp
auth.v = decodeU256Scalar(entryBody);      // 最宽 32 字节
```

`evmone::state::Authorization::v` 是 `intx::uint256`(`bcos-evm/bcos-evm/eth/state/transaction.hpp:30`),而 op-geth 的 `SetCodeAuthorization`(`core/types/tx_setcode.go`)是:

```go
type SetCodeAuthorization struct {
    ChainID uint256.Int
    Address common.Address
    Nonce   uint64
    V       uint8          // ←
    R       uint256.Int
    S       uint256.Int
}
```

**`V uint8`**。一个 **≥2 字节** 的 `yParity` 在 op-geth 是 **RLP 解码错误**(`rlp: input string too long for uint8`),整笔 `SetCodeTx` `UnmarshalBinary` 失败 → payload 的交易列表解不出 → `newPayload` 返回 **INVALID**。

本实现:`decodeU256Scalar` 接受,后续 `process_authorization_list`(`OpTransition.cpp:67`)看到 `auth.v > 1` **`continue` 跳过这一项**,交易其余部分正常执行、产出正常回执,块可以判 **VALID**。

**具体失效场景**:一笔 0x04 交易,`authorizationList` 的某一项 `yParity` 编码为 `0x82 0x01 0x00`(= 256,规范编码,无前导零)。

| | 本实现 | op-geth |
|---|---|---|
| 解码 | `auth.v = 256`,交易完整解出 | `rlp: input string too long for uint8` → 整笔失败 |
| 执行 | 跳过该授权(EIP-7702 skip 语义),其余照常 | 从不执行 |
| **判决** | **VALID**(六项比对面全部自洽) | **INVALID** |

**边界要说清楚**:`yParity` 只占 1 字节时(如 `0x02`)**两边一致** —— geth 解出 `V=2`,`Authority()` 的 `ValidateSignatureValues` 返回 false,`applyAuthorizations` 注释明写 "errors are ignored, we simply skip invalid authorizations",与本实现的 `continue` 同义。分歧**只在 `yParity >= 256`**。窄,但确实是同一串字节两个判决。

顺带核对(**全部一致,不是发现**):`auth.chain_id`/`auth.r`/`auth.s` geth 是 `uint256.Int`(≤32 字节),本实现 `decodeU256Scalar` 也是 ≤32;`auth.nonce` 两边都是 uint64。外层交易的 `yParity` 本实现在 `:583`/`:630` 显式 `> 1` 拒绝,geth 走 `recoverPlain`→`ValidateSignatureValues` 拒绝,判决一致。

**修法**:`decodeAuthorizationList` 里把 `auth.v` 改为 `intx::uint256{decodeU64Scalar(entryBody)}` 是不够的(uint64 仍宽于 uint8);应新增一条 `readCanonicalScalar(entryBody, 1, "authorization.yParity")` 路径,或直接沿用 `decodeBoolField` 的宽度约束再放宽到"0/1 两值 + 空串"。注意与 op-geth 对齐的是**宽度**(≤1 字节 ⇒ 解码失败),不是**取值**(0/1 ⇒ 跳过),两者语义不同,不能合并成一个检查。

**`[需验证]` 最小验证步骤**:在 `OpSchedulerImplTest.cpp` 现有 setcode 用例基础上,把某个授权项的 `yParity` 字段编成 `0x82 0x01 0x00`,断言 `decodeOneRawTx` 抛 `OpConsensusError`。修复前该断言失败(解码成功)。

---

### C-3 legacy / 0x01(access list) / 0x03(blob) 交易被硬拒 —— op-geth 能正常处理的块,本实现判 INVALID

**位置**:`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:648-676`(`decodeOneRawTx` 三分支 dispatch,`:674` 兜底 throw)
**已记账?** **否** —— 且它直接**证伪** spec §6.4 rev.3.3 引言(第 311 行)的断言:

> 「前两条是 op-node 实连的最高优先级前置——它们是本清单里**唯二**"op-geth 能正常处理、本实现硬拒或误答"的路径」

**不是唯二,是三条。** 而且第三条比 (k)/(l) 触发频率高一个量级:(k)/(l) 需要先有一个坏块或一次重组才发生,本条**只要块里有一笔 legacy 或 0x01 交易就发生**,而 legacy 交易在 OP 主网上是绝对多数常态流量。

**具体失效场景**:任何一个含 legacy 交易的真实 OP 块。legacy 信封没有类型字节,首字节是 RLP 列表头(≥0xC0),`decodeOneRawTx` 三个分支都不匹配 → 抛 `OpConsensusError` → engine 映射为 **INVALID + latestValidHash = parent**(`EngineServiceImpl.h:994-999`)。op-geth 正常执行、返回 VALID。0x01(EIP-2930)同理。

代码注释(`:250`、`:649-650`)把这解释为「matching the t8n corpus」,这准确描述了**当期实现范围**,但它是"最小闭环"的一个**未落账的功能缺口**,不能只活在注释里 —— 因为 §6.4 是本项目声明的"防绿灯误读"清单,而该清单当前明确宣称这类路径只有两条。

**修法**:实现层面就是补 `decodeLegacyTx`(含 EIP-155 与 pre-155 双形态的 `v`/chainId 推导)与 `decodeAccessListTx`(0x01),`evmone::state::Transaction::Type` 已经有对应枚举。0x03(blob)在 OP L2 上 op-geth 本身也拒绝,可以继续拒但应给出与其一致的理由。**至少**:立刻进 §6.4 台账并修正"唯二"的表述。

---

## Important

### Imp-1 `toBlockInfo` 的块 gas 池窄化没有自守 —— 唯一守卫在另一个模块里

**位置**:`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:237`

```cpp
blk.gas_limit = static_cast<int64_t>(env.gasLimit);   // env.gasLimit 是 uint64_t
```

同一个文件在 `:150-190` 花了 40 行注释论证"本仓有静默截断事故史,必须 widen → explicit check → narrow",并为此写了 `narrowU256ToU64` 与 `narrowGasLimit` —— 然后在 `:237` 对**块级** gas 上限做了裸 `static_cast`。

唯一的守卫在 `engine/bcos-engine/EngineServiceImpl.cpp:269-273`(批 2 B2-3 加的 `gasLimit > 2^63-1` 检查,对齐 op-geth `params.MaxGasLimit` / `consensus/beacon/consensus.go:262`)。也就是说:**这个不变式跨了模块边界**,`OpSchedulerImpl` 作为一个可独立调用的公开组件(测试就在直接调 `executeOpBlock`)对此毫无自守。

**当前后果是 fail-closed 而非 fail-open**:`gasLimit >= 2^63` ⇒ `blockGasLeft` 为负 ⇒ 首笔 deposit 的 `validate_transaction` 返回 `GAS_LIMIT_REACHED` ⇒ `runDeposit` 抛 → INVALID。所以今天不是漏洞,是**防线位置错了**:`EngineServiceImpl.cpp:261-268` 的注释自己也承认「No acceptance surface changes … what changes is that it is rejected for the stated reason instead of by accident」。一旦将来 builder 侧或别的组合根调用 `executeOpBlock`,守卫就没了。

**修法**:`toBlockInfo` 里改成 `blk.gas_limit = static_cast<int64_t>(narrowU256ToU64(...))` 的同形态检查,或直接复用 `narrowGasLimit(env.gasLimit, "OpBlockEnv::gasLimit")`。engine 侧那条保留(它给出更好的 validationError 文案)。

### Imp-2 回执映射丢字段,而这些回执是**要落库**的

**位置**:`bcos-evm/bcos-evm/engine/OpReceiptMap.h:72-81`,消费者 `EngineServiceImpl.h:1228-1237`(写 `SYS_HASH_2_RECEIPT`)

`mapOpReceipt` 只映射 `status`/`gasUsed`/`logs`,并且:

- **`cumulative_gas_used` 完全丢弃**。`evmone::state::TransactionReceipt::cumulative_gas_used` 已被 `processOpBlock`(`OpBlockExecute.cpp:60/87`)正确累加、并且是 `receiptsRoot` 的输入(`OpReceiptEncode.cpp:15`),但落库的 FISCO 回执里没有它。以太坊 `eth_getTransactionReceipt` 的 `cumulativeGasUsed` 因此**无法从本表答出**。
- **`contractAddress` 恒空**。金值语料里 `isthmus_contract_create` / `jovian_contract_create` 就是合约创建 deposit,落库回执的合约地址是空的。
- **`output`/`blockNumber` 恒为空/0**。
- **`status` 把 evmc 的全部失败码塌成 1**(`:36-37`)。

文件头注释(`:5-21`)把这解释为 brief 规定的窄范围,并把"OP meta 字段(deposit_nonce/l1_fee/operator_fee/da_footprint)"挂到 §2 非目标表下 —— **那一半确实有台账**。但 `cumulativeGasUsed`/`contractAddress`/`output` **不是 OP meta,是普通以太坊回执字段**,它们不在 §6.4 的任何一条里,而回执**确实被写进了账本**(`registerOpBlock` 是五张表之一)。

**为什么不是 Critical**:六项比对面的 `receiptsRoot` 走的是 `sealOpBlock` → `encodeReceiptForRoot` → **evmone 原始回执**,完全绕开 `mapOpReceipt`。所以共识判决不受影响。受影响的是**账本内容的正确性**:一个被判 VALID 的块,其落库回执是残缺的。

**修法/记账**:要么把 `mapOpReceipt` 补全到 `cumulativeGasUsed` + `contractAddress`(两者在 `evmone::state::TransactionReceipt` / `OpTxReceipt` 里都拿得到),要么进 §6.4 台账并写明"OP 块的 `SYS_HASH_2_RECEIPT` 内容是残缺的,不可被任何 RPC 直接服务"。**当前两者皆无**。

### Imp-3 规范性严格层被复制粘贴成两份互不相干的实现

**位置**:`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:300-335`(`readCanonicalScalar` / `readFixedWidth`)与 `bcos-codec/bcos-codec/rlp/EthBlockHeader.cpp:35-132`(`expectString` / `decodeFixed` / `decodeScalarBytes`)

两处是**同一套规则的两份独立实现**:同样的"list → 报错"、同样的"payloadLength > maxBytes → 报错"、同样的"payloadLength > 0 && in[0] == 0 → 报错"、同样的"固定宽度必须精确相等"。`EthBlockHeader.cpp:30-33` 的注释明确说明这是**有意**保持局部的("RLPDecode.h's generic overloads have other consumers whose behaviour must not change")—— 动机成立,但代价是:

**C-1 的修复必须同时打到两处才完整**。更普遍地,这正是共享上下文点名的"测了一处不能说明另一处"模式:`OpSchedulerImplTest.cpp` 的 (l)–(q) 六条严格性用例**只测 `OpSchedulerImpl.h` 那份**;`EthBlockHeaderTest.cpp` 对应的负例(错宽度/前导零)我没有找到 —— 它只有 33 条 golden 的正向往返。

同类复制还有(优先级更低,均已在注释里交代动机,列出以备后续统一):
- `narrowU256ToU64`:`OpSchedulerImpl.h:154`(抛异常) vs `EngineServiceImpl.cpp:166`(返回 optional),两份;
- secp256k1 半阶常量:`OpSchedulerImpl.h:209`、`OpTransition.cpp:28`、`eth/state/state.cpp:21`、`test/opstack/T8nReplayHarness.h`,四份;
- `process_authorization_list`:`OpTransition.cpp:50-90`(OP 路径,真 ecrecover) vs `eth/state/state.cpp:92-176`(通用路径,`if (!auth.signer.has_value()) continue;` —— 从不 ecrecover 的上游测试实现)。**两份规则集逐条对过,OP 那份是正确的**;但通用那份如果哪天被 OP 路径误用,授权签名等于不校验。属既有代码,不是本分支引入。

---

## Minor

### Min-1 不支持类型字节的错误消息把十进制当成十六进制

`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:674-675`:

```cpp
throw OpConsensusError("OpSchedulerImpl: raw tx decode: unsupported tx type byte 0x" +
                       std::to_string(static_cast<unsigned>(typeByte)));
```

`std::to_string` 产十进制。类型字节 `0xff` 被报成 **`0x255`**,`0x7f` 报成 `0x127`。`EngineOpBranchTest.cpp:884` 只做前缀断言,所以永远测不到这条尾巴。运维照着这个消息去查 EIP-2718 类型表会查错。

### Min-2 deposit 回执的 `depositNonce`/`depositReceiptVersion` 无分叉门控

`bcos-evm/bcos-evm/opstack/OpReceiptEncode.cpp:9-18` 无条件把 `deposit_nonce` 与 `deposit_receipt_version` 编进 receipt RLP。按 op-geth,`DepositNonce` 是 **Regolith+**、`DepositReceiptVersion` 是 **Canyon+** 才出现的。

**当前范围内是正确的**:-38005 门(`EngineServiceImpl.h:692-702`)保证只有 Isthmus+ 时间戳能进 OP 分支,Isthmus 远在 Canyon 之后,两个字段恒应出现。33/33 金值 gate 也锚住了这一点。列为 Minor 是因为它是一个**靠外部门控成立的隐式前提**,`OpReceiptEncode.cpp` 自身既没有 `cfg` 参数也没有注释说明这个依赖 —— 若将来放开 pre-Isthmus,这里会静默产出错的 receiptsRoot。属既有代码(不在本分支 diff 内)。

### Min-3 `OpFork::Karst` 没有"别当成已适配"的护栏

`bcos-evm/bcos-evm/opstack/OpForkSchedule.cpp:92-100` 的 `karstConfig()` 是导出的公开函数,注释说"temporarily identical to Jovian (see README)"。`configAt`(`:102-110`)只在 Isthmus/Jovian 之间选,`OpForkTimestamps`(`OpForkSchedule.h:45-49`)也没有 `karstTime` 字段 —— **所以 Karst 在本闭环里事实上不可达**,这是最强的护栏形式(结构性不可达优于运行时断言)。

但护栏是**隐式**的:`karstConfig()` 仍可被任何 TU 直接取用,而它与 `jovianConfig()` 唯一的差别只是 `c.fork = OpFork::Karst` 这个 tag。MEMORY 里记的"karstConfig 仅 jovianConfig 别名,勿称已适配"在代码里的唯一载体是那两行注释。建议:要么给 `karstConfig()` 加 `[[deprecated("Karst is a Jovian placeholder; not adapted")]]`,要么在 `configAt` 旁加一条 `static_assert`/注释显式声明 Karst 不参与解析。

---

## 逐条回答视角问题清单

1. **解码器严格性(逐交易类型 × 非规范编码)**

   | 非规范形态 | op-geth | 本实现 | 结论 |
   |---|---|---|---|
   | 整数前导零(`0x82 0x00 0x01`) | `ErrCanonInt` | `readCanonicalScalar:311` 拒 | ✅ |
   | 单字节 `0x00` 当整数 | `ErrCanonInt` | 同上(`decodeHeader` 单字节不前移,`in[0]==0`)拒 | ✅ |
   | 整数超宽(uint64 收 9 字节) | `errUintOverflow` | `readCanonicalScalar:308` 拒 | ✅ |
   | `0x81 0x05`(单字节应直接编码) | `ErrCanonSize`(`Stream.Bytes`) | `RLPDecode.h:53-63` 拒 | ✅ |
   | 长形态但 payload < 56 | `ErrCanonSize` | `RLPDecode.h:78-83/107-112` 拒 | ✅ |
   | **长形态长度字节带前导零** | **`ErrCanonSize`** | **接受** | ❌ **C-1** |
   | address/hash 宽度不等 | "input string too short/long" | `readFixedWidth:329` 拒 | ✅ |
   | bool 非 空串/`0x01` | "invalid boolean value" | `decodeBoolField:371-379` 拒 | ✅ |
   | 尾随字节 / 多余列表元素 | EOL 检查 | `expectExhausted:434` 拒(每个信封两处) | ✅ |
   | 内层元素声明长度超出外层 | `ErrElemTooLarge` | `enterList` 返回受限视图 + `decodeHeader:114` 拒 | ✅ 等效 |
   | **7702 `yParity` ≥ 2 字节** | **`too long for uint8`** | **接受后跳过** | ❌ **C-2** |
   | legacy / 0x01 / 0x03 | 正常处理(legacy/0x01) | 硬拒 | ❌ **C-3** |

2. **签名校验**:`requireLowSSignature`(`:207-215`)覆盖 `r,s ∈ [1,n-1]` 且 `s <= n/2`,与 op-geth `crypto.ValidateSignatureValues(homestead=true)` 逐条对齐;`yParity ∈ {0,1}` 在 `:583`/`:630`;chainId 在 `:566`/`:615`(op-geth 在 signer 层 `ErrInvalidChainId`)。**只有一处实现,无复制**。legacy 的 EIP-155/pre-155 双形态**不适用**(legacy 未支持,见 C-3)。7702 授权项的签名规则是另一套码路,见 Imp-3 末尾。

3. **0x04 / 0x7E 字段完整性**:两者字段逐个显式解出,**没有被默认值静默填充的字段**。三处刻意的"不填":①`auth.signer` 留 `nullopt`(`:466-465` 注释),使 `OpTransition.cpp:77` 必走真 ecrecover —— 正确且必要,若从线上字节填它就等于跳过授权签名校验;②`dep.mint` 解成 `optional(0)` 而非 nullopt —— RLP 上不可区分,无后果(见"正面核实");③deposit 的 `signedEnvelope` 留空 —— 符合 `OpBlockExecute.h:18` 契约。`to == nil` 语义:deposit 与 1559 用 `decodeOptionalAddressField`(`:409-419`)正确处理;setcode 也用了它(op-geth 那里是非指针,阶段不同判决相同)。

4. **块头 21 字段往返**:**无假绿**,详见"正面核实"第一条。补两点范围说明:(a) `decode()` 要求 21 个字段**全部在场**,而 op-geth 的 `baseFee` 及其后 6 个字段是 `rlp:"optional"` —— 本实现只读自己写的头,自洽;pre-Cancun 头会解失败,但不可达。(b) op-geth `e8800cffe` 的 `Header` 已经有第 22 个 optional 字段 `SlotNumber`(EIP-7843),本实现不发射 —— 尾部 optional 字段缺席在 RLP 上与 op-geth 的 nil 编码一致,当前无分歧;未来 OP 若激活 EIP-7843 需同步。

5. **`executeOpBlock` 异常边界**(`OpSchedulerImpl.h:805-928`):

   | 步骤 | 在 try 内? | 抛出后谁接住 → 分类 |
   |---|---|---|
   | step 1 解码循环(`:812-815`) | **否** | engine `catch (ConsensusError&)`(`EngineServiceImpl.h:994`)→ **INVALID**。端到端有用例(`EngineOpBranch.ConsensusErrorFromExecutionMapsToInvalid`) |
   | step 3 `processOpBlock`(`:836`) | **是**(`:834-873`,typed + `catch(...)` 成对) | poisoned 优先 → `OpStorageError`/-32603,否则 `OpConsensusError`/INVALID |
   | step 5 `visitAccounts`/`sealOpBlock`/`stateRootOf`(`:880-894`) | **否** | 三处 `poisoned()` 显式检查(`:888`/`:893`)覆盖了存储侧;evmone 侧异常逃逸 → engine `catch(...)`(`:1008`)→ **-32603** |
   | step 6 `computeOpTxRoot`/`mapOpReceipt`(`:909-919`) | **否** | 同上 → **-32603** |

   分类是完整的(engine 侧 `catch(...)` + `handleOpNewPayload` 的分类屏障两层兜底)。**残留代价已记账**:§6.4 (j) —— `catch(...)` 丢 `e.what()`,`OpBlockExecute.cpp` 四处块级 throw 抵达 engine 后共用一条 `validationError`。**我的独立判断:该裁定仍成立,优先级不应上调** —— 它损害的是可观测性,不是判决正确性,且真正修法(消除 RTTI 变通)不属本闭环。

6. **gas 记账**:块 gas 池窄化见 **Imp-1**;`blockGasLeft` 递减、`cumulative` 累加、deposit "不付费但计入 gasUsed" 三项均与 op-geth `state_transition.go` 一致,溢出/负值路径被 `narrowGasLimit`(`:183-190`)+ `validate_transaction` 的 `GAS_LIMIT_REACHED` 双重挡住(详见"正面核实")。`narrowGasLimit` 那 20 行注释里推演的攻击链我复核过,**成立**,该检查是必需的。

7. **收据映射**:见 **Imp-2**(丢字段)与 **Min-2**(deposit nonce/version 的分叉门控)。`status`/`logs`/`bloom` 本身正确;`bloom` 不经 `mapOpReceipt`,由 `sealOpBlock`(`OpBlockSeal.cpp:41-55`)按位或聚合,与 op-geth 一致。

8. **`OpForkSchedule`**:比较用 `>=`、边界归属正确(见"正面核实");Karst 护栏见 **Min-3**。

---

## 给协调者的裁定请求

1. **C-3 的定性**。它是"已知范围限制"还是"缺陷"?我按判决口径记为 Critical(op-geth VALID / 本实现 INVALID),但如果协调者认为最小闭环就该只吃三种类型,那么**至少**必须修正 spec 第 311 行的"唯二"表述并把它作为条目补进 §6.4 —— 那句话现在会让读者相信这类路径已被穷举。
2. **C-1 的修复位置**。改共享的 `RLPDecode.h::decodeHeader`(正确,但会影响 `Web3Transaction` 等既有以太坊解码消费者的接受面 —— 我判断那是**修正**而非回归,因为 Go 的行为就是这一条),还是在 OP 侧再复制一层严格解码头(不影响他人,但把 Imp-3 的复制问题从 2 份变 3 份)?我倾向前者,但这跨出了本闭环的零触碰边界之外的既有模块,需要裁定。
3. **Imp-2 的处置**。补齐 `mapOpReceipt` 还是入台账?若入台账,需要明确"OP 块的 `SYS_HASH_2_RECEIPT` 不可被 RPC 直接服务"这条约束由谁承接 —— §6.4 (q) 说 OP 路径生产上还没有 RPC 入口,所以今天没有消费者,但 (f) 已经把"一个键两张表"当作可检索面写进设计了。
4. 本报告的 C-1/C-2 都需要一次构建才能从"读代码得出"升级为"实测确认"。我已给出两条最小验证步骤,均只需新增测试用例、不改生产代码,**建议在批 6 注入实验结束后一并跑**。
