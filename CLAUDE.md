# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

FISCO BCOS v3 — 企业级金融区块链平台的 C++20 内核。采用 CMake/vcpkg 单仓多模块架构，支持三种节点模式：Air（全功能单进程）、Pro（TARS 微服务）、Light（轻客户端）。

## Build Commands

```bash
# 配置（macOS ARM）
export SDKROOT=$(xcrun --sdk macosx --show-sdk-path)
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTESTS=ON \
  -DWITH_TIKV=OFF -DWITH_WASM=OFF -DWITH_TARS_SERVICES=OFF \
  -DVCPKG_TARGET_TRIPLET=arm64-osx-release

# 配置（Linux Ubuntu 24.04）
CC=gcc-14 CXX=g++-14 cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DLINKER=mold -DTESTS=ON -DVCPKG_TARGET_TRIPLET=x64-linux-release

# 构建
cmake --build build --parallel $(nproc)

# 运行全部测试
cd build && CTEST_OUTPUT_ON_FAILURE=TRUE ctest -j$(nproc)

# 运行单个测试（CTest 正则匹配）
ctest -R "TestSuiteName/TestCaseName" -V

# 运行单个测试（Boost.Test 二进制直接执行）
./build/tests/fisco-bcos-test -t TestSuite/TestCase
./build/bcos-utilities/test-bcos-utilities -t TestSuite/TestCase

# 代码格式化
clang-format -style=file -i <file>

# 预提交检查（格式校验 + 提交大小限制）
bash tools/.ci/pre-commit
```

### Key CMake Options

| Option | Default | Description |
|---|---|---|
| `TESTS` | OFF | 启用测试构建 |
| `FULLNODE` | ON | 构建全节点 |
| `WITH_LIGHTNODE` | OFF | 轻节点支持 |
| `WITH_TIKV` | OFF | TiKV 分布式存储 |
| `WITH_TARS_SERVICES` | OFF | TARS 微服务模式 |
| `WITH_WASM` | OFF | WASM 合约支持 |
| `WITH_CPPSDK` | ON | C++ SDK |
| `ALLOCATOR` | default | 内存分配器 (default/tcmalloc/mimalloc) |
| `LINKER` | default | 链接器 (default/mold/gold) |

vcpkg 首次配置自动初始化子模块。不支持 32 位构建。建议 GCC 12+/Clang 14+。

## Architecture

### 数据/交易主路径

```
RPC/Gateway → bcos-txpool（入池）→ 共识(pbft/rpbft) → Scheduler → Executor(EVM/WASM)
  → bcos-storage(RocksDB/TiKV) → Ledger 持久化 → Sync/Front/Gateway P2P 传播
```

### 核心模块

- **bcos-framework/** — 所有接口定义（Ledger, Storage, Executor, Scheduler, Consensus, TxPool, Gateway, RPC, Front）和任务/协程系统（`bcos-task`）
- **bcos-executor/** — 交易执行引擎，支持串行、DMC（无死锁并发）、DAG 并行三种执行模式
- **bcos-scheduler/** — 区块调度，将交易路由到对应 Executor
- **bcos-pbft/** / **bcos-rpbft/** — PBFT / rPBFT 共识算法
- **bcos-txpool/** — 交易池管理与广播
- **bcos-storage/** — RocksDB/TiKV 存储后端
- **bcos-ledger/** — 区块与回执持久化
- **bcos-gateway/** — P2P 网络层与 AMOP 消息协议
- **bcos-rpc/** — JSON-RPC 服务接口
- **bcos-sync/** — 区块同步
- **bcos-front/** — 前端路由，连接 Gateway 与共识模块
- **bcos-crypto/** — 密码学（ECDSA/SM2, SHA3/SM3, secp256k1/sm2p256v1）
- **bcos-protocol/** / **bcos-tars-protocol/** — 协议定义与 TARS 编解码实现
- **bcos-table/** — Table 存储层，事务隔离与缓存
- **lightnode/** — 轻节点实现
- **libinitializer/** — 节点组装与启动编排

### 节点入口

- **Air 模式**: `fisco-bcos-air/main.cpp` → `AirNodeInitializer`
- **Pro 模式**: `fisco-bcos-tars-service/` 下各独立服务
- **Light 模式**: `lightnode/`

### 初始化顺序

Protocol/Crypto → Storage(RocksDB) → Ledger → TxPool → Scheduler/Executor → PBFT/Sync/Sealer → Front → Gateway → RPC

## Conventions

- **接口与实现分离**: 接口集中在 `bcos-framework/`，各模块提供实现
- **CMake 目标别名**: 跨模块链接使用 `cmake/TargetSettings.cmake` 中的别名（如 `STORAGE_TARGET`, `LEDGER_TARGET`）
- **异步存储接口**: `bcos-framework/bcos-framework/storage2/Storage.h` 使用 `tag_invoke` 模式，配合 `co_await readOne/writeOne/readSome/writeSome`
- **协程**: 基于 `bcos-task`，函数返回 `task::Task<T>`，使用 `co_return`/`co_await`
- **存储值三态**: `StorageValueType = std::variant<NOT_EXISTS_TYPE, DELETED_TYPE, Value>`
- **测试框架**: Boost.Test（`BOOST_AUTO_TEST_SUITE` / `BOOST_AUTO_TEST_CASE`）
- **协议版本**: `BlockVersion` 使用 3 字节语义版本（如 3.16.4 = `0x03100400`）
- **代码风格**: Chromium-based，列宽 100，缩进 4 空格（见 `.clang-format`）
- **新增特性开关**: 在 `Options.cmake` 定义 → 附加到 `VCPKG_MANIFEST_FEATURES` → `add_compile_definitions` → 根 `CMakeLists.txt` 条件 `add_subdirectory`

## Caveats

- 覆盖 `CMAKE_TOOLCHAIN_FILE` 时需与 `vcpkg-configuration.json` 的 registry 设置匹配
- 某些模块/测试仅在对应开关开启（如 RocksDB、TiKV）时才编译
- 预提交钩子限制：单次提交不超 1000 行变更、35 个文件、单文件不超 300 行新增
