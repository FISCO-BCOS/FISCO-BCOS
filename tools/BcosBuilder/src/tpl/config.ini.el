; ============================================================================
; FISCO-BCOS Ethereum L1 EL-mode node configuration (config.ini)
; Run as a pure Ethereum execution-layer client: [ethereum] mode=el makes the
; node download blocks from RLPx bootnodes, verify them with the Ethereum
; block verifier (executor v2) and commit them locally. No FISCO gateway /
; PBFT / txpool network is started.
;
; Usage:
;   fisco-bcos -c config.ini -g config.genesis [--el] [--bootnodes bootnodes.json]
; ============================================================================

[service]
    ; AIR nodes run in-process (no tars framework needed). Keep without_tars_framework
    ; commented so the tars proxy file is not required.
    ; without_tars_framework = true
    ; tars_proxy_conf = conf/tars_proxy.ini
    rpc=chain0
    gateway=chain0

[ethereum]
    ; EL mode: none (default) = normal FISCO node; el = Ethereum L1 execution layer
    mode=el
    ; RLPx listen address/port (the Ethereum devp2p protocol, NOT the FISCO gateway)
    listen_ip=0.0.0.0
    listen_port=30303
    ; geth-style bootnodes file (a JSON array of enode:// strings); see bootnodes.json
    ;
    ; TRUST MODEL: the bootnodes list is the trust root for chain data. The node
    ; commits the chain the bootnodes serve as long as it is internally consistent
    ; (parent-hash linkage + re-executed state roots); pre-merge PoW difficulty /
    ; total difficulty are NOT verified, and there is no consensus-layer finality
    ; feed. Configure ONLY bootnodes you trust, and consider pinning
    ; finalized_checkpoint below. A full checkpoint/finality mechanism is a known
    ; limitation and tracked as follow-up work.
    bootnodes_file=./bootnodes.json
    ; secp256k1 node identity: a file holding the 32-byte private key as hex
    ; (optional 0x prefix). Empty = auto-generate a persistent key on first start
    ; (node.rlpx.key next to the FISCO node key) and reuse it on restart.
    ; A stable key is strongly recommended so bootnodes can authenticate us.
    node_key_file=
    ; max blocks requested per batch
    max_batch_size=192
    ; optional operator-pinned finalized checkpoint, "<number>:<0xHASH>": the
    ; committed block at <number> must carry <0xHASH>; on a mismatch the sync loop
    ; stops with a fatal error (the bootnodes serve a wrong fork). Take the value
    ; from a trusted source (e.g. a block explorer or your own archive node).
    ; Empty = no checkpoint.
    ; finalized_checkpoint=9200000:0x...

[chain]
    ; use SM crypto or not — EL mode is always plain secp256k1/keccak
    sm_crypto=false
    ; the group id (kept for FISCO compatibility; unused by the Ethereum stack)
    group_id=group0
    ; the chain id — must be the numeric Ethereum chain id (11155111 for Sepolia)
    chain_id=11155111

[web3]
    ; Ethereum chain id used by web3 RPC (decimal string)
    chain_id=11155111

[security]
    private_key_path=conf/node.pem
    enable_hsm=false

; FISCO gateway config — present only because the core node still builds a gateway
; object; in EL mode it is NEVER started (AirNodeInitializer skips m_gateway->start()).
; nodes.json must be an empty list so no FISCO peer is contacted.
[p2p]
    listen_ip=0.0.0.0
    listen_port=30300
    sm_ssl=false
    nodes_path=./
    nodes_file=nodes.json

[cert]
    ; directory the certificates located in
    ca_path=./conf
    ca_cert=ca.crt
    node_key=ssl.key
    node_cert=ssl.crt

[storage]
    data_path=data
    enable_cache=true
    type=RocksDB
    key_page_size=10240

[log]
    enable=true
    log_path=./log
    level=info
    max_log_file_size=200

[thread_pool]
    ; Shared IOServicePool thread count
    ;io_thread_count=
