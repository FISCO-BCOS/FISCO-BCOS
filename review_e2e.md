# E2E Test Suite Self-Review

**Scope**: tools/op-e2e/ (17 files, 2731 lines, all new)
**Branch**: op-alignment-on-scheduler
**Review date**: 2026-08-18

## Findings

### Finding A · IMPORTANT — Hardcoded absolute paths break portability

**Files**: state_verify.py:16, b4_persist.py:35-36

**Impact**: Scripts fail on any machine that isn't the original developer's workstation. CI, other developers, and fresh checkouts all break.

**Evidence**:
```python
# state_verify.py:16
TOOL = os.environ.get("OP_STATE_READ", "/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment/tools/op-e2e/op_state_read")

# b4_persist.py:35-36
r = subprocess.run(["bash", "/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment/"
                   "tools/op-e2e/restart_b3.sh"], capture_output=True, text=True)
```

**Suggested fix**: Use relative paths based on `__file__`:
```python
# state_verify.py
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TOOL = os.environ.get("OP_STATE_READ", os.path.join(_SCRIPT_DIR, "op_state_read"))

# b4_persist.py
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
r = subprocess.run(["bash", os.path.join(_SCRIPT_DIR, "restart_b3.sh")], ...)
```

---

### Finding B · IMPORTANT — Massive code duplication (keccak256/rlp_encode/to_bytes_min)

**Files**: chain_driver.py:40-101, b3_contracts.py:30-96, predeploy_matrix.py:137-202, a1_active.py:137-202

**Impact**: ~200 lines of identical cryptographic code copy-pasted 4 times. Any bug fix must be applied 4 times. The keccak256 implementation is security-critical (used for tx signing).

**Evidence**: The `_RC`, `_ROT`, `_rotl64`, `_keccak_f`, `keccak256`, `rlp_encode`, `to_bytes_min` blocks are byte-identical across all 4 files.

**Suggested fix**: Extract to a shared module (e.g., `tools/op-e2e/eth_utils.py`) and import from it.

---

### Finding C · SUGGESTION — b3_contracts.py:190 potential UnboundLocalError

**File**: b3_contracts.py:190

**Impact**: If `rec2` is None (deploy timeout), `code2` is never assigned, but line 190 references it unconditionally.

**Evidence**:
```python
if isinstance(addr2, str) and addr2.startswith("0x"):
    code2 = rpc.call("eth_getCode", [addr2, "latest"])  # only assigned here
check("revert getCode == runtime", code2 == "0x" + rev_runtime, ...)  # always executed
```

**Suggested fix**: Move the `check` inside the `if` block, or initialize `code2 = None` and guard the check.

---

### Finding D · SUGGESTION — predeploy_matrix.py sequenceNumber test is always DIVERGENCE

**File**: predeploy_matrix.py:292-301

**Impact**: The sequenceNumber cross-block probe reads `seq0` and `seq1` both at `latest` within the same block (only 1.2s sleep, but both reads happen at the same block height). The `int(seq0, 16) == 0` branch always triggers because the deposit reverts. The test always passes but never actually tests cross-block increment.

**Evidence**:
```python
b0 = rpc.eth("eth_getBlockByNumber", ["latest", False])
time.sleep(1.2)
b1 = rpc.eth("eth_getBlockByNumber", ["latest", False])
seq0 = rpc.eth("eth_call", [{"to": L1_BLOCK, "data": "0x64ca23ef"}, "latest"])  # same block
seq1 = rpc.eth("eth_call", [{"to": L1_BLOCK, "data": "0x64ca23ef"}, "latest"])  # same block
```

**Suggested fix**: Read `seq0` at block N, then `seq1` at block N+1 (use the block numbers from b0/b1).

---

### Finding E · SUGGESTION — setup_op_node.sh:312 sed enable=false too broad

**File**: setup_op_node.sh:312

**Impact**: The sed command `s/enable=false/enable=true/` replaces ALL occurrences of `enable=false` in the B3a config, not just `op_engine_rpc.enable`. If any other section has `enable=false`, it gets flipped.

**Evidence**:
```bash
sed -i.bak \
    -e "s/enable_single_node_consensus=true/enable_single_node_consensus=false/" \
    -e "s/enable=false/enable=true/" \    # <-- matches any enable=false
    -e "s/produce_empty_blocks=true/produce_empty_blocks=false/" \
    "$B3A/config.genesis"
```

**Suggested fix**: Scope the replacement: `s/\[op_engine_rpc\]/&,enable=true/` or use a more specific pattern like `s/op_engine_rpc$/&\n    enable=true/`.

---

### Finding F · NIT — Duplicated Rpc class across files

**Files**: rpc_matrix.py:36-55, chain_driver.py:118-130, b3_contracts.py:111-123, predeploy_matrix.py:84-96, a1_active.py:58-73

**Impact**: 5 slightly different RPC client implementations. The rpc_matrix version has JWT support; others don't. All bypass the proxy, but with different patterns.

**Suggested fix**: Same as Finding B — extract to shared module.

---

### Finding G · NIT — chain_driver.py:200 assumes effectiveGasPrice = 1 gwei

**File**: chain_driver.py:200

**Impact**: If the effective gas price ever changes (e.g., baseFee != 0), the balance assertion breaks.

**Evidence**:
```python
gas_cost = cumulative_gas * 1_000_000_000  # effectiveGasPrice = 1 gwei
expected = bal0 - (args.txs * args.value) - gas_cost
```

**Suggested fix**: Read `effectiveGasPrice` from each receipt and sum the actual gas costs.

---

### Finding H · NIT — predeploy_matrix.py abi_encode_call silently truncates

**File**: predeploy_matrix.py:113-133

**Impact**: The `abi_encode_call` function silently converts unknown types via `int(a)`. If a caller passes a wrong type (e.g., a list), it crashes with an unclear error instead of a helpful message.

---

### Finding I · NIT — run_all.sh step comments don't match assert counts

**File**: run_all.sh:34-52

**Impact**: Minor — the step comments say "51 pass + 8 tier-1 known-red" and "35 asserts" etc., but these numbers drift as tests evolve. The stale-gate mechanism in rpc_matrix.py handles this, but the comments could mislead.

---

## Verification Boundary

- Read all 17 files in full
- Did NOT run the tests (no live node)
- Did NOT verify keccak256 against test vectors (copy-paste from verified source)
- Did NOT check the C sign_secp.c against the libsecp256k1 API docs

## Verdict

**APPROVE** with suggestions. No CRITICAL findings. The two IMPORTANT findings (hardcoded paths, code duplication) are maintainability/portability issues, not correctness bugs — the tests pass on the original machine. The code duplication (Finding B) is the highest-priority cleanup since the keccak256/RLP code is security-sensitive.
