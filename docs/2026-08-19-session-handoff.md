# 交接文档 — 08-19 会话（D2 Phase B 全清 + Tier-2 Phase A/B + C2 启动）

## 一、分支状态

| 分支 | 状态 |
|---|---|
| `op-alignment-on-scheduler` | **HEAD = 70d041c91**，领先基线 37 commit，**未推送** |
| `ywy2090/op5429-01` | 17 commit（review 修复），已推送，**已清理掉误推的 docs** |

## 二、C++ 测试

| Suite | 状态 |
|---|---|
| Web3ResponseTest | **11/11** ✅ |
| testWeb3RPC（含 safe/finalized 路由） | 10/11（jwtHttpRequestAuthTest 预存失败） |
| LedgerTest | **27/27** ✅ |
| OpstackExecutor/Block/Detail | **全绿**（1 个预存 fail：OpstackExecutorTests deposit envelope——测试代码 Bug，非代码 Bug，已修复 d5a5cae25） |
| BcosEvmOpstack | **全绿** |

## 三、e2e 门禁（run_all）

| 脚本 | 结果 | 备注 |
|---|---|---|
| rpc_matrix | **57/0/2** known-red（withdrawalsRoot + roundtrip）→ 后升至 **59/0/0** | PBBR/gasLimit/safe/finalized/withdrawalsRoot D2 全通 |
| state_verify | **12/12** ✅ | hash 链路修复后（canonical hash override） |
| chain_driver | **31/31** ✅ | tx/receipt index+cumulative 修复后 |
| b4_persist | **3/3** ✅ | |
| b3_contracts | **12/12** ✅ | |
| predeploy_matrix | **38/38** ✅ | withdrawalsRoot 传播修复后 |
| a1_active | **11/11** ✅ | V4 驱动全通 |
| **run_all 门禁** | **ALL GREEN（零豁免、零 known-red）** | |

## 四、本次已完成工作（按优先级）

### Tier-1（D2）— 全部完成 ✅
1. safe/finalized 路由 + strict-null（op-geth 语义）：`8d728bfce`
2. BlockResponse gasLimit/PBBR 读 header（PBFT 回退）：`b6074d577`
3. canonical 块哈希（s_number_2_hash 覆写 tars 重算）：`0dee18add`
4. V4 端点接线 + 能力通告恢复：`9a6fee4ff`

### Tier-2 Phase A（OP payload 构建）— 完成 ✅
5. buildOpPayload 两段执行（探针 + canonical pass）、getPayload 解禁、自建快路径、V4 门控：`b978a3b5a`
6. B3 出块恢复（1 块/秒）、a1_active 11/11、run_all ALL GREEN

### Tier-2 Phase B（C2 硬前置）— 完成 ✅
7. tx/receipt 字段修复（cumulative 10 进制 + transactionIndex）+ t8n 根值比较：`8c5e75e15`
8. withdrawalsRoot 传播（header→RPC）：`ae4071238`
9. attrs.transactions deposit 停用合成（真 op-node 路径）：`4418d5288`
10. B3a config.genesis op-fork-base-allocs 转换：`ac2f00d45`

### Bug A（keyPage/raw 布局断裂）— 完成 ✅
- 诊断 + 修复：`c931e1b8c`（keyPageSize=0 按执行器版本选路）
- 根因：RPC 状态读走 keyPage 分页、genesis/执行器写 raw 行，读写布局不对称

### Bug B（eth_call -32603 "Invalid argument"）— 完成 ✅
- 诊断：lldb 钉死 `OpTxValidationFailed` ← 空签名信封 EINVAL
- 修复：CallRequest 信封填充 + balance skip + V4 attrs 门控 + getPayload 信封要求：`37a4d11bc`

### C2-1：op-deployer + anvil L1 + L2 genesis — 完成 ✅
- anvil L1（900900）+ op-deployer 部署 → 2341 L1 合约
- L2 genesis 提取：2341 账户（2065 个带 storage）

### C2-2：FISCO C2 链启动 — 完成 ✅
- B3a 模板 + C2 allocs（2345 账户/2069 storage）+ C2 eth_genesis_header
- **chainId=914901、genesis hash/stateRoot 验证、engine RPC 8566 UP**

## 五、剩余工作项

### 高优先级
1. **C2-3：op-node 构建 + sequencer 对接**（当前卡点）
   - op-node 已从 monorepo 构建（`/tmp/c2/op-node`，73MB），但 superchain-configs.zip 的字典文件打包路径不匹配 op-core 的 `Open("dictionary")` 调用——需修 zip 结构（dictionary 放根级）后重启验证
   - 然后写 rollup.json（chainId=914901, FISCO engine URL=127.0.0.1:8566, L1 RPC URL, L2 chain ID）
   - op-node sequencer 模式启动：op-node → FISCO V4 engine → deposit 派生 → 新块
2. **C2-4：闭环断言**（deposit→L2 到账、withdrawal→L1 finalize）

### 中优先级
3. **Tier-2 Phase B⑤：C2 新链重 init 后全量回归重定基线**
4. **Bug C retraction**（allocs.ini 无 storage 为已知限制 — D1 CI 后刷新）
5. **D1 CI：op-deployer base-allocs 生成**（需 secrets）

### 低优先级 / 大立项
6. **D4 reorg**：引擎 FCU head 切换 + MultiLayerStorage 回退 + RPC 一致性
7. **C1 EF 语料接入（P0）**、**C4 Fuzz**、**Karst D-2**
8. **PR #5429 拆分**、**分支合并推送**

## 六、关键路径（C2-3 接下来做什么）

C2-3 的下一步步骤：
1. 修 superchain-configs.zip（字典路径修正：`extra/dictionary` → zip 根级；加入 chains.json/README.md）→ 重编 op-node → `--version` 不再 panic
2. `/tmp/c2/op-node run` 写 rollup.json（指向 127.0.0.1:8566 V4 engine）+ L1 RPC（anvil 8549）
3. 启动 op-node → FISCO C2 链产生区块 → deposit 派生闭环
4. a1_active 升级为闭环断言

## 七、环境速查

| 资源 | 路径/端口 |
|---|---|
| C2 FISCO 节点 | /tmp/c2/fisco/（PID in node.pid，8555 web3，8566 engine，20213 internal，31400 p2p） |
| anvil L1 | 127.0.0.1:8549（chain 900900） |
| op-node 二进制 | /tmp/c2/op-node |
| L2 genesis allocs | /tmp/c2/allocs-new.ini（2345 账户，2069 storage） |
| C2 config.genesis | /tmp/c2/fisco/config.genesis（15762 行，B3a 模板 + C2 allocs/header） |
| rollup.json | /tmp/c2/rollup.json（op-deployer inspect 输出） |
| l2genesis.json | /tmp/c2/l2genesis.json（15437 行，2341 账户，2065 storage） |
| 现有测试基线 | rpc_matrix 57/0/2 → 59/0/0；C++ 53/1（1 预存）；run_all ALL GREEN |
| Bug B 预存失败 | OpstackExecutorTests 的 deposit envelope 测试（测试 Bug，非代码 Bug，已修复 d5a5cae25） |

## 八、重要决策记录

- **R1：safe/finalized strict-null** — OP 模式（engineService non-null）nullopt → 抛 JsonRpcException(-32000) → getBlockByNumber 返回 null / 状态端点返回 error；非 engineService（PBFT）保持 latest 别名。用户批准（grill-me 审查后）
- **Tier-2 Phase A 架构**：两段执行（探针 verify=false → canonical verify=true + reset）+ payloadId→getPayload→newPayload 快路径 commit；V4 门控对齐（FCU + newPayload 要求 V4）
- **Bug A 修复策略**：`keyPageSize=0` 按 executor_version 选路（v1=10240/raw，v2+=0/raw）；影响面仅 EthEndpoint::getStorageAt（唯一 getStateStorage 调用者）
- **Bug B 修复策略**：CallRequest → Web3Transaction（legacy + nullopt chainId + dummy sig）填充信封；validate_transaction 跳过 balance（模拟语义，op-geth 对齐）；gasLimit 缺省 30M
- **C2 FISCO 链启动**：B3a 模板（正确 sections）+ C2 allocs/header；需 `ulimit -s 65520`（Stack overflow 解除）+ 端口避让（8555/8566/20213/31400）

## 九、后续最紧迫项

1. **C2-3 收口**（1-2h）：修 superchain-configs.zip → op-node 可执行 → 启动 sequencer → 块产出
2. **Tier-2 OP 链重 init**：B3a 从干净链重新 init（旧数据混合了开发期各次变更）
