# 终审 batch1 修复报告:C2/C3/C4 + yParity 测试盲区

工作目录 `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`,
分支 `feat-op-validator-loop`。基准 op-geth `e8800cffe`
(`/Users/octopus/octo/code/blockchain-impl/op-geth`)。**本轮不豁免编译/测试**——
统一验证阶段已过,全部结论基于真实 `cmake --build` + `ctest` 二进制运行结果。

## 状态:已编译验证

所有诊断/修复/自验均通过实际编译(`cmake --build build --target
bcos-evm-opstack-tests -j8`)与运行(`build/bcos-evm/test/bcos-evm-opstack-tests`)
完成,非静态走查。

---

## C4:deposit/eip1559/setcode 的 gas_limit 无检查窄化

### 根因

`OpSchedulerImpl.h` 三处(`decodeDepositTx`/`decodeEip1559Tx`/`decodeSetCodeTx`)
均是 `dep.gas_limit = static_cast<int64_t>(decodeU64Scalar(listBody));`——裸窄化,
与同文件 `narrowU256ToU64`(见 :154-161)已建立的"宽转→显式 `>` 检查→窄化"纪律矛盾。
规范 8 字节 `0xFFFFFFFFFFFFFFFF`(uint64_t 最大值)`static_cast<int64_t>` 后变成 `-1`。

### op-geth 对照

- op-geth 侧:`GasPool.SubGas` 对超限 gas_limit 返回 `ErrGasLimitReached`;
  `state_transition.go` 的 failed-deposit 分支明确把该错误排除在"仍扣费、仍视为
  执行成功"的容忍路径之外——即负/超限 gas_limit 在 op-geth 侧导致**整块无效**。
- 本实现侧攻击链(deposit,协调者原始追实):负 `gas_limit` → `validate_transaction`
  因低于 intrinsic gas 判定失败 → `runDeposit`(OpDepositTx.cpp)只特判
  `GAS_LIMIT_REACHED`,故落入"失败-deposit"通用分支,`receipt.gas_used = dep.gas_limit`
  (仍为负)→ `OpBlockExecute.cpp` 的 `blockGasLeft -= gas_used` 因减去负数反而
  **抬高**约 `2^63` 的剩余块 gas 池 → 同块后续交易可越过真实 gasLimit;`gasUsed`
  经 `static_cast<uint64_t>` 在 `OpExecuteBlockResult` 投影上回绕成一个巨大但"看似
  合理"的值——攻击者在 payload 侧填同一回绕值即可通过六项比对面,块本实现判 **VALID**。

### 修法

新增 `narrowGasLimit(uint64_t) -> int64_t`(`> INT64_MAX` 即 `throw
OpConsensusError`),应用于 deposit/eip1559/setcode 三处 gas_limit 解码点。

### 自验(注释掉→翻红→恢复)

- **deposit 路径**(`DepositGasLimitOverflowIsConsensusError`):注释掉
  `narrowGasLimit` 调用改回裸 `static_cast`,重新编译跑测——**翻红,且失败原因是
  "throws nothing"**(即协调者所述"执行不报错、静默腐化结果",不是别的原因)。恢复后
  复绿。
- **eip1559/setcode 路径**(`TypedTxGasLimitOverflowIsConsensusError`):同样操作,
  **第一轮测试仍然通过**——`validate_transaction` 对普通(非 deposit)交易的 gas
  校验失败本身就是一个 block-level 硬错误,这个硬错误经本实现的 `catch(...)` 兜底
  也会被分类成 `OpConsensusError`,与 `narrowGasLimit` 检查是否存在**巧合地产出
  同一异常类型**。当时结论是"这个测试不能作为存在性证据"。**该结论已被二审 I-1
  推翻**(见下)。

### 二审 I-1 修正:setcode 子用例改消息断言,补齐翻红证据

审查者实测证明遮蔽只在异常*类型*层,消息文本不同(修复在位:
`"raw tx decode: gas limit exceeds int64_t range: setcode.gasLimit"`;移除后:
`catch(...)` 兜底的固定通用文案)。`M-2` 补上 `narrowGasLimit` 的 `fieldName`
形参(三处调用点消息各异)后,`TypedTxGasLimitOverflowIsConsensusError` 的
setcode 子用例改用
`expectOpConsensusErrorWithMessage(..., "gas limit exceeds int64_t range:
setcode.gasLimit")`,重做自验:

```
# 注释掉 setcode 分支的 narrowGasLimit(改回裸 static_cast),重新编译跑测
[ RUN      ] OpSchedulerImpl.TypedTxGasLimitOverflowIsConsensusError
.../OpSchedulerImplTest.cpp:450: Failure
Expected: (std::string(e.what()).find(expectedSubstring)) != (std::string::npos)
e.what()="OpSchedulerImpl: processOpBlock threw a block-level error (typed catch
bypassed by a known RTTI issue across the -fno-rtti evmone library boundary;
original exception message unavailable, see this catch(...) clause's comment)"
does not contain "gas limit exceeds int64_t range: setcode.gasLimit"
[  FAILED  ] OpSchedulerImpl.TypedTxGasLimitOverflowIsConsensusError (1 ms)
```

翻红确认后恢复,重新编译跑测复绿。C4 现在 deposit/eip1559/setcode **三处均有
独立翻红证据**("防御纵深、不应依赖巧合"这条协调者原话不再是本报告绕不开的局限,
而是已经落地为可验证的独立覆盖)。

---

## C2:eip1559/setcode 跨链重放(chain_id 从未校验)

### 根因

`decodeEip1559Tx`/`decodeSetCodeTx` 解出 `tx.chain_id` 后从未与调度器自身
`m_chainId` 比对。执行侧 `OpValidate.cpp` → `eth/state/state.cpp:420-529
validate_transaction` 全文无 chain_id 判断(`chain_id` 仅用于 CHAINID 操作码与
EIP-7702 authorization 匹配)。

### op-geth 对照

op-geth `transaction_signing.go:284-285` 在签名恢复阶段即校验 `ErrInvalidChainId`
并冒泡为整块无效——一笔在链 A 签名的 EIP-1559 交易原样塞进链 B 的 payload,签名
本身照样合法(secp256k1 签名与链 id 无关),op-geth 在解码/签名校验阶段就会拒绝;
本实现原先会 ecrecover 成功、sender 正确、正常执行、块判 VALID。

### 修法

`chainId` 经 `executeOpBlock` 已有的 `m_chainId` 成员,以**普通 `uint64_t` 实参**
传给 `detail::decodeOneRawTx` → 两个类型化解码器(`decodeEip1559Tx`/
`decodeSetCodeTx`,deposit 无 chain_id 字段不涉及),解出 `tx.chain_id` 后立即
`!= chainId` 判断即 `throw OpConsensusError`。

**签名不落在 `OpSchedulerImpl` 类的成员函数上**——`decodeOneRawTx`/
`decodeEip1559Tx`/`decodeSetCodeTx` 全部是 `detail` 命名空间下的**自由函数**,
不是类模板 `OpSchedulerImpl<Storage>` 的成员。T5b 曾踩过"类模板成员函数签名随类
实例化即时具化(即使函数体是惰性实例化的),往里面塞 OP 专属依赖名会让通用组合根
（实例化同一个 `EngineServiceImpl<..., SchedulerType=非OP调度器, ...>`)找不到匹配
声明而编译失败"的坑(见 `c0288b8b0 fix(engine): 统一编译验证——registerOpBlock
签名 OP 依赖名致通用组合根无法实例化`)。本次改动只在 `detail::` 自由函数签名上加了
一个 `uint64_t chainId` 形参(自由函数按 ODR 惰性实例化,不受类模板具化时机影响),
`OpSchedulerImpl` 类自身的公开成员(`executeOpBlock`/`executeBlock`/
`commitmentsOf`/`computeTxRoot`/`isIsthmusActiveAt`/`isJovianActiveAt` 等
"engine-facing seam surface")**签名逐字未动**。

**证据修正(二审 M-1)**:本节初版把 `build/engine/test/test-bcos-engine`(engine
模块自身的 Boost.Test 套件)说成"含 OP 与通用两条实例化路径"的证据,是张冠李戴——
实测 `engine/test/` 目录对 `OpSchedulerImpl` **零引用**,单独 `touch`
`OpSchedulerImpl.h` 不会触发该 target 重编译,它验证的是 engine 模块自身逻辑,与
本次改动是否安全无关。真正验证"改动未污染通用组合根实例化"的证据是
`bcos-evm/test/opstack/EngineNewPayloadGateTest.cpp:351-354`(属 212 套件的一部分,
本次改动前后均编译通过):同一 TU 内先 `using OpScheduler =
bcos::evm::engine::OpSchedulerImpl<ViewType>;` 构造 `OpEngineService =
EngineServiceImpl<..., OpScheduler>`,再构造
`GenericEngineService = EngineServiceImpl<..., SchedulerSerialImpl>`(通用调度器,
零依赖 `OpSchedulerImpl`),两者相邻声明,后跟
`static_assert(OpEngineService::c_opMode, ...)` /
`static_assert(!GenericEngineService::c_opMode, ...)`——同一 TU 内两条组合根路径
双双编译通过,是"改动未让 OP 依赖名污染通用侧"的直接证据。`test-bcos-engine`
的编译+运行仍在文末回归结果里保留(engine 模块自身零改动,属常规回归确认),但
不再作为本节论点的支撑证据。

### 自验(注释掉→翻红→恢复,第一轮 + 二审 I-1 修正后)

- 第一次尝试(合约创建 `to=空` + `gasLimit=21000`)两处均**未翻红**——排查后发现
  是测试构造缺陷,见下"自验中发现并修正的测试构造问题"。
- 修正测试构造后第一轮自验:
  - **eip1559**:注释掉 chainId 判断,重新编译跑测,`try/catch` 诊断打印确认
    `[DIAG] no exception`——即**跨链重放交易被完整正常执行,块判 VALID,零异常**,
    与协调者预判逐字吻合。恢复后复绿。
  - **setcode**:同样注释掉后,诊断显示仍抛 `OpConsensusError`(经 `catch(...)`
    RTTI 旁路兜底,原始消息不可恢复)——当时判定为"与 C4 typed-tx 类似的巧合防御,
    不构成独立翻红证据"。**该结论已被二审 I-1 推翻**(见下)。

### 二审 I-1 修正:setcode 子用例改消息断言,补齐翻红证据

审查者用备份→改→重建→跑→还原的完整实验证明:C4-typed/C2-setcode 的"遮蔽"只发生
在异常**类型**层——修复在位与移除后都抛 `OpConsensusError`,但**消息不同**:修复
在位时是检查自身的具体文案(如 `"raw tx decode: chain id mismatch (setcode)"`),
移除后是 `catch(...)` RTTI 旁路兜底的固定通用文案
(`"processOpBlock threw a block-level error (typed catch bypassed...)"`)。把
`EXPECT_THROW` 换成 `try/catch` + `e.what()` 子串断言(复用本文件
`ExecuteBlockThrowsInOpMode` 已有的 `std::string(e.what()).find(...) !=
std::string::npos` idiom,新增 `expectOpConsensusErrorWithMessage` 辅助函数封装,
不引入 gmock 新依赖)即可 100% 判别。

`TypedTxChainIdMismatchIsConsensusError` 的 setcode 子用例改用
`expectOpConsensusErrorWithMessage(..., "chain id mismatch (setcode)")`,重做自验:

```
# 注释掉 setcode 分支的 chainId 判断,重新编译跑测
[ RUN      ] OpSchedulerImpl.TypedTxChainIdMismatchIsConsensusError
.../OpSchedulerImplTest.cpp:450: Failure
Expected: (std::string(e.what()).find(expectedSubstring)) != (std::string::npos)
e.what()="OpSchedulerImpl: processOpBlock threw a block-level error (typed catch
bypassed by a known RTTI issue across the -fno-rtti evmone library boundary;
original exception message unavailable, see this catch(...) clause's comment)"
does not contain "chain id mismatch (setcode)"
[  FAILED  ] OpSchedulerImpl.TypedTxChainIdMismatchIsConsensusError (1 ms)
```

翻红确认后恢复,重新编译跑测复绿。C2 现在 eip1559/setcode **两个子用例均有独立
翻红证据**,不再有"依赖巧合"的表述残留。

---

## C3:签名延展性(未强制 EIP-2 低 s)

### 根因

`recoverTxSender` 直调 `evmmax::secp256k1::ecrecover`——这是 **ECRECOVER 预编译**
语义,按 EIP 定义只要求 `0 < r,s < n`,不要求 `s <= n/2`。

### op-geth 对照

op-geth `crypto.ValidateSignatureValues`(经 `transaction_signing.go:520` 以
`homestead=true` 调用)额外拒绝 `s > n/2`(`crypto/crypto.go:244-248`)。把合法交易
的 `(r,s,yParity)` 改写为 `(r, n-s, 1-yParity)`——sender 与执行效果不变(同一椭圆曲线
点的另一种合法表示),但交易的**原始字节**不同,进而 txRoot/blockHash 不同:该块
在本实现判 VALID,op-geth 因签名延展判**整块拒绝**。

### 修法

新增 `requireLowSSignature(r, s)`:`r==0 || r>=n || s==0 || s>=n || s>n/2` 即
`throw OpConsensusError`,复用 `evmmax::secp256k1::Curve::ORDER`(同一常量已用于
`eth/state/state.cpp:21/117`、`OpTransition.cpp:28/71` 的 EIP-7702 authorization
低-s 校验——本任务是本仓首次将其用于**外层交易**签名,此前没有任何代码路径需要从
原始字节反解外层交易签名)。在 `recoverTxSender` 内单点调用,eip1559/setcode 两个
调用点均自动覆盖。

### 自验(注释掉→翻红→恢复)

注释掉 `requireLowSSignature(r, s);` 一行,重新编译跑测
`HighSSignatureIsConsensusError`——**翻红,失败原因"throws nothing"**(即高-s 签名
被正常接受、正常执行、无异常)。恢复后复绿。这是四项修复中最干净的翻红证据(单一
函数、单一调用点,无"巧合防御"现象)。

---

## yParity>1 测试盲区(I-2,本批次核心缺口)

T4 fix 轮已落地的 `if (yParity > 1) throw` 检查(eip1559/setcode 各一处)此前
5 个既有测试对其零覆盖(视角 3 实测:删掉两处检查后 206/206 仍全绿)。

### 根因

`decodeEip1559Tx`/`decodeSetCodeTx` 的 `if (yParity > 1) throw` 是**两条独立
复制粘贴的语句**,不是像 C3 `requireLowSSignature` 那样的单一共享函数——第一轮
`YParityEquals2`/`YParityEquals256` 只喂 `buildEip1559RawTx`,对 `decodeSetCodeTx`
那份检查零信息量。语料里的 4 笔真实 setcode 交易 yParity 均合法,正向回归路径
天然不构成对这条检查的守护。二审 I-2 实测:单独删掉 `decodeSetCodeTx` 里那一行,
212/212 依旧全绿。

### op-geth 对照

op-geth `crypto.ValidateSignatureValues` 的 `(v==0||v==1)` 判定对 yParity 超出
{0,1} 的交易整块拒绝。若 `decodeSetCodeTx` 的检查被删除,`yParity=256` 会被
`tx.v = static_cast<uint8_t>(yParity)` 截成 `0`,恢复出一个"看起来合法但语义
错误"的 sender,与 op-geth 的拒绝语义相悖。

### 修法

无新增 production 检查(检查本身已存在于两处),两个测试各补一个
`buildSetCodeRawTx` 子用例(builder 早已现成)。

### 自验(注释掉→翻红→恢复)

- **eip1559 分支**(第一轮已验证):注释掉后两个测试均翻红,失败原因均为
  "throws nothing"。恢复后复绿。
- **setcode 分支**(二审新增):单独注释掉 `decodeSetCodeTx` 那一行,先用
  `EXPECT_THROW(..., OpConsensusError)` 型断言跑测——**测试仍然通过**,诊断
  (`try/catch` 打印)显示实际抛出的是 `catch(...)` RTTI 旁路兜底的通用消息,
  不是 `"invalid y parity (setcode)"`——与 I-1 揭示的同一类"类型层遮蔽"现象在
  这里独立复现。改用 `expectOpConsensusErrorWithMessage(...,
  "invalid y parity (setcode)")` 后重新自验:

  ```
  # 注释掉 decodeSetCodeTx 的 if (yParity > 1) throw,重新编译跑测
  [ RUN      ] OpSchedulerImpl.YParityEquals2IsConsensusError
  .../OpSchedulerImplTest.cpp:450: Failure
  Expected: (std::string(e.what()).find(expectedSubstring)) != (std::string::npos)
  e.what()="OpSchedulerImpl: processOpBlock threw a block-level error (typed catch
  bypassed by a known RTTI issue across the -fno-rtti evmone library boundary;
  original exception message unavailable, see this catch(...) clause's comment)"
  does not contain "invalid y parity (setcode)"
  [  FAILED  ] OpSchedulerImpl.YParityEquals2IsConsensusError (1 ms)

  [ RUN      ] OpSchedulerImpl.YParityEquals256IsConsensusError
  .../OpSchedulerImplTest.cpp:450: Failure
  (同上,not contain "invalid y parity (setcode)")
  [  FAILED  ] OpSchedulerImpl.YParityEquals256IsConsensusError (0 ms)
  ```

  翻红确认后恢复,重新编译跑测复绿。`decodeSetCodeTx` 的 yParity 检查现在有
  独立于 eip1559 分支的翻红证据,I-2 缺口已补齐。

---

## 自验中发现并修正的测试构造问题(过程记录,非 production 代码问题)

四项自验的第一轮尝试里,`TypedTxChainIdMismatchIsConsensusError`(eip1559 子用例)
与最初版本的 `TypedTxGasLimitOverflowIsConsensusError` 都出现过"注释掉修复后测试
仍然通过"的假阳性,排查后定位到两个独立的测试构造缺陷,均已修正:

1. **单交易块违反 processOpBlock 的"首笔须为 L1 attributes deposit"不变式**:
   早期草稿把恶意的 eip1559/setcode 交易当作区块的**唯一**交易。若目标检查(C2/C3/
   C4/yParity)真的被删除,解码本应成功、Step 1 顺利完成——但 Step 3
   `processOpBlock` 随即因"首笔不是 L1 attributes deposit"抛出**同一个异常类型**
   `OpConsensusError`,与目标检查是否存在无关地让测试"看起来通过"。修正:把恶意
   交易放在两笔交易区块的索引 1,索引 0 用一笔结构合法的 `OP_DEPOSITOR→OP_L1_BLOCK`
   deposit 填充(`leadingL1AttributesDeposit()`)——只要索引 1 的解码在 Step 1 内
   抛出,`processOpBlock` 根本不会被调用,不再有confound。
2. **`to=空`(合约创建)与测试固定 `gasLimit=21000` 的 intrinsic gas 下限不匹配**:
   合约创建的 intrinsic gas 下限是 `TX_BASE_COST + TX_CREATE_COST = 21000 + 32000
   = 53000`(`eth/state/state.cpp::compute_tx_intrinsic_cost`),而本文件全部
   "本应有效"的测试交易固定用 `gasLimit=21000`——即所有合约创建形态的测试交易本身
   就低于 intrinsic 下限,`validate_transaction` 会因此拒绝,与被测的具体字段无关。
   修正:`to` 改为固定占位地址(`kPlaceholderTransferTo =
   0x0000...0000dead`,普通转账,不是合约创建),intrinsic 下限精确等于
   `TX_BASE_COST=21000`,与测试交易的 `gasLimit=21000` 恰好吻合(不是宽松匹配,是
   精确匹配,不会掩盖差一错误)。

两处问题均在编译+运行的自验循环中被实测发现(不是纸面审查发现),修正后重新走完
整的"翻红→恢复→复绿"闭环。

---

## 全量回归结果(二审修复后,最终状态)

```
$ cmake --build build --target bcos-evm-opstack-tests -j8
[100%] Built target bcos-evm-opstack-tests   # 0 errors, 0 warnings from own code

$ ./build/bcos-evm/test/bcos-evm-opstack-tests --gtest_filter='OpSchedulerImpl.*'
[==========] 11 tests from 1 test suite ran. (3 ms total)
[  PASSED  ] 11 tests.                        # 测试用例数未变(I-1/I-2 是往既有
                                                # TEST() 里加子用例/改断言方式,不是
                                                # 新增 TEST() 条目)

$ ./build/bcos-evm/test/bcos-evm-opstack-tests \
    --gtest_filter='Engine*:OpSchedulerImpl*:EthBlockHeader*:OpDepositEncode*'
[==========] 56 tests from 7 test suites ran. (82 ms total)
[  PASSED  ] 56 tests.

$ ./build/bcos-evm/test/bcos-evm-opstack-tests   # 全量、零 filter
[==========] 212 tests from 33 test suites ran. (109 ms total)
[  PASSED  ] 212 tests.

$ cmake --build build --target test-bcos-engine -j8
[100%] Built target test-bcos-engine            # engine 模块自身编译确认(常规回归,
                                                 # 非 M-1 修正后本节论点的支撑证据)

$ ./build/engine/test/test-bcos-engine
*** No errors detected
```

三层 + engine 自身套件均零回归。测试用例数与二审前一致(11/56/212),因为 I-1/I-2
都是在**既有** `TEST()` 内新增子用例代码块或替换断言方式,没有新增 `TEST()` 条目
——覆盖面的扩大体现在每条测试内部的断言更严格(消息级)、分支更全(eip1559+setcode
两侧都覆盖),而不是用例计数增长。

## Commit

```
0811e4976 fix(bcos-evm): OpSchedulerImpl 终审 batch1——deposit gas_limit 无检查窄化(C4)+跨链重放(C2)+签名延展性(C3)+yParity 测试盲区
a47b00e78 fix(bcos-evm): batch1 二审必修——I-1 消息断言破遮蔽 + I-2 setcode yParity 测试盲区 + M-2 gas_limit 消息带字段名
```

精确路径(`rtk git add` 逐路径,无宽路径):

- 第一轮(`0811e4976`):`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h`
  (100 insertions/12 deletions)、`bcos-evm/test/opstack/OpSchedulerImplTest.cpp`
  (288 insertions)。
- 二审(`a47b00e78`):`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h`
  (19 insertions/6 deletions,M-2 的 `fieldName` 形参 + 三处调用点)、
  `bcos-evm/test/opstack/OpSchedulerImplTest.cpp`(159 insertions/28 deletions,
  I-1 的 `expectOpConsensusErrorWithMessage` 辅助函数 + 4 处 setcode 子用例改消息
  断言、I-2 的两个测试各加 `buildSetCodeRawTx` 子用例、builder 注释补 `r=1` 约束
  说明)。

`clang-format -style=file:.clang-format --dry-run --Werror` 对两个文件两轮均确认
CLEAN。`.superpowers/sdd/2026-07-28-op-validator-minimal-loop/progress.md` 的
未暂存改动不属于本任务(与本次 diff 无关),未 `git add`。

## 已知局限(诚实记账,二审后更新)

- 二审前的"C4-typed/C2-setcode 无法独立证明"结论,以及 I-2 未发现前的
  "decodeSetCodeTx yParity 检查零覆盖",均已通过消息断言(I-1)与补测(I-2)解决,
  不再是残留局限——见上文对应小节的翻红证据。
- `kPlaceholderTransferTo`/`r = intx::uint256{1}` 这套 builder 约束(已在
  builder 注释里点明)本身仍是本文件全部负向用例共享的隐式前提:`to` 必须是
  "普通转账"形状(intrinsic gas 恰好 21000)、`r=1` 必须保持是 secp256k1 上合法
  的 x 坐标。这两条不是本轮遗留 bug,而是这套测试基础设施的**设计约束**,一旦
  未来有人改动这两个常量而不理解其数学/gas 含义,可能重新引入"负向用例以错误理由
  静默转绿"的同类问题——已用注释显式标注,留给后续维护者。
- 本次改动完全限定在 `OpSchedulerImpl.h` + 其配套测试文件,`engine/` 模块本身
  零改动(仅用于回归验证)。
