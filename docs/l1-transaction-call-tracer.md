# L1 交易调用追踪

Eth lane（genesis 的 `executor.version=2`）通过 `debug_traceTransaction` 提供原生 `callTracer`。请求只能追踪已入块、且父块状态仍可读取的交易。保留区块和回执不代表保留了可重放的历史状态。本次不包含 Op lane、区块级追踪或 JavaScript tracer。

## 启用

在需要提供追踪服务的 listener 配置中启用 `enable_trace_api`，然后重启节点：

```ini
[web3_rpc]
enable=true
enable_trace_api=true

[engine_rpc]
enable_trace_api=false
```

两个 listener 的开关相互独立，默认均为 `false`。Engine listener 仍使用原有 JWT 认证。

追踪允许使用有限的 `storage.mpt_prune_window`，不要求永久 archive。`enable_trace_api` 不开放 `debug_dbGet`、`debug_getRawHeader` 等挑战者 preimage 接口。原有 `enable_debug_api` 及其 `storage.mpt_prune_window=-1` 启动约束保持不变。

## 请求

参数为交易哈希和选项对象，均须提供：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "debug_traceTransaction",
  "params": [
    "0x0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    {
      "tracer": "callTracer",
      "timeout": "5s",
      "tracerConfig": { "onlyTopCall": false }
    }
  ]
}
```

| 选项 | 类型 | 默认值与限制 |
| --- | --- | --- |
| `tracer` | 字符串 | 必填，仅支持 `callTracer` |
| `timeout` | 时长字符串 | 默认 `5s`；范围 `1ms` 至 `30s`；支持 `500ms`、`1.5s`、`1s250ms` 等时长 |
| `tracerConfig.onlyTopCall` | 布尔值 | 默认 `false`；为 `true` 时只返回顶层 frame，仍执行内部调用 |

不支持 JavaScript tracer、其他 tracer、`withLog` 或未声明的追踪选项。不支持的选项会返回参数错误。

## 重放与结果

节点读取目标块的父块 MPT 状态，应用目标块环境和块前系统操作，按顺序执行同块前序交易，再执行目标交易。费用、fork 和调用结果使用真实交易执行规则。追踪使用独立的状态写入层和 VM，不提交状态，不改变规范链、状态根或交易池。

成功响应的 `result` 是调用树。frame 包含 `type`、`from`、`gas`、`gasUsed`、`input`，并按执行结果包含 `to`、`value`、`output`、`error`、`revertReason` 和 `calls`。合法交易中的 REVERT 或执行失败通过 frame 的 `error` 表达，不等同于 RPC 请求失败。`onlyTopCall` 不改变顶层执行结果和 gas。

## 资源与错误

每个节点最多同时执行 2 个追踪请求；超出时立即返回错误。每次请求最多收集 10,000 个 frame，输出采用 8 MiB 保守预算（包含 JSON 转义开销），JSON 深度上限为 1,024。超时或超预算时返回错误，不返回截断的调用树。EVM 指令循环检查取消，前序交易和块前系统操作也使用同一追踪 VM。

截止时间在指令循环和重放边界检查。正在执行的单次存储读取或原生预编译需要先返回，因此 `timeout` 不提供硬实时截止保证。超时错误文案为 `execution timeout`。

| 场景 | RPC 结果 |
| --- | --- |
| listener 未启用追踪 | `-32601`，方法不存在 |
| 哈希格式、选项类型或 tracer 不受支持 | `-32602`，参数错误 |
| 交易不存在或不在支持的执行 lane | `-32000`，明确说明原因 |
| 父状态或所需 MPT 节点已缺失 | `-32004`，历史状态不可用 |
| 超时、并发已满、输出超预算或重放失败 | `-32000`，明确说明原因 |

历史状态缺失时不会退回 `latest`。应改用仍在状态保留窗口内的交易，或在具备对应历史状态的节点上重试。
