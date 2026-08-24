# OP Stack 交叉验证

## 任务

独立验证 FISCO BCOS 对 OP Stack 执行层规范的实现完整度。

## 快速开始

```bash
bash docs/op-cross-verify.sh HEAD
```

## 产出物

| 文件 | 说明 |
|------|------|
| `docs/op-spec-requirements.yaml` | 102 条原子需求（供对比） |
| `docs/op-code-mapping.yaml` | 代码映射（供对比） |
| `docs/op-spec-gap-report.md` | 差距报告（供对比） |
| `docs/op-cross-verification-handoff.md` | 交接文档 |
| `docs/op-cross-verify.sh` | 验证脚本 |

## 验证要求

1. 独立从 OP 规范提取需求，与现有 102 条对比
2. 对每条需求验证代码实现
3. 产出对比报告：认同/异议/遗漏/多余

## 关键代码路径

```
engine/bcos-engine/EngineServiceImpl.h    # Engine API
opstack-executor/OpstackExecutor.h        # 交易解码
opstack-executor/OpBlockExecute.h         # 块级操作
bcos-evm/bcos-evm/opstack/OpTransition.cpp # transition + fee
bcos-evm/bcos-evm/opstack/OpFeeParams.h   # fee 参数
bcos-evm/bcos-evm/opstack/OpPredeploys.h  # 预部署地址
bcos-evm/bcos-evm/opstack/OpForkSchedule.h # 分叉调度
```
