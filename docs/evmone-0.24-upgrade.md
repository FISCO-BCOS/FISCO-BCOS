# evmone 0.24 / EVMC ABI 19 upgrade notes

This upgrade includes evmone/EVMC 0.24.0 and blst 0.3.17 (previous port version
20240520). The evmone SM3 and CREATE/CREATE2 hooks preserve the existing chain
hashing and address rules. Created-address warmth changes remain controlled by
`bugfix_evm2929_created_address_warm`.

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
result handling. A legacy child result marked `EVMC_INTERNAL_ERROR` together with
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
