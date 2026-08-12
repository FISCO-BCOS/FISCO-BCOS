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
| `SystemConfig.sol` | `0x43000000000000000000000000000000000000C0` (`0x43...00C0`) |
| `L2ValidatorSet.sol` | `0x43000000000000000000000000000000000000C1` (`0x43...00C1`) |

The self-written predeploys live under the `0x43...` prefix because
`0x4200000000000000000000000000000000000000`–`0x42...000007FF` is the OP-Stack
reserved predeploy namespace: parking FISCO-specific contracts inside it would
collide with any future upstream predeploy assignment. `0x43...` mirrors the OP
addressing convention (2-byte suffix identifies the contract) one prefix up.

`SystemConfig` is a generic key→value store: `setValueByKey(key, value,
enableNumber)` / `getValueByKey(key)`, each entry packed as `(uint192 value,
uint64 enableNumber)` in a single slot. Runtime writes are whitelisted —
currently only `block_tx_count_limit`; `chain_id`, `gas_limit`,
`compatibility_version` and `feature_flags` are genesis-frozen (D4 authority
boundary). The L2 upper layers read a key's slot
directly (no EVM) via `keccak256(utf8(key) ‖ be32(baseSlot))`, where `baseSlot`
is the slot of `_config` (101, after the OZ upgradeable base; pinned by PR-7's
storage-layout gate). `getValueByKey` is the EVM-callable path for external
callers. Access control is OZ `OwnableUpgradeable` (owner = the L2 governance entity; the ERC-1967 admin slot separately holds ProxyAdmin). See
`2026-06-17-systemconfig-slot-kv-redesign.md` for the full contract.

`L2ValidatorSet` is a single-record-CRUD validator registry (BSC-style fields):
`addValidator` / `removeValidator` / `updateValidator` / `getValidator` /
`getValidators` / `isValidator`, all O(1), backed by OZ `EnumerableSet.AddressSet`
plus a parallel record mapping. The `Validator` struct packs `feeAddress` +
`jailed` + `votingPower` into one slot; `consensusPublicKey` is algorithm-neutral. Access
control is OZ `OwnableUpgradeable` (owner = the L2 governance entity; the
ERC-1967 admin slot separately holds ProxyAdmin). `jailed` (always
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

Regenerate the storage-layout fixtures (only when storage intentionally changes):

```bash
forge inspect SystemConfig storage-layout --json > storage-layout/SystemConfig.json
forge inspect L2ValidatorSet storage-layout --json > storage-layout/L2ValidatorSet.json
```

The `--json` flag is mandatory: without it forge 1.5.1 emits an ASCII table
instead of JSON, which corrupts the fixtures.

CI gate (PR-7) fails if `forge inspect <C> storage-layout` diverges from the
checked-in `storage-layout/*.json`. The gate normalizes away compiler-internal
AST node ids (`astId` and the numeric suffix in struct type identifiers) before
diffing, so it only fires on a real slot / offset / label / type-shape change —
see `.github/workflows/l2-contracts.yml`.

## Runbooks

| Runbook | Covers |
|---------|--------|
| [`docs/runbook-l2-mode.md`](docs/runbook-l2-mode.md) | Bring up a node in L2 mode: 5-step quick start, L2-vs-pbft behavior, the exact genesis error strings, and upgrade paths |
| [`docs/runbook-op-fork-upgrade.md`](docs/runbook-op-fork-upgrade.md) | Bump the pinned OP fork commit: edit `op-fork-pin.toml`, CI re-clones, storage-layout hard gate |

## Genesis deployment notes

`SystemConfig` and `L2ValidatorSet` are **genesis predeploys**: on-chain they
are instantiated by writing runtime bytecode + storage slots directly into the
genesis state (allocs), so their constructors never run on-chain (only in tests
and simulated deployments). Consequences for genesis tooling (PR-2
`build-allocs.py`):

1. **All storage state must come from allocs.** A predeploy constructor does
   not execute on-chain, so every storage slot a node reads at block 0 must be
   materialized in the genesis allocs, not derived from constructor logic.
2. **Three-layer terminal state per self-written predeploy.** `build-allocs.py`
   emits (a) the proxy account at the predeploy address — Proxy bytecode,
   EIP-1967 implementation + admin slots, `_initialized = 1`, the owner slot
   and the contract state (packed SystemConfig entries / validator records) —
   and (b) a separate implementation account (`0xc3d3...<suffix>`) carrying the
   implementation bytecode with `_initialized = 255` (initializers disabled).
   The two authorities stay split: EIP-1967 admin = ProxyAdmin,
   `Ownable.owner` = governance entity.
3. **Constructor-emitted events are NOT emitted at genesis.** The
   `OwnershipTransferred(address(0), _owner)` (SystemConfig) and the initial
   `ValidatorSetUpdated` (L2ValidatorSet) logs only appear in
   tests/simulated deployments. Do not rely on them for bootstrap indexing.
