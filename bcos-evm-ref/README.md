# bcos-evm-ref

Spec: `bcos-evm/docs/superpowers/specs/2026-07-09-bcos-evm-ref-rev8-opstack-foundation-design.md` (rev.8.2)
（前置：`2026-07-08-bcos-evm-ref-evmone-reuse-design.md` rev.7，evmone 基线 / §4.3 OP 接口草图仍有效，冲突以 rev.8 为准）

复用 evmone::state（vcpkg overlay port，REF 3585c2cb = evmone 0.21.0 + SM3）的 **ETH + OP 统一 evmone 执行底座**：
`bcos-evm-ref/eth/` 为 OpStack 与纯 ETH 的共享内核，`bcos-evm-ref/opstack/` 在其之上实现 OP 薄层（仓库主流双名布局，头源同居）。
生产编排仍留 `bcos-evm/opstack/`；与现有 `bcos-evm/` 严格隔离（互不 include / 不链接）。

## 当前阶段（rev.8.2）

| 里程碑 | 状态 |
|--------|------|
| M0 overlay 导出 / M1 writeback / M2 EEST state / M3 EEST blockchain / M3.5 P1 读放大 spike | ✅ 完成（见下方验收记录） |
| M4 OP 数据层（`OpForkSchedule`/`OpPredeploys`/`OpPrecompiles`/`OpFeeParams`/`OpDepositTx` + 向量 schema） | ✅ 完成（11 单测 + `docs/vector-schema.md`） |
| M5 OP 执行层（`OpHost`/`opValidate`/`opTransition`/`runDeposit`/`RollupCost` + 块级 harness） | ✅ 完成（32 单测含 §4.4 冒烟） |
| Jovian tx+receipt + Karst 占位 + G-1 空 envelope 护栏 | ✅ 完成（见 `2026-07-10-bcos-evm-ref-jovian-karst-tx-receipt-design.md`） |
| P1 tx-alignment（7702/7623/L1/vault/pre-Isthmus） | ✅ 完成（见 `bcos-evm/docs/superpowers/specs/2026-07-10-bcos-evm-ref-p1-tx-alignment-design.md`） |
| M6 零值差分 + upstream diff | ✅ 完成（`OpZeroDiff` 运行时护栏 + `scripts/upstream-diff.sh` 照抄面静态漂移检测） |
| M3.5 P2 真账本桥接 / E-b（ref t8n gate + 生产切内核） | 🅿️ **park**（E-b 解冻前不得宣称 OP 路径生产可用 / op-geth 等价，见 spec §1.1 R2） |

### Jovian / Karst / Isthmus P0（2026-07-10）

- **Jovian tx 执行**：operator 公式 `gas×scalar×100+constant`、precompile 限长、`da_footprint_gas_scalar` 解包、`jovianConfig()`
- **Receipt**：`opTransition` 返回 `OpTxReceipt { receipt, meta }`；`deriveOpReceiptMeta` 填充 op-geth 对齐的 L1 直通字段（`l1_gas_price` / `l1_blob_base_fee` / 两个 L1 scalar / `l1_fee`）；Jovian+ 另填 `da_footprint_gas_scalar` / `da_footprint`；`operator_fee` 为 FISCO 扩展
- **Karst**：`karstConfig()` 占位，执行与 receipt 行为暂等同 Jovian（`OpFork::Karst` 枚举值不同）
- **G-1**：非 deposit 用户 tx 若 `signedTxEnvelope` 为空 → `opValidate` 失败（禁止静默 `l1_cost=0`）
- **N-1（澄清）**：`opTransition` L1 vault 结算**不变**——无条件 `AddBalance(L1FeeVault, l1_cost)`，即使 `l1_cost==0` 也会 touch；**不**声称「l1_cost==0 时跳过 L1 vault」
- **M6 不变**：零值差分断言层仍用 `nonVaultDeleted` 排除 vault 账户（含 L1 vault 被 touch 后剪除的空账户）

**显式非目标**（本轮不做）：块头 DA / `extraData` / E-b t8n gate / receipt 字段 `L1GasUsed`（Fjord+ 恒 0）/ `FeeScalar`（pre-Ecotone 遗留）

### P1 tx-alignment（2026-07-10）

Spec: `bcos-evm/docs/superpowers/specs/2026-07-10-bcos-evm-ref-p1-tx-alignment-design.md`

- **pre-Isthmus fork 配置**：`ecotoneConfig()` / `fjordConfig()` / `graniteConfig()` / `holoceneConfig()`；`OpForkConfig::has_ecotone_l1_formula` 区分 Ecotone calldata-gas 与 Fjord+ FastLZ L1 公式
- **fork-aware L1 cost**：`computeL1Cost(fee, env, cfg)` — Ecotone 用 `bedrockCalldataGasUsed = zeroes×4 + nonzeroes×16`（**无** +68）；Fjord+ 用 FastLZ 公式
- **L1Block 解 fee**：`loadOpFeeParams(StateView)` 读 OP_L1_BLOCK slots 1/3/7/8；`opValidateFromState` / `opTransitionFromState` 薄封装（**配对约束**：二者须成对使用，勿与注入 `OpFeeParams` 的 overload 混用）
- **Vault stub**：`seedOpPredeploys` 为四 fee vault 写入非空 stub code，避免零费执行删光账户
- **EIP-7702**：`process_authorization_list` 真 ecrecover（`keccak256(0x05‖rlp[chain_id,addr,nonce])` → secp256k1）；校验序 v→s→recover；金值由 `test/opstack/scripts/gen_7702_vectors.py` 产出
- **EIP-7623 floor**：用户 tx 与 deposit 均受 calldata floor 约束（Isthmus 无豁免）；钉死金值 51000 / 33000 / 21000

**pre-Isthmus `precompiles = nullptr`**：Ecotone/Fjord/Granite/Holocene 配置使用 evmone 基表（`precompiles = nullptr`）；**pre-Isthmus precompile 集合保真非本里程碑目标**。

**继承不变量**：
- **N-1**：`opTransition` L1 vault 结算不变——无条件 `AddBalance(L1FeeVault, l1_cost)`，即使 `l1_cost==0` 也会 touch
- **G-1**：非 deposit 用户 tx 若 `signedTxEnvelope` 为空 → `opValidate` 失败

**显式非目标 / 不宣称**：本里程碑**不**声称 op-geth 块级或生产路径等价；E-b t8n gate 解冻前仍不得宣称 OP 路径生产可用（同 spec §1.1 R2）。

### M6 零值差分口径

`OpFeeParams=0`、`has_operator_fee=false`、关闭 precompile override 时，同一笔普通转账：

- `status` / `gas_used` 与 `eth::runTransaction` 相等
- **非** BaseFee/L1/Operator vault 的 `state_diff`（含 coinbase=SequencerFeeVault tip）逐位相等
- OP 侧 `BaseFeeVault.balance == gasUsed × baseFee`（ETH 隐式销毁 → OP 显式入账）
- 不验证 L1/operator fee 本身；t8n 仍属 E-b

### M6 upstream diff（照抄面静态护栏）

对比 `OpTransition.cpp` / `OpHost.cpp` 中与 evmone REF `3585c2cb` 对齐的源码片段；**有意 fork**（P1 ecrecover、OP fee 路径、OpHost precompile 派发等）编码在 `scripts/upstream-diff/golden/*.patch`。

```bash
export EVMONE_GIT=/path/to/evmone   # 须含 REF 3585c2cb
./scripts/upstream-diff.sh          # CI / 本地检查
./scripts/upstream-diff.sh --show build_message
./scripts/upstream-diff.sh --regenerate-goldens   # 有意改照抄面后更新 golden 并提交
```

映射表：`scripts/upstream-diff/manifest.tsv`（改行号后同步更新）。

## Naming

| 类别 | 风格 | 例 |
|------|------|-----|
| 类 / 结构体 | PascalCase | `OpHost`、`DepositTx`、`OpFeeParams` |
| 自由函数 / 方法 | camelCase | `runTransaction`、`opValidate`、`opTransition`、`runDeposit`、`applyStateDiff` |
| 结构体字段 | snake_case | `l1_base_fee`、`gas_limit`、`has_operator_fee` |
| 成员变量 | `m_` + snake_case | `m_chain_id` |
| 常量 | `OP_*` / `kCamel` | `OP_L1_BLOCK`、`kL1CostIntercept` |

例外：从 evmone 逐行照抄的匿名 ns 助手（如 `build_message`、`process_authorization_list`）保留母本 snake_case，不改名；
照抄段内的常量（如 `SECP256K1N_OVER_2`、`AUTHORIZATION_*`，见 `scripts/upstream-diff/manifest.tsv` 的 `auth_constants` 段）同理保留母本 SCREAMING_SNAKE_CASE——改名会破坏 upstream-diff 护栏。

## Build (standalone)

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
    cmake --build build

## Build (in-tree, as part of FISCO-BCOS root build)

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DBCOS_EVM_REF=ON
    cmake --build build --target bcos-evm-ref-eth

`BCOS_EVM_REF`（根 `CMakeLists.txt`，默认 `OFF`）挂接本模块的 `add_subdirectory`；关闭时对现有构建零影响。

## Test

    export EVM_REF_EEST_ROOT=<EEST fixtures root>   # 未设置则 EEST 用例 SKIP
    ctest --test-dir build --output-on-failure

## M2 验收记录

- EEST release: 见 `test/EEST_VERSION`（v5.4.0, `fixtures_develop.tar.gz`；与 evmone REF 3585c2cb 配对；Task 5 Step 1 选定）
- state fixtures（harness 日志原样）：`files=2723 skipped=0 failed_files=0`
  （耗时约 17–19s；`ctest --test-dir bcos-evm-ref/build -R BcosEvmRefEthTests` 全绿）
  - 2723 个 fixture 文件覆盖全部 fork 子目录；其中 **2717 个文件含 Cancun+ case**
    （harness 内部按 `rev >= EVMC_CANCUN` 过滤，非 Cancun+ 的旧 fork 文件被遍历计数但不产生断言）
  - **Cancun+ case 总数 55,233，全部通过**（63,556 全 fork case 中）
- skip 清单（`EVM_REF_EEST_SKIP`）：无（本次运行未设置该变量，0 skip）
- 数字来源：Task 5 审查者独立复测（详见 `.superpowers/sdd/task-5-report.md` 控制器更正节）

## M3 验收记录（EEST blockchain）

- EEST release: 同 `test/EEST_VERSION`（v5.4.0）
- blockchain fixtures: 2848 files, failed_files=0, unsupported_files=2（cancun/eip4844 无效 RLP 块，属 spec §1.3 范围外）, ~61s
- `EestBlockchain.Smoke`（cancun/ 前 20 文件）进 ctest；`EestBlockchain.Full` 由 `EVM_REF_EEST_BLOCKCHAIN_FULL=1` 门控（3.2 GB，夜跑级）
- 判据经两组定点变异测试证伪假绿（破坏 stateRoot / receiptTrie 各一次，均按预期 FAIL 并精确定位）

**重要说明（spec §7.0 rev.5）**：本模块 0 失败**不构成 parity gap 证据**。`bcos-evm` 在同一批 fixture 的
Cancun+ 区间上同样干净，其 405 个失败 100% 落在 pre-Cancun（404 `fork_Frontier` + 1 `fork_Homestead`）。

## M3.5 Phase 1（读放大 spike）

见 `spike/README.md`。判定 GO：粗粒度 `get_account` 放大 1.16x；最大浪费是上游负查询不缓存（占全部
账本读 27.9%），适配器侧 5 行可修。

## Include hygiene (IWYU)

项目约定编码于 `iwyu-bcos-evm-ref.imp`（evmone `test/state/state.hpp` 伞形头、`<evmc/evmc.hpp>` 优先）。
运行：`scripts/iwyu-check.sh`（需 `brew install include-what-you-use` + `CMAKE_EXPORT_COMPILE_COMMANDS=ON`）。
评估报告：`docs/iwyu-evaluation.md`。
