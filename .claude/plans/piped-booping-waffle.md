# Fix PR #5460 Review Findings

## Context

PR #5460 review found 1 blocker (buffer overread) and 5 test coverage gaps. The worktree (`feat-opstack-e2e`) already has some of the missing tests (e.g., `OpMismatchedFieldTest.cpp`). The fixes target the worktree branch.

## Fix 1: Buffer overread in toEvmoneTransaction (BLOCKER)

**File**: `opstack-executor/OpstackExecutor.h`
**Lines**: 153, 158, 166, 209

Add size checks before each `std::copy_n`:

```cpp
// line 153: access-list account
if (entry.account.size() < sizeof(evmc_address))
    throw OpConsensusError("access-list account address too short");
std::copy_n(entry.account.begin(), sizeof(evmc_address), addr.bytes);

// line 158: storageKeys
if (sk.size() < sizeof(evmc_bytes32))
    throw OpConsensusError("access-list storage key too short");
std::copy_n(sk.begin(), sizeof(evmc_bytes32), key.bytes);

// line 166: blobVersionedHashes
if (h.size() < sizeof(evmc_bytes32))
    throw OpConsensusError("blob versioned hash too short");
std::copy_n(h.begin(), sizeof(evmc_bytes32), hash.bytes);

// line 209: authorizationList[].address
if (auth.address.size() < sizeof(evmc_address))
    throw OpConsensusError("authorization entry address too short");
std::copy_n(auth.address.begin(), sizeof(evmc_address), ea.addr.bytes);
```

Pattern matches existing `auth.signer` check at line 211.

## Fix 2: Test coverage verification

The worktree already has:
- `OpMismatchedFieldTest.cpp` — tests `mismatchedFieldOf` (8 fields, first-mismatch-wins, optional comparison)
- `OpRlpDecodeTest.cpp` — tests `fixedSizeConversions` and `narrowU256ToU64Bounds`
- `OpstackExecutorTest.cpp` — likely tests deposit envelope and tx convert

Verify these exist and are comprehensive. If gaps remain, add tests.

## Verification

1. Build: `cmake --build build --target opstack-executor-tests`
2. Test: `cd build && ctest -R OpstackExecutorTests`
