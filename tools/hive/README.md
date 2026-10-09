# FISCO-BCOS hive client (Ethereum L1 EL mode)

This directory contains the [hive](https://github.com/ethereum/hive) client
definition for running the FISCO-BCOS Ethereum L1 EL-mode node
(`[ethereum] mode=el`, executor v2) against the official Ethereum cross-client
test suites.

## Layout

```
clients/bcos/        hive client definition (copy/symlink into hive's clients/)
  Dockerfile         wraps prebuilt binaries from bin/ (see build-local.sh)
  hive.yaml          roles: eth1
  bcos.sh            entrypoint: genesis.json -> config.genesis, HIVE_* env
                     mapping, /chain.rlp + /blocks/ import, node startup
  enode.sh           /hive-bin/enode.sh: prints the container's enode URL
  config.ini.tpl     node config template (substituted by bcos.sh)
  genesis.json       default genesis (Cancun-at-genesis, post-merge) baked into
                     the image; hive bind-mounts the simulator's /genesis.json
                     over it when a test supplies one (the smoke/network suite
                     starts the client with no files at all)
build-local.sh       collects build/ binaries into clients/bcos/bin/ and
                     optionally installs the client into a local hive checkout
```

## Quick start

```bash
# 1. build the node (see the repo's CI workflow for the full recipe)
cmake --build build --target fisco-bcos eth-sync-check -j"$(nproc)"

# 2. collect binaries and install into a hive checkout
git clone https://github.com/ethereum/hive ../hive && (cd ../hive && go build ./...)
bash tools/hive/build-local.sh ../hive

# 3. run (note: --sim is a suffix-anchored regex, so name suites explicitly)
(cd ../hive && ./hive --sim "smoke/(genesis|network)" --client bcos)
(cd ../hive && ./hive --sim ethereum/rpc-compat --client bcos)

# eels with locally staged fixtures (EEST release tarball extracted under
# fixtures/): stage fixtures.tar.gz next to the simulator's Dockerfile and
# select it with the fixtures build arg. --sim.limit is re.match-anchored,
# so use ".*pattern.*" for substring selection.
(cd ../hive && ./hive --sim ethereum/eels/consume-rlp --client bcos \
    --sim.buildarg fixtures=/fixtures --sim.limit ".*mcopy.*")
```

## Real-hive results (2026-09/10, local docker)

Final numbers with the gcc-16 Release client (ubuntu:26.04-based image); the
eels rows were rerun after the ethBlockVersion and fork-field-presence fixes:

- `smoke/network`: **2/2 pass** (container starts, TCP 8545 reachable).
- `smoke/genesis`: **4/6 pass** — genesis hash matches the geth-computed
  expectation byte-exactly for the non-empty-alloc cases. The two failures
  ("empty genesis", "all forks") inject `genesis-empty.json` (no `config`
  section, `gasLimit: 1`, no London) — a pre-London genesis — and "all forks"
  additionally sets `HIVE_TERMINAL_TOTAL_DIFFICULTY=24` (a PoW phase). Both
  are the pre-London limitation below, previously masked by the empty-alloc
  rejection firing first.
- `ethereum/eels/consume-rlp` (EEST fixtures v5.4.0, staged locally):
  - `.*mcopy.*` **186/186 pass** (Cancun + Prague, incl.
    `blockchain_test_from_state_test`).
  - Prague `.*(eip7702|eip2935).*` **628/628 pass**
    (`--client.checktimelimit 25m`; the `many_delegations` fixtures import a
    single block in ~10 min under the Debug+ASan build, and container start +
    genesis conversion add enough overhead that 15m is too tight there).
  - `.*(eip4895_withdrawals|eip6780_selfdestruct|eip1153_tstore).*` **794/794
    pass** — all 22 `fork_Paris` selfdestruct cases pass after the
    ethBlockVersion stamping fix; the 3 former failures (`test_large_amount`,
    `test_multiple_withdrawals_same_address` ×2, empty-alloc Shanghai
    fixtures) pass after the empty-alloc fix below — verified by a targeted
    rerun of both fixture files (9/9 pass, incl. Cancun/Prague variants).
  - `.*eip4844_blobs.*` **4408/4408 pass** — incl. the
    `invalid_pre_fork_block_with_blob_fields` fork-transition variants, fixed
    by the symmetric fork-field-presence check in `EthPoSHeaderValidation.h`.
- `ethereum/rpc-compat`: client launch fails by design — its fixture chain is
  pre-London (londonBlock=27, TTD>0, merge at block 36) and `--genesis2ini`
  rejects `terminalTotalDifficulty > 0`. Recorded as the known pre-London gap.

## How it works

Hive injects `/genesis.json` (geth format), optional `/chain.rlp` and
`/blocks/*.rlp`, plus `HIVE_*` environment variables, then waits for TCP 8545.
The entrypoint:

1. converts `/genesis.json` into the node's INI `config.genesis` with
   `eth-sync-check --genesis2ini` (alloc MPT state root and the
   `[eth_genesis_header]` hash are computed by the tool and verified by the
   node on load);
2. maps `HIVE_BOOTNODE` onto the devp2p bootnodes file (a placeholder enode
   keeps the autonomous sync loop idle when no peer is injected);
3. enables the Engine API listener (8551, JWT) when the simulator mounted
   `/jwtsecret`;
4. imports `/chain.rlp` then `/blocks/` via
   `fisco-bcos --import-blocks <path>` — invalid blocks are skipped, so the
   best block after startup is the last valid imported block. The import
   exits 0 normally, and exits 1 when nothing could be imported on a fresh
   ledger (`imported == 0`, `skipped > 0`, head never advanced past genesis);
   the entrypoint deliberately tolerates both (`|| true`) because the
   simulator's pass/fail verdict comes from the RPC assertions, not the
   import exit status;
5. execs the node.

## Supported HIVE_* variables

`HIVE_CHAIN_ID`, `HIVE_NETWORK_ID`, `HIVE_LOGLEVEL`, `HIVE_BOOTNODE`,
`HIVE_MINER` (accepted; block production is Engine-API driven), `HIVE_NODETYPE`.

## Current limitations (tracked follow-up work)

- **Outbound-only devp2p**: the node does not listen for inbound RLPx
  connections yet, so `enode.sh` output is informational; the `devp2p` eth
  suite and `ethereum/sync` (which dial into the client) are not expected to
  pass yet.
- **No node discovery** (discv4/discv5): `devp2p` discovery suites are out of
  scope.
- **No GraphQL**: `ethereum/graphql` is out of scope.
- **No Clique/PoW**: `HIVE_CLIQUE_*` are ignored.
- **Pre-London chains are not representable**: the EL fork schedule
  (`[fork_timestamps]`) models London and later only, so fixtures whose chain
  starts before London — including the current `ethereum/rpc-compat` fixture
  chain (londonBlock=27, TTD>0, merge at block 36) — cannot be imported yet.
  `--genesis2ini` rejects `terminalTotalDifficulty > 0` loudly for now.
- ~~**Empty alloc**~~ (fixed): `NodeConfig::validateL2Invariants` now exempts the
  L1 EL lane (`[ethereum] mode=el`) from the non-empty-alloc invariant — an
  empty-alloc genesis publishes the canonical empty-trie root as its stateRoot
  (`Ledger::buildGenesisBlock`, matching geth byte-exactly) — and
  `--genesis2ini` converts an `alloc: {}` genesis instead of rejecting it. The
  L2/OP lanes still require allocs (the SystemConfig predeploy's feature_flags
  slot travels in them).
- `engine_forkchoiceUpdatedV4` (Osaka) is not implemented yet.
- ~~**EthBlockVersion stamping**~~ (fixed): `makeExecutionBlockHeader`
  (EthereumBlockVerifier.h) previously never called `setEthBlockVersion`, so
  committed headers read back as `NON_ETH`. Shanghai+ blocks were served
  through the OP-shaped fallback (same values, accidental pass), but
  Paris/London headers — no withdrawalsRoot — fell into the NON_ETH mock
  branch: `eth_getBlockByNumber` lost `miner` and mocked `mixHash`/
  `withdrawalsRoot`, failing hive's FixtureHeader validation on all 22
  fork_Paris cases. The version is now derived from the fork-gated field
  presence (mirroring `EthBlockHeader::rlpDecode`) and stamped at commit.
- ~~**Fork-field presence was one-sided**~~ (fixed):
  `validateForkFieldPresence` (EthPoSHeaderValidation.h) only rejected headers
  MISSING a field their active fork requires, not headers carrying a field
  BEFORE its fork. A pre-Cancun Shanghai header with stray blob fields passed
  (the positional RLP decode parks the extra field in the blobGasUsed slot),
  so `invalid_pre_fork_block_with_blob_fields` got imported instead of
  rejected. The check is now symmetric (geth's "x is not allowed before
  fork-y" rules), covering withdrawalsHash/blobGasUsed/excessBlobGas/
  parentBeaconBlockRoot/requestsHash.
- **Coroutine stack depth**: the EL executor's stackless-coroutine await chain
  accumulates one resume level per sequential storage write (no symmetric
  transfer on completion), so blocks touching hundreds of accounts — e.g. the
  EIP-7702 `many_delegations` / `many_valid_authorizations_multiple_signers`
  fixtures — overflow the default 8MiB main-thread stack under ASan
  (`AddressSanitizer: stack-overflow` in `applyToStorage` → `EVMAccount::setCode`
  / `setBalance`). `bcos.sh` / `test-local.sh` raise the limit to a **finite**
  2GiB (`ulimit -s 2097152` — `unlimited` breaks ASan's shadow-memory layout,
  sanitizers#856); with it, the imports complete and head hashes match the
  fixtures byte-exactly. A proper fix (symmetric transfer in the task
  completion cascade, or bounded apply batching) is pending.

### Production lifecycle notes

- **EthBlockVersion stamping is forward-only** (no backfill): blocks committed
  by a pre-fix binary keep `NON_ETH` headers, so `eth_getBlockByNumber` on
  historical Paris/London blocks of such a datadir still loses `miner` (the
  NON_ETH mock branch). Consequence for `--import-blocks`: the resume anchor
  re-encodes the stored header, and a pre-fix `NON_ETH` header never matches
  the next block's `parentHash`, so resuming an import on a pre-fix datadir
  skips every block and exits non-zero. EL-mode datadirs are hive-test
  artifacts today; if a real EL chain ever accumulated pre-fix history, the
  answer is a fresh import with the fixed binary.
- **The importer is strictly linear**: a block that does not extend the
  current import head is skipped (loudly) — side-chain placement is not
  supported. The `reorg_window` comment in `clients/bcos/config.ini.tpl`
  concerns the devp2p sync lane, not `--import-blocks`; hive fork tests that
  rely on side-chain imports are out of scope. Verify-lane consolidation with
  the sync lane is tracked in #5670.
- **`--import-blocks` holds the whole input in memory**: the file is loaded
  whole and every block is copied out into its own buffer, so peak memory is
  roughly TWICE the input file size; plan RAM accordingly for multi-GB chain
  exports. Slicing + streaming the input is tracked in #5669.

## Host-level testing without docker

`test-local.sh` mirrors the container entrypoint on a bare host (genesis
conversion, block import, node startup, RPC sanity checks):

```bash
# convert an EEST blockchain fixture into genesis.json + chain.rlp
python3 tools/hive/eest_fixture_to_hive.py \
    .cache/fixtures/blockchain_tests/shanghai/eip4895_withdrawals/test_multiple_withdrawals_same_address.json \
    --out-genesis /tmp/genesis.json --out-chain /tmp/chain.rlp

bash tools/hive/test-local.sh /tmp/genesis.json /tmp/chain.rlp
```

Verified: Cancun/Prague EEST blockchain fixtures import with `skipped=0` and
the RPC-reported genesis/head hashes match the fixture's
`genesisBlockHeader.hash` / `lastblockhash` byte-exactly.

## Continuous integration

- **PR gate** (`workflow.yml` → `hive_smoke`, <10 min): the dockerised
  `smoke/(genesis|network)` suites plus a host-level replay of one EEST
  fixture (`tests/withdrawals_large_amount.*` — Shanghai-at-genesis, empty
  alloc) through `test-local.sh`, pinning the served genesis/head hashes
  byte-exactly. Reuses the build job's artifact; no C++ build of its own.
- **Nightly** (`.github/workflows/hive-nightly.yml`): the full eels
  `consume-rlp` matrix (mcopy / eip7702+2935 / withdrawals+selfdestruct+tstore
  / eip4844_blobs + a smoke shard) against a Release (MinSizeRel) build,
  sharded across parallel jobs with the EEST fixtures tarball cached.
- Both gate on `check-hive-results.py`, which parses hive's run JSON (the
  hive process exits 0 even when tests fail) and fails the job unless the
  failure set exactly matches the known-failure allow-list — today the two
  pre-London smoke/genesis cases ("empty genesis", "all forks").
