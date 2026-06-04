# bcos-l2-contracts

Solidity suite for FISCO-BCOS OP-Stack L2 mode. Built and tested with
[Foundry](https://book.getfoundry.sh/) (`forge` / `cast`), independent of the
C++ build. The C++ tree only wires this in when `WITH_L2_CONTRACTS=ON`
(default `OFF`), so a normal node build does not require the forge toolchain.

## Layout

| Path | Contents |
|------|----------|
| `src/` | FISCO-BCOS self-written predeploys + interfaces |
| `test/` | Foundry tests for the self-written contracts |
| `storage-layout/` | Golden `forge inspect ... storage-layout` fixtures (PR-7 CI gate) |
| `op-fork-pin.toml` | Pinned upstream OP-Stack commit + dep SHAs (CI clones into `/tmp`) |

### Self-written predeploys (`src/`)

| Contract | Predeploy address |
|----------|-------------------|
| `SystemConfig.sol` | `0x42000000000000000000000000000000000000C0` (`0x42...00C0`) |
| `L2ValidatorSet.sol` | `0x42000000000000000000000000000000000000C1` (`0x42...00C1`) |

`SystemConfig` exposes an aggregate getter `getChainConfig()`
(selector `0x606c0c94`) returning `(chainId, l2BlockGasLimit,
compatibilityVersion, featureFlags)` in one call, so PR-4's `L2ConfigLoader`
makes exactly one `staticcall` per block. `chainId` is `immutable` (fixed at
deploy, no setter, no storage slot).

`L2ValidatorSet` follows the BSC ValidatorSet style. Phase A maintains only the
active set; the struct's `jailed` (always `false`) and `incoming` (always `0`)
fields and the `felony` / `misdemeanor` methods (which revert) are reserved for
Phase B governance.

### Upstream OP fork (not vendored)

The 11 entry-point OP-Stack predeploys that L2 genesis materializes are NOT
checked into this repo. The exact upstream commit + the four external Solidity
dependency SHAs (OpenZeppelin × 2, solady, solmate) live in `op-fork-pin.toml`.
CI clones the pinned commit into `/tmp` and builds it there with OP's own
foundry config (this project's `foundry.toml` only compiles the self-written
`src/` contracts). Bumping the pin is documented in
[`docs/runbook-op-fork-upgrade.md`](docs/runbook-op-fork-upgrade.md) (added by
PR-7).

The 11 entry-point predeploys (listed in `op-fork-pin.toml`):
`L1Block`, `L2ToL1MessagePasser`, `L2CrossDomainMessenger`, `L2StandardBridge`,
`GasPriceOracle`, `ProxyAdmin`, `SequencerFeeVault`, `BaseFeeVault`,
`L1FeeVault`, `WETH` (the WETH9 predeploy, renamed to `WETH` at v1.6.0),
`OptimismMintableERC20Factory`.

## Build / Test

```bash
forge install foundry-rs/forge-std   # one-time, into lib/ (git-ignored)
forge build
forge test -vvv
```

To exercise the OP fork build locally (CI does this automatically — clones the
pin into `/tmp/op-fork` and builds from there):

```bash
COMMIT=$(grep -E '^commit' op-fork-pin.toml | awk -F'"' '{print $2}')
git clone https://github.com/ethereum-optimism/optimism /tmp/op-fork
git -C /tmp/op-fork checkout "$COMMIT"
git -C /tmp/op-fork submodule update --init --recursive --depth=1 \
    packages/contracts-bedrock/lib
forge build --root /tmp/op-fork/packages/contracts-bedrock
```

Regenerate the storage-layout fixtures (only when storage intentionally changes):

```bash
forge inspect SystemConfig storage-layout --json > storage-layout/SystemConfig.json
forge inspect L2ValidatorSet storage-layout --json > storage-layout/L2ValidatorSet.json
```

The `--json` flag is mandatory: without it forge 1.5.1 emits an ASCII table
instead of JSON, which corrupts the fixtures.

CI gate (PR-7) fails if `forge inspect <C> storage-layout` diverges from the
checked-in `storage-layout/*.json`.

## Genesis deployment notes

`SystemConfig` and `L2ValidatorSet` are **genesis predeploys**: on-chain they
are instantiated by writing runtime bytecode + storage slots directly into the
genesis state (allocs), so their constructors never run on-chain (only in tests
and simulated deployments). Consequences for genesis tooling (PR-2
`build-allocs.py`):

1. **All storage state must come from allocs.** A predeploy constructor does
   not execute on-chain, so every storage slot a node reads at block 0 must be
   materialized in the genesis allocs, not derived from constructor logic.
2. **`SystemConfig.chainId` is `immutable`** — it lives in the runtime bytecode
   (solc `immutableReferences`), not in storage. `build-allocs.py` MUST patch
   the artifact's `deployedBytecode` at the offsets in
   `out/SystemConfig.sol/SystemConfig.json -> deployedBytecode.immutableReferences`,
   otherwise on-chain `getChainConfig()` returns `chainId == 0`.
3. **Constructor-emitted events are NOT emitted at genesis.** The
   `OwnershipTransferred(address(0), _owner)` (SystemConfig) and the initial
   `ValidatorSetUpdated` (L2ValidatorSet) logs only appear in
   tests/simulated deployments. Do not rely on them for bootstrap indexing.
