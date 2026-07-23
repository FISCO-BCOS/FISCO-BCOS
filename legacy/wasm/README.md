# WASM / Liquid support — archived

This directory holds the WASM (Liquid) execution support that was removed from
FISCO-BCOS. It is **reference material only**.

- **Not built.** `legacy/CMakeLists.txt` never does `add_subdirectory(wasm)`, so
  nothing here is compiled, linked, tested, or packaged. (The parent `legacy/`
  tree is built, but only the subdirectories it explicitly lists; this one is
  deliberately not among them. The headers sit on the `bcos-legacy` INTERFACE
  include path but are inert unless `#include`d, and nothing includes them.)
- **Not buildable.** The code depends on two vcpkg ports that were removed with
  it — `fbwasm` (the Rust WASM engine, `github.com/FISCO-BCOS/bcos-wasm`) and
  `wabt`. Restoring these files alone will not produce a working build.
- **Not maintained.** It will not be updated as the surrounding executor evolves,
  and it already refers to APIs that have since changed.

## Provenance

Everything here was taken verbatim from commit `decb19395`, the last commit
before WASM removal began. To retrieve the original bytes of any file:

```bash
git show decb19395:bcos-executor/src/vm/gas_meter/GasInjector.cpp
git checkout decb19395 -- bcos-executor/src/vm/gas_meter/   # restore a whole directory
```

Prefer git history over this directory when you need an exact original: the
repository pre-commit hook runs `clang-format -i` over every `.h`/`.cpp` file it
sees, so the C++ files here have been reformatted to the current repo style.
Their content is unchanged, but they are not byte-identical to `decb19395`.

## Layout

| Path | Original location | What it is |
|---|---|---|
| `executor/vm/EVMCWasm.h` | `bcos-executor/src/vm/` | EVMC extension structs — see the note below |
| `executor/vm/gas_meter/` | `bcos-executor/src/vm/gas_meter/` | WASM gas metering: bytecode instrumentation (`GasInjector`) and the per-opcode cost table (`Metric`). Built on `wabt`. |
| `executor/tools/` | `bcos-executor/tools/` | `inject_meter`, a standalone CLI that ran the gas injector over a `.wasm` file |
| `test/liquid/` | `bcos-executor/test/liquid/` | Liquid contract fixtures. The `.rs` files are the sources; the `.wasm` and `.h` files are compiled artifacts (the `.h` are hex dumps of the `.wasm`). |
| `test/unittest/` | `bcos-executor/test/unittest/` | `TestWasmExecutor.cpp` and the `WasmPath.h.in` template that pointed it at the fixtures |
| `ports/fbwasm/` | `ports/fbwasm/` | vcpkg port building the Rust engine. Pinned `rustup override set nightly-2024-02-25`. |
| `ports/wabt/` | `ports/wabt/` | vcpkg port for the WebAssembly Binary Toolkit, used only by `gas_meter/` |

## Important: `EVMCWasm.h` is not purely WASM

The archived copy defines **two** structs, and only one of them was WASM-specific:

- `wasm_host_interface` — the WASM host callback table. Dead, archived here.
- `evmc_gas_metrics` — **still live.** `evmc.h` only forward-declares this struct
  and holds a pointer to it in `evmc_host_context`; the definition lived here.
  The EVM path uses it via `ethMetrics` in `bcos-executor/src/Common.h`.

`evmc_gas_metrics` was therefore extracted into
`bcos-executor/src/vm/EVMCGasMetrics.h` and remains part of the active build. Do
not restore `EVMCWasm.h` into the source tree — you would get a duplicate
definition.

## Why WASM was removed

WASM support was reachable only through `executor.is_wasm=true` in the genesis
config, which is no longer parsed. The flag had already been made mutually
exclusive with parallel execution, auth check, and L2 ethereum-compat mode, so
its supported configuration surface had narrowed to almost nothing.

Two things were deliberately **kept** in the live tree for compatibility and must
not be "cleaned up" later:

- `GenesisConfig::m_isWasm` still exists and is permanently `false`, because
  `NodeConfig::generateGenesisData()` serializes it into the genesis string.
  Removing the field would change the genesis data of every existing chain.
- `Transaction::Attribute::LIQUID_SCALE_CODEC` and the `WASM*` values in
  `TransactionStatus` keep their numeric values. They are no longer produced, but
  the numbers are part of persisted data and must not be reused.
