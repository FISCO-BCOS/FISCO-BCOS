---
status: accepted
date: 2026-10-09
---

# 运维工具内置为 `fisco-bcos` 二进制的子命令，不是独立二进制，也不是继续依赖 Java 控制台

`fisco-bcos status` / `tx` / `log` / `attach` / `log-level` / `tui` 由节点二进制本身提供。`fisco-bcos-air/main.cpp` 在 `initAirNodeCommandLine` 之前加一层分派：第一个参数不以 `-` 开头就进入 `bcos::ops::runOps`，否则走原来的节点启动路径。无子命令时行为与旗标集合完全不变。

## 为什么不是独立二进制

发版验证的现场是「刚解包的节点目录」，运维手上只有这一个二进制和 `config.ini`。独立二进制要多一个分发物、多一套版本对齐（挂接通道只承诺同版本可用，ADR 0010），而且要重复链接同一批库（`bcos-cpp-sdk`、boostssl、jsoncpp）。把命令放进节点二进制，`./fisco-bcos status` 在任何节点目录下都能跑。

## 为什么不是继续用 Java 控制台

Linux 生产机没有 JDK；装 JDK、拉控制台、配 `sdk.crt` 是三个额外步骤，每一步都可能因为网络或权限卡住。控制台也拿不到进程内部状态（timer 剩余、交易池按状态分布），`admin_*` 方法只在本机 socket 上注册（ADR 0010），控制台连不上。CI 里 `tools/.ci/ci_check_air.sh` 先让 `tx smoke` + `status` 与控制台测试并存一个版本周期，对比结论后再退役控制台测试。

## 代价

- 节点二进制多链接 `bcos-cpp-sdk`（签名、ABI 编解码、WsService 客户端）与 FTXUI。`bcos-boostssl` 本来就在 AIR 二进制里，增量只是 SDK 的薄壳与 TUI 库。
- 根 `CMakeLists.txt` 的 `add_subdirectory(bcos-sdk)` 从 `fisco-bcos-air` 之后挪到之前，`WITH_CPPSDK` 在 FULLNODE 下必须开。
- `main.cpp` 多一层分派；子命令名空间与未来的旗标名不冲突（ADR 0004 的 EL 壳全是旗标）。
- `libinitializer/CommandHelper.cpp` 坏旗标与缺文件的 `exit(0)` 顺手改为 `exit(1)`，脚本能区分「打印了帮助」和「启动失败」。

## Consequences

- 新模块 `bcos-ops/`：`NodeStatus`、收集器（RPC / 挂接 / 日志）、判定器、日志解析、子命令、TUI。单测在 `bcos-ops/test/unittests/`，唯一注入点是 `RpcCall`。
- 只有 AIR 二进制带子命令；tars 服务与 lightnode 不做。
- 输出契约：非 TTY 默认 JSON，退出码 0 / 1（用法或连接）/ 2（节点可达但判据失败或 auth 拒绝）。
