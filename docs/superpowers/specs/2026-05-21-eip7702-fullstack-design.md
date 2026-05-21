# EIP-7702 Full-Stack Design (transaction-executor path)

| Field | Value |
| --- | --- |
| Status | Draft (rewritten to target the new execution path) |
| Spec version | 0.3.1 |
| Author | EVM compat team |
| Date | 2026-05-21 |
| Implementation target | **`transaction-executor` path only** (`executor.version == 1`) |
| EIP reference | [EIP-7702 — Set Code for EOAs](https://eips.ethereum.org/EIPS/eip-7702) |
| Related plans | `docs/superpowers/plans/2026-05-21-eip7702-implementation-plan.md` (will be rewritten after this spec is approved) |
| Related work | EIP-2929 / EIP-2930 / EIP-7623 ([P0 plan](2026-05-20-eip2929-2930-7623-p0-implementation-plan.md)), Phase-2 EVMC/evmone Prague work |

## 0. Reading Guide

This spec replaces the earlier draft that targeted the legacy `bcos-executor` path
(`TransactionExecutor` + `TransactionExecutive`). After review we discovered that the
old path is missing EIP-2930 / EIP-7623 wiring (see
`bcos-executor/src/executor/TransactionExecutor.cpp:2870` `TODO(C2)` and the absence of
`calcEip7623CalldataGas` calls in `TransactionExecutive::execute`). Rather than fix
those gaps just to land EIP-7702 on top, the team decided to land EIP-7702 exclusively
on the new `transaction-executor` path (`TransactionExecutorImpl` + `HostContext`),
which already integrates EIP-2929/2930/7623 cleanly and is selected when
`system.executor_version == 1`.

If you are looking for the legacy-path strategy, refer to git history before
2026-05-21; that approach is intentionally abandoned.

## 1. Goals and Non-Goals

### 1.1 Goals

- Add EIP-7702 Type-4 transaction support to FISCO-BCOS end to end on the
  `transaction-executor` path:
  1. RPC ingress accepts Type-4 raw transactions and produces a `bcostars::Transaction`
     carrying everything needed for execution.
  2. `Web3Transaction` and the Tars protocol surface an `authorization_list` field with
     stable encode/decode, mirroring the `accessList` pattern.
  3. `TransactionExecutorImpl::ExecuteContext` applies authorizations between
     `updateNonce()` and the EVM frame, with persistence semantics that match the EIP
     (delegations survive EVM revert; survive `consumeBalance` rollback).
  4. `HostContext` honours delegation indicators (`0xEF0100 ‖ delegate_addr`) when the
     EVM dispatches `CALL`/`DELEGATECALL`/`STATICCALL`/`EXTCODE*` to authorised EOAs.
  5. Gas accounting matches EIP-7702: per-tuple cost `25000`, refund `12500` per existing
     account, plus correct interaction with EIP-2930 / EIP-2929 / EIP-7623.
- Add static guards (`FC_G_7702_*`) and unit tests on
  `transaction-executor/tests/CompatHostContextTest.cpp` that lock the design.
- Keep SM2 explicitly out of scope; authorization tuples must be `secp256k1` only.
- Gate everything behind `EVMC_PRAGUE` (`feature_evm_prague`).

### 1.2 Non-Goals

- **No work on the legacy `bcos-executor` path.** When `executor.version == 0` the chain
  must reject Type-4 transactions at the protocol layer; we will not retrofit
  delegations into `TransactionExecutor` / `TransactionExecutive`.
- No SM2 / HSM-SM2 authorisation. SM2 chains must reject `authorization_list`
  outright.
- No precompile changes beyond what EIP-7702 itself requires (none today).
- No changes to consensus, transaction pool ordering, or storage layout.
- We do not change the Tars IDL contract for fields other than introducing one new
  optional structure (`AuthorizationListEntry`).

## 2. Decisions Recap

| ID | Decision | Source |
| --- | --- | --- |
| Q1 | Implementation target is the `transaction-executor` path only. `bcos-executor` path is out of scope. | User confirmation (2026-05-21) |
| Q5 | The Tars IDL is extended with a new optional vector `authorizationList` mirroring `accessList`. We do **not** rely solely on `extraTransactionBytes` parsing for type-4. | User: `Q5=A` |
| Q6 | EIP-7702 authorisations are applied **before** `m_startSavepoint` is refreshed inside `ExecuteContext::executeStep<1>`, so they share persistence semantics with `updateNonce()` (survive both EVM revert and `consumeBalance` rollback). | User: `Q6=A` |
| Q7 | Authorisations populate the per-frame `HostContext` and warm the authorised addresses in `Eip2929AccessState` at the start of the top-level frame, alongside the existing EIP-2929 W1 / EIP-2930 W2 warm-ups. | User: `Q7=A` |
| Q8 | A new unified parser `parseEip7702FromWeb3Transaction` mirrors `parseEip2930FromWeb3Transaction`'s Path A/B/C strategy (Tars first; `extraTransactionBytes` fallback; mismatch warning). | User: `Q8=A` |
| Q-Refund | EIP-7702 refunds are accumulated on `evmc_result.gas_refund` after `applyEip7702AuthorizationList()`. **EIP-3529 refund cap is a known gap** on the new path (not implemented today); do not claim cap enforcement in M1. | User: `Q-Refund=B` (2026-05-21) |
| Q-Gas | Use the **EIP-7702 original** gas model: intrinsic `25000 * len(authorization_list)` debited up front; `12500` refund per successful tuple whose authority already existed. | User: `Q-Gas=original` (2026-05-21) |

## 3. Reference Specification (Source of Truth)

These are the EIP-7702 rules we must respect; if the spec text below disagrees with the
EIP, the EIP wins.

### 3.1 Transaction envelope

A Type-4 transaction is the standard EIP-1559 fee-market transaction extended with
`authorization_list`:

```text
tx_payload = [
  chain_id,
  nonce,
  max_priority_fee_per_gas,
  max_fee_per_gas,
  gas_limit,
  destination,        // 20-byte address (no contract creation in type-4)
  value,
  data,
  access_list,        // EIP-2930
  authorization_list, // EIP-7702 -- list of authorization tuples
]

authorization_tuple = [chain_id, address, nonce, y_parity, r, s]
```

Encoding rules:

- Outer RLP: `0x04 ‖ rlp(tx_payload)`; signed envelope is
  `0x04 ‖ rlp(tx_payload ‖ [y_parity, r, s])`. The outer signature **does not**
  include the inner authorisation tuples' `(y_parity, r, s)`.
- Outer signing hash: `keccak256(0x04 ‖ rlp(tx_payload))`. Note: this is the **outer**
  domain; do not confuse with the per-tuple `0x05` domain below.

### 3.2 Authorisation tuple signing

Each `authorization_tuple` is signed independently by the **authority** (the address
whose code is being set, not the transaction sender):

```text
magic        = 0x05                          // distinct from the 0x04 outer envelope
auth_hash    = keccak256(magic ‖ rlp([chain_id, address, nonce]))
(y_parity, r, s) = secp256k1_sign(auth_hash, authority_priv_key)
```

- `chain_id` of `0` means "valid on any chain"; otherwise must match the active chain.
- Only `secp256k1` is permitted. SM2 / HSM-SM2 chains must reject `authorization_list`.
- `y_parity ∈ {0, 1}`; `s ≤ secp256k1n / 2` (low-S, per EIP-2 / EIP-7702).

### 3.3 Validation & application per tuple

For each tuple, in order:

1. Verify `chain_id == 0` or `chain_id == active_chain_id`. Otherwise: skip.
2. Recover `authority` from `auth_hash + (y_parity, r, s)`. If recovery fails: skip.
3. If `authority` has existing code that is **not** a delegation designator
   (`!= 0xEF0100‖addr`): skip. (Already-delegated EOAs may be redelegated.)
4. Verify `tuple.nonce == authority.nonce`. Otherwise: skip.
5. **Refund accounting**: if `authority` already exists in state, add
   `PER_EMPTY_ACCOUNT_COST - PER_AUTH_BASE_COST = 12500` to the gas refund counter.
6. **Apply**:
   - If `address == 0x00…00`: clear the delegation — set code to empty
     and reset code hash to the empty-code hash.
   - Otherwise: set the authority's code to `0xEF0100 ‖ address` (23 bytes total) and
     update the code hash to `keccak256(0xEF0100 ‖ address)`.
7. `authority.nonce += 1`.
8. Add `authority` and the delegated target address to the EIP-2929 warm
   address set.

Steps 4-8 happen even if step 5 produced a refund; "skip" in steps 1-4 means **only that
tuple is skipped**, the rest of the list continues.

### 3.4 Gas

- **Intrinsic gas** includes `25000 * len(authorization_list)`
  (`PER_EMPTY_ACCOUNT_COST` per tuple, regardless of success). This is in addition to
  the existing EIP-1559 / EIP-2930 / EIP-7623 intrinsic gas calculation.
- **Refund**: `12500 * (#successful tuples whose authority already existed)` where
  `12500 = PER_EMPTY_ACCOUNT_COST - PER_AUTH_BASE_COST` (EIP-7702 original accounting).
  Refunds are added to `evmc_result.gas_refund` on the new path. **EIP-3529 cap
  (`refund <= gas_used / 5`) is not implemented on `transaction-executor` today** — see
  §5.8 (known gap, Q-Refund=B).
- If `len(authorization_list) == 0`: type-4 transactions are **invalid** (the EIP
  forbids empty lists). We reject at RPC and protocol layers.

### 3.5 Delegation runtime semantics

When the EVM performs any opcode that observes code at `addr`:

| Opcode | Behaviour for a delegated EOA (`addr` has code `0xEF0100 ‖ target`) |
| --- | --- |
| `EXTCODESIZE addr` | Returns 23 (length of delegation indicator). |
| `EXTCODEHASH addr` | Returns `keccak256(0xEF0100 ‖ target)`. |
| `EXTCODECOPY addr` | Copies the 23-byte indicator verbatim. |
| `CALL addr` / `STATICCALL addr` | Execute the code at `target`, with storage of `addr`. |
| `DELEGATECALL addr` | Execute the code at `target` with the storage and caller of the **current** frame (standard DELEGATECALL semantics, but the target is resolved through the indicator). |

If the indicator points to another delegation indicator, do **not** chain — execute the
indicator bytes as if they were normal code (in practice they would `STOP` since
`0xEF` is an invalid opcode; this is the intended "fail-soft" behaviour).

## 4. Architecture & Module Map

```text
┌─────────────────────────────────────────────────────────────────────┐
│ bcos-rpc                                                            │
│ └─ web3jsonrpc/model/Web3Transaction.{h,cpp}                        │
│    - TransactionType::EIP7702 = 4                                   │
│    - struct AuthorizationListEntry                                  │
│    - RLP encode/decode for type-4                                   │
│    - takeToTarsTransaction() also serialises authorizationList      │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│ bcos-tars-protocol                                                  │
│ └─ tars/Transaction.tars                                            │
│    - new struct Web3AuthorizationListEntry                          │
│    - new optional field TransactionData.authorizationList           │
│ └─ protocol/TransactionImpl.{h,cpp}                                 │
│    - mutable Web3AuthorizationList m_web3AuthorizationListCache     │
│    - mutable bool m_web3AuthorizationListCacheBuilt                 │
│    - web3AuthorizationList() virtual override                       │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│ bcos-framework                                                      │
│ └─ protocol/Transaction.h                                           │
│    - virtual Web3AuthorizationList const& web3AuthorizationList()   │
│ └─ protocol/Web3AuthorizationList.h  (new)                          │
│    - struct Web3AuthorizationEntry { string authorityHex, chainId,  │
│        nonce, addressHex, yParity, r, s }                           │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│ bcos-executor (helpers used by both paths; only new path consumes)  │
│ └─ src/Web3Eip7702Fill.{h,cpp}  (new, mirrors Web3Eip2930Fill)      │
│    - struct Web3Eip7702Parsed                                       │
│    - parseEip7702FromWeb3Transaction(tx) with Path A/B/C            │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│ transaction-executor (the implementation target for this spec)      │
│ └─ bcos-transaction-executor/TransactionExecutorImpl.h              │
│    - ExecuteContext::Data picks up m_eip7702Parsed                  │
│    - executeStep<1>() applies authorizations between                │
│      updateNonce() success and m_startSavepoint refresh             │
│    - Refund counter merged into evmc_result.gas_refund              │
│ └─ bcos-transaction-executor/vm/HostContext.{h,cpp}                 │
│    - execute(): EIP-7702 intrinsic gas on msg.gas (like EIP-7623)   │
│    - constructor accepts authorizationList                          │
│    - prepare(): warm authority + target addresses (EIP-7702 W3)     │
│    - call(): follow EF0100 indicator on CALL/STATICCALL/DELEGATECALL│
│    - extcodesize/extcodehash/extcodecopy honour indicator           │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│ EVMC / evmone                                                       │
│ - feature_evm_prague gate already exists                            │
│ - No new EVMC additions needed beyond what Prague already provides  │
└─────────────────────────────────────────────────────────────────────┘
```

## 5. Detailed Design

### 5.1 RPC and `Web3Transaction` extensions

`bcos-rpc/bcos-rpc/web3jsonrpc/model/Web3Transaction.h`:

```cpp
enum class TransactionType : uint8_t
{
    Legacy = 0,
    EIP2930 = 1,
    EIP1559 = 2,
    EIP4844 = 3,
    EIP7702 = 4,   // new
};

// EIP-7702 authorisation entry. Stored on Web3Transaction; signed by the authority.
struct AuthorizationListEntry
{
    uint64_t chainId = 0;             // 0 = "any chain"
    Address address;                  // delegation target (20 bytes, 0 means clear)
    uint64_t nonce = 0;
    uint8_t yParity = 0;              // 0 or 1
    h256 r;
    h256 s;

    // Recovered lazily (cached); not part of the canonical signed payload.
    mutable std::optional<Address> cachedAuthority;

    // 0x05 ‖ rlp([chainId, address, nonce])
    bcos::bytes encodeForSign() const;
    bcos::crypto::HashType signingHash() const;
    Address recoverAuthority(bcos::crypto::Hash& hashImpl) const;

    friend bool operator==(const AuthorizationListEntry&, const AuthorizationListEntry&) noexcept;
};

class Web3Transaction
{
    // ... existing fields ...
    std::vector<AuthorizationListEntry> authorizationList;  // new
    // ...
};
```

RLP codec (`Web3Transaction.cpp`):

- Add `encode/decode` for `AuthorizationListEntry`.
- Extend `decodeTransaction()` to accept envelope byte `0x04`, decoding the new
  `authorization_list` field at the tail of `tx_payload`.
- Extend `encode()` to emit type-4 envelope when `type == EIP7702`.
- The `decodeTransaction()` switch must reject type-4 with empty
  `authorizationList` (per EIP).

`takeToTarsTransaction()` (mirrors current `accessList` block):

```cpp
if (this->type == TransactionType::EIP7702)
{
    tarsTx.data.authorizationList.reserve(this->authorizationList.size());
    for (auto const& entry : this->authorizationList)
    {
        bcostars::Web3AuthorizationListEntry e;
        e.chainId = std::to_string(entry.chainId);
        e.address = entry.address.hex();
        e.nonce = std::to_string(entry.nonce);
        e.yParity = static_cast<tars::Char>(entry.yParity);
        e.r.assign(entry.r.begin(), entry.r.end());
        e.s.assign(entry.s.begin(), entry.s.end());
        tarsTx.data.authorizationList.emplace_back(std::move(e));
    }
}
```

### 5.2 Tars IDL extension

`bcos-tars-protocol/bcos-tars-protocol/tars/Transaction.tars`:

```idl
struct Web3AuthorizationListEntry
{
    0 optional string         chainId;        // decimal string, 0 = any
    1 optional string         address;        // 40-char hex, no 0x prefix
    2 optional string         nonce;          // decimal string
    3 optional byte           yParity;        // 0 or 1
    4 optional vector<byte>   r;              // 32 bytes
    5 optional vector<byte>   s;              // 32 bytes
};

struct TransactionData {
    // ... existing fields ...
    16 optional vector<Web3AuthorizationListEntry> authorizationList; // new
};
```

`TransactionData.accessList` is field 15; `authorizationList` is field 16. We do not
touch any pre-existing tag.

Storage cache implications:

- `TransactionImpl` (`bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionImpl.h`)
  gets one more `mutable Web3AuthorizationList m_web3AuthorizationListCache` plus a
  built-flag bool.
- The `AnyHolder<TransactionImpl, 184>` size budget (see existing comment in the file
  about the 184-byte cap) needs verification. Two new members (`vector` is 24 bytes
  on libstdc++ x86_64, bool is 1 byte aligned) bring the holder to roughly 184+25 =
  209 bytes. **Action:** confirm at implementation time with a `static_assert` on
  `sizeof(TransactionImpl)`; if it overflows, raise the holder size in one step (the
  number is itself a placeholder enforced by `static_assert`).

### 5.3 New framework header `Web3AuthorizationList.h`

`bcos-framework/bcos-framework/protocol/Web3AuthorizationList.h`:

```cpp
namespace bcos::protocol
{
struct Web3AuthorizationEntry
{
    std::string chainIdDec;     // decimal-string; "0" means any chain
    std::string addressHex;     // 40-char lowercase hex, no 0x prefix
    std::string nonceDec;       // decimal-string
    uint8_t      yParity = 0;   // 0 or 1
    h256         r;
    h256         s;
};

using Web3AuthorizationList = std::vector<Web3AuthorizationEntry>;
}  // namespace bcos::protocol
```

`bcos-framework/bcos-framework/protocol/Transaction.h` gets:

```cpp
// EIP-7702 list when populated at submission (may be empty for non-type-4 web3 txs).
virtual Web3AuthorizationList const& web3AuthorizationList() const;
```

Default implementation returns an empty static list.

### 5.4 `Web3Eip7702Fill` — Path A/B/C parser

`bcos-executor/src/Web3Eip7702Fill.h`:

```cpp
namespace bcos::executor
{
struct Eip7702Authorization
{
    uint64_t chainId;
    bcos::Address address;          // delegation target
    uint64_t nonce;
    uint8_t  yParity;
    bcos::h256 r;
    bcos::h256 s;
};
using Eip7702AuthorizationList = std::vector<Eip7702Authorization>;

struct Web3Eip7702Parsed
{
    uint8_t web3TypedTxKind = 0;
    // Non-null iff the tx is type-4 AND a non-empty list was decoded.
    std::shared_ptr<const Eip7702AuthorizationList> authorizationList;
};

Web3Eip7702Parsed parseEip7702FromWeb3Transaction(protocol::Transaction const& tx);
}
```

Implementation mirrors the existing `parseEip2930FromWeb3Transaction` exactly:

- **Path A** — Tars `web3TypedTxKind == 4` and `tx.web3AuthorizationList()` non-empty:
  fast path, copy from Tars. Optionally re-decode from `extraTransactionBytes` and
  compare; log `WEB3_EIP7702 [WARNING]` on mismatch.
- **Path B** — Tars kind == 4 but `web3AuthorizationList()` empty: re-decode from
  `extraTransactionBytes` to recover the list (e.g. when an older peer stripped the
  Tars field). If still empty, the tx is invalid; the caller will reject.
- **Path C** — Tars kind == 0: fall back entirely to `parseEip7702FromExtraBytes`.

Logging badge: `WEB3_EIP7702` (new), parallel to `WEB3_EIP2930`.

**Path C prerequisite (`extraTransactionBytes` fallback):**

`parseEip7702FromExtraBytes` must decode envelope `0x04`. Today
`bcos-executor/src/Web3Eip2930Fill.cpp` (lines 101–108) treats unknown typed envelopes as
"kind only" and returns early **without** decoding the payload, so type-4 lists are lost
when only `extraTransactionBytes` is present. Implementation MUST either:

- extend the whitelist in `parseEip2930FromExtraBytes` to include
  `TransactionType::EIP7702` (and delegate to the 7702 decoder), **or**
- implement a dedicated `parseEip7702FromExtraBytes` that does not share the early-return
  branch.

This is required for Path B/C (Q8=A, Q5=A).

### 5.5 `ExecuteContext` integration

File: `transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h`.

`ExecuteContext::Data` additions:

```cpp
executor::Web3Eip7702Parsed m_eip7702Parsed;
// initialised in the member-init-list right after m_eip2930Parsed:
m_eip7702Parsed(executor::parseEip7702FromWeb3Transaction(transaction)),
```

`m_hostContext` construction adds two new arguments (after the existing EIP-2930
arguments). See §5.6 for the signature.

`executeStep<1>()` change — the critical placement:

```cpp
else if constexpr (step == 1)
{
    auto updated = co_await updateNonce();
    if (updated)
    {
        // EIP-7702 application happens HERE — before refreshing m_startSavepoint
        // so that delegations persist exactly like sender-nonce updates:
        //   * They are committed before the refreshed savepoint, so the post-execute
        //     rollback in consumeBalance() (which rolls back to m_startSavepoint on
        //     EVMC_INSUFFICIENT_BALANCE) does NOT undo them.
        //   * They are committed before the EVM frame savepoint inside execute(),
        //     so EVM REVERT/exceptional halt does NOT undo them either.
        co_await applyEip7702AuthorizationList();
        m_data->m_hostContext.warmEip7702Addresses();
        m_data->m_startSavepoint = m_data->m_rollbackableStorage.current();
    }
    m_data->m_evmcResult.emplace(co_await m_data->m_hostContext.execute());
    m_data->m_evmcResult->gas_refund += m_data->m_eip7702Refund;
    co_await consumeBalance();
}
```

`applyEip7702AuthorizationList()` body (new method on `ExecuteContext`):

1. Early-out if `m_eip7702Parsed.authorizationList == nullptr` or empty.
2. **Do not debit intrinsic gas here.** Per-tuple `25000 * N` is debited in
   `HostContext::execute()` on `msg.gas` before the EVM frame runs (§5.7, same pattern
   as EIP-7623). This method only mutates state and accumulates refunds.
3. For each tuple in order (§3.3 algorithm):
   - Validate `chainId`; recover authority; validate authority has no non-delegation
     code; validate `nonce`.
   - Skip on validation failure (continue to next tuple).
   - Construct `ledger::account::EVMAccount authority(...)`; if it does not exist,
     `co_await authority.create()` (matches §3.3 step 5 semantics: a "new" authority
     has no refund; an existing authority adds `12500` to the refund counter).
   - Write delegation indicator: if `target == 0x00..00`, clear code; otherwise
     `setCode(0xEF0100 ‖ target_address)`.
   - `authority.setNonce(authority.nonce() + 1)`.
   - Append `authority` and `target` to a "to-be-warmed" set that
     `HostContext::prepare` consumes.
4. After `m_hostContext.execute()` returns, merge `m_data->m_eip7702Refund` into
   `m_evmcResult->gas_refund` (Q-Refund=B). **Do not apply EIP-3529 cap in M1** — the new
   path does not implement refund-cap or `gasUsed -= refund` today (§5.8).

The "to-be-warmed" set is exposed to `HostContext` via the constructor parameter (see
next section), so the warm-up happens once at the top-level frame inside
`HostContext::prepare()`, alongside the existing EIP-2929 W1 and EIP-2930 W2 warm-ups.

### 5.6 `HostContext` threading

File: `transaction-executor/bcos-transaction-executor/vm/HostContext.h`.

Constructor — add two parameters at the end (after the EIP-2930 pair):

```cpp
HostContext(Storage& rollbackable, ... ,
            std::shared_ptr<const Eip2930AccessList> accessList,
            uint8_t                                  web3TypedTxKind,
            std::shared_ptr<const std::vector<Address>> authorizedAddresses,
            std::shared_ptr<const std::vector<Address>> delegationTargets);
```

(The two new shared_ptrs are filled by `applyEip7702AuthorizationList()` with the
deduplicated authority and target addresses respectively. They are `nullptr` for
non-type-4 transactions.)

**EIP-7702 W3 warm-up timing:** `executeStep<0>()` runs `prepare()` **before**
`applyEip7702AuthorizationList()` (step 1). Per §3.3 step 8, only **successfully applied**
tuples should be warmed. Therefore W3 MUST NOT rely solely on `prepare()` unless we
over-warm from the parsed list (acceptable shortcut). **Recommended:** add
`HostContext::warmEip7702Addresses()` and call it from `executeStep<1>()` immediately
after `applyEip7702AuthorizationList()` and before `execute()`:

```cpp
// EIP-7702 W3: warm authority + target for successfully applied tuples only.
void warmEip7702Addresses()
{
    if (m_authorizedAddresses)
        for (auto const& a : *m_authorizedAddresses) { m_eip2929Access->warmAddress(a); }
    if (m_delegationTargets)
        for (auto const& a : *m_delegationTargets) { m_eip2929Access->warmAddress(a); }
}
```

`m_authorizedAddresses` / `m_delegationTargets` are populated by
`applyEip7702AuthorizationList()` (deduplicated). `prepare()` keeps only EIP-2929 W1 /
EIP-2930 W2 warm-ups.

Delegation resolution helper:

```cpp
// Returns the resolved code address for the given account: if it has a
// 23-byte EF0100 delegation indicator, returns the indicator's target;
// otherwise returns `addr` unchanged. Recursion is bounded to one hop.
co_await resolveDelegate(evmc_address const& addr) -> evmc_address;
```

Used inside:

- `call()` for `EVMC_CALL` / `EVMC_STATICCALL` / `EVMC_DELEGATECALL` — when the
  target account has a delegation indicator, execute the indicator's target code
  while keeping the caller-frame semantics correct:
  - `CALL` / `STATICCALL`: storage and address remain the original target; only
    code resolves to the delegate.
  - `DELEGATECALL`: storage and caller remain the current frame; code resolves to the
    delegate of `addr`.
- `extcodesize()`, `extcodehash()`, `extcodecopy()` return the 23-byte indicator
  itself, **not** the delegate's code. (EIP-7702 §"behaviour for code-reading
  opcodes".)

### 5.7 Intrinsic gas

EIP-7702 intrinsic gas is **not** debited via `ExecuteContext::Data::m_gasLimit`. That
field is initialised from `ledgerConfig.gasLimit()` (block gas ceiling) and passed into
`newEVMCMessage()` as `evmc_message::gas` (`TransactionExecutorImpl.h:85–93,
`TransactionExecutorImpl.cpp:20`). Mutating `m_gasLimit` would corrupt
`consumeBalance()`'s `gasUsed = m_gasLimit - gas_left` accounting.

Follow the **EIP-7623 pattern** in `HostContext::execute()` (`HostContext.h:468–486):
for the top-level frame (`m_level == 0`), Prague revision, and a non-empty type-4
`authorization_list`, debit `msg.gas` **before** transfer / EVM execution:

```cpp
// Insert immediately after the EIP-7623 block (HostContext.h ~486), same guards:
if (m_level == 0 && m_revision >= EVMC_PRAGUE && m_web3TypedTxKind == 4 &&
    m_authorizationList && !m_authorizationList->empty())
{
    auto& msg = mutableMessage();
    const int64_t authIntrinsic =
        static_cast<int64_t>(m_authorizationList->size()) * EIP_7702_PER_EMPTY_ACCOUNT_COST;
    if (msg.gas < authIntrinsic)
    {
        evmResult.emplace(makeErrorEVMCResult(..., EVMC_OUT_OF_GAS, ..., "EIP-7702 auth intrinsic OOG"));
    }
    else
    {
        msg.gas -= authIntrinsic;
    }
}
```

`HostContext` receives `m_authorizationList` (or tuple count) via constructor parameters
filled from `m_eip7702Parsed` at `ExecuteContext::Data` construction time — intrinsic
gas must be knowable **before** `applyEip7702AuthorizationList()` runs in `executeStep<1>()`.

Constants (in `transaction-executor/bcos-transaction-executor/Common.h` or
`Eip7702Common.h`):

```cpp
constexpr int64_t EIP_7702_PER_EMPTY_ACCOUNT_COST = 25000;  // intrinsic per tuple (Q-Gas)
constexpr int64_t EIP_7702_PER_AUTH_BASE_COST     = 12500;  // reference only (EIP Prague)
// Refund per existing-authority tuple (original EIP-7702): 25000 - 12500 = 12500.
constexpr int64_t EIP_7702_REFUND_PER_EXISTING_AUTHORITY = 12500;
```

### 5.8 Refund accounting (Q-Refund=B)

`ExecuteContext::Data` adds:

```cpp
int64_t m_eip7702Refund = 0;
```

Incremented inside `applyEip7702AuthorizationList()` for each **successful** tuple whose
authority pre-existed at application time (§3.3 step 5, `+12500` each).

**M1 wiring (accumulate only):**

```cpp
// In executeStep<1>(), after m_hostContext.execute():
m_data->m_evmcResult->gas_refund += m_data->m_eip7702Refund;
```

**Known gap — EIP-3529 not on the new path:**

Code review (2026-05-21) shows `transaction-executor` sets `gas_refund = 0` on most
paths (`EVMCResult.cpp`, precompiles, `HostContext.h:815`) and `finish()` builds the
receipt from `m_gasUsed` without subtracting refunds or applying `min(refund, gasUsed/5)`.
Therefore:

| Behaviour | M1 (this spec) | Full Ethereum compliance (follow-up) |
| --- | --- | --- |
| Accumulate 7702 refund on `evmc_result.gas_refund` | ✅ Required | ✅ |
| Reduce `gasUsed` by effective refund | ❌ Not implemented | ✅ |
| EIP-3529 cap `refund <= gasUsed / 5` | ❌ Known gap (Q-Refund=B) | ✅ |

Do not add tests that assert EIP-3529 cap enforcement until a separate EIP-3529 task
lands on `transaction-executor`.

### 5.9 RLP encode / decode order

Within `tx_payload`, the field order required by EIP-7702 is `chain_id, nonce,
max_priority_fee_per_gas, max_fee_per_gas, gas_limit, destination, value, data,
access_list, authorization_list`. The signed envelope tail is `[y_parity, r, s]`. The
implementation in `Web3Transaction.cpp` must match this exactly; we add a unit test
(§9.2 R-codec tests) with the official EIP test vectors.

## 6. Persistence & Rollback Semantics

Three rollback layers exist on the new path; each must not undo delegations.

| Layer | Where | Effect on delegations (target behaviour) |
| --- | --- | --- |
| EVM frame revert | inside `HostContext::execute` (frame-level `Rollbackable::Savepoint`) | **No effect** — delegations were written before this savepoint. ✅ matches EIP. |
| `consumeBalance` insufficient-balance | `executeStep<1>()` line 189 rollback to `m_startSavepoint` | **No effect** — delegations were written before `m_startSavepoint` refresh in §5.5. ✅ matches FISCO design choice (Q6=A). |
| Block-level reorg | outside transaction-executor | **Effect: rolled back** — same as any other state write. ✅ matches Ethereum. |

This is exactly the same persistence class as sender-nonce updates in
`updateNonce()`; the comment block in §5.5 should be preserved verbatim in code so
future readers don't accidentally move the application point.

## 7. Compatibility & Path Gating

### 7.1 `executor.version` gating

- `executor.version == 1` (new path): full EIP-7702 support per this spec.
- `executor.version == 0` (legacy path): reject type-4 transactions at the protocol /
  RPC layer. The implementation MUST guarantee a type-4 transaction never reaches
  `TransactionExecutor` / `TransactionExecutive`.
  - RPC: in `EthEndpoint::sendRawTransaction` after `decodeTransaction`, if the
    decoded `TransactionType == EIP7702` and the active executor version is 0,
    reject with JSON-RPC error `-32000 type-4 unsupported on legacy executor`.
  - Pool: a defence-in-depth check inside the TxPool admission path symmetric to the
    chain-id check.

### 7.2 Prague gate

All EIP-7702 logic is gated on `m_revision >= EVMC_PRAGUE` (HostContext.h:224).
`feature_evm_prague` must be enabled at the ledger level; otherwise type-4
transactions are rejected with `EVMC_UNDEFINED_INSTRUCTION`-equivalent status (we
reuse the existing Prague gate mechanism — no new feature flag).

### 7.3 Crypto suite

- Hard requirement: `secp256k1`. The authorisation-tuple parser uses
  `bcos::crypto::Secp256k1Crypto::recoverAddress` explicitly; it does not consult the
  chain's default `SignatureCrypto`.
- SM2 chains must reject type-4 transactions during RPC decode, **even when
  `executor.version == 1`**, since EIP-7702 authority recovery requires `secp256k1`.

### 7.4 Forward compatibility

- The Tars IDL only adds new optional fields, so older peers can still parse the
  envelope.
- Old peers that strip the new fields are tolerated via Path B / C in the parser
  (re-decode from `extraTransactionBytes`).

## 8. Static Guards

Add to `bcos-executor/test/unittest/evmone/compat/CompatStaticGuardsTest.cpp` (these
tests are already the home of the `FC_G_*` family; they run on both paths even
though only the new path implements 7702):

| Guard | What it asserts |
| --- | --- |
| `FC_G_7702_target_path_only` | `executor::isLegacyExecutorPath() && hasEip7702Tx` is unreachable; build fails if anything in `bcos-executor/src/executor/TransactionExecutor.cpp` references `authorization_list`. |
| `FC_G_7702_constants` | `EIP_7702_PER_EMPTY_ACCOUNT_COST == 25000` and `EIP_7702_REFUND_PER_EXISTING_AUTHORITY == 12500`. |
| `FC_G_7702_signing_domains` | Outer envelope signing-hash prefix byte is `0x04`; per-tuple signing-hash prefix byte is `0x05`. |
| `FC_G_7702_secp256k1_only` | The authorisation recovery path uses `Secp256k1Crypto::recoverAddress`; `SM2Crypto` / `HsmSM2Crypto` are not referenced. |
| `FC_G_7702_idl_has_field` | `bcostars::TransactionData` has the `authorizationList` field tagged 16. |
| `FC_G_7702_application_before_savepoint_refresh` | Source-level guard: the comment block in `executeStep<1>()` (§5.5) is present; if a developer deletes it, the guard fails. (Implemented with a `static_assert` keyed off a `constexpr std::string_view` marker placed adjacent to the comment.) |

## 9. Test Plan

All new tests live under `transaction-executor/tests/`, primarily extending
`CompatHostContextTest.cpp`. The legacy `bcos-executor/test/unittest/evmone/compat/`
tests gain only the static guards from §8 and tests that confirm legacy-path
rejection of type-4 transactions.

### 9.1 Web3 model / RLP

In `bcos-rpc/test/unittests/rpc/Web3TransactionTest.cpp`:

- `decode/encode round-trip` for the EIP-7702 official test vectors (linked from the
  EIP; we keep a copy under `bcos-rpc/test/data/eip7702/`).
- `decodeTransaction` rejects type-4 with empty `authorizationList`.
- `takeToTarsTransaction` faithfully serialises authorities and verifies bit-exact
  round-trip back to `Web3Transaction`.

### 9.2 Tars / `TransactionImpl`

In `bcos-tars-protocol/test/unittest/TestTransactionImpl.cpp`:

- `web3AuthorizationList()` caches correctly and returns identical references on
  repeated calls.
- Static-assert on `sizeof(TransactionImpl)` stays under the holder's static limit.

### 9.3 Parser

In `bcos-executor/test/unittest/Web3Eip7702FillTest.cpp` (new):

- Path A, B, C all produce identical `Eip7702AuthorizationList`.
- Mismatch between Tars and extra bytes produces a `WEB3_EIP7702 [WARNING]` log but
  uses the Tars version (matches the existing `WEB3_EIP2930` semantics).

### 9.4 `ExecuteContext` & `HostContext`

In `transaction-executor/tests/CompatHostContextTest.cpp` (new EIP-7702 section), the
`CompatTEHostFixture::makeHost()` harness is extended with an optional
`authorizationList` parameter:

| Test | What it checks |
| --- | --- |
| `Eip7702/IntrinsicGasAddedPerTuple` | Top-level `msg.gas` reduced by `25000 * #tuples` in `HostContext::execute()` (OOG if insufficient). |
| `Eip7702/SkipsInvalidChainId` | `chainId != 0 && chainId != active` tuples are skipped, no state change, no refund, no extra gas refund. |
| `Eip7702/SkipsInvalidNonce` | Mismatching `nonce` skips the tuple. |
| `Eip7702/SkipsNonDelegationCode` | Authority with non-`EF0100` existing code is skipped. |
| `Eip7702/AppliesIndicator` | After a successful tuple, `getCode(authority)` returns `0xEF0100 ‖ target` and `codeHash == keccak256(0xEF0100 ‖ target)`. |
| `Eip7702/ClearsOnZeroTarget` | `target == 0x00..00` resets code to empty and codeHash to empty-hash. |
| `Eip7702/RefundForExistingAuthority` | Existing authority adds 12500 to refund counter; new authority adds 0. |
| `Eip7702/IncrementsAuthorityNonce` | Authority's nonce is exactly `tuple.nonce + 1` after application. |
| `Eip7702/WarmsAddresses` | `authority` and `target` end up warm in `Eip2929AccessState` at the start of the top-level frame. |
| `Eip7702/CallFollowsIndicator` | A `CALL` to a delegated EOA executes the delegate's code with the EOA's storage. |
| `Eip7702/DelegatecallFollowsIndicator` | A `DELEGATECALL` to a delegated EOA executes the delegate's code with the current frame's storage. |
| `Eip7702/ExtcodeOpsReturnIndicator` | `EXTCODESIZE`, `EXTCODEHASH`, `EXTCODECOPY` return the 23-byte indicator verbatim. |
| `Eip7702/RevertDoesNotUndoDelegations` | EVM `REVERT` inside the top-level frame does not roll back delegations. |
| `Eip7702/InsufficientBalanceDoesNotUndoDelegations` | A simulated `EVMC_INSUFFICIENT_BALANCE` rollback to `m_startSavepoint` does not undo delegations. |
| `Eip7702/RefundAccumulatedOnGasRefund` | After execute, `evmc_result.gas_refund` includes `12500` per successful pre-existing authority. |
| `Eip7702/RefundCapDeferred` | Document-only / `GTEST_SKIP`: EIP-3529 cap not asserted in M1 (Q-Refund=B known gap). |
| `Eip7702/PreservesOrder` | Application order matches list order even when multiple tuples target the same authority. |

### 9.5 Path gating tests

In `bcos-rpc/test/unittests/rpc/EthEndpointTest.cpp`:

- `sendRawTransaction` with type-4 + `executor.version == 0` → JSON-RPC error.
- `sendRawTransaction` with type-4 + SM2 chain → JSON-RPC error.

### 9.6 End-to-end (smoke)

In `transaction-executor/tests/CompatSmokeTest.cpp` (or a new
`CompatEip7702SmokeTest.cpp`):

- Submit a type-4 transaction via the full
  `TransactionExecutorImpl::executeTransaction` path; verify the receipt status,
  gas-used, and final state of the authorities and target.

## 10. Implementation Outline

The plan file (separately rewritten) will break these into T-coded tasks. The summary
sequencing here is just to make scope reviewable:

1. **R-codec & Web3 model** — extend `Web3Transaction`, add struct, RLP, sender-side
   handling, `takeToTarsTransaction`.
2. **Tars IDL & TransactionImpl** — add `Web3AuthorizationListEntry` and the cache
   pattern.
3. **Framework header** — `Web3AuthorizationList.h` and the virtual accessor on
   `Transaction.h`.
4. **Parser** — `Web3Eip7702Fill.{h,cpp}` mirroring `Web3Eip2930Fill`.
5. **Constants** — `EIP_7702_PER_*` in `transaction-executor/bcos-transaction-executor/`.
6. **HostContext threading** — constructor params, `warmEip7702Addresses()` (W3),
   delegation resolution helpers, `extcode*` and `call/delegatecall` integration.
7. **ExecuteContext integration** — `applyEip7702AuthorizationList()` between
   `updateNonce()` and savepoint refresh; `warmEip7702Addresses()` before `execute()`;
   merge refund into `evmc_result.gas_refund`; type-4 reject when `executor.version != 1`.
8. **HostContext intrinsic gas** — debit `25000 * N` on `msg.gas` in `execute()` (§5.7).
9. **Path C fix** — extend `Web3Eip2930Fill.cpp` envelope whitelist or dedicated
   `parseEip7702FromExtraBytes` (§5.4).
10. **Path gating** — EthEndpoint + TxPool admission checks; SM2 reject.
11. **Static guards** — `FC_G_7702_*`.
12. **Tests** — sections 9.1-9.6.

## 11. Risks & Open Items

| Risk | Mitigation |
| --- | --- |
| `AnyHolder<TransactionImpl, 184>` size overflow when adding a second cache member. | `static_assert` at implementation time; bump the holder bound in one well-documented step. |
| Authority recovery uses `Secp256k1Crypto` directly, bypassing chain-default crypto. | Document loudly in `Web3Eip7702Fill.cpp`; static guard `FC_G_7702_secp256k1_only`. |
| Path-A/B/C divergence between RPC submission and stored Tars (e.g. a peer drops the new IDL field). | Mismatch warning + extra-bytes fallback identical to EIP-2930's handling. |
| Refund cap interaction with EIP-3529. | Q-Refund=B: accumulate on `gas_refund` in M1; EIP-3529 cap and `gasUsed` reduction are **known gaps** on `transaction-executor` (follow-up task). Test `Eip7702/RefundCapDeferred` documents this. |
| Delegated EOA loops (A → B → A) at runtime. | EIP rule: do not chain; execute the indicator bytes as code, which fails (`0xEF` invalid opcode). Test `Eip7702/CallFollowsIndicator` includes the loop case. |
| Old-path activation in production. | Defence in depth: RPC layer rejects type-4 when `executor.version == 0`; TxPool repeats the check; static guard fails the build if `bcos-executor/src/executor/TransactionExecutor.cpp` references type-4. |
| `bcos-rpc/test/data/eip7702/` vectors drift from upstream. | Pin the source commit of the EIP test vectors in the data folder's README. |

## 12. Glossary

- **Authority**: the EOA whose code is being set by an authorisation tuple.
- **Target / Delegate**: the contract whose code the authority will delegate to.
- **Delegation indicator**: the 23-byte sequence `0xEF0100 ‖ target_address` written
  as the authority's code.
- **`m_startSavepoint`**: a `Rollbackable<Storage>::Savepoint` on the new path,
  refreshed inside `executeStep<1>()` so that `consumeBalance()`'s insufficient-balance
  rollback rolls back to it, leaving the pre-execute writes (nonce + 7702) intact.

## Appendix A. Algorithm — `applyEip7702AuthorizationList()`

```text
function applyEip7702AuthorizationList(ctx):
    list = ctx.m_eip7702Parsed.authorizationList
    if list == nullptr or list.empty(): return

    for tuple in list:
        if tuple.chainId != 0 and tuple.chainId != ctx.activeChainId: continue
        authority = secp256k1.recover(tupleSigningHash(tuple), tuple.y_parity, tuple.r, tuple.s)
        if authority is invalid: continue

        acct = EVMAccount(ctx.storage, authority)
        code = acct.code()
        if code is not empty and not isDelegationIndicator(code): continue
        if tuple.nonce != acct.nonce(): continue

        existed = acct.exists()
        if !existed: acct.create()

        if tuple.address == 0x00..00:
            acct.setCode(empty, empty_abi, emptyCodeHash)
        else:
            indicator = 0xEF0100 ‖ tuple.address
            acct.setCode(indicator, empty_abi, keccak256(indicator))

        acct.setNonce(acct.nonce() + 1)

        if existed:
            ctx.m_eip7702Refund += 12500

        ctx.m_authorizedAddresses += authority
        ctx.m_delegationTargets   += tuple.address
```

## Appendix B. Persistence semantics summary

```text
Time →

[ Data() ctor: original startSavepoint #0 set ]
              │
              ▼
[ executeStep<0>: prepare() — W1/W2 warm-up only                   ]
              │
              ▼
[ executeStep<1>:                                                  ]
[   updateNonce()  ──► writes sender nonce         ◄── kept on rollback ]
[   if updated:                                                    ]
[     applyEip7702() ──► writes delegations        ◄── kept on rollback ]
[     warmEip7702()  ──► EIP-7702 W3 warm-up                       ]
[     m_startSavepoint = current()  (refresh: savepoint #1)        ]
[   execute()  ──► EIP-7702 intrinsic on msg.gas; EVM frame        ]
[                  gas_refund += m_eip7702Refund (no 3529 cap M1)  ]
[                  EVM REVERT rolls back to its own frame savepoint;]
[                  never touches #1 or anything before.            ]
[   consumeBalance() ──► on EVMC_INSUFFICIENT_BALANCE, rollback(#1) ]
[                        which undoes EVM writes but NOT sender-nonce]
[                        or 7702 delegations.                      ]
              │
              ▼
[ executeStep<2>: finish() — receipt from m_gasUsed (3529 cap deferred) ]
```

— End of spec —
