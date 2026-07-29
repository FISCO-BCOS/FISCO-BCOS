# Task 2 报告: 离线金值仪式(全量 33 条 + 1 对链式向量)

## 结论

Step 1-4 全部完成。33 条向量的 engine gate 金值(`blockHash`/`transactionsRoot`/
`extraData`/`excessBlobGas`/`rawTransactions`/`encodedHeaderHex`)已按裁定 A3(扩展
opt8n-ref 发射段,非从 env+txs 构头)离线生成;1 对链式向量(块 1→2,`InsertChain`
头校验)已按裁定 A2 生成,B 的 `pre` = A 的 `post`,非拼接。`vectors/` 目录逐字节零
触碰(自检 (e) 空)。五条自检全部通过。Commit 已完成。

## 语义依据

`docs/superpowers/specs/2026-07-28-op-validator-minimal-loop-design.md` §7.1(金值
策略/诚实口径)、§5.1(extraData 原样发射:Isthmus 9B/Jovian 17B 含 minBaseFee)。

## 工具扩展 diff 摘要

`bcos-evm/test/opstack/t8n/generator/main.go`:367 insertions(+), 50 deletions(-)
(367 行几乎全为新增函数;50 行删除主要是把候选集构建的"生成前半段"从
`processBlockVector` 挪进新的 `assembleOutput`,逻辑原样迁移未改写)。

**定性**(裁定 A3):扩展 opt8n-ref 发射段,不是从 env+txs 重新构头——`assembleOutput`
把 `processBlockVector` 原有的候选集/postState/回执期望逻辑原封不动地抽成一个可复用
函数(33 条向量路径与链式对路径共用同一份代码),唯一新增的计算是 `buildGoldenRecord`
末尾对已生成的 `block`/`header`/`txs` 取值,不触碰生成管线本身。

新增内容:

1. **CLI 扩展**(`main()`,向后兼容,原三个模式不变):
   - `--golden-output <path>`:`--input`/`--output` 的可选伴生参数,额外发射本任务
     的金值 JSON,向量输出路径/字节完全不受影响。
   - `--chain-output-dir <dir>`:全新独立模式,驱动 `processChainPair`/`runChainPair`。
2. **`goldenRecord` 类型 + `buildGoldenRecord`**:从已生成的 `block`/`header`/`txs`
   取 `block.Hash()`/`header.TxHash`/`header.Extra`(原样,非人工选值)/
   `tx.MarshalBinary()`(含 deposit 0x7E 完整 envelope)/`rlp.EncodeToBytes(header)`;
   对 `header.ExcessBlobGas` 做真实断言(必须存在且 =0),不是假定字面量。
3. **`assembleOutput`**:33 条路径与链式对路径共用的组装函数(候选集/postState/
   回执期望/vault 交叉核对/`outputVector` 组装 + 金值提取),从 `processBlockVector`
   抽取,逻辑未改写,只是把"生成前"与"生成后"两段候选集构建合并成一段(此前分两段
   纯粹是因为历史实现顺序,合并不改变语义)。
4. **`processChainPair`/`runChainPair`**(裁定 A2 全新代码):驱动
   `core.GenerateChainWithGenesis(genesis, engine, 2, gen)` 一次性生成两块(块 B 的
   nonce 用 `bg.TxNonce(sender)` 现查、不手工递推;`bg.Signer()`/`bg.Timestamp()`/
   `bg.Number()` 现取,不手工重算),`selfCheck` 对两块一并 `InsertChain`;块 B 的
   `inputCase.Pre` 在生成完成后赋值为块 A 的 `assembleOutput` 输出的 `PostState`
   (类型对齐,无需转换)——这就是"B 的 pre = A 的 post"的实现点,不是先验设定。
5. **`chainedBlockOutput`**:33 条金值(扁平、只含缺失字段)与链式对金值(完整
   payload + 金值合并、无 `vectors/` 对应物可补)两种 schema 分别对应 brief 的
   Interfaces 描述逐字匹配。

## 生成命令与 pin SHA

pinned op-geth 检出:`/Users/octopus/octo/code/blockchain-impl/op-geth`
@ `e8800cffe53d459cde8a07c8e8f1de9d86e79e07`(tag `v1.101702.2`)——与
`vectors/manifest.txt`/每条向量 `_op_test_vectors.generator_commit` 同一 pin,金值
是同一次生成语义下的补充发射,不是另一版本 op-geth 的产物。仪式开始前确认
`git rev-parse HEAD` 等于该 SHA 且 `git status --porcelain` 为空;仪式结束后同样
确认(见下"清理"一节)。

```bash
OP_GETH=/Users/octopus/octo/code/blockchain-impl/op-geth
PIN=e8800cffe53d459cde8a07c8e8f1de9d86e79e07
GEN_DIR=<repo>/bcos-evm/test/opstack/t8n/generator
T8N_DIR=<repo>/bcos-evm/test/opstack/t8n

rm -rf "$OP_GETH/cmd/opt8n-ref"
cp -r "$GEN_DIR" "$OP_GETH/cmd/opt8n-ref"
( cd "$OP_GETH" && go build ./cmd/opt8n-ref )     # 首次即成功,零编译错误

# 33 条金值:向量输出到 SCRATCH(绝不写 vectors/),逐条与入库向量字节 diff
for in_json in "$T8N_DIR"/cases/*.in.json; do
  base="$(basename "$in_json" .in.json)"
  "$OP_GETH/opt8n-ref" --input "$in_json" \
    --output "$SCRATCH/vec/${base}.json" \
    --golden-output "$T8N_DIR/golden/engine/${base}.golden.json" \
    --op-geth-commit "$PIN"
  diff -q "$SCRATCH/vec/${base}.json" "$T8N_DIR/vectors/${base}.json"   # 33/33 一致
done

# 链式对
"$OP_GETH/opt8n-ref" --chain-output-dir "$T8N_DIR/golden/engine/chained" --op-geth-commit "$PIN"

# 清理(README 既有纪律:检出内不留痕、不 commit)
rm -rf "$OP_GETH/cmd/opt8n-ref" "$OP_GETH/opt8n-ref"
```

（另建了一个不入库、用后即删的验证工具 `cmd/opt8n-verify`，同一仪式内 build/run/删除，
用于自检 (a)，见下。）

## 自检五条(裁定 B4)输出

**(a) `_op_expected.header` 7 个共有字段与 golden 逐字段对账**:用临时验证工具
(`cmd/opt8n-verify`,同仪式内构建、运行、删除,不提交)——对 33 条向量 + 2 块链式向量,
**只用向量自身的 `env`/`_op_expected.header` 字段 + 本目录的
`extraData`/`excessBlobGas`/`transactionsRoot`**(不读 `encodedHeaderHex`)重建
`types.Header`,RLP 编码后与 `golden.encodedHeaderHex` 逐字节比对,再 keccak256 与
`golden.blockHash` 比对——这是真正的字段级交叉验证,不是同义反复。结果:
**35/35 PASS**(33 条 + chainA + chainB),`encodedHeaderHex` 字节全等,
`hash()==blockHash` 全部成立,即
`receiptsRoot`/`gasUsed`/`logsBloom`/`withdrawalsRoot`/`blobGasUsed`/`stateRoot`/
`requestsHash` 七字段全部对账通过。

**(b) `golden.rawTransactions[i] == vectors/<id>.json._op_raw[i]` 逐字节**:对 33
条向量的全部非 deposit 交易(167 笔)逐字节比对:**167/167 match**。deposit 交易
(39 笔)当前 `vectors/*.json` 无 `_op_raw` 字段(结构性缺失,非本任务范围)——按 brief
明确"deposit 走 `OpDepositEncode` 交叉,Task 3 完成后补跑",本任务已就位
`golden.rawTransactions` 供 Task 3 使用,该半部分显式留待 Task 3。

**(c) golden 每条已附 `encodedHeaderHex`**:33 条 + chainA/chainB 共 35 个文件全部
含该字段,已并入 (a) 的验证(既做字段级编码比对,也做哈希比对)。

**(d) 33 个文件与 manifest 集合相等**:`cases/*.in.json` 去后缀集合 ==
`golden/engine/manifest.txt`(去注释)集合 == `golden/engine/*.golden.json` 去后缀
集合,三者互相 `diff` 为空,基数 33。

**(e) `git diff --stat -- bcos-evm/test/opstack/t8n/vectors/` 为空**:仪式全程向量
输出只写 SCRATCH 目录,从未写入 `vectors/`;33 次逐条 `diff -q` 全部字节一致(证明
生成确定性,亦顺带证明新代码路径与旧代码路径生成同样的向量字节)。任务开始前、33 条
生成后、链式对生成后、最终提交前四次检查均为空。

## op-geth 检出清理

`cmd/opt8n-ref/`、`cmd/opt8n-verify/`、`opt8n-ref`、`opt8n-verify` 二进制全部
`rm -rf`;清理后 `git -C $OP_GETH status --porcelain` 为空,`HEAD` 仍为 pin SHA,
未向 op-geth 提交任何内容。

## Commit

```
test(bcos-evm): 验证者 gate 离线金值表(33 条 + 1 对链式向量,extraData 原样发射,pinned op-geth)
```

暂存路径(精确路径,`golden/engine/` 受 `.git/info/exclude` 中
`bcos-evm/test/opstack/t8n/` 规则影响,对该目录用 `add -f`;`generator/main.go`
已被跟踪,正常 `add`,不受影响):

- `bcos-evm/test/opstack/t8n/generator/main.go`(modified)
- `bcos-evm/test/opstack/t8n/golden/engine/manifest.txt`(new)
- `bcos-evm/test/opstack/t8n/golden/engine/README.md`(new)
- `bcos-evm/test/opstack/t8n/golden/engine/*.golden.json` × 33(new)
- `bcos-evm/test/opstack/t8n/golden/engine/chained/`(chainA/chainB 各 `.golden.json`
  + `.pre.json` + `.post.json`,共 6 个文件,new)

共 42 个文件。`git status --porcelain` 在提交前确认只含以上 42 个文件,无宽路径误吸
(未使用 `add -A`/`add .`)。
