# W1: EIP-2929 Transaction Initial Prewarm Design

## Problem

When `feature_evm_eip2929` is enabled, the first access to `origin`, `to`, and precompile addresses `0x01-0x09` returns `EVMC_ACCESS_COLD` and charges the cold gas cost (2600 extra for accounts). Per EIP-2929 spec, these addresses must be prewarmed at transaction start so the first access returns `EVMC_ACCESS_WARM` (100 gas). This is a protocol correctness violation.

## Scope

Prewarm exactly the addresses specified by EIP-2929:
- `tx.sender` (origin)
- `tx.to` (receiveAddress)
- Precompile addresses `0x01` through `0x09`

BLS precompiles (0x0b-0x11) and p256verify (0x0100) are NOT included — they are not part of the EIP-2929 spec.

## Approach

Each executor path prewarms independently at its transaction entry point (方案 A).

### bcos-executor

**File:** `bcos-executor/src/executive/TransactionExecutive.cpp`  
**Location:** `TransactionExecutive::execute()`, after EIP-7623 floor check (~line 313), before actual execution.

**Logic:**
- Guard: `seq == 0` AND `feature_evm_eip2929` is enabled
- Insert `toEvmC(origin)`, `toEvmC(receiveAddress)`, and addresses 0x01-0x09 into `getEip2929AccessState(contextID())->warmAccounts`
- Uses existing `toEvmC()` from `Common.h` for string→evmc_address conversion

**Behavior change:** When flag is on, origin/to/precompile first access returns WARM (100 gas) instead of COLD (2600 gas). Contracts only consume less gas — no risk of failure.

### transaction-executor

**File:** `transaction-executor/bcos-transaction-executor/vm/HostContext.h`  
**Location:** Constructor, after `m_eip2929Access` initialization.

**Logic:**
- Guard: `m_level == 0` AND `m_revision >= EVMC_BERLIN` AND `feature_evm_eip2929` is enabled
- Insert `m_origin`, `m_message.recipient`, and addresses 0x01-0x09 into `m_eip2929Access->warmAccounts`

### Guard conditions (both paths)

| Condition | bcos-executor | transaction-executor |
|-----------|---------------|---------------------|
| Top-level only | `seq == 0` | `m_level == 0` |
| Berlin+ revision | implicit (flag implies Berlin+) | `m_revision >= EVMC_BERLIN` |
| Feature flag | `features().get(feature_evm_eip2929)` | `ledgerConfig.features().get(feature_evm_eip2929)` |

### Flag-off behavior

When `feature_evm_eip2929=false`:
- No prewarming occurs
- `accessAccount()` returns `EVMC_ACCESS_COLD` for all addresses (existing behavior)
- Zero regression

## Tests

**File:** `bcos-executor/test/unittest/evmone/compat/CompatEip2929Test.cpp`

| Test ID | Description | Assertion |
|---------|-------------|-----------|
| `FC_A_initial_prewarm_origin` | First accessAccount(origin) returns WARM | `== EVMC_ACCESS_WARM` |
| `FC_A_initial_prewarm_to` | First accessAccount(receiveAddress) returns WARM | `== EVMC_ACCESS_WARM` |
| `FC_A_initial_prewarm_precompiles` | 0x01-0x09 first access returns WARM | `== EVMC_ACCESS_WARM` for each |
| `FC_A_initial_prewarm_flag_off` | feature_evm_eip2929=false → all COLD | `== EVMC_ACCESS_COLD` |

## Known limitations

- This design does NOT address W3 (REVERT rollback) — that requires a separate snapshot/journal mechanism.
- transaction-executor currently creates a new `Eip2929AccessState` per HostContext instance. This means prewarming in the top-level HostContext does NOT propagate to child calls. This is a pre-existing bug (review item #7) that must be fixed separately by sharing the access state across depths.
