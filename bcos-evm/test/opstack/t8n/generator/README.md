# opt8n-ref — M-B3+M6 块级 OP-Stack 向量生成器（op-geth 当库）

产出 `test/opstack/t8n/vectors/*.json`（schema **v3-block**）与语料
`test/opstack/t8n/cases/*.in.json`。拷贝基座为 M-T tx 级生成器
`bcos-evm/test/opstack/t8n/generator/main.go`（`buildTx` 三臂 deposit/eip1559/setcode
沿用改造；`processVector` 换 `processBlockVector`；postState 由 diff 语义换为
决策记录 8 的「候选集全账户全槽」全量发射）。

## 构建与运行（M-T 先例）

本目录**不参与** FISCO-BCOS 的 C++ 构建。它 import op-geth 内部包，必须从
op-geth checkout 内构建：

```bash
OP_GETH=/Users/octopus/octo/code/blockchain-impl/op-geth   # v1.101702.2 @ e8800cffe53d459cde8a07c8e8f1de9d86e79e07
cp -r test/opstack/t8n/generator $OP_GETH/cmd/opt8n-ref
cd $OP_GETH && go build ./cmd/opt8n-ref
# op-geth checkout 只是 scratch 构建空间：用完删除 cmd/opt8n-ref，不 commit 任何东西

# 1) 重新发射 25 个语料文件（确定性，字节可复现）
./opt8n-ref --write-cases <repo>/test/opstack/t8n/cases

# 2) 逐案生成向量（fork 取自案内 _info.hardfork）
./opt8n-ref --input cases/<name>.in.json --output vectors/<name>.json \
            --op-geth-commit e8800cffe53d459cde8a07c8e8f1de9d86e79e07

# 3) dev 探针：ring wrap 可行性（见「已知边界」）
./opt8n-ref --probe-genesis-number
```

## 生成管线（plan Step 1 关键序，全部走 op-geth 真实代码路径）

1. **启动一致性断言**（铁律）：L1Block 预置槽 1/3/7/8 ↔ attributes calldata
   逐字段对账（布局见 `main.go` `assertL1BlockConsistency` 头注释；槽 8 的
   DA 字节位 `[18:20)` 与 C++ `OpFeeParams.h` 镜像一致）；Jovian 配置 +
   Isthmus 长度（176B）分支：要求 deposits-only、slot8 DA 字节为零、跳过
   `[176:178]` 对账；全语料禁踩 first-Ecotone 回退陷阱
   （`rollup_cost.go:169-179`：slot7 与 slot3 两个 4 字节 scalar 不得同时全零）。
   语料由 `cases.go` 的**单一 `feeParams` 源**同时生成槽与 calldata，结构上
   不可能漂移；断言仍保留以防手改。
2. **genesis**：`Alloc=pre` + Timestamp/GasLimit/BaseFee 旋钮 +
   `ExtraData = eip1559.EncodeOptimismExtraData(...)`（9B Isthmus / 17B Jovian
   含强制 minBaseFee）——genesis 与生成块**双双**编码（块 1 CalcBaseFee 除零 /
   InsertChain verifyHeader 两个失败模式各由其一挡住）。
3. **生成**：`db, blocks, receipts := core.GenerateChainWithGenesis(genesis,
   beacon.New(ethash.NewFaker()), 1, gen)`；gen 内 `SetCoinbase` →
   `SetExtra` → `SetParentBeaconRoot`（4788 构建/回放对称）→ 逐笔 `AddTx`
   （`NewEVMBlockContext` 已无条件接 L1CostFunc/OperatorCostFunc，无手接）。
   receipts 已经 `DeriveFields`（含 OP 字段；deposit-only 块自安全早退）。
4. **自检（禁绕过）**：`core.NewBlockChain(rawdb.NewMemoryDatabase(), genesis,
   engine, nil)` + `InsertChain(blocks)` = 独立 fresh DB 上的真
   `Process`+`ValidateState`（withdrawalsRoot / Jovian DA footprint /
   extraData 全套头校验）。失败 = 生成器/语料缺陷，修后整批重生成。
5. **发射**：`pre` 以 EF state-test 账户形状发射（balance/nonce/code 恒在场，
   `emitPre`——回放器原样喂 evmone `from_json<TestState>`，三字段硬必填；
   `postState` 保持 GenesisAlloc 形状，由回放器自己的比较器消费，见
   vectors/DIVERGENCES.md PRE-COMMIT-1）；`env` 从 `blocks[0].Header()` 回写（number=1、time=genesis+10、
   baseFee 由 genesis extraData 经 CalcBaseFee 导出、mixDigest=0 →
   `currentRandom:"0x0"`、`parentHash` = genesis hash）；header 期望 =
   GasUsed/ReceiptHash/Bloom/WithdrawalsHash/RequestsHash/BlobGasUsed
   （Jovian 的 blobGasUsed 为**有值的 0x0** 亦必填）；逐 receipt =
   type/status/gasUsed/cumulative/logsCount + deposit nonce/version +
   `_op_l1_fee` + `_op_operator_fee` + `_op_da_footprint`（Jovian 非 deposit，
   = receipt.BlobGasUsed = daScalar × EstimatedDASize）。

### postState：候选集全账户全槽 + 完备性硬校验

候选集（决策记录 8）= pre 全账户 ∪ tx 参与方（sender/to/7702 authority）∪
OP 固定集（SequencerFeeVault(=coinbase)/BaseFeeVault/L1FeeVault/
OperatorFeeVault/L1Block/L2ToL1MessagePasser/4788/2935）∪ 案内
`extra_candidates`。槽集 = pre 已声明槽 ∪ 案内 `extra_storage` ∪ 解析已知的
系统槽（4788 的 `time%8191`、`+8191`；2935 的 `(number−1)%8191`）。

**完备性不靠作者记性**：生成器对块后状态做 secure-trie 迭代（账户 trie +
每个候选账户的 storage trie，哈希键比对、无需 preimage）——
出现候选集外账户或未声明槽即硬错（负测试见下）。发射时每个候选账户输出
balance/nonce/code 与全部非零已声明槽；块后不存在的候选账户按 trie 语义发射
零账户 `{"balance":"0x0"}`（≡ 不存在）。

### 费用双重交叉核对

`_op_operator_fee` 由生成器**自算**（铁律：Isthmus `gasUsed×scalar/1e6+constant`
vs Jovian `gasUsed×scalar×100+constant`，`rollup_cost.go:254-287`），然后：

1. 逐笔与 op-geth 自己的 `types.NewOperatorCostFunc`（喂 pre 槽 8）对账；
2. Σ(per-tx operator fee) 与 OperatorFeeVault 余额增量对账、
   Σ(per-tx `_op_l1_fee`) 与 L1FeeVault 余额增量对账——发射值可证等于
   state transition 实扣值，而非同源重算。

`_op_operator_fee` 的出现规则镜像 `deriveOPStackFields`：槽 8 的
scalar/constant 均为零时不发射。

## 语料（cases.go，25 向量）

计数勘注：plan 表 fork 列枚举 = 行 1-9,11,14 × {I,J}（22）+ 行 10（仅 I）
+ 行 12,13（仅 J）= **25**；plan 行文的「26」为上游计数笔误，矩阵格无一缺失
（manifest.txt 先于生成从 plan 表写死）。

语料公共骨架：chainID 8453（0x2105）；genesis time 1000 → 块 time 1010；
denominator 50 / elasticity 6（Jovian minBaseFee=0）；coinbase =
SequencerFeeVault；L1Block 与 L2ToL1MessagePasser 每案在册且 **nonce=1**
（预部署真身带码非空；此处 L1Block 刻意无码，value-0 CALL 触碰 EIP-158
空账户会在 commit 期连 storage 一起被删——`unexpected storage wiping`——
nonce=1 使其非空，同时结构性保证「无 tx 写 L1Block 槽」）。attributes
deposit calldata 的 sequence/l1 timestamp/l1 number/l1 hash/batcher hash
字段置零（不被任何 cost 函数读取、不在 plan 槽对账清单内；槽 0/2 相应不预置）。

## 4788/2935 真字节码 provenance（铁律 13：非手打）

- EIP-4788 beacon roots 运行时码：execution-specs checkout
  `/Users/octopus/octo/code/blockchain-impl/execution-specs`
  @ `c3462e030f7e2cebe93688d15d2423dcf16fc8cc`，文件
  `packages/testing/src/execution_testing/forks/forks/eips/cancun/eip_4788.py`
  （EEST fork pre-allocation 字面量，机器拷贝）。
- EIP-2935 history storage 运行时码：同 checkout
  `.../eips/prague/contracts/history_contract.bin`（`xxd -p` 转储）。
- 本仓 `test/EEST_VERSION` 钉的 EEST v5.4.0 fixtures tarball 本机未展开
  （`EVM_REF_EEST_ROOT` 未设置）；execution-specs 即 EEST 的上游宿主仓
  （其 `packages/testing` 就是 execution-spec-tests），两码与 EIP 正文
  部署码一致。

## 已知边界（如实记录）

1. **共识失败形态的 deposit**（gasUsed=gasLimit）无合法块向量——生成器只产
   `InsertChain` 可过的块；案 5 覆盖的是 EVM-revert deposit（status 0、
   **实际** gasUsed、mint 照记）。
2. **2935 ring wrap 无判别用例**：尝试 `Genesis.Number=8191`（块 8192 落
   index 0）被 op-geth 拒绝——`Genesis.Commit` 硬拒 number>0
   （`core/genesis.go:728`），且 `InsertChain` 需祖先在链上，两条路都封死。
   运行时探针 `--probe-genesis-number` 输出：
   `probe: Genesis.Number=8191 REJECTED (panic: can't commit genesis block with number > 0)`。
3. **系统调用顺序**：`chain_makers` 在 gen 回调前跑 EIP-2935、回调内
   `SetParentBeaconRoot` 才跑 EIP-4788（真 `Process` 反序）。两者写不相交
   合约的存储，终态与 receipts 等价；InsertChain 自检亦证。
4. **stateRoot 不入向量比对**：候选集外且「两侧都写」的账户理论上可漏——
   本生成器侧已由 trie 完备性校验消除（生成侧写集全覆盖）；回放侧由
   Task 3 的双向比对（applyDiff 累计写集）兜底。
5. 零账户发射语义：`{"balance":"0x0"}`（无 nonce/code/storage）≡ trie 不存在，
   回放侧按 trie 语义规约后比对。

## 负测试（守卫真的会咬人）

- 篡改 L1Block slot1 ≠ calldata[36:68] → 启动断言拒绝（已验证）。
- 删除 `extra_storage` 声明（合约写未声明槽）→ trie 完备性校验拒绝（已验证）。

## 确定性

同一输入两次生成字节相同（`sort.Strings`/Go map JSON 键排序/无时间戳）；
`--write-cases` 亦字节可复现。`_op_test_vectors.generator_commit` 记录
op-geth 全量 sha `e8800cffe53d459cde8a07c8e8f1de9d86e79e07`。
