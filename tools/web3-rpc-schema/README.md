# Web3 RPC parameter schemas

The pilot uses parameter Schema Objects from [Ethereum execution-apis at
`68d76a5b1c09dff023782d97eec6ce87a6f6505e`](https://github.com/ethereum/execution-apis/tree/68d76a5b1c09dff023782d97eec6ce87a6f6505e).
The files under `upstream/` match the corresponding Git blobs at that commit.
The snapshot contains the method and component files required to regenerate
the pilot's request schemas, along with the upstream CC0-1.0 license.
`upstream.json` records the source hashes.

With Python 3.10 or newer and the pinned `requirements.txt` installed, run these
commands from the repository root. Generation and checking use local files.

```sh
python3 tools/web3-rpc-schema/generate.py
python3 tools/web3-rpc-schema/generate.py --check
python3 tools/web3-rpc-schema/test_generate.py
```

The C++ build uses the checked-in `ParameterSchemas.generated.h`. Commit Check
verifies that header and `generated-manifest.json` against the generator,
upstream sources and RPC registrations. The manifest inventories the schema
keywords used by each migrated method. For an upstream upgrade, update the
snapshot and hashes, review the method and component changes, then regenerate.
Record each newly registered endpoint or subscription in `coverage.json`
with its migration status and reason.

## Local acceptance

On macOS with Python 3.11 or newer, build AIR from the release baseline and
from the candidate using the same compiler, configuration and dependencies.
Build the `eth-sync-check` target for genesis conversion. Then run the three-lane
HTTP and WebSocket comparison and paired performance measurements on loopback.

```sh
clang -std=c17 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -dynamiclib \
  tools/web3-rpc-schema/malloc_count.c -o /tmp/web3-rpc-malloc-count.dylib
python3 tools/web3-rpc-schema/run_acceptance.py \
  --old /path/to/baseline/fisco-bcos --new /path/to/candidate/fisco-bcos \
  --genesis-tool /path/to/eth-sync-check \
  --malloc-counter /tmp/web3-rpc-malloc-count.dylib \
  --out /tmp/web3-rpc-acceptance --mode both
```

Choose an output directory that does not exist. The harness writes `report.json`
with binary hashes, functional results, startup, throughput, latency and RSS.
Allocation counting is optional. Omit `--malloc-counter` to skip it, or use
`--mode functional` for the HTTP and WebSocket comparison alone.

## Pilot parameter decisions

| Field | Upstream | Pilot |
| --- | --- | --- |
| Address in both state reads | `0x` prefix and 40 hex digits, including uppercase digits | Keep exactly 40 digits and preserve existing `0X` and unprefixed addresses. For example, both `0X` plus 40 digits and the same 40 digits without a prefix pass. Short or non-hex addresses return `-32602`. |
| Storage slot | Lowercase `0x` and up to 64 lowercase hex digits | Preserve leading zeros, odd lengths, uppercase hex, `0X`, unprefixed slots and the empty slot. For example, `0X00F` and `0x` pass. Non-hex and more than 64 digits return `-32602`. |
| Block selector in the state reads | Optional. Accepts a number, tag, bare hash or EIP-1898 object | Preserve omission as `latest` and retain bare hash lookups used by op-deployer. Explicit `null`, an empty string and EIP-1898 objects return `-32602`. |
| Block selector in `eth_getBlockByNumber` | Required number or tag | Preserve the shared decoder's bare hash lookup. Missing selectors, explicit `null`, empty strings and EIP-1898 objects return `-32602`. |
| Block number in all three methods | Minimal lowercase hex quantity | Require the lowercase `0x` prefix and allow uppercase digits and redundant leading zeros. For example, `0x000F` and `0x7fffffffffffffff` pass. Reject values above the signed block-height range, such as `0x8000000000000000`. Exactly 64 digits after `0x` are a bare hash as in the existing decoder. Selectors longer than 66 bytes, including quantities padded beyond that length, return `-32602`. |
| Hydrated transactions in `eth_getBlockByNumber` | Required boolean | Require JSON `true` or `false`. For example, numeric `0` previously converted to false and now returns `-32602`. |
| Extra positional arguments | The method lists its accepted parameters | Reject extras with `-32602`. For example, `[address, "latest", false]` formerly ignored the third argument in `eth_getBalance`. |

The five upstream block tags remain available. The existing resolver still
determines the block or its business error, including lane-specific handling of
`safe` and `finalized`.
