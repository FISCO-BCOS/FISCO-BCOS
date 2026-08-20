# Self-Review Context — op-alignment-on-scheduler

**Branch**: `op-alignment-on-scheduler` (vs `dev` base)
**Total**: ~40 commits, 252 production files, ~20K lines changed
**Scope**: Full OP Stack L2 port — engine API, RPC, protocol, precompiles, genesis, e2e

## Review Focus Areas (production risk, descending)

### 1. Engine API (engine/bcos-engine/EngineServiceImpl.{cpp,h})
- **V4 endpoints**: FCU/getPayload/newPayloadV4 + executionRequests
- **L1-attributes deposit**: synthesis + skip when attrs.transactions has CL-derived deposit
- **Attribute-driven OP payload building**: two-pass execute, self-built fast-commit
- **V4 vs V3 gating**: FCU-with-attrs gated to V4
- Risk: consensus-critical, wrong payload → chain halt or invalid blocks

### 2. RPC Layer (bcos-rpc/web3jsonrpc/)
- **Canonical block hash from registry** (s_number_2_hash overrides tars derivation)
- **withdrawalsRoot reads header** (Isthmus MessagePasser root)
- **safe/finalized tags** route to engine-tracked numbers; strict null when untracked
- **gasLimit/parentBeaconBlockRoot** read from header (fallback 30M/zero for PBFT)
- **Web3TxHandler** — 1226 new lines, typed tx handling
- **EngineHelper** — 564 new lines
- Risk: RPC correctness, consensus state exposure

### 3. Protocol Layer (bcos-tars-protocol/)
- **BlockHeaderImpl**: 406 changed lines — OP header fields (baseFee, mixHash, extraData, withdrawalsRoot, parentBeaconBlockRoot)
- **TransactionImpl**: 351 changed lines — OP deposit tx encoding
- **TransactionReceiptImpl**: 194 new lines
- **Tars schema changes**: Block.tars, Transaction.tars, TransactionReceipt.tars
- Risk: serialization/deserialization correctness, cross-node compatibility

### 4. Precompiles / Executor (bcos-executor/)
- **L2DisabledSet**: 131 new lines — disables FISCO precompiles in OP mode
- **AuthManagerPrecompiled**: 253 changed lines
- **ContractAuthMgrPrecompiled**: 253 changed lines
- **EVMHostInterface**: 164 changed lines
- Risk: security boundary, precompile routing

### 5. Genesis / Tooling (tools/opstack-genesis/, bcos-tool/)
- **mpt_state_root.py**: 276 new lines — MPT root computation
- **NodeConfig.cpp**: 834 changed lines — OP config parsing
- **chain-config-c2.yaml**: op-deployer base allocs
- Risk: genesis correctness, config parsing bugs

### 6. Scheduler (bcos-scheduler/)
- **OpCallScheduler**: OP-mode scheduler adapter
- **SchedulerImpl**: 36 changed lines
- Risk: block execution ordering

## Hard Constraints
- All existing tests must continue to pass (160 e2e assertions + ctest)
- No consensus regression
