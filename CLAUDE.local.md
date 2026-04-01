# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

FISCO-BCOS is an enterprise-grade blockchain platform (v3.x). It uses a modular architecture with 25+ C++ modules, supporting two deployment modes: AIR (lightweight single-process) and MAX (microservice via Tars framework).

## Build Commands

```bash
# Configure (macOS ARM example, enable tests)
export SDKROOT=$(xcrun --sdk macosx --show-sdk-path)
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTESTS=ON \
  -DWITH_LIGHTNODE=ON \
  -DWITH_CPPSDK=ON \
  -DWITH_TIKV=OFF \
  -DWITH_WASM=OFF \
  -DWITH_TARS_SERVICES=OFF \
  -DTOOL=OFF ..

# Build everything
cmake --build . --parallel $(sysctl -n hw.ncpu)

# Build a single test target
cmake --build . --target test-bcos-txpool --parallel $(sysctl -n hw.ncpu)
```

Key CMake options: `TESTS=ON/OFF`, `BUILD_STATIC=ON/OFF`, `FULLNODE=ON/OFF`, `WITH_LIGHTNODE`, `WITH_CPPSDK`, `WITH_WASM`, `WITH_TIKV`, `ALLOCATOR=default|tcmalloc|mimalloc`, `LINKER=default|gold|mold`, `SANITIZE_ADDRESS=ON/OFF`, `SANITIZE_THREAD=ON/OFF`.

## Running Tests

```bash
# Run all tests
cd build && CTEST_OUTPUT_ON_FAILURE=TRUE ctest -j$(sysctl -n hw.ncpu)

# Run tests matching a pattern (by CTest name)
ctest -R TxpoolMemoryStorage

# Run a specific Boost test case directly
./build/bcos-txpool/test/test-bcos-txpool --run_test=TxpoolMemoryStorageTest/InsertExistsAndSize
```

Test framework: Boost.Test (`BOOST_AUTO_TEST_SUITE`, `BOOST_AUTO_TEST_CASE`, `BOOST_FIXTURE_TEST_SUITE`). Mocking via FakeIt. Test binaries are auto-discovered by `cmake/SearchTestCases.cmake` using `GLOB_RECURSE` on `unittests/*.cpp` -- new test files are picked up automatically without CMakeLists.txt changes.

## Architecture

```
bcos-framework          Core interfaces (INTERFACE library, no .cpp)
  ├── bcos-utilities     Common helpers
  ├── bcos-crypto        Crypto (SM2/secp256k1/ed25519)
  ├── bcos-codec         ABI encoding
  └── bcos-protocol / bcos-tars-protocol   Serialization (Tars RPC)
        │
bcos-txpool             Transaction pool & validation
bcos-executor           EVM execution engine
bcos-scheduler          Parallel execution scheduling (DMC/DAG)
bcos-pbft / bcos-rpbft  Consensus
bcos-storage            Storage abstraction (RocksDB/TiKV)
bcos-ledger             Blockchain ledger
bcos-gateway            P2P networking
bcos-sync               Block synchronization
bcos-rpc                JSON-RPC service
bcos-front              Frontend message routing
libinitializer          Node bootstrap orchestration
```

Transaction flow: RPC -> Front -> TxPool -> PBFT consensus -> Scheduler -> Executor -> Storage/Ledger.

Entry points: `fisco-bcos-air/` (AIR node), `fisco-bcos-tars-service/` (MAX microservices), `lightnode/` (light node).

## Coding Conventions

- **C++20**, compiled with GCC 11+ or Clang 4+
- Formatting enforced by `.clang-format` (Chromium-based, 100 col limit, 4-space indent). Pre-commit hook runs `clang-format` on staged files
- Static analysis rules in `.clang-tidy`
- **Naming**: PascalCase for types, camelCase for functions, `m_` prefix for members, `c_` prefix for class-level constants
- **Interfaces**: suffix `Interface` (e.g. `StorageInterface`), defined in `bcos-framework/`
- **Smart pointer aliases**: each class typically defines `using Ptr = std::shared_ptr<ClassName>`
- **Async pattern**: callback-based `void asyncFoo(params, std::function<void(Error::Ptr, Result)>)` and C++20 coroutines via `bcos-task/`
- **Error handling**: `Error::Ptr` as first callback argument (nullptr = success)
- Pre-commit hook enforces: format check, license header in first 20 lines, max 35 files per commit, max 300 inserted lines

## Dependencies

Managed via vcpkg (`vcpkg.json`). Key deps: Boost (test, log, beast, context), TBB, RocksDB, evmone, secp256k1, OpenSSL, protobuf, FakeIt (test mocking), redis++.

## Test File Conventions

Each module has tests under `<module>/test/unittests/`. Test files are auto-collected by `GLOB_RECURSE`. When adding regression tests for bug fixes, prefer creating a separate `.cpp` file (e.g. `FIB48_DoubleResumeTest.cpp`) over appending to an existing test file, to avoid merge conflicts when multiple PRs touch the same file.
