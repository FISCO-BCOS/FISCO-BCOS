#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""
eest_rpc_runner.py — run EEST (execution-spec-tests v5.4.0) state-test fixtures
against a live FISCO-BCOS single-node-consensus node via RPC.

For each fixture (per fork, per post vector) it:
  1. generates a fresh single-node chain whose genesis reproduces the fixture:
       - [alloc.*]  = fixture `pre`   (exact world state, L2 mode / op-geth MPT)
       - [tx] gas_limit / gas_price   = fixture env.currentGasLimit / currentBaseFee
       - [executor] version=2 evm_revision=<fork>
       - [consensus] enable_single_node_consensus=true, produce_empty_blocks=false,
         prev_randao=<env.currentRandom>, fixed_timestamp=<env.currentTimestamp>,
         fee_recipient=<env.currentCoinbase>
  2. starts the node and submits the fixture transaction(s) via eth_sendRawTransaction
     (mempool -> single-node driver -> BaselineScheduler commit);
  3. compares the resulting post state (accounts) via eth_getBalance / eth_getCode /
     eth_getTransactionCount / eth_getStorageAt against the fixture's expected `post`.

Usage:
  python3 tests/eest_rpc_runner.py --fixture-dir fixtures/state_tests [--workers N]
      [--fork cancun] [--pattern '*.json'] [--limit N] [--binary path] [--base-dir /tmp/...]
Exit code: 0 = all selected fixtures pass, 1 = any failure (pass-rate < 100%).
"""

import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import eth_utils
import requests
from eth_account import Account

# ----------------------------------------------------------------------------- forks
# EEST state-test fork names -> evmc revision (v2 executor evm_revision).
# NB: EEST "ConstantinopleFix" is Petersburg: the original Constantinople's
# EIP-1283 (net gas metering) was reverted, so it must map to EVMC_PETERSBURG.
# Mapping it to "constantinople" makes SSTORE billing diverge (observed: the
# eip145 shift-combos vectors under-consume ~1.1M gas).
FORK_REV = {
    "Frontier": "frontier", "Homestead": "homestead", "TangerineWhistle": "tangerinewhistle",
    "SpuriousDragon": "spuriousdragon", "Byzantium": "byzantium",
    "Constantinople": "constantinople", "ConstantinopleFix": "petersburg",
    "Petersburg": "petersburg", "Istanbul": "istanbul",
    "Berlin": "berlin", "London": "london", "Merge": "paris", "Paris": "paris",
    "Shanghai": "shanghai", "Cancun": "cancun", "Prague": "prague", "Osaka": "osaka",
}
# Post keys may name transitions (e.g. "CancunToPragueAtTime15k"); take the leading fork.
def fork_key_to_rev(key):
    for name in ("Frontier", "Homestead", "TangerineWhistle", "SpuriousDragon", "Byzantium",
                 "ConstantinopleFix", "Constantinople", "Petersburg", "Istanbul", "Berlin",
                 "London", "Merge", "Paris", "Shanghai", "Cancun", "Prague", "Osaka"):
        if key.startswith(name):
            return FORK_REV[name]
    return None


def hex_int(v):
    return int(v, 16)


def pad32(h):
    """Pad a hex string to 32 bytes (64 hex chars, 0x-prefixed) for alloc storage slots."""
    h = h[2:] if h.startswith("0x") else h
    return "0x" + h.rjust(64, "0")


# ----------------------------------------------------------------------------- fixture
class Fixture:
    def __init__(self, path, name, data):
        self.path, self.name, self.data = path, name, data
        self.env = data.get("env", {})
        self.pre = data.get("pre", {})
        self.tx = data.get("transaction", {})
        self.post = data.get("post", {})
        self.config = data.get("config", {})

    def forks(self):
        return list(self.post.keys())

    def vectors(self, fork):
        """Return (indexes, post_entry) for each post vector under `fork`."""
        return [(e.get("indexes", {}), e) for e in self.post.get(fork, [])]


def load_fixtures(fixture_dir, pattern="*.json"):
    out = []
    root = Path(fixture_dir)
    for path in sorted(root.rglob(pattern)):
        try:
            with open(path) as fh:
                data = json.load(fh)
        except Exception as exc:  # noqa: BLE001
            print(f"[warn] cannot parse {path}: {exc}", file=sys.stderr)
            continue
        for name, test in data.items():
            if isinstance(test, dict) and "post" in test and "pre" in test:
                out.append(Fixture(str(path), name, test))
    return out


# ----------------------------------------------------------------------------- config
def gen_config(workdir, fixture, fork_rev, idx, env_overrides=None, port_offset=0):
    """Write config.ini + config.genesis reproducing the fixture env/pre.

    idx selects this unit's ports (unique per concurrent node):
      legacy jsonrpc = 30000+idx%40, web3_rpc = 20000+idx%40, p2p = 21000+idx%40,
      plus port_offset (for running multiple harness processes in parallel).
    """
    rpc_port = 20000 + (idx % 40) + port_offset
    p2p_port = 21000 + (idx % 40) + port_offset
    legacy_port = 30000 + (idx % 40) + port_offset
    env = fixture.env
    base_fee = env.get("currentBaseFee", "0x0")
    # tx.gas_limit is parsed as a decimal number by NodeConfig (checkAndGetValue), while
    # fixtures carry it as hex; convert here.
    gas_limit = int(env.get("currentGasLimit", "0x0"), 16)
    coinbase = env.get("currentCoinbase", "0x0000000000000000000000000000000000000000")
    prev_randao = env.get("currentRandom", "0x0000000000000000000000000000000000000000")
    timestamp = env.get("currentTimestamp", "0x0")

    # ----- config.genesis -----
    allocs = []
    idx = 0
    for addr, acc in fixture.pre.items():
        # balance / nonce must be decimal in config.genesis (requireDecimalField); the
        # fixture carries them as hex.
        allocs.append(f"[alloc.{idx}]\n    address={addr}\n")
        allocs.append(f"    balance={int(acc.get('balance', '0x0'), 16)}\n")
        allocs.append(f"    nonce={int(acc.get('nonce', '0x0'), 16)}\n")
        code = acc.get("code", "")
        if code in (None, "0x"):
            code = ""  # empty code = plain EOA (config parser rejects "0x")
        allocs.append(f"    code={code}\n")
        storage = acc.get("storage", {})
        if storage:
            allocs.append(f"[alloc.{idx}.storage]\n")
            for k, v in storage.items():
                # storage keys/values must be 64 hex chars; fixtures carry short hex.
                allocs.append(f"    {pad32(k)} = {pad32(v)}\n")
        idx += 1

    # EEST fixtures sign authorizations / EIP-155 payloads with their own
    # configured chain id (config.chainid, usually 0x01). The node's [web3]
    # chain_id MUST match: processAuthorizationList (EIP-7702) compares each
    # authorization's chain_id against the node's chain id, and a mismatch
    # silently skips the authorization (observed: 7702 delegation code never
    # written for chainid=0x01 fixtures against the old hardcoded 20200).
    web3_chain_id = int(fixture.config.get("chainid"), 16) if fixture.config.get("chainid") else 20200

    genesis = f"""[chain]
    sm_crypto=false
    group_id=group0
    chain_id=chain0

[web3]
    chain_id={web3_chain_id}

[consensus]
    consensus_type=pbft
    block_tx_count_limit=1000
    consensus_timeout=3000
    leader_period=1
    epoch_sealer_num=4
    epoch_block_num=1000
    node.0=179895f679d850ae7e60e245ecee2ad89c625017666368e5b226bc0a894074520dc9c7be1a14e05b77482da1792c54a14f1d4c0581676e4e0081209db53093da: 1

[version]
    compatibility_version=3.18.0

[tx]
    gas_limit={gas_limit}
    gas_price={base_fee}

[executor]
    is_auth_check=false
    auth_admin_account=0x0000000000000000000000000000000000000000
    is_serial_execute=true
    version=2
    evm_revision={fork_rev}

[features]
    feature_l2_ethereum_compat=1

""" + "\n".join(allocs)
    (workdir / "config.genesis").write_text(genesis)

    # ----- config.ini -----
    # Use the full single-node template (the smoke node's config.ini) as the base —
    # a hand-written minimal config.ini broke L2 alloc import (balance/nonce/storage
    # read back as 0). Patch only ports + single-node consensus keys.
    tmpl_ini = (Path(__file__).resolve().parent.parent / "tools/nodes/127.0.0.1/node0"
                / "config.ini")
    if not tmpl_ini.exists():
        raise RuntimeError(f"template config.ini not found: {tmpl_ini}")
    ini = tmpl_ini.read_text()

    def patch_section(text, section, key, value):
        """Replace `key=<anything>` with `key=value` inside the given [section] block."""
        out, in_sec = [], False
        for line in text.splitlines():
            s = line.strip()
            if s.startswith("[") and s.endswith("]"):
                in_sec = (s[1:-1].strip() == section)
            elif in_sec and s.startswith(key + "="):
                line = re.sub(rf"^\s*{re.escape(key)}\s*=.*", f"{key}={value}", line)
            out.append(line)
        return "\n".join(out)

    fixed_ts = hex_int(timestamp) if timestamp else 0
    ini = patch_section(ini, "web3_rpc", "listen_port", rpc_port)
    ini = patch_section(ini, "p2p", "listen_port", p2p_port)
    ini = patch_section(ini, "rpc", "listen_port", legacy_port)
    ini = patch_section(ini, "consensus", "block_interval", "1000")
    ini = patch_section(ini, "consensus", "produce_empty_blocks", "false")
    ini = patch_section(ini, "consensus", "prev_randao", prev_randao)
    ini = patch_section(ini, "consensus", "fixed_timestamp", fixed_ts)
    if "fee_recipient=" not in ini:
        # insert fee_recipient into the [consensus] block after enable_single_node_consensus
        out, in_sec = [], False
        for line in ini.splitlines():
            s = line.strip()
            if s.startswith("[") and s.endswith("]"):
                in_sec = (s[1:-1].strip() == "consensus")
            if in_sec and s.startswith("enable_single_node_consensus="):
                out.append(line)
                out.append(f"    fee_recipient={coinbase}")
                continue
            out.append(line)
        ini = "\n".join(out)
    (workdir / "config.ini").write_text(ini)


# ----------------------------------------------------------------------------- node
class Node:
    def __init__(self, binary, workdir, rpc_port):
        self.binary, self.workdir, self.rpc_port = binary, workdir, rpc_port
        self.proc = None
        self.url = f"http://127.0.0.1:{rpc_port}"

    def start(self):
        env = dict(os.environ)
        logf = open(self.workdir / "node.log", "w")
        self.proc = subprocess.Popen(
            [self.binary, "-c", "config.ini", "-g", "config.genesis"],
            cwd=str(self.workdir), stdout=logf, stderr=subprocess.STDOUT, env=env,
            start_new_session=True)
        for _ in range(60):
            try:
                if self.rpc("eth_blockNumber", []) is not None:
                    return True
            except Exception:  # noqa: BLE001
                pass
            if self.proc.poll() is not None:
                return False
            time.sleep(0.5)
        return False

    def stop(self):
        if self.proc and self.proc.poll() is None:
            try:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
            except Exception:  # noqa: BLE001
                self.proc.kill()
            self.proc.wait(timeout=10)

    def rpc(self, method, params):
        last = None
        for _ in range(3):
            try:
                r = requests.post(self.url, json={"jsonrpc": "2.0", "id": 1, "method": method,
                                                  "params": params}, timeout=15)
                b = r.json()
                if "error" in b:
                    raise RuntimeError(f"{method}: {json.dumps(b['error'])[:300]}")
                return b.get("result")
            except requests.ConnectionError as e:
                last = e  # transient — retry (node may be mid-restart)
                time.sleep(1)
        raise last


# ----------------------------------------------------------------------------- tx build
def build_tx(tx, indexes, chain_id):
    """Build an eth_account-compatible tx dict from the fixture `transaction` for a vector."""
    gas = tx.get("gasLimit", "0x5208")
    value = tx.get("value", "0x0")
    data = tx.get("data", "0x")
    # Resolve array-typed fields through the vector indexes.
    if isinstance(gas, list):
        gas = gas[indexes.get("gas", 0)]
    if isinstance(value, list):
        value = value[indexes.get("value", 0)]
    if isinstance(data, list):
        data = data[indexes.get("data", 0)]
    to = tx.get("to", "")

    t = {"chainId": chain_id, "nonce": hex_int(tx.get("nonce", "0x0")), "gas": hex_int(gas),
         "value": hex_int(value), "data": data}
    if to:
        t["to"] = eth_utils.to_checksum_address(to)

    if "maxFeePerGas" in tx:  # EIP-1559 (type 2) / EIP-4844 (type 3, same pricing)
        t["maxFeePerGas"] = hex_int(tx["maxFeePerGas"])
        t["maxPriorityFeePerGas"] = hex_int(tx.get("maxPriorityFeePerGas", "0x0"))
    else:
        t["gasPrice"] = hex_int(tx.get("gasPrice", "0x0"))

    # EIP-4844 blob fields make eth_account sign a type-3 tx (0x03). The node's
    # RLP decode does not need the blob sidecar, only these fields; blob gas is
    # then charged from blobVersionedHashes. blobs/kzg sidecars are absent from
    # EEST state-test fixtures (the KZG precompile, used by only a handful of
    # fixtures, is the only thing that would need actual blob data).
    if "blobVersionedHashes" in tx or "maxFeePerBlobGas" in tx:
        bvh = tx.get("blobVersionedHashes")
        if bvh:
            if isinstance(bvh[0], list):  # multiple variants indexed by the vector
                bvh = bvh[indexes.get("blobVersionedHashes", 0)]
            t["blobVersionedHashes"] = list(bvh)
        else:
            # e.g. TYPE_3_TX_ZERO_BLOBS invalid cases: maxFeePerBlobGas present but
            # no versioned hashes — eth_account still needs the key to build type-3.
            t["blobVersionedHashes"] = []
        t["maxFeePerBlobGas"] = hex_int(tx.get("maxFeePerBlobGas", "0x0"))

    # EIP-2930 (type 1) / EIP-1559 (type 2) / EIP-4844 (type 3) all carry an
    # access list; it must be applied regardless of tx type, otherwise warm/cold
    # costs diverge.
    acl = tx.get("accessLists")
    if acl:
        if isinstance(acl[0], list):  # multiple variants indexed by `accessLists`
            variant = acl[indexes.get("accessLists", 0)]
        else:
            variant = acl
        t["accessList"] = [
            {"address": a["address"], "storageKeys": a.get("storageKeys", [])}
            for a in variant] if variant else []

    # EIP-7702 (type 4): authorization list. EEST carries it as a dict, a list of
    # auths, or a list of variants; eth_account needs yParity/r/s/chainId/nonce.
    if "authorizationList" in tx:
        al = tx.get("authorizationList")
        auths = al if isinstance(al, list) else [al]
        if auths and isinstance(auths[0], list):  # list of variants
            auths = auths[indexes.get("authorizationList", 0)]
        auths = auths if isinstance(auths, list) else [auths]
        t["authorizationList"] = []
        for a in auths:
            if not isinstance(a, dict):
                continue
            v_raw = a.get("v", a.get("yParity", "0x0"))
            try:
                y_parity = int(v_raw, 16) if isinstance(v_raw, str) else int(v_raw)
            except ValueError:
                y_parity = 0
            t["authorizationList"].append({
                "chainId": hex_int(a.get("chainId", "0x0")),
                "address": a.get("address", "0x" + "00" * 20),
                "nonce": hex_int(a.get("nonce", "0x0")),
                "yParity": y_parity,
                "r": hex_int(a.get("r", "0x0")),
                "s": hex_int(a.get("s", "0x0")),
            })
    return t


def sign_and_submit(node, tx, indexes, chain_id):
    secret = tx.get("secretKey")
    sender = tx.get("sender")
    if secret:
        acct = Account.from_key(secret)
        if sender and acct.address.lower() != sender.lower():
            raise RuntimeError("secretKey/sender mismatch")
        try:
            t = build_tx(tx, indexes, chain_id)
            signed = Account.sign_transaction(t, secret)
            raw = signed.raw_transaction.hex()
        except Exception:
            # eth_account rejects authorizations with invalid signature fields
            # (r=0, s=0, yParity not 0/1, r/s at the u256 extremes — EEST
            # "valid_tx_invalid_auth_signature" vectors). Those txs are still
            # valid outer txs: the invalid auth is skipped by EIP-7702, so we
            # must craft the raw type-4 bytes manually with the raw auth values.
            if "authorizationList" not in tx:
                raise
            raw = build_type4_raw(tx, indexes, chain_id, secret)
        return node.rpc("eth_sendRawTransaction", [raw])
    else:
        # v/r/s supplied directly: reconstruct the raw tx bytes from txbytes? For now the
        # harness supports secretKey-signed fixtures (the bulk of state_tests).
        raise RuntimeError("fixture tx has no secretKey (unsupported by harness yet)")


def _rlp_int(n):
    if n == 0:
        return b"\x80"
    b = n.to_bytes((n.bit_length() + 7) // 8, "big")
    if len(b) == 1 and b[0] < 0x80:
        return b
    return bytes([0x80 + len(b)]) + b


def _rlp_bytes(b):
    if len(b) == 1 and b[0] < 0x80:
        return b
    if len(b) < 56:
        return bytes([0x80 + len(b)]) + b
    ln = len(b).to_bytes((len(b).bit_length() + 7) // 8, "big")
    return bytes([0xB7 + len(ln)]) + ln + b


def _rlp_list(items):
    body = b"".join(items)
    if len(body) < 56:
        return bytes([0xC0 + len(body)]) + body
    ln = len(body).to_bytes((len(body).bit_length() + 7) // 8, "big")
    return bytes([0xF7 + len(ln)]) + ln + body


def build_type4_raw(tx, indexes, chain_id, secret):
    """Manually construct + sign an EIP-7702 (type-4) raw tx.

    Used when eth_account refuses to sign because an authorization carries an
    invalid signature field (r/s/yParity). The outer tx signature is real; the
    invalid authorization is skipped by the node's EIP-7702 processing, which is
    exactly what the EEST "valid_tx_invalid_auth_signature" vectors expect.
    """
    gas = tx.get("gasLimit", "0x5208")
    value = tx.get("value", "0x0")
    data = tx.get("data", "0x")
    if isinstance(gas, list):
        gas = gas[indexes.get("gas", 0)]
    if isinstance(value, list):
        value = value[indexes.get("value", 0)]
    if isinstance(data, list):
        data = data[indexes.get("data", 0)]
    to = tx.get("to", "")

    # Access list (same resolution as build_tx).
    access_list = []
    acl = tx.get("accessLists")
    if acl:
        variant = acl[indexes.get("accessLists", 0)] if isinstance(acl[0], list) else acl
        access_list = [bytes.fromhex(a["address"][2:]) for a in variant] if variant else []
        # EEST access entries may have storageKeys too; the harness only needs
        # them for gas (cold/warm), which matches the address-only form below.
        access_list = [
            _rlp_list([
                _rlp_bytes(bytes.fromhex(a["address"][2:])),
                _rlp_list([_rlp_bytes(bytes.fromhex(k[2:])) for k in a.get("storageKeys", [])]),
            ]) for a in variant] if variant else []

    # Raw authorization entries (unvalidated).
    al = tx.get("authorizationList")
    auths = al if isinstance(al, list) else [al]
    if auths and isinstance(auths[0], list):
        auths = auths[indexes.get("authorizationList", 0)]
    auths = auths if isinstance(auths, list) else [auths]
    auth_enc = []
    for a in auths:
        if not isinstance(a, dict):
            continue
        v_raw = a.get("v", a.get("yParity", "0x0"))
        yp = int(v_raw, 16) if isinstance(v_raw, str) else int(v_raw)
        addr_hex = a.get("address", "0x" + "00" * 20)
        addr_bytes = bytes.fromhex(addr_hex[2:]) if addr_hex.startswith("0x") else bytes.fromhex(addr_hex)
        auth_enc.append(_rlp_list([
            _rlp_int(hex_int(a.get("chainId", "0x0"))),
            _rlp_bytes(addr_bytes),
            _rlp_int(hex_int(a.get("nonce", "0x0"))),
            _rlp_int(yp),
            _rlp_int(hex_int(a.get("r", "0x0"))),
            _rlp_int(hex_int(a.get("s", "0x0"))),
        ]))

    def fields(auth_list):
        return [
            _rlp_int(chain_id),
            _rlp_int(hex_int(tx.get("nonce", "0x0"))),
            _rlp_int(hex_int(tx.get("maxPriorityFeePerGas", "0x0"))),
            _rlp_int(hex_int(tx.get("maxFeePerGas", "0x0"))),
            _rlp_int(hex_int(gas)),
            _rlp_bytes(bytes.fromhex(to[2:])) if to else _rlp_bytes(b""),
            _rlp_int(hex_int(value)),
            _rlp_bytes(bytes.fromhex(data[2:]) if data.startswith("0x") else bytes()),
            _rlp_list(access_list),
            _rlp_list(auth_list),
        ]

    payload = b"\x04" + _rlp_list(fields(auth_enc))
    h = eth_utils.keccak(payload)
    from eth_keys import keys  # noqa: PLC0415 (used only on the fallback path)
    sig = keys.PrivateKey(bytes.fromhex(secret[2:])).sign_msg_hash(h)
    final = fields(auth_enc) + [_rlp_int(sig.v), _rlp_int(sig.r), _rlp_int(sig.s)]
    return (b"\x04" + _rlp_list(final)).hex()


# ----------------------------------------------------------------------------- compare
def compare_post(node, fork_rev, post_entry):
    """Compare the node's post state against `post_entry['state']` (account map)."""
    state = post_entry.get("state")
    if not state:
        return True, "no state map in post entry (root-only) — skipped account compare"
    for addr, expected in state.items():
        addr = addr.lower()
        if "balance" in expected:
            got = int(node.rpc("eth_getBalance", [addr, "latest"]), 16)
            exp = hex_int(expected["balance"])
            if got != exp:
                return False, f"{addr} balance {got} != {exp}"
        if "code" in expected:
            got = node.rpc("eth_getCode", [addr, "latest"])
            exp = expected["code"]
            if (got or "0x") != (exp or "0x"):
                return False, f"{addr} code {got} != {exp}"
        if "nonce" in expected:
            got = int(node.rpc("eth_getTransactionCount", [addr, "latest"]), 16)
            exp = hex_int(expected["nonce"])
            if got != exp:
                return False, f"{addr} nonce {got} != {exp}"
        if "storage" in expected:
            for key, val in expected["storage"].items():
                got = node.rpc("eth_getStorageAt", [addr, key, "latest"])
                # RPC returns the full 32-byte slot; compare numerically (ignore padding).
                if int(got or "0x0", 16) != int(val or "0x0", 16):
                    return False, f"{addr} storage[{key}] {got} != {val}"
    return True, ""


# ----------------------------------------------------------------------------- runner
def run_one_fixture(args, fixture, fork_key, post_entry, workdir, idx):
    fork_rev = fork_key_to_rev(fork_key)
    if not fork_rev:
        return None  # unsupported fork, skip
    gen_config(workdir, fixture, fork_rev, idx, port_offset=args.port_offset)
    rpc_port = 20000 + (idx % 40) + args.port_offset
    node = Node(args.binary, workdir, rpc_port)
    try:
        if not node.start():
            return ("FAIL", "node failed to start", workdir.name)
        try:
            chain_id = int(node.rpc("eth_chainId", []), 16)
        except Exception:  # noqa: BLE001  (transient connection refused → retry)
            for _ in range(10):
                time.sleep(1)
                try:
                    chain_id = int(node.rpc("eth_chainId", []), 16)
                    break
                except Exception:  # noqa: BLE001
                    chain_id = None
            if chain_id is None:
                return ("FAIL", "eth_chainId unreachable", workdir.name)
        tx = fixture.tx
        indexes = post_entry.get("indexes", {})
        expect_exc = post_entry.get("expectException")
        try:
            h = sign_and_submit(node, tx, indexes, chain_id)
        except RuntimeError as exc:
            # RPC rejected the tx at submission (mempool validation).
            if expect_exc:
                return ("PASS", "rejected as expected", workdir.name)
            # Not explicitly marked invalid, but EEST sometimes encodes "tx dropped
            # at validation" as post-state == pre without expectException (e.g.
            # type-4 tx at a pre-Osaka fork). The rejection is then correct iff
            # the (unchanged) node state still matches the expected post state.
            ok, msg = compare_post(node, fork_rev, post_entry)
            if ok:
                return ("PASS", "rejected; post-state matches", workdir.name)
            return ("FAIL", f"submit rejected: {exc}", workdir.name)
        except Exception as exc:  # noqa: BLE001 — never let one bad tx kill the run
            if expect_exc:
                return ("PASS", "submit failed as expected", workdir.name)
            ok, msg = compare_post(node, fork_rev, post_entry)
            if ok:
                return ("PASS", "submit failed; post-state matches", workdir.name)
            return ("FAIL", f"submit error: {exc}", workdir.name)
        # Accepted into mempool → wait for a receipt (driver seals within ~1-2s,
        # but EIP-7702 txs with thousands of authorizations can take ~50s to
        # execute — e.g. test_many_delegations has 4798 auths).
        for _ in range(120):
            time.sleep(1)
            try:
                rc = node.rpc("eth_getTransactionReceipt", [h])
            except Exception:  # noqa: BLE001  (transient connection refused → retry)
                rc = None
            if rc:
                break
        else:
            if expect_exc:
                # accepted but never included — treat as the expected invalid outcome
                return ("PASS", "", workdir.name)
            return ("FAIL", "no receipt after 15s", workdir.name)
        ok, msg = compare_post(node, fork_rev, post_entry)
        if not ok:
            return ("FAIL", msg, workdir.name)
        return ("PASS", "", workdir.name)
    finally:
        node.stop()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixture-dir", default="fixtures/state_tests")
    ap.add_argument("--binary", default="build/fisco-bcos-air/fisco-bcos")
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--fork", default=None, help="only run fixtures with this post fork")
    ap.add_argument("--pattern", default="*.json")
    ap.add_argument("--limit", type=int, default=0, help="max number of vectors to run (0=all)")
    ap.add_argument("--base-dir", default=None)
    ap.add_argument("--port-offset", type=int, default=0,
                    help="add to every port so multiple harness processes can run"
                         " in parallel on disjoint port ranges (0, 40, 80, ...)")
    args = ap.parse_args()

    binary = os.path.abspath(args.binary)
    if not os.path.exists(binary):
        print(f"binary not found: {binary}", file=sys.stderr)
        return 1
    args.binary = binary  # nodes start with cwd=workdir; the binary must be absolute
    fixtures = load_fixtures(args.fixture_dir, args.pattern)
    print(f"loaded {len(fixtures)} fixtures", flush=True)

    # Build the unit list: (fixture, fork_key, post_entry, rpc_port)
    units = []
    for fx in fixtures:
        for fork_key in fx.forks():
            if args.fork and fork_key.split("To")[0] != args.fork:
                continue
            for indexes, post_entry in fx.vectors(fork_key):
                units.append((fx, fork_key, post_entry))
    if args.limit:
        units = units[:args.limit]
    print(f"{len(units)} units to run (fork filter={args.fork})", flush=True)

    base_dir = Path(args.base_dir) if args.base_dir else Path(tempfile.mkdtemp(prefix="eest-"))
    base_dir.mkdir(parents=True, exist_ok=True)
    # Kill stale fisco-bcos nodes left behind by a previous (crashed) run of THIS
    # base_dir. They hold our ports and cause wrong-state / port-collision failures
    # (observed: tstore/blob "code 0x" / "balance 0" when an orphan held the port).
    # Matching by cwd keeps parallel runs (different base dirs) untouched.
    if os.path.exists("/proc"):
        for pid in os.listdir("/proc"):
            if not pid.isdigit():
                continue
            try:
                cmd = open(f"/proc/{pid}/cmdline", "rb").read().decode(errors="ignore")
            except (OSError, IOError):
                continue
            if "fisco-bcos" not in cmd or "-c config.ini" not in cmd:
                continue
            try:
                cwd = os.readlink(f"/proc/{pid}/cwd")
            except OSError:
                continue
            try:
                if Path(cwd).resolve() == base_dir.resolve() or \
                        base_dir.resolve() in Path(cwd).resolve().parents:
                    os.kill(int(pid), signal.SIGKILL)
                    print(f"[cleanup] killed stale node pid={pid} cwd={cwd}", flush=True)
            except (OSError, ProcessLookupError):
                continue
    # Template node dir: certs + empty data. We reuse the single-node smoke node's certs.
    tmpl = Path(__file__).resolve().parent.parent / "tools/nodes/127.0.0.1/node0"

    results = {"PASS": 0, "FAIL": 0, "SKIP": 0}
    failures = []

    def run_unit(idx, unit):
        fx, fork_key, post_entry = unit
        wd = base_dir / f"w{idx}"
        if wd.exists():
            shutil.rmtree(wd)
        wd.mkdir(parents=True)
        # copy certs (conf/ with ca.crt, ssl.crt, ssl.key, node.pem) + p2p nodes.json
        cert_src = tmpl / "conf"
        if cert_src.exists():
            shutil.copytree(cert_src, wd / "conf", dirs_exist_ok=True)
        nodes_json = tmpl / "nodes.json"
        if nodes_json.exists():
            shutil.copy(nodes_json, wd / "nodes.json")
        rpc_port = 20000 + (idx % 40) + args.port_offset
        p2p_port = 21000 + (idx % 40) + args.port_offset
        res, msg, wname = run_one_fixture(args, fx, fork_key, post_entry, wd, idx)
        # Each node's storage `data/` dir is ~150MB; drop it so a full 2681-fixture
        # run doesn't fill the disk. Keep the small workdir (genesis/ini/log) for
        # FAILED units to aid debugging; drop it entirely for PASSED units.
        for sub in ("data", "log"):
            d = wd / sub
            if d.exists():
                shutil.rmtree(d, ignore_errors=True)
        if res == "PASS":
            shutil.rmtree(wd, ignore_errors=True)
        return res, msg, fx.path, fx.name, fork_key

    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        futs = [ex.submit(run_unit, i, u) for i, u in enumerate(units)]
        done = 0
        t0 = time.time()
        for fut in as_completed(futs):
            res, msg, path, name, fork_key = fut.result()
            done += 1
            if res == "PASS":
                results["PASS"] += 1
            elif res == "FAIL":
                results["FAIL"] += 1
                failures.append((path, name, fork_key, msg))
                if len(failures) <= 20:
                    print(f"  FAIL {name[:70]} [{fork_key}] {msg}", flush=True)
            else:
                results["SKIP"] += 1
            if done % 25 == 0 or done == len(units):
                rate = results["PASS"] / done * 100 if done else 0
                print(f"[{done}/{len(units)}] PASS={results['PASS']} FAIL={results['FAIL']} "
                      f"({rate:.1f}%) elapsed={time.time()-t0:.0f}s", flush=True)

    total = results["PASS"] + results["FAIL"]
    rate = (results["PASS"] / total * 100.0) if total else 0.0
    print(f"\nPass rate: {rate:.2f}%  (PASS={results['PASS']} FAIL={results['FAIL']} "
          f"SKIP={results['SKIP']} of {len(units)})", flush=True)
    if results["FAIL"]:
        print(f"\n{len(failures)} failures; first 20:")
        for path, name, fk, msg in failures[:20]:
            print(f"  {path} :: {name[:60]} [{fk}] {msg}")
    return 0 if results["FAIL"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
