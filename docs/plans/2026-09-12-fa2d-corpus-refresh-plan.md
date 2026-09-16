# F-A2-D 语料期望刷新 —— 可评审配方（dry，未执行）

> 状态：**未执行**。需要语料授权（语料仓对本仓只读；推送需单独授权）。
> 产物：本文件（untracked 过程文档）。执行前请先读 §5 的回滚与风险。
> 关联：WI-31（`d6ca9860c` 等值门 + `975c2ec58` 导出修复）；审计项 F-A2-D。

## 1. 事项摘要（三处）

| # | 位置 | 现状 | 目标 |
|---|---|---|---|
| 1 | 语料生成器 `t8n/generator/main.go` 的 static-shape 表项 #11 | message = `"DA footprint (blobGasUsed) exceeds the block gas limit"` | 改为新顺序的等值消息子串 |
| 2 | 语料向量 `vectors/invalid_jovian_transfer_basic_static_11.json` | `_op_expected.reject.{op_geth, fisco.validation_error_contains}` 均为旧串 | 由 regen 重生后变为新串 |
| 3 | 我方 `opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp` 的 `daFootprintOrderAlt` | 临时容忍"旧串 or 新串" | **删除**（regen 后它不再触发） |

**影响面**：生成器 1 行 + 1 个向量文件 + 我方 1 处 alt。全 `vectors/` grep 旧串计数 = **1**。

## 2. 为什么过期（诊断）

- 该 payload：`blobGasUsed = 0x2000000`（33,554,432）> `gasLimit = 0x989680`（10,000,000），本地 Σ = 0（该向量 attributes scalar = 0）。
- **WI-31 之前**：本仓只有范围校验 → 报 `"DA footprint (blobGasUsed) exceeds the block gas limit"`（= 向量期望，当时一致）。
- **WI-31 之后**：按 op-geth 顺序（`core/block_validator.go:127` 等值 → `:131` 范围）先撞**等值门** → 报
  `"invalid DA footprint in blobGasUsed field (remote: 33554432 local: 0)"`
  → 仍 INVALID、仍含 `DA footprint`，但整串不同。
- 期望串的**唯一来源**是生成器表项：`main.go` static-shape 表的第 5 个元素；消费者在
  `main.go:3589`（`OpGeth: item.fiscoMessage`）与 `:3594`（`ValidationErrorContains: item.fiscoMessage`）——
  **两个字段同源**，所以改表项即同时改到它们。

## 3. 生成器补丁（unified diff，待评审）

文件：`<corpus>/opstack-executor/tests/t8n/generator/main.go`（表项 #11，当前 `:3547-3551`）

**patch 文件已生成**：`docs/plans/patches/F-A2-D-invalid-jovian-static-11.patch`（untracked；已通过
`git -C <corpus> apply --check` 校验，语料仓未被改动）。

```diff
@@ static-shape 表项 #11 @@
 	{11, "jovian_da_footprint_over_gaslimit", "jovian", func(p map[string]interface{}) error {
 		p["blobGasUsed"] = "0x2000000" // 2^25 = 33554432 > 10M gasLimit
 		return nil
-	}, "DA footprint (blobGasUsed) exceeds the block gas limit"},
+		// The engine evaluates the DA equality gate BEFORE the range gate, mirroring
+		// op-geth's block validator (core/block_validator.go:127 equality, :131 range).
+		// A payload whose blobGasUsed differs from the locally recomputed footprint is
+		// therefore rejected by the equality branch even when it also exceeds gasLimit.
+		// Use the stable prefix as a substring: the remote/local numbers are payload-specific.
+	}, "invalid DA footprint in blobGasUsed field"},
```

**为什么用子串而非整串**：消费侧是 `strings.Contains`（`ValidationErrorContains` 语义），本仓实际消息带
`(remote: 33554432 local: 0)`；前缀子串既精确钉住"是等值门拒绝的"，又不会被数列牵动。

**待决策（可后置）**：该表的 `OpGeth` 字段装的是**本 lane 的措辞**，不是 op-geth 的 Go 原文。op-geth 两条分支的真实文本是：
- 等值：`invalid DA footprint in blobGasUsed field (remote: %d local: %d)`（`block_validator.go:127`）
- 范围：`DA footprint %d exceeds block gas limit %d`（`:131`）

若要让 `op_geth` 名副其实，需要把该字段与 `fisco.validation_error_contains` 拆开赋不同值（改表结构 + 消费者），
属本配方之外的独立小改；**本次不夹带**，只把两个字段都刷成"新顺序下的可断言子串"。

## 4. 重生与验证命令（授权后执行；本地、不推送）

```bash
# 0) 前置：语料仓干净 + pin 断言（regen.sh:53 PIN=e8800cffe，:64 硬断言 op-geth HEAD == PIN）
C=~/.cache/fisco-t8n-corpus
git -C "$C" status --porcelain | head        # 期望为空
grep -n '^PIN=' "$C/opstack-executor/tests/t8n/generator/regen.sh"

# 1) 应用已生成的 patch（本 worktree 内，untracked，不随代码入库）
#    patch 已就位并做过只读预检：git apply --check 通过
P=/Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal/docs/plans/patches/F-A2-D-invalid-jovian-static-11.patch
git -C "$C" apply --check "$P"    # 只读预检（不改语料）
git -C "$C" apply "$P"            # 应用（未提交，随时可回滚）

# 2) 重生（脚本会把 generator/ 拷进 op-geth/cmd/opt8n-ref 并 go build；本机 Go 1.23.4 ✓）
OPGETH=~/.cache/op-geth/op-geth-e8800cffe53d459cde8a07c8e8f1de9d86e79e07 \
  bash "$C/opstack-executor/tests/t8n/generator/ensure-vectors.sh"

# 3) 验证（新串在、旧串不在）
V="$C/opstack-executor/tests/t8n/vectors/invalid_jovian_transfer_basic_static_11.json"
grep -c 'invalid DA footprint in blobGasUsed field' "$V"     # 期望 2（op_geth + fisco 各一）
grep -c 'DA footprint (blobGasUsed) exceeds the block gas limit' "$V"   # 期望 0
git -C "$C" diff --stat                                      # 期望：该向量 + SHA256SUMS/manifest 等再生产物
```

**我方仓库收尾**（独立小 commit）：
```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal
# 删除 daFootprintOrderAlt（它只在期望串 == 旧串时触发，刷新后已无用）
# 重编译并跑：
ninja -C build opstack-executor-block-tests
./build/opstack-executor/tests/opstack-executor-block-tests --run_test=OpNewPayloadRpcE2eSuite 2>&1 | tail -3
./build/opstack-executor/tests/opstack-executor-block-tests 2>&1 | grep -m1 -o 'Running [0-9]* test cases'   # 期望 146 不变
```

## 5. 风险与回滚

| 风险 | 处置 |
|---|---|
| regen 会重写**全部** vectors + goldens（不只该向量） | 属正常产物：DA 槽在 WI-31 后与实现一致、本次只改一个期望串；但 diff 会较大 → 先 `git -C "$C" diff --stat` 复核范围，只接受"该向量 + 校验清单"变更 |
| pin 用错 → 溯源失效 | `regen.sh:64` 硬断言 `op-geth HEAD == e8800cffe`；本机 `~/.cache/op-geth/op-geth-e8800cffe…` 已具备 |
| regen 需要在 op-geth 工作树里写 `cmd/opt8n-ref` | 用**缓存** checkout（而非 `blockchain-impl/op-geth`），脚本自带 cleanup trap |
| 改错/想回退 | 语料仓 `git -C "$C" checkout -- .`（未提交即完全可逆）；**本次不推送** |
| 我方 alt 删除后若期望串仍不匹配 | 那说明 regen 未生效或补丁未落 → 按 §4 step 3 的 grep 先证伪，再决定是否回滚 |

## 6. 执行清单（勾选式）

- [ ] 授权：允许改语料仓（本地）＋（后续）推送语料仓
- [ ] §3 补丁评审通过并落盘
- [ ] §4 step 0–3 执行、验证三项 grep 与 diff 范围
- [ ] 我方删 `daFootprintOrderAlt`、重编译、跑 `OpNewPayloadRpcE2eSuite` 与全量 executor（146）
- [ ] 登记：F-A2-D 关闭（附语料仓 commit 与 regen 产物摘要）
- [ ] （可选）§3 的 `op_geth` 语义拆分另立小项
