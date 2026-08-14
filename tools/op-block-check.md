# op-block-check.sh — 跑代码裁决 wrapper

> 零新 C++，复用现有 Boost 测试二进制 `opstack-executor-block-tests`（CI 的 OP 块执行 gate）。
> 用途：审查/裁决时把「跑代码实证」变成一条命令，退出码语义明确，供人工与 skill 接线消费。

## 构建前置

两条前置，缺任一脚本以退出码 `2` 拒绝执行：

```bash
# 1) 测试二进制（缺省路径 build/opstack-executor/tests/opstack-executor-block-tests，可被 BLOCK_TESTS 覆盖）
ninja -C build opstack-executor-block-tests

# 2) t8n 参考向量 json（.gitignore'd，不入库；manifest.txt 被跟踪但不能当哨兵）
bash opstack-executor/tests/t8n/generator/ensure-vectors.sh
```

`ensure-vectors.sh` 需要：

- **Go 工具链**（`go build` op-geth 模块内的 `opt8n-ref`）。
- **op-geth@pin**：缺省自动浅克隆到缓存目录 `~/.cache/op-geth/op-geth-<pin>` 并钉住 `regen.sh` 里的 `PIN`（单一事实源）；也可用 `OPGETH=/path/to/op-geth` 复用现有 checkout（pin 不符则报错，绝不删除用户 checkout）。

本地与 CI 共用同一逻辑（GitHub composite action 委托本脚本），因此「本地跑 ensure == CI 行为」。

## 用法

```bash
bash tools/op-block-check.sh
BLOCK_TESTS=/path/to/opstack-executor-block-tests bash tools/op-block-check.sh   # 覆盖测试二进制路径
```

无位置参数。脚本从仓库根（`git rev-parse --show-toplevel`）推导路径。

## 退出码

| 退出码 | 含义 |
|--------|------|
| `0` | 全绿：测试通过，输出 `PASS 全绿 exempt=<n> \| single-path summary: ...` |
| `1` | 测试失败或崩溃。内部 `rc`（测试二进制的退出码）进一步区分，见下 |
| `2` | 前置错误：向量 json 缺失，或测试二进制未构建（打印缺哪条 + 修复命令） |

脚本内 `rc`（测试二进制退出码）细分：

- `rc >= 128`：测试进程崩溃（signal = `rc - 128`，如段错误/abort），会再 grep 输出里的崩溃字样辅助定位。
- `rc == 200`：setup 错误（如 Boost filter 无匹配），不视为用例失败。
- 其余非零：有真实用例失败，会 grep `error: in` / `failures are detected` 摘前 20 行。

## 依赖的测试输出字样

- `single-path summary:`：逐向量汇总行；脚本打印 `<absent>` 兜底（输出格式漂移也能诊断）。
- `KNOWN-DIVERGE`：已知分歧（豁免）计数 `exempt=<n>`，打印时顺带该计数，全绿与否以 `rc` 为准，输出只作诊断辅助。

## 向量生命周期（重要）

- `opstack-executor/tests/t8n/vectors/*.json` 全部 gitignore'd（`.gitignore`：`opstack-executor/tests/t8n/vectors/*.json`）。
- 仓库只跟踪 `manifest.txt` + 少量 .md（ANCHOR-CORRECTIONS/DIVERGENCES/OP_RECEIPT_FIELDMAP）。
- 向量由 `ensure-vectors.sh`（现场）或 CI 的 composite action（`.github/actions/opstack-t8n-regen`）按需生成。因此**新鲜 clone / CI 环境必须先跑 ensure-vectors**，且不能把 .json 提交入库。

## 局限（spec 已知）

- 判据是「整二进制退出码 = CI gate」，非 per-receipt 粒度；定位需回看输出里 `single-path summary:` 对应行。
- 覆盖范围随 Boost 测试二进制（含 Path A/B 双路径）演进，脚本不做向量级 filter。
