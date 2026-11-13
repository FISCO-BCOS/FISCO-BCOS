# evmone 0.24 / EVMC ABI 19 upgrade notes

This upgrade includes evmone/EVMC 0.24.0 and blst 0.3.17 (previous port version
20240520). The evmone SM3 and CREATE/CREATE2 hooks preserve the existing chain
hashing and address rules. Created-address warmth changes remain controlled by
`bugfix_evm2929_created_address_warm`.

## Persisted revision preflight

Before replacing binaries, inspect `evmc_revision` in the `SYS_CONFIG` table (`s_config`) for every
chain that uses an Ethereum/OP executor version (`executor_version >= 2`).
`getLedgerConfig` parses this row only for those lane versions and only when the
row exists; the check does not apply to every FISCO v1/v2 chain. New Ethereum/OP
genesis configurations derive schedules from fork timestamps, but an existing
datadir can still contain this row.

Check both a bare name and every name in a `blockNumber:revision,...` schedule:

| Name | Upgrade behavior |
| --- | --- |
| `constantinople` | Previously accepted; now refused because ABI 19 removed its EIP-1283 schedule. |
| `experimental` | Previously accepted; now refused because it is not a released, supported schedule. |
| `amsterdam` | Refused; not a previously accepted BASE configuration name. State-gas support is not enabled. |
| `petersburg`, `constantinoplefix` | Accepted aliases for Petersburg. |

An unsupported entry throws `InvalidEVMCRevisionConfig` while loading the ledger
configuration, before executing blocks. For a bare `constantinople` value, the
operator diagnostic contains:

```text
unknown EVM revision name 'constantinople' in evmc_revision config value: constantinople
```

Do not rename `constantinople` to `petersburg`/`constantinoplefix`, remove a
historical schedule entry, or rewrite the row as a migration shortcut: Petersburg
does not reproduce Constantinople's EIP-1283 metering. Keep the previous binary
and a backup until the chain's revision history has a verified replay/migration
plan. This upgrade does not provide an in-place migration for those schedules.

## Created-address warmth compatibility

For chains with `feature_evm_eip2929`,
`bugfix_evm2929_created_address_warm` also controls the v2 creation hook:
when OFF, pin a previously cold derived address before evmone's creation-time
access so a subsequent creator rollback preserves the historical v2 warmth. An
address already warmed by an ordinary access in the creator keeps that access
revertible when initcode fails; successful initcode retains the historical child
pin propagation. When ON, journal
that access in the creator's scope, so reverting the creator restores its prior
access set. Failed initcode alone still keeps the created address warm.

The v1 flag retains its existing behavior: OFF suppresses the new creation-time
query's insertion; ON uses the upgraded VM behavior. Enable the flag only at the
chain's agreed activation point. EIP-2929 does not make all accesses irreversible;
its CREATE exception for failed initcode does not exempt the creator's own revert.

## Host failure boundary

A host callback cannot unwind through EVMC's `noexcept` host adapters. It records
the first fault and supplies a temporary fail-closed answer. That answer is not
a successful recovery from a storage failure.

After VM execution, v1 and v2 throw `HostContextFault` for node-local callback
failures, including storage/parse errors and unknown exceptions. The frame rolls
back and the exception escapes transaction receipt mapping. The legacy executor
returns an execution error with no `ExecutionMessage`; the v2 transaction task
fails before `finish()` can construct a receipt. The scheduler must abandon the
failed execution; it must not commit a partial output or synthesize an ordinary
transaction revert. Retry or node recovery requires a healthy storage backend.
The error remains an executor error even when `bugfix_v1_error_handling` is off.

An explicit v1 scheduler `DEAD_LOCK` cancellation remains a deterministic revert.
Ordinary VM reverts, out-of-gas and mapped precompiled failures keep their existing
result handling. The legacy marker was emitted by `TransactionExecutive::go` in this PR’s
intermediate commit `321c53273e6da4bcb991869bc56df08c3f467910` and replaced by
the error channel in `117c2911d399a4a17f7e73deb4c03b5d07c5c62a`. This identifies
source history, not evidence of a released or deployed producer. A child result marked `EVMC_INTERNAL_ERROR` together with
`RevertInstruction` is treated as a host fault by an upgraded parent, rather than
allowing the parent to catch it as a contract failure.

These notes describe the FISCO v1/v2 host paths. EthereumHost and the OP Ethereum
host do not use the new recorded-fault mechanism.

## Scheduler/executor rollout

Upgrade the scheduler and all its executor services together before resuming
block execution. `ExecutionMessage` Tars field 35 carries `schedulerSeq`; absence
is represented by -1. A resumed frame which subsequently needs the historical
FISCO CREATE address derivation cannot reconstruct that derivation without the
scheduler handback. It fails execution instead of guessing an address. Nonce-based
creation and CREATE2 have different derivation rules; this does not establish that
mixing old and new services is supported for them.

The upgraded scheduler rejects an outgoing resume with a missing stamp before
dispatching the batch. This detects its own protocol violations, not an old
scheduler that does not contain the check.

There is no startup capability negotiation for field 35. Do not use rolling
scheduler/executor service replacement while a transaction is suspended. Drain or
stop block execution, upgrade both sides, then restart execution with consistent
versions. ABI19 executors are not a supported mixed-service rollout with an old
scheduler.

## Regression checks

The host fault regression requires an error callback and no transaction output.
Child-result tests traverse the production `toExecutionResult` and
`createCallParameters` adapters before resuming coroutine/Promise frames. Tars
tests exercise `writeTo`/`readFrom` for -1, 0 and a positive scheduler stamp,
together with the child status/evmStatus pair. These are separate from the existing
setter/getter tests.

The crypto precompiled fixture clears the existing global selector cache at its
boundaries so an SM3 fixture cannot contaminate a Keccak fixture in the same test
process. This is test isolation; the separate production selector-cache change is
not included here.
