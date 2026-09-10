; ============================================================================
; FISCO-BCOS Ethereum L1 EL-mode genesis configuration (config.genesis)
; Used with [ethereum] mode=el in config.ini. Values below are the REAL Sepolia
; genesis (chain id 11155111) — the node verifies blocks from this anchor.
;
; TRUST MODEL: the node commits the chain its bootnodes serve as long as it is
; internally consistent (parent-hash linkage + re-executed state roots); PoW
; difficulty / total difficulty are NOT verified and there is no consensus-layer
; finality feed. The [ethereum] bootnodes_file in config.ini is therefore the
; trust root — configure only trusted bootnodes, and consider the
; finalized_checkpoint setting there. A full checkpoint/finality mechanism is a
; known limitation (follow-up work).
; ============================================================================

[chain]
    sm_crypto=false
    group_id=group0
    chain_id=11155111

[web3]
    ; Ethereum chain id (decimal)
    chain_id=11155111

[version]
    compatibility_version=3.18.0

[consensus]
    consensus_type=pbft
    block_tx_count_limit=1000
    leader_period=100
    node.0=

[tx]
    gas_limit=3000000000

[executor]
    ; Ethereum L1 EL mode REQUIRES executor version 2 (the pure-Ethereum executor).
    version=2
    is_auth_check=false
    auth_admin_account=0x0000000000000000000000000000000000000000
    is_serial_execute=true

[fork_timestamps]
    ; Ethereum L1 PoS fork schedule — timestamp-based (0 = active from genesis).
    ; Sepolia values (verified against the live chain):
    london_time=0
    ; Paris (The Merge). Set 1661128380 on Sepolia (PoW phase until 2022-08-22);
    ; omit on pure-PoS chains (Holesky) for 0 = active from genesis.
    paris_time=1661128380
    ; The merge (terminal total difficulty) BLOCK NUMBER — the chain's only
    ; block-based fork. Blocks below it follow PoW header rules (non-zero
    ; difficulty, ommers allowed); from it onward PoS rules apply. It also feeds
    ; the EIP-2124 fork-id handshake. REQUIRED (no chain-agnostic default):
    ; Sepolia: 1735371. merge_block=0 means the chain is PoS from genesis
    ; (pure-PoS chains like Holesky). Non-Sepolia chains MUST set their own
    ; value here.
    merge_block=1735371
    shanghai_time=1677557088
    cancun_time=1706655072
    prague_time=1741159776
    ; Post-Prague forks (optional while inactive; MUST be set once activated — geth's
    ; EIP-2124 fork-id checksum chains every activated fork). Sepolia values (2025-10):
    osaka_time=1760427360
    bpo1_time=1761017184
    bpo2_time=1761607008

[features]
    ; Ethereum-compatible world-state MPT (needed for the v2 state root)
    feature_l2_ethereum_compat=1

; ----------------------------------------------------------------------------
; Real Sepolia genesis allocs (15 pre-funded EOAs from eth-clients/sepolia
; metadata/besu.json). The MPT over exactly these allocs equals the canonical
; Sepolia genesis state root 0x5eb6e371... (verified).
; ----------------------------------------------------------------------------
[alloc.0]
    address=0xa2a6d93439144ffe4d27c9e088dcd8b783946263
    balance=1000000000000000000000000
    nonce=0
[alloc.1]
    address=0xbc11295936aa79d594139de1b2e12629414f3bdb
    balance=1000000000000000000000000
    nonce=0
[alloc.2]
    address=0x7cf5b79bfe291a67ab02b393e456ccc4c266f753
    balance=1000000000000000000000000
    nonce=0
[alloc.3]
    address=0xaaec86394441f915bce3e6ab399977e9906f3b69
    balance=1000000000000000000000000
    nonce=0
[alloc.4]
    address=0xf47cae1cf79ca6758bfc787dbd21e6bdbe7112b8
    balance=1000000000000000000000000
    nonce=0
[alloc.5]
    address=0xd7eddB78ED295B3C9629240E8924fb8D8874ddD8
    balance=1000000000000000000000000
    nonce=0
[alloc.6]
    address=0x8b7f0977bb4f0fbe7076fa22bc24aca043583f5e
    balance=1000000000000000000000000
    nonce=0
[alloc.7]
    address=0xe2e2659028143784d557bcec6ff3a0721048880a
    balance=1000000000000000000000000
    nonce=0
[alloc.8]
    address=0xd9a5179f091d85051d3c982785efd1455cec8699
    balance=1000000000000000000000000
    nonce=0
[alloc.9]
    address=0xbeef32ca5b9a198d27b4e02f4c70439fe60356cf
    balance=1000000000000000000000000
    nonce=0
[alloc.10]
    address=0x0000006916a87b82333f4245046623b23794c65c
    balance=10000000000000000000000000
    nonce=0
[alloc.11]
    address=0xb21c33de1fab3fa15499c62b59fe0cc3250020d1
    balance=100000000000000000000000000
    nonce=0
[alloc.12]
    address=0x10f5d45854e038071485ac9e402308cf80d2d2fe
    balance=100000000000000000000000000
    nonce=0
[alloc.13]
    address=0xd7d76c58b3a519e9fa6cc4d22dc017259bc49f1e
    balance=100000000000000000000000000
    nonce=0
[alloc.14]
    address=0x799d329e5f583419167cd722962485926e338f4a
    balance=1000000000000000000
    nonce=0

; ----------------------------------------------------------------------------
; Real Sepolia genesis block header (the sync anchor). Core fields are REQUIRED;
; the fork-gated fields (base_fee_per_gas / withdrawals_root / blob_gas_used /
; excess_blob_gas / parent_beacon_block_root / requests_hash) are OPTIONAL and
; are OMITTED here because Sepolia's genesis predates those forks — the ledger
; re-encodes exactly the fields present, which is what makes the hash below
; byte-exact. (An L2 chain emits all 21 keys instead.)
; ----------------------------------------------------------------------------
[eth_genesis_header]
    parent_hash=0x0000000000000000000000000000000000000000000000000000000000000000
    sha3_uncles=0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347
    miner=0x0000000000000000000000000000000000000000
    state_root=0x5eb6e371a698b8d68f665192350ffcecbbbf322916f4b51bd79bb6887da3f494
    transactions_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421
    receipts_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421
    logs_bloom=0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
    difficulty=0x20000
    number=0x0
    gas_limit=0x1c9c380
    gas_used=0x0
    timestamp=0x6159af19
    extra_data=0x5365706f6c69612c20417468656e732c204174746963612c2047726565636521
    mix_hash=0x0000000000000000000000000000000000000000000000000000000000000000
    nonce=0x0000000000000000
    base_fee_per_gas=0x3b9aca00
    ; keccak256(rlp(header)) — checksum; Ledger recomputes and refuses on
    ; mismatch. This is the REAL canonical Sepolia genesis hash: the header
    ; RLP carries only the 16 London-era fields (base_fee present, no
    ; withdrawalsRoot/blobGasUsed/excessBlobGas/parentBeaconBlockRoot/
    ; requestsHash), so the re-encoding is byte-exact with geth.
    hash=0x25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9
