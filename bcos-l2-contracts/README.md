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
| `op-fork-pin.toml` | Pinned upstream OP-Stack commit + dep SHAs (CI clones into `/tmp`) |

### Self-written predeploys (`src/`)

| Contract | Predeploy address |
|----------|-------------------|
| `SystemConfig.sol` | `0x42000000000000000000000000000000000000C0` (`0x42...00C0`) |
| `L2ValidatorSet.sol` | `0x42000000000000000000000000000000000000C1` (`0x42...00C1`) |

`SystemConfig` is a generic key→value store: `setValueByKey(key, value,
enableNumber)` / `getValueByKey(key)`, each entry packed as `(uint192 value,
uint64 enableNumber)` in a single slot. The L2 upper layers read a key's slot
directly (no EVM) via `keccak256(utf8(key) ‖ be32(baseSlot))`, where `baseSlot`
is the slot of `_config` (101, after the OZ upgradeable base; pinned by PR-7's
storage-layout gate). `getValueByKey` is the EVM-callable path for external
callers. Access control is OZ `OwnableUpgradeable` (owner = ProxyAdmin). See
`2026-06-17-systemconfig-slot-kv-redesign.md` for the full contract.

`L2ValidatorSet` is a single-record-CRUD validator registry (BSC-style fields):
`addValidator` / `removeValidator` / `updateValidator` / `getValidator` /
`getValidators` / `isValidator`, all O(1), backed by OZ `EnumerableSet.AddressSet`
plus a parallel record mapping. The `Validator` struct packs `feeAddress` +
`jailed` + `votingPower` into one slot; `consensusPublicKey` is algorithm-neutral. Access
control is OZ `OwnableUpgradeable` (owner = ProxyAdmin). `jailed` (always
`false`), `incoming` (always `0`) and `felony` / `misdemeanor` (revert) are
reserved for Phase B.

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
# one-time, into git-ignored lib/; OZ pinned to op-fork-pin.toml [deps] SHAs
forge install foundry-rs/forge-std
forge install OpenZeppelin/openzeppelin-contracts@ecd2ca2cd7cac116f7a37d0e474bbb3d7d5e1c4d
forge install OpenZeppelin/openzeppelin-contracts-upgradeable@0a2cb9a445c365870ed7a8ab461b12acf3e27d63
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

The storage-layout baseline (`storage-layout/*.json`) and its drift gate are
added by PR-7: the gate regenerates `forge inspect <C> storage-layout --json`
and fails if it diverges from the checked-in baseline.

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
