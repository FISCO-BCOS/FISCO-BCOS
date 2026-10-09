---
status: accepted
date: 2026-10-09
---

# 本机挂接走 unix socket，跑完整公开方法表加 `admin_*`；跨机只做明文 RPC，不提供证书模式

节点在 `[storage] data_path` 下监听 `fisco-bcos.ipc`（`bcos-rpc/bcos-rpc/ipc/IpcServer`），线协议是换行分隔的 JSON-RPC：一请求一行、一响应一行，请求体与 TCP 上的 HTTP body 相同，服务端直接复用 `JsonRpcInterface::onRPCRequest` 的分派。`admin_getLogLevel` / `admin_setLogLevel` 只注册进 socket 专用的方法表（`JsonRpcInterface::m_ipcOnlyMethods`），HTTP/WS 路径查不到它们，返回 `-32601`。鉴权就是文件权限：socket 继承进程 umask，能读数据目录的人才能挂接。`[rpc] ipc_enable=false` 关闭。

## 为什么是 unix socket

同机场景要的是「ssh 上去、进节点目录、敲命令」，不碰 IP、端口、TLS、证书。TCP 端口上加一套鉴权（token、白名单）等于新造一个攻击面，而数据目录的文件权限是运维已经在管的边界。socket 文件放在 `data_path` 下而不是 `/tmp`，一台机器多个节点各自一份，路径由节点目录唯一确定。

## 为什么跨机不做证书模式

RPC 服务端强制校验客户端证书（`bcos-boostssl/bcos-boostssl/context/ContextBuilder.cpp` 的 `verify_peer | verify_fail_if_no_peer_cert`），要连 TLS 端口就得给工具配 `sdk.crt/sdk.key`，这正是 Java 控制台让人放弃的那一步。`build_chain.sh` 默认生成 `[rpc] enable_ssl=true`，但生产部署常把它关掉（无证书分发负担）；跨机检查只对关了 SSL 的端口成立。工具先向端口发一条最小 TLS ClientHello，收到 ServerHello 或 alert 即判定为 TLS 端口，直接报错退出码 1、提示不支持证书模式，而不是尝试无证书连接后给一个含糊的超时（明文节点对这条记录静默关连接，随后的 WebSocket 连接给出真实错误）。

## 备选

- **信号 dump**（SIGUSR1 把状态写文件）：单向、无参数、没有结构化返回，`admin_setLogLevel` 这种带参写操作做不了。
- **admin 方法走 TCP + 鉴权**：见上，新攻击面。
- **ptrace / 调试器**：需要 root 或同 uid 且关掉 ptrace 保护，生产机不可用。

## Consequences

- 挂接只承诺同版本可用：客户端与服务端是同一个二进制，不做跨版本协议兼容。
- 一个连接上串行处理（读完一条、回完一条再读下一条），不做 pipelining；单行上限与 `rpc.max_msg_size` 一致。
- 第二步再加 `admin_consensusState` / `admin_txpoolState` / `admin_syncState` / `admin_config`，在日志整理定下 viewchange 原因与拒绝原因枚举之后。
- 来源选择顺序：`--rpc` 显式指定 → socket 存在且可连（`source=attach`）→ 节点目录的明文 RPC（`source=rpc`）；TLS 报错。
