# Phase 1 Spike 轨 diff（opTransition vs 基线）

日期：2026-08-10 · 分支：`feat-op-eest-baseline` · HEAD `9cefb9555`

## 1. 运行记录

| 轨 | 命令 | 结果 |
|---|---|---|
| 基线（Task 3） | `eest-runner --exec-mode baseline`（全量） | **0 失败**（`assets/baseline-fails-v540.json` = `[]`） |
| spike（Task 5/6） | `eest-runner --fixture-dir …/state_tests/prague --exec-mode optransition --op-fork isthmus` | 见下 |

**本次 spike 全量运行（Prague state_tests，post key == `Prague` 子集）：**

```
Total tests:   2010
Passed:        1949
Failed:        61
Pass rate:     96%
Files:         105
Skipped files: 2048    (non-Prague post key — Osaka / transition / pre-Prague)
Load failures: 0
Elapsed:       5s
```

- 失败明细写入 `/tmp/spike-fails.json`（61 条，5 元组 `(testName, forkName, dataIndex, gasIndex, valueIndex)` 全部唯一）。
- Error breakdown：Balance 2 / Storage 3 / Nonce 60 / Code 0 / Expected-exception 0 / Unexpected-failure 60。（Nonce 60 项是 blob 拒绝「未执行 → sender nonce 未递增」的下游痕迹，见 §2.1，非独立 bug 类。）

**spike-only 5 元组 diff（vs 基线）：**

```
baseline fails: 0   spike fails: 61   spike-only: 61   shared: 0
```

基线空集 → 61 项 spike 失败全部为 spike-only。逐一完成分诊后无转录 bug 候选（§3）。

## 2. 失败分诊（61 项，3 类）

### 2.1 Blob type 拒绝 — 60 项（`prague/eip7623_increase_calldata_cost`，全部 `type_3`）

- 机制：`opValidate`（`OpTransition.cpp:345-347`）白名单拒绝 `tx.type == blob`（0x03）→ `std::errc::not_supported`；EESTRunner `runOpTransitionTx`（`EESTRunner.cpp:1559`）将其抛为 `std::runtime_error("opValidate rejected: not supported")`；因 libevmone.a（`-fno-rtti`）typeinfo 非唯一，typed `catch (std::exception&)` 漏接，落入 `catch(...)` 兜底 → 日志显示 `opTransition threw (typed catch bypassed, type: St13runtime_error)`。原始 reason 不可恢复，但 `opValidate` 白名单路径是唯一会在 blob 上抛 `runtime_error` 的出口，且 60 项 test name 全部含 `type_3`（EIP-4844 blob tx）。
- 佐证：同目录 `type_0 / type_1 / type_2`（EIP-1559/2930/7702）用例全部通过 → 白名单只拦 blob，未误伤其他类型。
- 分类：**OP 语义白名单**。OP 链（op-geth）不接受 blob tx；`opValidate` 整体拒绝 `type == 0x03` 是 OP 语义，非 opTransition 转录 bug。原因写为：`opValidate 白名单拒绝 blob type（OP 语义：op-geth 不接受 EIP-4844 blob 交易）`。

### 2.2 eip2537 BLS12_PAIRING input-size limit — 1 项（`test_valid_multi_inf`）

- 契约 `0x117c426daafd0dbe3cf3ef5210523b43d296827f` 以 `CALL(gas, 0x0f, …)` 调用 **BLS12_PAIRING**（EIP-2537），`input_size = 1,231,104` bytes（全零 multi-inf 向量）。
- `isthmusPrecompileOverrides`（`OpPrecompiles.cpp:27-33`）对 `0x0f` 登记 `max_input_size = 235008`（op-geth Isthmus BLS pairing 上限，`params/protocol_params.go` 引用在头注释）。
- `OpHost::call`（`OpHost.cpp:121-122`）：`input_size > max_input_size → return EVMC_FAILURE`。故 inner CALL 失败（success=0），契约存 `storage[0]=0, storage[1]=0, storage[2]=sha3("")` → 3 项 storage mismatch；gas 少耗 → sender/beneficiary 余额各多留（2 项 balance mismatch，均非 vault 账户，§2.3）。
- 对照：同一 fixture 用 baseline 模式单跑 **通过**（exit 0）——L1 路径无 BLS input-size 上限，配对返回正常结果。
- 分类：**OP 预编译 override 故意差异（已知分歧白名单）**。`OpPrecompiles.cpp` 头注释明确这是 op-geth Isthmus 语义（BLS MSM/pairing input-size 上限），非 opTransition 转录 bug。原因写为：`op-geth Isthmus BLS pairing precompile input-size 上限 235008 bytes（OpPrecompiles.cpp，op-geth params/protocol_params.go）`。
- 差分门备注：`OpT8nReplayTest`（`bcos-evm/test/opstack/OpT8nReplayTest.cpp`）为 OP 块级差分回放 gate，理论上可加一个「超限 BLS pairing input」向量确认该上限在 baseline/spike 两侧一致应用；因非 bug 候选，属可选扩向量，非验收前提。

### 2.3 vault / base-fee 泄漏 — 0 项（无 harness 缺陷）

- `verifyPostStateSpike`（`EESTRunner.cpp:1576-1651`）已剔除 4 个 OP vault（`OP_BASE_FEE_VAULT / OP_L1_FEE_VAULT / OP_OPERATOR_FEE_VAULT / OP_SEQUENCER_FEE_VAULT`，全 `0x4200…` 前缀）。
- eip2537 的 2 项 balance mismatch 账户为 `0x2adc…`、`0x5936…`，均非 vault 地址；差值为「precompile 失败少耗 gas → sender/beneficiary 多留」的下游效应，非 base-fee 路由语义偏移。零费用 spike 下 L1/operator fee 恒为 0，与 spec rev.5 判定一致。
- 结论：**无 vault 剔除泄漏，无 harness 缺陷**。

## 3. 转录 bug 候选：0

61 项 spike-only 全部可解释为两类 OP 语义/已知分歧：

| 类 | 数量 | 分类 | 证据 |
|---|---|---|---|
| eip7623 `type_3`（blob） | 60 | OP 语义白名单 | `opValidate` 白名单拒绝 blob（`OpTransition.cpp:345`）；同目录 type_0/1/2 全过 |
| eip2537 `test_valid_multi_inf` | 1 | 预编译 override 已知分歧 | `OpHost.cpp:121` input-size 超限 `EVMC_FAILURE`；基线同 fixture 通过 |

**结论：spike 轨验收通过**（per plan 决策树：转录 bug 候选 = 0 → opTransition 转录正确性高置信，白名单 61 项）。

## 4. 机械 diff：opTransition.cpp vs 上游 evmone v0.21.0 `test/state/state.cpp`

- 上游参照：`vcpkg/buildtrees/evmone/src/v0.21.0-a0dfff0636.clean/test/state/state.cpp`（git `f6729a3ac`，5 个 v0.21.0 buildtree 同源同 commit，任选其一）。
- `OpTransition.cpp` 头注释自述「多段照抄自 evmone test/state/state.cpp（官方 v0.21.0）」；以下为**重写版独有分支清单**（opTransition 新增/改写 vs 上游），作为后续扩差分门向量的优先清单。机械 diff 是分析辅助，非硬 gate——spike-only 失败集合仍是主判据。

### A. 交易执行主路径（`opTransition` vs 上游 `transition()`）独有分支

| # | 分支/循环 | 位置 | 相对上游的变化 |
|---|---|---|---|
| A1 | L1 cost 预扣 `sender_acc.balance -= props.l1_cost` | `OpTransition.cpp:275` | **新增**。上游只扣 `tx_max_cost`（+blob fee）；opTransition 额外预扣 L1 数据费（validate-time 快照值） |
| A2 | Operator fee 预扣 `if (has_operator_fee) balance -= operator_cost_at_gas_limit` | `:276-277` | **新增**。validate-time 快照计费 |
| A3 | blob fee 扣除整支 | 上游 `state.cpp:586-595` | **删除**（blob type 被 opValidate 白名单拒绝，该分支不可达） |
| A4 | access_list 预热循环加 `get_or_insert` | `:166-178` | **改写**。`OpHost::access_account` 对 override-table 地址早退不插入账户，须先 `get_or_insert` 再 `get_storage`（上游直接 `get_storage`） |
| A5 | Base fee 路由 vault `state.touch(OP_BASE_FEE_VAULT).balance += gas_used * base_fee` | `:295-296` | **新增**。替代上游「base fee 隐含 burn」（上游无此记账，余额只体现 sender 扣减） |
| A6 | L1 fee 路由 vault `state.touch(OP_L1_FEE_VAULT).balance += props.l1_cost` | `:297` | **新增** |
| A7 | Operator fee vault 记账 + 差额回退 `touch(OP_OPERATOR_FEE_VAULT) += opAtUsed; sender += cap - opAtUsed` | `:300-303` | **新增**。`opAtUsed` 用 validate-time `props.fee + props.jovian_operator_formula`，跨 fork 守恒（`OperatorFeeConservesWhenCfgDisagreesWithProps` 覆盖） |
| A8 | 执行/退款/floor 提为 `runTxMessage` helper | `:159-206` | **改写**。上游内联于 `transition()`；opTransition 抽出供 deposit 复用 |
| A9 | state diff 过 sanitizer `sanitizeStateDiff(view, state.build_diff(rev))` | `:312`（receipt 内） | **新增**。上游直接 `state.build_diff` |

### B. 验证路径（`opValidate` vs 上游 `validate_transaction()`）独有分支

| # | 分支 | 位置 | 相对上游的变化 |
|---|---|---|---|
| B1 | blob/超枚举白名单 `type == blob || type > set_code → not_supported` | `:345-347` | **新增**。上游接受 blob（revision 门控）；opTransition 整体拒绝 blob 并封堵 `0x05..0xFF`（含 0x7E deposit 外泄 hole） |
| B2 | `signedTxEnvelope.empty()` 检查 | `:349-350` | **新增**（L1 cost 计算需要 envelope） |
| B3 | L1 cost 计算 `computeL1Cost / computeL1CostFromFlz` | `:356-366` | **新增**。上游无 L1 数据费概念 |
| B4 | Operator cost 计算 `computeOperatorCost` | `:367-369` | **新增** |
| B5 | 512-bit 余额上限含 `l1Cost + opCost` | `:377-382` | **改写**。上游 `max_total_fee` 只含 `gasLimit*price + value + blob_fee`；opTransition 加 L1/op 两项（防 uint256 回绕下 mint） |

### C. Deposit 路径（`runDeposit`）— 整函数为 opTransition 独有

| # | 分支 | 位置 |
|---|---|---|
| C1 | `DepositValidationView` 掩码 view（mask INSUFFICIENT_FUNDS / EIP-3607） | `:404-438` |
| C2 | mint `fromAcc.balance += dep.mint` | `:453-454` |
| C3 | failed-deposit 分支（validate error 或 balance < value → nonce 强制 +1、`gasUsed = gasLimit`、status=FAILURE） | `:479-499` |
| C4 | 成功分支经 `runTxMessage` + `OpHost` 执行 | `:500-513` |
| C5 | deposit receipt meta（`deposit_nonce` / `deposit_receipt_version`） | `:523-526` |

### D. FISCO 投影层（无上游对应物）

`toFiscoStatus` / `makeFiscoReceipt` / `mapOpLogs` / `intxToBcosU256` / `toOpStackMeta` / `deriveOpReceiptMeta`（`:130-235`）——将 evmone receipt 投影到 `bcos::protocol::TransactionReceipt`（gasUsed/status/logs/logsBloom/effectiveGasPrice/opStackMeta）。纯适配层，不涉执行语义。

### E. 复用面（机械 diff 噪声点）

- `processAuthorizationList`：opTransition 复用 vendored `bcos::evm::eth::processAuthorizationList`（`OpTransition.cpp:251`），上游用 state.cpp 内 `process_authorization_list`（`state.cpp:92-178`）。两者应等价，但属「复用面」非「照抄面」，行级 diff 会因函数体不同产生噪声，等价性由差分门/spike 覆盖。
- `State` 类方法（`build_diff / insert / find / get / get_or_insert / touch / get_storage / journal_* / rollback`）与 `compute_tx_intrinsic_cost / compute_access_list_cost / compute_tx_data_tokens / finalize`：opTransition 不包含（用 vendored `eth/state` 的 `State`），属照抄面之外的库代码，不参与单 tx 转录 diff。
- 头注释 `TODO(eth-utils-removal)`（`:17-18`）已声明「多段照抄自 state.cpp」的替换风险，与本文档 A/B/C 清单一致。

**后续扩差分门向量优先清单（由 §4 A/B/C 导出）**：A1/A2 预扣与回退守恒、A5/A6/A7 vault 记账、B3/B4 费用计算、B5 上限、C3/C4 deposit 成功/失败路径、A8 refund/floor 计算、A4 预热早退分支。
