# OP Stack L2 测试战役:实测记录与发现(spec §7 交付物)

> 分支 `feat-op-eest-baseline`,2026-08-12。
> 目标:OP Stack L2 执行器(`executor_version>=3` → OP 模式)的系统性端到端测试。
> 本记录覆盖:发现的 **18 个真实 bug**、已知限制、测试覆盖矩阵、EVM 手写 bytecode 教训。

## 1. 测试战役概览

| 阶段 | 内容 | 工具/脚本 |
|---|---|---|
| 阶段 0 | RPC 读路径修复 + 测试基础设施 | rpc_matrix / chain_driver / state_verify / restart_b3 |
| 阶段 1 | Part A RPC 矩阵验证(eth_call 族) | rpc_matrix.py |
| 阶段 2 | Part B 区块状态验证 | state_verify.py / b4_persist.py |
| 阶段 3 | Part C golden 消费(离线对比 op-geth) | OpT8nReplayTest |
| 阶段 4 | 回归门 | run_all.sh(ALL OP-E2E GREEN) |
| 真实节点 | B3a 主动实例 + FCU/getPayload/newPayload | a1_active.py |
| op-geth 扩展 | item 2/3/4/5/6 + B4 + B6 | OpL1BlockDepositTest + a1_active |

**当前状态**:1930 ctest + op-e2e 回归门全绿(强制约束"测试不可退步"全程遵守)。

## 2. 发现的 Bug 清单(18 个)

### 2.1 阶段 1:eth_call 验证(5 个,B3 真实节点)

| # | 症状 | 根因 | 修复 |
|---|---|---|---|
| 1 | **eth_call 崩溃(SIGSEGV 信号风暴)** | `OpCallScheduler::coCallLatest` 里 `auto const& header = *block->blockHeader()` 悬空引用——`BlockImpl::blockHeader()` **每次返回 fresh Ptr**,临时在完整表达式末析构 | 持有 `auto headerPtr = block->blockHeader()` |
| 2 | eth_call 抛 `OpForkRevisionMismatch` | B3 `s_config:evmc_revision=cancun`,OP 需 **prague** | 改 DB 值 cancun→prague |
| 3 | 空 envelope `invalid_argument` | eth_call 无签名 envelope → opValidate 拒绝 | 放行空 envelope(L1 cost=0) |
| 4 | `intrinsic gas too low` | eth_call gas 默认 0 | `evmTx.gas_limit==0 → blockGasLeft` |
| 5 | `nonce too low` | eth_call 普通 call nonce 空 | coCallLatest 用 sender 当前 nonce(注意 sender() 是原始 20 字节) |

### 2.2 阶段 0-V2:RPC 读路径(4 个)

| # | 症状 | 根因 | 修复 |
|---|---|---|---|
| 6 | `transactionIndex` 恒 0 | processOpBlock 构造回执没设 index | 循环 `setTransactionIndex(txIndex++)`(不影响 receipts root) |
| 7 | `cumulativeGasUsed` 读 0 | `safeCastToU256`=boost::lexical_cast **不支持 0x 前缀** | 直接 `bcos::u256(std::string)` 构造 |
| 8 | getPayloadV4 transactions 空 + withdrawalsRoot 缺失 | serialize 只读 `transactions`,OP 在 `rawTransactions` | 读 rawTransactions + 输出 withdrawalsRoot |
| 9 | 合约创建回执 `contractAddress` null | makeFiscoReceipt 没填 | 用 `evmc_result.create_address` 填充 |

### 2.3 B1 sequencer / 真实节点(3 个)

| # | 症状 | 根因 | 修复 |
|---|---|---|---|
| 10 | 块 VALID 但 `eth_getTransactionReceipt` OP 扩展字段缺 | 方案A:OP 回执缺 l1GasUsed/operatorFee/effectiveGasPrice | 3 字段补齐 |
| 11 | `eth_blockNumber=0` + 空块 | `opstackRegisterBlock` 只写 5 张块表,缺 `SYS_CURRENT_STATE/current_number` + `SYS_NUMBER_2_TXS` | 对照 `Ledger::asyncPrewriteBlock` 表全集补写 |
| 12 | 3 个 pre-existing RPC bug | from 双重编码、checksum OOB、log 地址 | fix wave 修复 |

### 2.4 L1Block predeploy 手写 bytecode(5 个,全部离线复现测试抓出)

| # | 症状 | 根因 | 修复 |
|---|---|---|---|
| 13 | deposit gasUsed=1M(异常停机) | dispatch JUMPI 目标**非 JUMPDEST**(EVM 跳转目标必须是 0x5b) | 三个跳转目标落到显式 JUMPDEST |
| 14 | deposit 28 gas REVERT,slot 不写 | size-guard **LT 方向反了**:EVM `LT`=top<second,`CALLDATASIZE PUSH1 4 LT` = cd>4 才跳 | 改为 `PUSH1 4 CALLDATASIZE LT` = cd<4 |
| 15 | slot1=0x0000ff..16(掩码泄漏) | M224 用 **PUSH29(0x7c)只给 28 字节**,吞下一条指令首字节;应 PUSH28(0x7b) | 修掩码;slot1/slot7 简化为直接 CALLDATALOAD |
| 16 | slot 布局与 `unpackOpFeeParams` 不匹配 | 4 处移位错:baseFeeScalar/blob/opFeeScalar/da 位置不符 FISCO 消费端 | slot3=(base<<96)\|(blob<<64),slot8=(da<<96)\|(opFee<<64)\|opConst;slot1=CALLDATALOAD(36),slot7=CALLDATALOAD(68) |

### 2.5 item ③ withdraw 测试(3 个根因)

| # | 根因 | 说明 |
|---|---|---|
| 17 | **OP_L2_TO_L1_MESSAGE_PASSER = 0x4200...0016,不是 0x4200...0011** | tx 签错地址 → 空跑(对照 t8n message_passer_write 向量 to=0016) |
| 18 | EVM `CALLDATACOPY` pop dest,offset,size(TOP-first) | push 顺序必须 size,offset,dest |
| 18b | EVM `KECCAK256` pop offset,size | push 顺序必须 size,offset |

## 3. 已知限制(记录,未处理)

| 限制 | 说明 | 状态 |
|---|---|---|
| 历史块 eth_call | OP 模式无历史状态快照(无 MPT/checkpoint),`callAtBlock` fallback 到 latest | 已记录,需历史状态方案 |
| Karst 适配 | `karstConfig()` 仅是 jovianConfig 别名(占位) | 用户裁定不处理 |
| V4 端点 | 真实节点块执行需 V4 端点 + maxEngineVersion=4 | 骨架已评估 |
| op-e2e 全链路 | deposit→withdraw 依赖 op-node,无法直接跑 | EL 侧已模拟 |
| eth_getBlockByNumber hash | 是 FISCO tars 头 hash(非 OP keccak),既有 RPC hash 语义差异 | 记录 |

## 4. 测试覆盖矩阵

### 4.1 opstack-executor 单测(15 用例,OpL1BlockDepositTest)
- 零值 deposit 写 slot ✓
- 非零 L1 参数与 unpackOpFeeParams 对齐 ✓
- deposit 写 fee slot → loadOpFeeParams 读取 ✓
- 失败 deposit 封块(全额 gas + nonce bump)✓
- C-3/C-4 fork 边界(激活块 deposits-only、selector/length)✓
- 块级拒绝(first-tx 非 deposit、deposit 后置、gas 超预算)✓
- MessagePasser withdrawal root(seed + tx 驱动)✓
- 块级 7702 set-code ✓

### 4.2 op-geth 测试栈扩展
| item | 内容 | 状态 |
|---|---|---|
| 2 | rollup_cost golden 端到端(deposit→fee loop) | ✅ |
| 3 | 交易驱动 withdraw 全流程 | ✅ |
| 4 | MessagePasser withdrawal root | ✅ |
| 5 | 块级拒绝矩阵 | ✅ |
| 6 | 块级 EIP-7702 | ✅ |
| B4 | 真实节点 Engine 错误码(篡改 gasUsed/receiptsRoot、假 payloadId、FCU 未知 head→SYNCING) | ✅ a1_active 16 断言 |
| B6 | Jovian C-3/C-4 边界块 | ✅ |

### 4.3 回归门(run_all.sh)
rpc_matrix 42 + state_verify 12 + chain_driver 31 + b4_persist 3 + b3_contracts 12 + a1_active 16 = **ALL OP-E2E GREEN**。

## 5. EVM 手写 bytecode 教训(重要)

1. **EVM 栈参数全部 TOP-first pop**。多次栽倒:
   - `LT` = top<second(≠ second<top)
   - `CALLDATACOPY` pop dest,offset,size → push size,offset,dest
   - `KECCAK256` pop offset,size → push size,offset
   - `SSTORE` pop key,value → 值先 push(落在 slot 之下)
2. **JUMP/JUMPI 目标必须是 JUMPDEST(0x5b)**,否则 exceptional halt(消耗全部 gas)。
3. **PUSH_N 推 N 字节**:PUSH29(0x7c)要 29 字节,28 字节会吞下一条指令首字节。M224 掩码用 PUSH28(0x7b)。
4. **手写 bytecode 必须真实 evmone 验证**——python trace 可能携带与 bug 一致的错误(本次 trace 的 LT/PUSH 也错了)。
5. **shift→byte 映射**:32-bit 值放 bytes[i:i+4] → shift=(224-8i)。
6. **OP predeploy 地址核对 OpPredeploys.h**(MessagePasser=0016 非 0011)。

## 6. 工具与基建

- `tools/op-e2e/`:gen_l1block.py(bytecode 生成器)、a1_active.py(引擎闭环)、rpc_matrix/chain_driver/state_verify/b3_contracts/b4_persist/run_all.sh
- `opstack-executor/tests/OpL1BlockDepositTest.cpp`:块级执行复现(离线 bridge + OpSchedulerImpl)
- `t8n/vectors`:op-geth 锚定语料(golden 引擎回放)
- 测试工具动 DB 必须用节点同款 vcpkg rocksdb(homebrew 11.1.2 写 format 7 不兼容)
