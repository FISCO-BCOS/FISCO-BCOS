# 交接文档 — op-node × FISCO 全链路打通 + P0 清零（2026-08-23 上午）

- **分支**: `feat-opstack-merged`（主仓库 `/Users/octopus/octo/code/FISCO-BCOS`）
- **状态**: op-node 驱动 FISCO 出块的全链路已打通且稳定（L2 高度 23200+，2s 节奏追平墙钟，
  零 INVALID）；所有 P0 修复已入库并验证；**无进行中的未完成工作**。
- **本文档用法**: compact 后让 agent 读这份文档即可继续。配套细节在
  `docs/2026-08-23-opnode-fisco-e2e-resolution.md`（问题裁决 + 修复记录 + 遗留清单）。

---

## 1. 本会话完成的工作（时间序）

| 提交 | 内容 |
|------|------|
| `3ff059552` | eth_call 断裂修复：opValidate 的 skipBalanceCheck 接线到 call 路径 + 失败 WARNING 日志 |
| `82b0e9f2c` | 结论文档（08-22 排查闭环：问题 A/B 推翻、根因 E/F/G/H） |
| `a9fcf74f1` | ①revert 呈现对齐 op-geth（code 3 + Error(string) 解码）②mempool 毒交易驱逐（tx hash 错误通道 + MemPoolImpl::removeByHash + buildOpPayload 驱逐重建循环） |
| `4d27c940c` | 文档：三项修复记录 + SystemConfig 裁决 + §5.1 六个测试失败清单 |
| `da02d7a32` | **P0**：通用 newPayload 分支恢复 release 语义（删 Accepted 转义 / 补 V3+V4 请求级校验 / 移除 !c_opMode V4 拒绝守卫）→ EngineServiceTest 26/26 |
| `33b5b8feb` | 文档：P0 诊断修正（通用分支回归，非 OP 快速路径） |

并行会话相关：`b265244ce`（并行会话把误退的 StubMemPool stub 合回）。**该会话曾两次
回退本会话未提交的工作树修改**——工作做完必须立即提交；开工前先 `git status` 确认。

## 2. 根因速查（全部已修复，细节见 resolution 文档）

- **E** genesis 时间戳毫秒双重放大（setup_c2.sh 已改秒，`db572716b` 内）
- **F** SystemConfig eip1559 全零 → anvil 上已 `setEIP1559Params(8,2)` + rollup.json 同步
- **G** eth_call 断裂 = skipBalanceCheck 未接线（`3ff059552`）
- **H** chain_driver"不上链"= 测试 key/地址配对错误（DEV0↔0xf39F…，0x7099…↔DEV1 key）
- **P0** 通用分支三处回归（`da02d7a32`）——注意诊断修正：**不是** OP 快速路径跳校验

## 3. 测试基线（当前全绿口径）

| 套件 | 结果 | 命令/备注 |
|------|------|----------|
| EngineServiceTest | **26/26** | `ctest --test-dir build -R "^EngineServiceTest/"` |
| rpc/mapper/helper/mempool/Any | **80/80** | `-R "^(EngineRpcTest\|MemPoolImplTest\|TestAny…)/"` |
| opstack-executor | **4/4** | `-R "opstack\|Op…"` |
| rpc_matrix | 56/3 | 3 失败=环境性（safe/finalized 停 genesis 因无 op-batcher；pending 2s 出块竞态） |
| chain_driver | 30/1 | 1 失败=driver 自身 L1 费用估算 95 wei 取整，链侧行为正确 |
| state_verify | **12/12** | 必须 `--db /tmp/c2/fisco/data/1/latest`（默认指旧 B3a 库会误报） |
| predeploy_matrix | 35/2 | 用**主仓库版**（worktree 旧版 SYSTEM_CONFIG 地址是错的 0xc0）；2 软失败=同 epoch 探针+本地缺 mpt_state_root |
| a1_active | 未跑 | 手动 FCU 与在线 sequencer 冲突，面向 idle 链 |

## 4. C2 环境快照（全部在跑）

- **anvil L1**: PID 95655, port 8549, chain 900900, mnemonic 标准 foundry dev
- **FISCO L2**: PID 54748, `/tmp/c2/fisco`，web3 **8555** / engine **8566**，
  二进制 `build/fisco-bcos-air/fisco-bcos`（含全部修复）
- **op-node**: PID 54997，经抓包代理 `--l2 http://127.0.0.1:8567`；代理
  `/tmp/c2/engine_proxy.py`（PID 98390，抓包 `/tmp/c2/engine_capture.jsonl`）
- genesis: hash `0x2f94870c...`，timestamp 1787405857（秒）；rollup.json
  eip1559Params=`0x0000000800000002`
- L1 SystemConfig `0xc5707403d7b5013eb314874c655b41bb5f7f2450`（已设 8/2，owner=DEV0）
- FISCO SystemConfig 预部署 `0x4200...1000`（工作正常，`getValueByKey`/`owner` 可用）

### 重启命令（依次）
```bash
# FISCO
kill $(cat /tmp/c2/fisco/node.pid); sleep 2
cd /tmp/c2/fisco && ulimit -s 65520 && nohup \
  /Users/octopus/octo/code/FISCO-BCOS/build/fisco-bcos-air/fisco-bcos \
  -c config.genesis -g config.genesis > nohup.out 2>&1 & echo $! > /tmp/c2/fisco/node.pid
# op-node（重启后 FISCO 高度会停住直到 op-node 回来）
nohup /tmp/c2/op-node --rollup.config /tmp/c2/rollup.json \
  --rollup.l1-chain-config /tmp/c2/l1_chain_config.json \
  --l1 http://127.0.0.1:8549 --l2 http://127.0.0.1:8567 \
  --l2.jwt-secret /tmp/c2/fisco/jwt.hex --l2.enginekind geth --l1.beacon.ignore \
  --sequencer.enabled --sequencer.l1-confs 1 \
  --p2p.sequencer.key 0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80 \
  --log.level debug --log.format json > /tmp/c2/op-node-debug.log 2>&1 & echo $! > /tmp/c2/op-node.pid
```

### e2e 运行参数
```bash
cd tools/op-e2e   # 主仓库版！worktree 版部分脚本常量过时
python3 rpc_matrix.py --port 8555 --engine-port 8566 --jwt-secret /tmp/c2/fisco/jwt.hex \
  --sender 0x70997970C51812dc3A010C7d01b50e0d17dc79C8
python3 state_verify.py --db /tmp/c2/fisco/data/1/latest --node-rpc http://127.0.0.1:8555
# chain_driver/predeploy：主仓库版硬编码旧链常量，用 /tmp/c2/chain_driver_c2.py
# 和 sed 替换过的 predeploy 副本（SENDER=0x7099…, PRIVKEY=59c6995e…90d, CHAIN_ID=914901, port=8555）
# 毒交易注入验证脚本: /tmp/c2/submit_poison.py
```

### 关键密钥配对（曾经踩坑）
- `0x70997970C51812dc3A010C7d01b50e0d17dc79C8`（L2 创世资助 10000 ETH）↔ key
  `59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d`（anvil #1）
- `0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266`（DEV0，L2 余额 0，可当毒交易发送者）↔
  `ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80`
- setup_c2.sh 里的 DEV1 key 字段**少一位是错的**，用 `cast wallet private-key --mnemonic … --mnemonic-index 1` 派生

## 5. 剩余工作项（P0 已清零，按优先级）

1. **op-batcher**（P1，下一步推荐）：monorepo（`/Users/octopus/octo/code/blockchain-impl/optimism`）
   有构建；向 anvil 8549 投 batch 后 cross-safe 才推进 → safe/finalized 解卡 →
   rpc_matrix 3 个失败转绿 → deposit/withdraw 跨链闭环可测（cast 命令在
   setup_c2.sh 尾部注释里）。
2. **RTTI 错误通道**（P1）：opstack-executor(-fno-rtti) 抛的分类错误跨边界丢失
   （现为 WARNING 日志补位）；根治=统一 RTTI 或错误码传递。
3. **工具整合**（P1）：主仓库 chain_driver/predeploy_matrix 换成 env 参数化版本
   （worktree op-alignment 有），消除双脚本 fixture 错位（SystemConfig 0xc0 乌龙来源）。
4. 打磨（P2）：setup_c2.sh DEV1 key 修复；l1block_seq 探针；mpt_state_root 模块；
   chain_driver L1 费用估算；e2e 基线进 CI。

## 6. 行为准则（本会话血泪教训）

1. **做完即提交**：并行会话会回退未提交修改（已发生 3 次）。
2. **开工先 `git status`**：确认没有别人未完成的半成品。
3. 测试跑错 fixture 比代码 bug 更常见：先确认脚本版本/数据库路径/地址常量。
4. urllib 必须配 `ProxyHandler({})`，否则 http_proxy 劫持本地请求。
5. `cast` 直接可用；`cast wallet/private-key --mnemonic` 派生密钥最可靠。
