# op-node × FISCO Engine API 问题闭环（08-22 排查的解决记录）

- **日期**: 2026-08-23
- **前置**: `2026-08-22-opnode-fisco-engine-interaction-investigation.md`（op-alignment worktree）
- **环境**: C2 devnet（anvil L1 8549 + FISCO L2 8555/8566 + op-node 经抓包代理 8567→8566）
- **分支**: feat-opstack-merged（修复提交 `3ff059552`；setup 脚本修复随 `db572716b`）
- **结论**: op-node 已能驱动 FISCO 持续出块（2s 节奏，Jovian extraData/baseFee/deposit 全正确），
  用户交易经 mempool→seal→执行→receipt 全链路打通，e2e 套件基本全绿。

---

## 1. 08-22 交接文档问题清单的裁决

| 编号 | 08-22 判断 | 裁决 |
|------|-----------|------|
| A（FCU 缺 minBaseFee → INVALID） | 疑似根因 | **推翻**。抓包证明 op-node 全字段发送（gasLimit/eip1559Params/minBaseFee/transactions/withdrawals/noTxPool），FISCO 接受并返回 payloadId |
| B（缺 gasLimit 被拒） | 已证实（模拟） | **推翻**（真实流）。手动模拟不带字段才触发；op-node 真实 attrs 完整 |
| C（txRoot 重算不一致） | 最深层疑点 | **未复现**。代理重放链路下 blockHash/txRoot 校验均通过（state_verify 12/12 含 parentHash 连续性） |
| D（旧链 L1Origin 断裂） | 已随重建消除 | **确认消除**。重建后 block 1 首笔 deposit 带真实 anvil L1 origin（L1Block.number()=733+ 随链推进） |

## 2. 实际根因（按发现顺序）

### E【已修复】genesis 时间戳毫秒双重放大 → sequencer 永不 getPayload

- 现象：FCU VALID + payloadId 后 sequencer 静默，`build-started` 的 next action
  wait = `2562047h47m16.854775807s`（max int64 = 永不触发）。
- 根因：`setup_c2.sh` 写 `[eth_genesis_header].timestamp = L1_ts*1000`（毫秒），
  但 C++ 契约是**秒**（`NodeConfig` 解析 → `applyEthGenesisHeader` ×1000 存内部毫秒；
  `BlockResponse.cpp:89` /1000 服务 RPC）。双重放大后 RPC 返回 1787405857000，
  op-node `parentTime + 2s` = year-58k 时间戳。
- 修复：脚本改写秒（`hex($L1_TS)`，随 `db572716b` 入库）；重新生成 header
  （新 genesis hash `0x2f94870c...`，timestamp=0x6a89a621=rollup l2_time）+
  清 data 重建 + step5 重新对齐 rollup.json。

### F【已修复】SystemConfig eip1559Params 全零 → newPayload INVALID 活锁

- 现象：`extraData must encode a non-zero eip-1559 denominator`（与 op-geth 错误串
  完全一致——FISCO 校验是对的，链配置是错的）。
- 根因链：C2 的 L1 SystemConfig 合约（0xc5707403...）eip1559Denominator/Elasticity
  均为 0（intent.toml 有 8/2 但部署产物为 0）→ op-node 启动时从 **L2 engine 的
  genesis 视图**（`SystemConfigByL2Hash` → rollup.json seed）加载，随后被 L1 合约
  状态覆盖 → attrs 带 `eip1559Params=0x0000000000000000` → FISCO 出的块 extraData
  denominator=0 → newPayload INVALID → unsafe head 永远停在 genesis → attrs 永远
  重derive自 L1:1（修复tx在 L1:586 永远追不上）→ **活锁**。
- 修复：`cast send SystemConfigProxy "setEIP1559Params(uint32,uint32)" 8 2`
  （owner=DEV0；L1 block 586）+ rollup.json seed 同步改 `0x0000000800000002` +
  杀掉所有 op-node 重启（注意：23:08 的一次"重启"因旧进程占用端口静默失败，
  旧进程带着旧配置继续跑——排障时先 `pkill -f` 全量确认）。

### G【已修复】eth_call 全断：skipBalanceCheck 未接线

- 现象：所有 `eth_call`/带数据 `eth_estimateGas` 返回
  `OpScheduler::call: unknown (RTTI-bypassed) exception`（历史 tag 也一样）。
- 定位：lldb 断 `__cxa_throw` + 逐步日志插桩 → `m_prepare` 的 `opValidate` 抛
  `boost::wrapexcept<OpTxValidationFailed>`（"insufficient funds"——模拟 sender
  无余额）。`opValidate(..., bool skipBalanceCheck=false)` 本为 eth_call 设计但
  **无任何生产调用方传 true**。分类后的 `OpConsensusError` 在 -fno-rtti 边界
  丢失类型信息，落进 `catch(...)` 变成 "unknown exception"。
- 修复（`3ff059552`）：`m_prepare` 增加 `skipBalanceCheck` 参数，两个调用点
  （ExecuteContext::prepare 与 executeTransaction）传 `call`；
  `opValidate` 失败时打 WARNING（reason/sender/nonce）——类型系统带不过去的
  错误信息用日志补位。

### H【测试配置乌龙，非缺陷】chain_driver 首跑"tx 不上链"

- `sealedTxs=1` 日志证明交易被 seal 进 payload；真实失败是
  `insufficient funds`：我把 PRIVKEY(ac0974=DEV0/0xf39F) 与 SENDER(0x7099=DEV1)
  配对错了。正确配对：`0x7099...` ↔ `59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d`
  （注意 setup_c2.sh 里 DEV1 的 key 少一位是错的，用 `cast wallet private-key
  --mnemonic ... --mnemonic-index 1` 取）。
- 附带发现：**失败交易会毒化 mempool**——验证不过的交易留在池里被反复 seal，
  每次块构建整体失败，新交易也无机会上链（重启清池恢复）。后续可考虑
  seal/执行失败后按 hash 驱逐。

## 3. 当前链上事实（修复后）

- 出块：op-node sequencer 每 2s 驱动，块内 = L1 attributes deposit +（有则）用户交易；
  extraData 17B Jovian（`01|00000008|00000002|minBaseFee`）。
- L1Block 预部署实时推进：`number()/timestamp()/basefee()` 跟随 anvil。
- 用户交易：submit→mempool→buildOpPayload seal→双 pass 执行→receipt/nonce/balance
  全链路 OK；**L1 data fee 正确收取**（每笔 ~0.0136 ETH，anvil 低 basefee 下）。
- baseFee 独立验证：state_verify 的 `baseFee == calcOpBaseFee(parent)` 连续块对通过。

## 4. e2e 结果快照（C2，2026-08-23 00:2x）

| 套件 | 结果 | 备注 |
|------|------|------|
| rpc_matrix | 56 pass / 3 fail | 3 个失败均为环境性：safe/finalized tag 停 genesis（无 op-batcher，cross-safe 不推进——见 §5）；pending tag 与 latest 实测相等（2s 出块竞态偶发） |
| chain_driver | 44 pass / 1 fail | 唯一失败 balance-exact 差 285 wei（3×95）：driver 的 L1 费用估算取整 vs 实际 L1 data fee，链侧行为正确 |
| state_verify | **12/12** | 需 `--db /tmp/c2/fisco/data/1/latest`（默认指向旧 B3a 库会误报 2 项） |
| predeploy_matrix | 29 pass / 2 软失败 | l1block_seq 探针取到同 epoch 两块（软）；mpt_state_root 模块缺失（测试环境）；SystemConfig 预部署组在"Proxy: implementation not initialized"处中断（allocs 未种 implementation slot，见 §5） |
| a1_active | 未跑 | 手动 FCU 会与在线 sequencer 冲突；该套件面向 idle engine-mode（B3a）链 |

## 5. 遗留问题（按优先级）

1. **无 op-batcher → safe/finalized 永停 genesis**：cross-safe 只有 L1 上出现 batch
   才推进。C2 想要完整 OP 闭环需跑 op-batcher（monorepo 有构建）向 anvil 投递。
2. **RTTI 错误通道**：opstack-executor（-fno-rtti TU）抛出的分类错误
   （OpConsensusError 等）跨边界后 `catch(std::exception&)` 不匹配，RPC 只能看到
   generic "unknown exception"。已用 WARNING 日志补位（G 修复），根治需统一
   RTTI 编译选项或改用错误码传递。
3. ~~**revert 错误呈现**~~【已修复 a9fcf74f1，2026-08-23】eth_call revert 现返回 op-geth 规范的
   `{"code":3,"message":"execution reverted[: <reason>]","data":...}`，Error(string)
   (0x08c379a0) 解码进 message。实测：`code 3 | execution reverted: Proxy: implementation
   not initialized` ✓。
4. ~~**SystemConfig 预部署播种**~~【结案：非链缺陷，是测试 fixture 用错地址】链上 0x4200...1000
   的 EIP-1967 slots 本来就正确（impl=0x...1002, admin=0x...18，eth_getStorageAt 验证），
   `getValueByKey()`/`owner()` 均正常。predeploy_matrix 旧 worktree 版查询了 OP 保留段
   0x4200...00c0（op-deployer base 里未初始化的占位 proxy）才报 "implementation not
   initialized"。主仓库版测试用对了地址，SystemConfig 组全绿（syscfg_get_default /
   syscfg_owner_governance / syscfg_set_reverts_unwritable）。
5. ~~**mempool 毒化**~~【已修复 a9fcf74f1，2026-08-23】三层修复：(a) executor 把失败 tx 的
   hash 以 `[tx=0x..]` 嵌进块校验错误串（bcos::Error 跨 delegate 只能带字符串）；
   (b) MemPoolImpl 新增 `removeByHash`；(c) buildOpPayload 重构为 build-and-probe 循环——
   失败点名 sealed tx 时驱逐该 tx 并重试（对齐 op-geth worker 对 Prepare 失败交易的丢弃
   语义），循环以 sealed 数为上界。实测：提交无余额 DEV0 毒交易 → 日志
   `evicted poisoned pool transaction, tx=0x0d835ca1...` → 出块不中断、毒交易永不上链、
   正常交易照常落地（chain_driver 30/1）。
6. **setup_c2.sh 的 DEV1 key 字段缺一位**（40 hex chars）：应换 mnemonic 派生。
7. Karst fork 特性：按用户决定当前版本不支持（维持排除）。

### 5.1 新发现：EngineServiceTest 6 个合并遗留失败（非本次修复引入）

`feat-opstack-merged` HEAD 上 `EngineServiceTest` 26 例中 6 失败（基线对照确认与
a9fcf74f1 无关、修复前后失败集合完全一致）：

- new_payload_v3_with_transactions_is_validated_not_accepted
- new_payload_v3_rejects_blob_versioned_hashes
- build_payload_excludes_native_transactions_full_loop
- new_payload_round_trips_deposit_raw_bytes
- karst_v3_build_v5_get_v4_commit_round_trip
- new_payload_v4_rejects_nonempty_lists_and_missing_fields

症状：self-built payload 走 newPayload 快速路径时**跳过了 V3/V4 请求级校验**
（expectedBlobVersionedHashes / executionRequests 等，校验代码在
EngineServiceImpl.cpp:498/:664 存在但未被该路径调用）——返回 Valid(0) 而非
Invalid(1)。与 feat-opstack-e2e 时代修复过的"统一 V3/V4 校验"在合并中丢失有关，
需在 engine 侧恢复"self-built 快速路径也要跑请求级校验"。

### 5.2 本次修复的验证记录（2026-08-23 10:0x）

- 单测：mempool/AnyMemPool/AnyEngineService/EngineRpc/Mapper/Helper **80/80 绿**；
  opstack-executor 套件 **4/4 绿**；EngineServiceTest 20/26（6 个为 §5.1 合并遗留）。
- 链上：revert 呈现 ✓；毒交易驱逐 ✓（出块连续 20683→20715）；chain_driver 30/1
  （唯一失败仍是 driver 自身 L1 费用估算取整）；rpc_matrix 56/3（无 batcher 的
  safe/finalized 环境性失败 + pending 竞态）；predeploy_matrix（主仓库版）35/2
  （SystemConfig 组全绿；余 2 个为同 epoch 探针与本地缺 mpt_state_root 模块）。

## 6. 环境快照（当前可用）

- anvil L1: 8549（chain 900900）；SystemConfig 0xc5707403d7b5013eb314874c655b41bb5f7f2450（已设 8/2）
- FISCO L2: /tmp/c2/fisco（feat-opstack-merged 二进制，web3 8555 / engine 8566）
- genesis: hash `0x2f94870c...`，timestamp 1787405857（秒）
- rollup.json: eip1559Params=0x0000000800000002，l2_time=1787405857
- op-node: 经抓包代理 `--l2 http://127.0.0.1:8567`（`/tmp/c2/engine_proxy.py`，
  抓包 `/tmp/c2/engine_capture.jsonl`）；日志 /tmp/c2/op-node-debug.log
- chain_driver C2 版: /tmp/c2/chain_driver_c2.py（DEV1 正确配对）
- 重启 FISCO: `cd /tmp/c2/fisco && ulimit -s 65520 && nohup <main-repo>/build/fisco-bcos-air/fisco-bcos -c config.genesis -g config.genesis &`
