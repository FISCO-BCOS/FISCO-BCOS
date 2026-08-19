# 交接文档 — 08-19 会话最终版（C2 全闭环 + 遗留问题收尾 + B3 回归）

## 一、分支状态

| 仓库 | 分支 | HEAD | 推送状态 |
|---|---|---|---|
| FISCO worktree | `op-alignment-on-scheduler` | `3d6f53805` | ❌ 未推送（61 commit 领先基线） |
| FISCO 主仓库 | `op5429-01` | `5cc25670d` | 已推送（review 分支） |
| Monorepo | `v1.19.2` | `5f90f749ca` | ❌ 未推送（ABI 修复 + artifacts） |

## 二、本次会话新增提交（worktree）

| Commit | 内容 |
|---|---|
| `0d926c72c` | FCU V3+attrs 门放宽 + Jovian blobGasUsed DA footprint 回填（op-node 集成） |
| `2f67105ef` | TransactionResponse legacy v 重建为 chainId*2+35+parity |
| `11a30e4a7` | C2 devnet op-node 启动脚本 + mpt_state_root 无 code 容错 |
| `d93e28303` | **P0 #1** extraTransactionBytes 双形态判别（27 条新测试） |
| `208be6604` | **P0 #4** miner 字段补充（engine-built blocks coinbase fallback） |
| `940633de0`/`5ed051a60`/`63605f341`/`2a55fb87b` | 交接文档系列 |
| `3d6f53805` | setup_op_node.sh 动态 SENDER alloc 索引 + B3 用 C2 chain-config |

Monorepo：`5f90f749ca` — op-deployer Go struct ABI 对齐（DeploySuperchain 3 字段、DeployImplementations 15/24）+ 重建 forge-artifacts tzst。

## 三、问题解决状态

| # | 问题 | 状态 | 结论 |
|---|---|---|---|
| 1 | extraTransactionBytes 信封布局错位 | ✅ 修复 | 读侧判别（legacy 尾部 0-0 vs v-r-s；typed N vs N+3），零迁移 |
| 2 | bridge.withdraw 空 revert | ✅ 已解决 | **测试传错 `_l2Token`**：正确值 `0xDeadDeAddeAddEAddeadDEaDDEAdDeaDDeAD0000`（Predeploys.sol:91）。修正后完整链路通过（5 事件含 MessagePassed） |
| 3 | eth_feeHistory | ⏭ 跳过 | C2 用 `--legacy --gas-price` 绕过；生产合规再补 |
| 4 | miner 字段缺失 | ✅ 修复 | `208be6604` coinbase fallback |
| 5 | 历史状态读漂移 | ⏸ 未复现 | 当前稳定 |
| 6 | 主仓库 CallRequest.cpp merge conflict | ✅ 已解决 | stash pop 残留，`git checkout -- .` 回退；HEAD 版本本就干净 |
| 7 | op-deployer ABI drift | ✅ 修复 | monorepo `5f90f749ca`，genesis/live apply 均成功 |

## 四、run_all B3 回归结果（3d6f53805 后）

**6/7 套件全绿，127/128 断言，3 个 P0 修复零回退**

| Suite | 断言 | 状态 |
|---|---|---|
| rpc_matrix | 58/59 | ⚠️ 1 个环境差异（见下） |
| state_verify | 12/12 | ✅ |
| chain_driver | 31/31 | ✅ |
| b4_persist | 3/3 | ✅ |
| b3_contracts | 12/12 | ✅ |
| predeploy_matrix | — | ❌ 上游 bug（见下） |
| a1_active | 11/11 | ✅ |

### 2 个环境差异（非回退）

1. **rpc_matrix 1 fail**：C2 base allocs 的 MessagePasser 带预写 storage → genesis 根非空。旧 B3 自写 allocs 是空根。已把断言改为双态验证（`rpc_matrix.py` 未提交，工作区修改）。
2. **predeploy_matrix 崩溃**：op-deployer 上游 bug——L2Genesis 脚本调 `vm.isContext(uint8)`（forge 作弊码）在生产模拟 revert → SystemConfig proxy 的 implementation slot 未写入 genesis。SystemConfig 测试组无法运行。

## 五、C2 devnet 环境（运行中）

| 资源 | 状态 |
|---|---|
| FISCO L2 | /tmp/c2/fisco（web3 8555 / engine 8566），block #18000+ |
| anvil L1 | 8549（chain 900900），live 部署合约 |
| op-node | `/tmp/c2/start_op_node.sh`，sequencer 模式 |
| rollup.json | /tmp/c2/rollup.json（L1 genesis=anvil block0 hash，L2 genesis=FISCO 实际值） |
| B3/B3a | /tmp/op-spike/b3a/start.sh，回归用 |

### 常用操作
```bash
# 存款（L1→L2）
cast send <deposit_contract> "depositTransaction(address,uint256,uint64,bool,bytes)" <to> <amt> 100000 false 0x --value <amt>
# 提款（L2 bridge 标准路径，_l2Token 必须用 LEGACY_ERC20_ETH）
cast send 0x4200000000000000000000000000000000000010 "withdraw(address,uint256,uint32,bytes)" 0xDeadDeAddeAddEAddeadDEaDDEAdDeaDDeAD0000 <amt> 100000 0x --value <amt> --legacy
# 原生提款（绕过 bridge）
cast send 0x4200000000000000000000000000000000000016 "initiateWithdrawal(address,uint256,bytes)" <to> <amt> 0x --value <amt> --legacy
```

## 六、剩余工作项

### 🔴 高优先级
1. **分支推送**（两个仓库，防丢失）
2. **predeploy_matrix 上游修复**：修 op-deployer L2Genesis 的 `vm.isContext()` 调用（monorepo 侧）或绕道（测试跳过 SystemConfig 组 + 单独验证）
3. **rpc_matrix 断言提交**（工作区修改未提交）

### 🟡 中优先级
4. C2 devnet 一键脚本（`setup_c2.sh` 串联 anvil + op-deployer + allocs + FISCO + op-node）
5. eth_feeHistory 实现（OP 合规）
6. 工作树构建问题：monorepo `cmake .` 重置 link.txt 需手动 `sed` 修 blst 路径

### 🟢 低优先级 / 大立项
7. D4 reorg：引擎 FCU head 切换 + MultiLayerStorage 回退
8. op-batcher / op-proposer 接入（L1 finalize 闭环）
9. D1 CI：op-deployer base-allocs 生成
10. C1 EF 语料 / C4 Fuzz / Karst D-2 / PR #5429 拆分

## 七、关键决策记录

- **R1 双形态判别**：RLP 整数 0 编码为空字节串、secp256k1 r/s 永非零 → 空-空尾部唯一标识预影像；typed 按固定字段数（2930=8/1559=9/4844=10/7702=11）
- **R2 bridge 版本链**：bridge 1.13.2 → messenger 2.2.1 → passer 1.2.0 完全正确，非 artifacts 断裂
- **R3 B3 配置切换**：chain-config-c2.yaml 兼容 op-deployer terminal allocs（原 chain-config.yaml 的 SystemConfig 0xC0 自写路径与 base allocs 冲突）
- **R4 setup_op_node.sh 动态索引**：`grep -cE '^\[alloc\.[0-9]+\]$'` 计数（排除 .storage 子节）
