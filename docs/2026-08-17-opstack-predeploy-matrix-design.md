# opstack 预部署合约行为矩阵测试 — 设计

> 日期:2026-08-17
> 范围:核心 5 个预部署合约的行为矩阵(第一波;其余 8 个后续波次)
> 测试层:真实节点为主(op-e2e 强化)+ t8n 差分覆盖共识项
> 参照:contracts-bedrock 现成 foundry 测试(字节码语义背书)+ op-geth 锚点 + OP spec

## 背景与动机

FISCO 的 genesis 部署了 13 个 OP-Stack 预部署合约(2 个 FISCO 自研:SystemConfig/L2ValidatorSet;11 个 OP-fork:字节码来自 pinned optimism)。现有 op-e2e 只部署**自定义测试合约**验证「节点能执行任意合约」,而**预部署合约各自的运行时语义从未在 FISCO 节点上验证**。这些合约是 L2 对用户/dapp 暴露的 OP 语义表面(桥/消息/费用/提现),行为错 = 用户可见或分叉。op-geth 靠 contracts-bedrock 的 foundry 套件验证它们;FISCO 缺失。

## 范围:核心 5 个(第一波)

L1Block(0x420...15)、L2ToL1MessagePasser(0x420...16)、L2CrossDomainMessenger(0x420...07)、L2StandardBridge(0x420...10)、SystemConfig(0x420...c0,FISCO 自研)。

其余 8 个(L2ValidatorSet / GasPriceOracle / fee vaults / WETH / OptimismMintableERC20Factory / ProxyAdmin)后续波次。

## 方案:op-e2e 扩展 + t8n 差分(已获批)

### 5 合约行为矩阵

**L1Block** — 每块被调用的核心
| 行为 | 断言 |
|---|---|
| setL1BlockValues Isthmus(176B,`0x098999be`)/ Jovian(178B,`0x3db6be2b`)经 L1-attributes deposit | 槽写入:l1_base_fee/scalars/blob_base_fee/op fee params/**da footprint**/blockhash/sequenceNumber 逐块 +1 |
| getter 读 | `number()/timestamp()/basefee()/baseFeeScalar()/daFootprintGasScalar()/operatorFeeScalar()/sequenceNumber()` 返回写入值 |
| 拒绝路径 | 错误 calldata 长度、非 deposit 调用方(仅 L1-attributes 系统调用可调) |
| 跨块 | sequenceNumber 每块递增、上块值持久 |

**L2ToL1MessagePasser** — 提现入口
| 行为 | 断言 |
|---|---|
| initiateWithdrawal(target, gasLimit, data) | **withdrawal hash 写入存储**、`MessagePassed` 事件、仅限特定调用方 |
| withdrawalsRoot | 块头 withdrawalsRoot **包含该消息**(t8n 对拍) |
| getter | `getSentMessage()/getSentMessageHash()` 返回写入值 |

**L2CrossDomainMessenger**
| 行为 | 断言 |
|---|---|
| sendMessage(target, message, minGasLimit) | `SentMessage` 事件、**nonce 递增**、版本化 |
| relayMessage | 经 L2ToL1MessagePasser 的 relay 流 + 重放保护 |
| 仅限调用方 | 非授权调用者拒绝 |

**L2StandardBridge**
| 行为 | 断言 |
|---|---|
| depositERC20/depositERC20To | L2 token **mint**、`DepositInitiated` 事件 |
| withdraw | L2 token **burn**、`WithdrawalInitiated` 事件 |

**SystemConfig**(FISCO 自研)
| 行为 | 断言 |
|---|---|
| setValueByKey(key, uint192 value, uint64 enableNumber) | Entry 写入(value/enableNumber/updatedAt 打包) |
| getValueByKey(key) | 返回写入值 + enableNumber |
| 仅限调用方/owner | 非授权拒绝;owner 转移 |

### 期望值来源

- **OP-fork 4 个**:合约 ABI + Solidity 源码(pinned optimism 33f06d2d)+ OP spec;关键值抽查 op-geth 锚点(DA 矩阵已证节点过渡级与 op-geth 一致,行为级再实断言)。
- **SystemConfig**:FISCO 自己的 Solidity 源码 + 设计意图(无 op-geth 对应,手工断言)。
- 期望值**写死在测试脚本**(非运行时自算),对齐「测试不可退步」。

### t8n 差分向量(共识项,stateRoot/withdrawalsRoot 对拍 op-geth)

复用现有 t8n harness(`opstack-executor/tests/t8n`,opt8n-ref 生成 + OpT8nReplayTest 回放)。现有向量已含 `message_passer_write` 与 deposit 基础,本设计补:
- **L1Block deposit 全槽断言**:isthmus(176B)/ jovian(178B)deposit 块 → 回放后读 L1Block 全部槽(slot1/3/7/8 + blockhash/sequenceNumber)断言写入值 + stateRoot 对拍。
- **withdrawalsRoot parity**:含 initiateWithdrawal 的块 → 块头 withdrawalsRoot 与 op-geth 逐位一致(isthmus/jovian 各一)。
- 由 OpT8nReplayTest 门覆盖(现有 gating)。

### 测试组织(真实节点)

- **新脚本** `tools/op-e2e/predeploy_matrix.py`,跑在 B3(eth RPC 8553):
  - 用现有 `sign_secp` 签交易 + SENDER(已资助);`gen_l1block.py`/`probe_l1block.py` 基础可复用。
  - 每组行为:发交易/eth_call → 断言回执 status/事件(logs 主题+data)/返回值/存储。
  - L1Block 走 eth_call getters + 跨块 sequenceNumber(节点单节点共识自动注入 deposit)。
  - WithdrawalsRoot 从块头读(`eth_getBlockByNumber` 的 `withdrawalsRoot` 字段)。
- **挂 run_all.sh**:在 chain_driver 之后、a1_active 之前;失败即 run_all.sh 失败。
- 期望值**写死**在脚本。

### 实施顺序(每步独立可提交)

1. **t8n 差分向量**(L1Block deposit 全槽 + withdrawalsRoot parity)——先锁共识项,复用 opt8n-ref。
2. **predeploy_matrix.py 骨架 + L1Block 组**(getters 读 + sequenceNumber 跨块)。
3. **L2ToL1MessagePasser 组**(initiateWithdrawal tx + 事件 + 块头 withdrawalsRoot)。
4. **L2CrossDomainMessenger + L2StandardBridge 组**(sendMessage nonce / deposit mint / withdraw burn)。
5. **SystemConfig 组**(setValueByKey/getValueByKey + 权限 + owner 转移)。
6. **挂 run_all.sh + 全量回归**(ctest + op-e2e,确认无退步)。

> 注:SystemConfig 的 owner/授权者需在实现时确认(auth_admin 或特定地址——影响组 5 的调用方)。

## 约束

- **测试不可退步**:只增补测试,不触碰预部署合约实现/节点执行逻辑。
- 期望值写死;t8n 向量经 opt8n-ref 单源生成(stateRoot 对拍 op-geth)。
- op-e2e 挂 run_all.sh 门;发现实现级分歧 → 登记 DIVERGENCES.md 或单独立案。
