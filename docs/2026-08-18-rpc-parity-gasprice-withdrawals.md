# RPC 对拍：eth_gasPrice(B1)+ withdrawalsRoot 全流程(B3)

> 2026-08-18。工作项 B1/B3（对照 op-geth/op-reth 差距清单的可拆分项）。
> 参考端 pin：op-geth `v1.101702.2`（commit `e8800cffe`）、contracts-bedrock `v1.19.2`。
> 硬约束：只增补测试/工具/文档，不改生产实现；分歧登记在案，修复单独立案。

## 1. eth_gasPrice

### op-geth 语义（参考）

- `eth_gasPrice` = `SuggestGasTipCap + head.BaseFee`（`internal/ethapi/api.go:77-96`，head 取
  `CurrentHeader()`，非 pending）。
- OP 链上 tip 走 `SuggestOptimismPriorityFee`（`eth/gasprice/optimism-gasprice.go:38-110`）：
  默认下限 `1e6 wei`（0.001 gwei，`--gpo.minsuggestedpriorityfee` 可调）；head 块 gas 打满时取
  tip 中位数 ×1.1；上限 500 gwei。任何失败路径回退下限，**永不返回 0、永不报错**。
- **不读 GasPriceOracle 预部署**（0x420…000F 全仓无引用）——GPO 合约仅供链上/钱包
  `eth_call`（其 `gasPrice()`/`baseFee()` 就是 `block.basefee`，contracts-bedrock
  `L2/GasPriceOracle.sol:134-142`）；L1 成本项完全不在 `eth_gasPrice` 里。
- `eth_maxPriorityFeePerGas` = 同一 tip（无 baseFee）。

### FISCO 现状

- `EthEndpoint::gasPrice`（`bcos-rpc/.../endpoints/EthEndpoint.cpp:121-139`）：读 ledger 系统配置
  `tx_gas_price`（hex）；缺失 → `"0x0"`。不读 header baseFee、不读 GPO。
- `maxPriorityFeePerGas`（同文件 :943-949）：常量 `"0x0"`。
- `eth_feeHistory`：未实现，声明缺口（rpc_matrix A.4 断言 `-32601`）。

### 分歧登记

| # | 分歧 | 影响 | 处置 |
|---|---|---|---|
| D-GP-1 | 值来源：ledger 配置 vs `head.baseFee + tip`。同一链状态下返回值不同；op-geth 永不为 0，FISCO B3 恒 `0x0` | 钱包/估算器把 0 当真值 | 已钉死两侧行为；对齐需生产改动（baseFee+下限 tip），单独立案 |
| D-GP-2 | `eth_maxPriorityFeePerGas`：动态（≥1e6）vs 常量 `0x0` | 1559 钱包 tip 估算 | 同上 |
| D-GP-3 | `eth_feeHistory`：op-geth 实现 vs 声明缺口 `-32601` | 工具链兼容 | 与 spec A.4 一致，维持声明缺口 |

### 本次钉死（B1）

- C++ `Web3RpcTest`（testWeb3RPC/handleValidTest）：absent-config → `"0x0"` 新用例 +
  既有 10086000 用例；`Web3NodeStatusTest/maxPriorityFeePerGasIsConstant` 由弱断言强化为
  钉死 `"0x0"`。16/16 绿。
- 活链 `rpc_matrix.py` a2_chain：`eth_maxPriorityFeePerGas == "0x0"` 钉死（0 failed）。

## 2. withdrawalsRoot（EIP-4895 语义在 L2 上的形态）

### op-geth 三段语义

| 阶段 | header.withdrawalsRoot | RPC JSON | 出处 |
|---|---|---|---|
| pre-Canyon | `nil` | 两个键**整体省略** | `consensus/beacon/consensus.go:282-284` |
| Canyon→pre-Isthmus | `EmptyWithdrawalsHash = 0x56e81f…b421` | `withdrawalsRoot` + `withdrawals: []` | `core/types/block.go:305-307` |
| Isthmus+ | **L2ToL1MessagePasser(0x420…0016) storage root** | 同上 | `consensus.go:416-427` |

L2 永不产生系统提款：withdrawals 只能来自 CL（op-node 恒空），Isthmus+ 硬拒绝非空列表；
引擎侧 Isthmus 要求 `withdrawalsRoot` 非空且 withdrawals 非 nil 空。

### FISCO 映射（逐项对上）

- Isthmus+ seal：`opStorageRoot(messagePasserStorage)`（`opstack-executor/OpBlockSeal.cpp:167-180`），
  secure-trie 构造 key=keccak(slot)、leaf=rlp(trimmed value) —— 与 op-geth `GetStorageRoot`
  一致；已由 golden 向量 `isthmus/jovian_message_passer_write`（op-geth 派生）字节级钉死。
- pre-Isthmus seal：`emptyRootHash()` = keccak256(RLP("")) = `0x56e81f…b421` —— 数值上等于
  op-geth Canyon 段的 `EmptyWithdrawalsHash`（同为空 trie 根）。
- 引擎校验：OP 路径要求 withdrawals 非空报错 + Isthmus+ root 必填
  （`engine/bcos-engine/EngineServiceImpl.cpp:344-361`），与 op-geth 引擎侧一致（既有测试覆盖）。

### 分歧登记

| # | 分歧 | 影响 | 处置 |
|---|---|---|---|
| D-WR-1 | JSON 形状：op-geth pre-Canyon **省略**两键；FISCO 恒输出 `withdrawals: []` + `withdrawalsRoot`（非 OP 头为零哈希） | 仅形状差异；Canyon+ 值一致，活 OP 场景无影响 | 登记；对齐属 BlockResponse 序列化微调，可随其它 RPC PR 顺手处理 |
| D-WR-2 | 非 OP 头 `withdrawalsRoot` 输出零哈希而非省略 | 同上（与 D-WR-1 同类） | 同上 |
| D-WR-3 | **创世头不携带 withdrawalsRoot 字段**（RPC 渲染零哈希、走非 OP 哈希路径）；op-geth Canyon+ 创世为 `0x56e81f…`。实测：创世 getProof 的 passer storageRoot 恰为 `0x56e81f…`，即只差头字段本身 | 跨客户端创世块哈希不一致；对本链内部一致性无影响 | 登记；修复需改 genesis fixture（`gen_eth_header_fixture.py` + setup），会改变创世哈希、需重建链，单独立案 |
| — | `eth_getProof` 仅创世可用：MPT 节点只在 genesis 导入路径写入（`bcos-ledger/Ledger.cpp:2205`，`feature_l2_ethereum_compat` 门控），运行时块 stateRoot 不入 MPT 存储 | 历史态证明不可用 | 已知范围限制（rpc_matrix 既有注释一致）；按块 MPT 快照属 #5417 后续 |

### 本次交付（B3 全流程）

活链（B3，块高 0xdc7+ 实测）+ 离线三层交叉：

1. **动态语义**（`predeploy_matrix.py` mp 组，30→35 断言全绿）：
   - `mp_root_changes_on_write`：withdrawal 落块后 header root 改变（root 只跟 passer 走）；
   - `mp_root_stable_no_writes`：无 passer 写入的后续块 root 不变；
   - `mp_sentmessages_slot0_physical`：`sentMessages` 在 slot 0，`keccak256(wh ‖ 0)` 读回 1
     （实测锚定映射槽位）。
2. **跨语言 golden**（同文件，不依赖节点）：
   - `mp_storage_root_python_golden`：纯 Python 存储 trie（复用 `tools/opstack-genesis/
     mpt_state_root.py`，op-geth 兼容 secure MPT）复算 t8n 向量 `isthmus_message_passer_write`
     的 postState → `0x02dffd0c…` == golden withdrawalsRoot。**Python == C++ opStorageRoot ==
     op-geth 三方一致**；
   - `mp_storage_root_empty_constant`：空存储 → `0x56e81f…b421`。
3. **创世交叉**（`rpc_matrix.py` a2_blocks）：创世块 `withdrawalsRoot` 零哈希形状钉死（D-WR-3）
   + `eth_getProof(passer, [], "0x0").storageHash == 0x56e81f…`（创世 MPT 写入器 vs 常数 vs
   op-geth EmptyWithdrawalsHash 三方一致）+ latest 块 `withdrawals: []`/32B root 形状。
4. **C++ 单元**（`opstack-executor/tests/OpL1BlockDepositTest.cpp` 新增
   `EmptyPasserStorageSealsEmptyRootConstant`）：`opStorageRoot({})` == 字面量 ==
   `bcos::ledger::mpt::emptyRootHash()`，且 seal 对空 passer 出同样字节。全 suite 137 用例绿。
5. **工具解耦**：`build-allocs.py` 的 yaml 依赖延迟到 CLI 路径，`mpt_state_root.py` 可无
   pyyaml 导入（e2e 套件复用其 trie 函数）；golden/空根双复算验证 + build-allocs CLI 回归通过。

## 3. 测试映射总表

| 断言/用例 | 位置 | 层 |
|---|---|---|
| gasPrice absent-config → 0x0 / set → 值 | `Web3RpcTest.cpp` handleValidTest | C++ RPC |
| maxPriorityFeePerGas == 0x0 | `Web3NodeStatusTest.cpp` | C++ RPC |
| eth_maxPriorityFeePerGas == 0x0（活链） | `rpc_matrix.py` a2_chain | e2e |
| withdrawals [] / root 32B / 创世形状 / getProof 交叉 | `rpc_matrix.py` a2_blocks | e2e |
| root 变化/稳定/物理槽位/Python-golden/空常数 | `predeploy_matrix.py` mp 组 | e2e + 离线 |
| opStorageRoot({}) == emptyRootHash == 字面量;seal 一致 | `OpL1BlockDepositTest.cpp` Item 4b | C++ executor |
| message_passer_write golden（既有） | `OpNewPayloadRpcE2eTest.cpp` + t8n vectors | C++ engine |

## 4. 结论

B1：op-geth 的 `eth_gasPrice` 是**本地计算**（baseFee+tip，不查 GPO），FISCO 是 ledger 配置回显
——语义分歧 D-GP-1/2 已钉死并登记，对齐为生产改动单独立案。`eth_feeHistory` 维持声明缺口。

B3：Isthmus+ 的 withdrawalsRoot=MessagePasser 存储根这条**共识关键链路**，现已形成
「op-geth golden（既有）→ C++ seal（既有+新增空态）→ Python trie（新增）→ 活链 seal↔RPC↔
getProof（新增）」四层一致证据；唯一实质缺口是创世头字段缺失（D-WR-3，单独立案）与运行时
MPT 快照范围（已知限制）。
