; ============================================================================
; FISCO-BCOS hive client node configuration (config.ini) — Ethereum L1 EL mode.
; Instantiated by bcos.sh: __CHAIN_ID__, __LOG_LEVEL__, __ENGINE_ENABLED__
; are substituted from the hive environment before the node starts.
;
; DISPOSABLE TEST PROFILE: RPC and the Engine API bind to 0.0.0.0 with
; wide-open CORS and no authentication — correct inside a throwaway hive
; container, NOT a starting point for real deployments. For production
; configs start from tools/BcosBuilder/src/tpl/config.genesis.el instead.
; ============================================================================

[service]
    rpc=chain0
    gateway=chain0

[ethereum]
    mode=el
    ; written by bcos.sh from HIVE_BOOTNODE (placeholder enode when unset —
    ; validateNodeConfig requires a non-empty list; hive testnets inject peers
    ; via the simulator instead)
    bootnodes_file=./bootnodes.json
    node_key_file=./node.rlpx.key
    max_batch_size=192
    tx_gossip=true
    ; allow shallow reorgs so hive fork tests (/blocks side chains) can rewind
    reorg_window=256

[chain]
    sm_crypto=false
    group_id=group0
    chain_id=__CHAIN_ID__

[web3]
    chain_id=__CHAIN_ID__

[engine_rpc]
    ; enabled by bcos.sh when the simulator mounted a JWT secret at /jwtsecret
    enable=__ENGINE_ENABLED__
    listen_ip=0.0.0.0
    listen_port=8551
    jwt_secret_file=conf/engine/jwt.hex

[security]
    private_key_path=conf/node.pem
    enable_hsm=false

; FISCO gateway config — in EL mode the gateway object is built but NEVER
; started; nodes.json stays an empty list.
[p2p]
    listen_ip=0.0.0.0
    listen_port=30300
    sm_ssl=false
    enable_ssl_verify=false
    nodes_path=./
    nodes_file=nodes.json

[cert]
    ca_path=./conf
    ca_cert=ca.crt
    node_key=ssl.key
    node_cert=ssl.crt

[rpc]
    listen_ip=127.0.0.1
    listen_port=20200
    sm_ssl=false
    enable_ssl=false

[web3_rpc]
    enable=true
    listen_ip=0.0.0.0
    listen_port=8545
    http_body_size_limit=10240000
    enable_cors=true
    cors_allowed_origins=*

[consensus]
    min_seal_time=500

[executor]
    enable_dag=true
    baseline_scheduler=false
    baseline_scheduler_parallel=false

[storage]
    data_path=data
    enable_cache=true
    type=RocksDB
    key_page_size=10240
    rocksdb_max_open_files=-1

[txpool]
    limit=15000

[sync]
    send_txs_by_tree=false

[log]
    enable=true
    ; hive collects the container's stdout
    enable_console_output=true
    log_path=./log
    level=__LOG_LEVEL__
    max_log_file_size=200
