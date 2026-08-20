# opstack-genesis: build-allocs.py

Merges FISCO's self-written predeploy overlay onto **op-deployer terminal
allocs** and emits FISCO-BCOS genesis `[alloc.N]` INI fragments for L2 mode.
L2 mode is enabled by the `feature_l2_ethereum_compat` flag in `[features]`
(there is no `chain_mode`); allocs require the flag and the flag requires
allocs (`validateL2Invariants`).

## Input authority split

The op-deployer-generated terminal alloc JSON (`--base-allocs`, **required**)
is the ONLY source for the OP-Stack side of genesis — every `0x42…` predeploy
proxy, every `0xc0d3…` implementation, ProxyAdmin ownership and prefunded
accounts, including full proxy storage. The tool never synthesizes an OP
account. On top of the base it overlays exactly the FISCO self-written
predeploys (`SystemConfig`, `L2ValidatorSet`) in their post-deployment terminal
state: proxy account (canonical Proxy runtime bytecode copied from a
designated base-alloc proxy, `proxy_code_source`) + EIP-1967 slots,
implementation account (bcos-l2-contracts forge bytecode, `_initialized=255`),
and the proxy contract storage (`_initialized=1`, `Ownable.owner`, packed
SystemConfig entries / L2ValidatorSet records — including the `feature_flags`
entry, which the C++ genesis path later VERIFIES rather than injects).

Provenance: pin the frozen base allocs' sha256 as `base_allocs_sha256` in the
chain-config YAML (mirroring `bcos-l2-contracts/op-fork-pin.toml`
`[karst_pin].base_allocs_sha256`); an unpinned or mismatching base warns/fails
at `verify_base_provenance`.

## Usage

```bash
# 1. build the self-written contracts (SystemConfig / L2ValidatorSet)
make contracts CONTRACTS=../../bcos-l2-contracts

# 2. copy the template and adjust the checklist/overlay for your chain
cp chain-config.template.yaml chain-config.yaml

# 3. emit allocs.ini (and optionally the geth-style alloc JSON that feeds the
#    op-reth oracle so both chains share one account set)
python3 build-allocs.py --config chain-config.yaml \
    --contracts ../../bcos-l2-contracts \
    --base-allocs /path/to/op-deployer-l2genesis.json \
    --out allocs.ini --out-json allocs.json
```

`allocs.ini` is appended to `config.genesis`; `NodeConfig::loadAllocs()` parses
the `[alloc.N]` / `[alloc.N.storage]` sections, and the Ledger materializes
them at genesis (bytecode + storage verbatim; zero-valued storage slots are
elided — trie-equivalent — and addresses are lowercased).

Dependencies: Python 3 stdlib + `pyyaml`. Tests also need `pytest`.

## Fork activation: genesis feature flag (OP Stack divergence)

OP Stack activates forks by L2 block timestamp (e.g. `chain.jovian_time`).
FISCO selects the OP fork at **genesis time** via the `[features]` section
instead — there is no timestamp-based activation mechanism in FISCO.

- **Isthmus** is the OP-mode baseline (default when `executor_version >= 3`).
- `feature_op_jovian=true` in `[features]` enables Jovian semantics
  (DA footprint, operator fee ×100, minBaseFee, new calldata selector).
- The fork is fixed at node start: **no mid-chain Isthmus→Jovian transition
  is possible**.

For new chains (genesis-activated Jovian), this is equivalent to the OP
spec. For chains that need to upgrade from Isthmus to Jovian mid-life, this
is a known limitation — a timestamp-based activation mechanism would be
required.

## chain-config.yaml fields

| field | meaning |
|-------|---------|
| `base_allocs_sha256` | expected sha256 of the frozen op-deployer base allocs (provenance pin) |
| `proxy_code_source` | base-alloc address whose Proxy runtime bytecode the overlay copies |
| `proxy_admin` / `governance_owner` | the two-authority split: EIP-1967 admin vs `Ownable.owner` (must differ) |
| `predeploys` | FISCO overlay predeploys: `{name, address, sol_file}`; the SystemConfig entry carries the `system_config` values (chain_id, gas_limit, feature_flags, …), the L2ValidatorSet entry carries `validators` records |
| `expected_predeploys` | existence checklist: each address must be present in the base allocs WITH code, else the build fails (guards against wrong/incomplete op-deployer output) |

## Safety rules (enforced by the build)

- An overlay address that already exists in the base allocs is a hard error.
- An `expected_predeploys` entry missing (or codeless) in the base fails the build.
- A self-written implementation artifact still carrying `immutableReferences`
  aborts the build naming the contract (zero-filled immutables would be broken
  runtime code); there is no opt-out.
- The EIP-1967 admin slot (upgrade authority) is ProxyAdmin while
  `Ownable.owner` (config/validator write authority) is the governance entity —
  two independent slots, never the same entity.

## Testing

```bash
python3 -m pytest test_build_allocs.py -v
```

The suite uses synthetic base-allocs + forge artifacts written to `tmp_path`;
it does not run `forge` or op-deployer. It covers the overlay layers, the
`expected_predeploys` checklist failure mode, provenance pinning, and the
immutables abort.
