# L2 integration tests

Black-box integration scenarios for FISCO-BCOS OP-Stack L2 mode (A6). Each
scenario drives a running L2 node over the Web3 JSON-RPC (`eth_*`) and asserts
an L2-specific invariant: predeploys present, SystemConfig/RPC chainId
agreement, FISCO-private precompiles disabled, ProxyAdmin ownership, predeploy
code observability, the KZG precompile, and the two acceptance gates.

## Dependencies

- `bash` ≥ 3.2 (macOS stock bash OK — the scripts use only `bash -n`-clean POSIX-ish bash + arrays; no bash 4+ `${var,,}` lowercasing)
- `curl` and `jq` (JSON-RPC plumbing) — every scenario `require_cmd`s these
- `cast` (Foundry) — optional, used by `extcodehash-extcodecopy.sh` for local
  keccak; the scenario degrades to a skip-note without it
- The A8 devnet at `tools/.ci/l2-devnet/docker-compose.yml` (NOT in this repo
  yet — see SKIP semantics)

## Running

```bash
# Run everything (from anywhere):
bash tools/.ci/l2-integration/run-all.sh

# Run one scenario:
bash tools/.ci/l2-integration/genesis-bootstrap.sh
```

Useful env overrides (defaults in `lib.sh`):

| Env | Default | Meaning |
|-----|---------|---------|
| `L2_RPC_URL` | `http://127.0.0.1:8545` | L2 Web3 RPC endpoint |
| `DEVNET_COMPOSE` | `<repo>/tools/.ci/l2-devnet/docker-compose.yml` | devnet presence gate |
| `L2_PROXY_ADMIN_OWNER` | `0x000...000` | expected ProxyAdmin owner |
| `POLL_ATTEMPTS` / `MIN_ADVANCE` | `30` / `3` | gate-g3 block-advance poll |
| `L1_BRIDGE_TOOLING` | `<repo>/tools/.ci/l2-devnet/l1-bridge/deploy.json` | gate-g4 A8 marker |

## SKIP semantics

There is no devnet in this repo (`tools/.ci/l2-devnet/` is an A8-workstream
deliverable). Every scenario calls `require_devnet`: if the compose file is
absent it prints `SKIP: requires A8 devnet (tools/.ci/l2-devnet)` and exits with
the distinct code **77**. `run-all.sh` maps exit 77 to `SKIPPED` (not FAIL).

- All scenarios skipped -> `run-all.sh` prints `ALL SKIPPED (devnet absent)`
  and exits 0.
- Some ran and all non-skipped passed -> `ALL <n> INTEGRATION TESTS PASSED`.
- Any scenario failed -> non-zero exit.

This is intentional: it lets CI invoke `run-all.sh` today (it reports SKIPPED)
without faking passes, and the same script becomes a real gate once A8 lands.

## Scenario status

| Script | Fully automated now | TODO-A8 |
|--------|--------------------|---------|
| `genesis-bootstrap.sh` | yes — getCode on 13 predeploys + getValueByKey("chain_id") == 2 words, nonzero | — |
| `system-config-roundtrip.sh` | yes — getValueByKey("chain_id") word 0 == eth_chainId | — |
| `disabled-precompile-call.sh` | yes — 3 disabled precompiles read as code-less | — |
| `proxy-upgrade.sh` | partial — ProxyAdmin.owner() check | full `upgrade()` tx roundtrip (funded key) |
| `extcodehash-extcodecopy.sh` | partial — getCode size/stability + local keccak | in-EVM EXTCODEHASH/EXTCODECOPY via deployed helper (funded key) |
| `kzg-precompile-call.sh` | yes — 0x0a known-answer vector | — |
| `gate-g3.sh` | gate — block-production poll | acceptance blocked on A8 op-node wiring |
| `gate-g4.sh` | skeleton only | full ERC-20 deposit roundtrip (A8 L1 + bridge tooling) |

The "partial" scenarios pass on the achievable subset today and SKIP-NOTE the
A8-blocked remainder; they do not fail for the missing piece. The two gates
(G3, G4) are acceptance gates whose PASS is blocked on the A8 devnet.
