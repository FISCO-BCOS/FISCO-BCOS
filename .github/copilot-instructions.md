# FISCO-BCOS-1 的 Copilot 指南

本说明旨在让 AI 编码代理在该单仓多模块项目中立即高效产出。

## 全局视图：模块与数据流
- 本仓库是 FISCO BCOS v3 C++ 内核（CMake/vcpkg）。根目录 `CMakeLists.txt` 通过 `add_subdirectory` 组织主要模块：`bcos-framework`（接口与任务系统）、`bcos-crypto`、`bcos-codec`、`bcos-protocol`、`bcos-tars-protocol`、`bcos-executor`、`bcos-scheduler`、`bcos-ledger`、`bcos-storage`、`bcos-txpool`、`bcos-pbft`/`bcos-rpbft`、`bcos-sync`、`bcos-gateway`、`bcos-rpc`、`bcos-tool` 等。
- 数据/交易主路径（全节点）：
  1) RPC/Gateway 接收交易 -> `bcos-txpool` 入池/排队
  2) 共识（`pbft`/`rpbft`）出块排序
  3) Scheduler 调度 -> Executor 执行交易（启用时通过 evmc/evmone 运行 EVM）
  4) 状态经 `bcos-storage` 写入 RocksDB/TiKV（可选）
  5) Ledger 持久化区块/回执；Sync 传播；Front/Gateway 处理 P2P
- 接口定义集中于 `bcos-framework`，各模块提供具体实现。目标别名在 `cmake/TargetSettings.cmake` 中定义，跨模块链接请优先使用别名。

## 构建、测试与选项
- 工具链：CMake 3.28+、GCC/Clang、vcpkg 清单。自定义 registry 见 `vcpkg-configuration.json`；覆盖与特性在 `vcpkg.json`。
- 从仓库根目录配置（与 CI 接近的 Linux 示例）：
  - 仅构建发布版全节点（关闭测试）：`cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLINKER=mold -DTESTS=OFF`
  - 启用测试：添加 `-DTESTS=ON`，随后在 `build/` 目录运行 `ctest -j`
  - 主要开关（见 `cmake/Options.cmake`）：`FULLNODE`(ON)、`WITH_LIGHTNODE`(OFF)、`WITH_TIKV`(OFF)、`WITH_TARS_SERVICES`(OFF)、`WITH_CPPSDK`(ON)、`WITH_WASM`(OFF)、`TOOLS`(OFF)；内存分配器通过 `-DALLOCATOR=tcmalloc|mimalloc|default` 指定
  - 常见 CI 构建参数可参考 `.github/workflows/workflow.yml`
- vcpkg：首次配置会自动初始化子模块 `vcpkg/`（若未指定 `CMAKE_TOOLCHAIN_FILE`）。依赖包括 Boost、fmt、protobuf、evmc/evmone、rocksdb（特性开启时）、tarscpp、wedprcrypto 等。

## 约定与模式
- 可等待/`tag_invoke` 的存储接口：`bcos-framework/bcos-framework/storage2/Storage.h` 以 tag 定义异步接口。
  - 读取：`co_await readOne(storage, key)` 或 `co_await readSome(storage, keys)`
  - 写入：`co_await writeOne(storage, key, value)` 或 `co_await writeSome(storage, kvs)`
  - 删除：`removeOne/removeSome`；遍历：`range(storage, ...)` 返回可等待迭代器，调用 `.next()`
  - `StorageValueType = std::variant<NOT_EXISTS_TYPE, DELETED_TYPE, Value>` 表达三态
  - 使用 `ReadableStorage/WritableStorage` 概念在较新 GCC/Clang 下做能力约束
- 任务/协程：基于 `bcos-task`（`bcos-framework` 链接 `bcos-task`、`Microsoft.GSL::GSL`、`fmt::fmt`）。推荐返回 `task::Task<T>`，使用 `co_return`/`co_await`。
- 目标命名：优先使用 `cmake/TargetSettings.cmake` 中的别名（如 `STORAGE_TARGET`、`LEDGER_TARGET`、`EXECUTOR_TARGET`），以稳定跨模块链接。
- 选项与 vcpkg 特性映射：由 `Options.cmake` 写入 `VCPKG_MANIFEST_FEATURES`（如 `fullnode`、`tests`、`lightnode`、allocator 特性），新增选项应遵循该模式。

## 关键入口
- 根层：`CMakeLists.txt`、`cmake/Options.cmake`、`cmake/TargetSettings.cmake` 展示模块装配方式
- 框架与契约：`bcos-framework/**`（接口、storage2、任务系统）
- 节点组装：全节点会加入 sealer/security/scheduler/executor/storage/ledger/rpc/gateway/pbft/rpbft/tool/table/txpool/sync/front/libinitializer 等子目录
- CI 工作流列出 Linux/macOS/Windows 的必要系统包与配置

## 示例
- 仅支持只读的自定义存储实现：
  - 提供 `tag_invoke(ReadOne{}, YourStorage&, Key, ...) -> task::Task<std::optional<Value>>`，和/或 `tag_invoke(ReadSome{}, YourStorage&, RangeOfKeys, ...)`
  - 随后 `co_await existsOne(storage, key)` 将通过 `readOne` 的回退自动生效
- 新增特性开关：在 `Options.cmake` 定义开关；若影响依赖，则附加到 `VCPKG_MANIFEST_FEATURES`；增加 `add_compile_definitions`；并在根 `CMakeLists.txt` 中通过条件 `add_subdirectory` 控制模块引入

## 易踩点
- 不支持 32 位构建。建议使用 GCC 12+/Clang 14+ 以更好支持 concepts/协程
- 如覆盖 `CMAKE_TOOLCHAIN_FILE`，需与 `vcpkg-configuration.json` 的 registry 设置匹配
- 某些模块/测试仅在对应开关/特性（如 RocksDB 或 TiKV）开启时才满足前提

若需更深入的模块级说明（如 executor/scheduler 边界、RPC API 粘合点），请指出希望扩展的部分，我们将迭代完善。