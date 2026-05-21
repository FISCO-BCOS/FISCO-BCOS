# EIP-7702 test vectors (T31)

Pinned RLP fixtures for `Web3Eip7702TransactionTest` (suite `testWeb3Eip7702`). These are **FISCO round-trip fixtures** aligned with
`makeRoundTripEip7702Tx`, not a full import of
[ethereum/execution-spec-tests](https://github.com/ethereum/execution-spec-tests).

## Source

| Field | Value |
|-------|--------|
| FISCO spec | `docs/superpowers/specs/2026-05-21-eip7702-fullstack-design.md` §9.1 |
| Generator | `Web3Eip7702TransactionTest.cpp` (`makeRoundTripEip7702Tx`) |
| Upstream reference | EIP-7702 type-4 envelope (Prague); execution-spec vectors optional follow-up |

## Files

| File | `expect` | Meaning |
|------|----------|---------|
| `tx_valid_roundtrip.json` | `ok` | Single auth entry; decode + type EIP7702 |
| `tx_reject_empty_auth_list.json` | `reject` | Empty `authorization_list` must fail decode |

Regenerate `rlp` after changing `makePinnedValidTx` by running:

```bash
./build/bcos-rpc/test/test-bcos-rpc --run_test=testWeb3Eip7702/pinned_rlp_export
```

Then copy printed hex into the JSON `rlp` fields.
