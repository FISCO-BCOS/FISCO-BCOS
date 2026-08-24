#!/usr/bin/env python3
"""B.3 contract semantics: deploy a minimal storage contract and a REVERT contract via
eth_sendRawTransaction, then assert eth_getCode == runtime and eth_call behaviour
(storage contract succeeds, REVERT contract fails). Spec §4 B.3 items 2-3.
"""
import json
import os
import subprocess
import sys
import time
import urllib.request

SIGN_SECP = os.environ.get("SIGN_SECP", "/tmp/op-spike/sign_secp")
SENDER = "0x6afa9580383E6627dA926B6f6ed9Ab2B9c8cC693"
PRIVKEY = "cdf753782bdb981198eab72e09b6c0ad780a9858ea4f3a8fe8b257016e2e0e29"
CHAIN_ID = 11155111

PASSED, FAILED = [], []


def check(name, cond, detail=""):
    if cond:
        PASSED.append(name)
        print(f"  PASS {name}")
    else:
        FAILED.append(name)
        print(f"  FAIL {name} {detail}")


_RC = [0x0000000000000001, 0x0000000000008082, 0x800000000000808A, 0x8000000080008000,
       0x000000000000808B, 0x0000000080000001, 0x8000000080008081, 0x8000000000008009,
       0x000000000000008A, 0x0000000000000088, 0x0000000080008009, 0x000000008000000A,
       0x000000008000808B, 0x800000000000008B, 0x8000000000008089, 0x8000000000008003,
       0x8000000000008002, 0x8000000000000080, 0x000000000000800A, 0x800000008000000A,
       0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008]
_ROT = [1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44]


def _rotl64(x, n):
    return ((x << n) | (x >> (64 - n))) & 0xFFFFFFFFFFFFFFFF


def _keccak_f(state):
    for rc in _RC:
        c = [state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20] for x in range(5)]
        d = [c[(x - 1) % 5] ^ _rotl64(c[(x + 1) % 5], 1) for x in range(5)]
        for x in range(5):
            for y in range(5):
                state[x + 5 * y] ^= d[x]
        b = [0] * 25
        b[0] = state[0]
        x, y = 1, 0
        for t in range(24):
            b[y + 5 * ((2 * x + 3 * y) % 5)] = _rotl64(state[x + 5 * y], _ROT[t])
            x, y = y, (2 * x + 3 * y) % 5
        for y in range(5):
            row = [b[x + 5 * y] for x in range(5)]
            for x in range(5):
                b[x + 5 * y] = row[x] ^ ((~row[(x + 1) % 5]) & row[(x + 2) % 5])
        b[0] ^= rc
        state = b
    return state


def keccak256(data):
    rate = 136
    padded = bytearray(data)
    padded.append(0x01)
    while len(padded) % rate != rate - 1:
        padded.append(0x00)
    padded.append(0x80)
    state = [0] * 25
    for off in range(0, len(padded), rate):
        for i in range(rate // 8):
            state[i] ^= int.from_bytes(padded[off + i * 8:off + i * 8 + 8], 'little')
        state = _keccak_f(state)
    return b''.join(state[i].to_bytes(8, 'little') for i in range(4))


def rlp_encode(item):
    if isinstance(item, bytes):
        if len(item) == 1 and item[0] < 0x80:
            return item
        return bytes([0x80 + len(item)]) + item if len(item) < 56 else \
            bytes([0xB7 + (len(item).bit_length() + 7) // 8]) + \
            len(item).to_bytes((len(item).bit_length() + 7) // 8, 'big') + item
    if isinstance(item, list):
        body = b''.join(rlp_encode(i) for i in item)
        return bytes([0xC0 + len(body)]) + body if len(body) < 56 else \
            bytes([0xF7 + (len(body).bit_length() + 7) // 8]) + \
            len(body).to_bytes((len(body).bit_length() + 7) // 8, 'big') + body


def to_bytes_min(x):
    return x.to_bytes(max(1, (x.bit_length() + 7) // 8), 'big') if x else b''


def make_deploy_tx(privkey, nonce, data, gas=200_000):
    fields = [to_bytes_min(CHAIN_ID), to_bytes_min(nonce), to_bytes_min(1_000_000_000),
              to_bytes_min(1_000_000_000), to_bytes_min(gas), b'', to_bytes_min(0), data, []]
    msg_hash = keccak256(b'\x02' + rlp_encode(fields))
    out = subprocess.run([SIGN_SECP, privkey, msg_hash.hex()], capture_output=True, text=True).stdout
    lines = out.strip().split('\n')
    r = int(lines[1], 16)
    s = int(lines[2].split('=')[1], 16)
    recid = int(lines[3].split('=')[1])
    full = fields + [to_bytes_min(recid & 1), to_bytes_min(r), to_bytes_min(s)]
    return '0x' + (b'\x02' + rlp_encode(full)).hex()


class Rpc:
    def __init__(self, port=None):
        self._opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.url = f"http://127.0.0.1:{port or os.environ.get('B3_ETH_PORT', 8553)}"

    def call(self, m, p=None):
        body = json.dumps({"jsonrpc": "2.0", "method": m, "params": p or [], "id": 1}).encode()
        req = urllib.request.Request(self.url, data=body, headers={"Content-Type": "application/json"})
        with self._opener.open(req, timeout=10) as r:
            out = json.load(r)
        if "error" in out:
            raise AssertionError(f"{m} error: {out['error']}")
        return out.get("result")


def deploy(rpc, nonce, init_code):
    raw = make_deploy_tx(PRIVKEY, nonce, bytes.fromhex(init_code))
    tx_hash = rpc.call("eth_sendRawTransaction", [raw])
    receipt = None
    for _ in range(60):
        receipt = rpc.call("eth_getTransactionReceipt", [tx_hash])
        if receipt:
            break
        time.sleep(1)
    return tx_hash, receipt


def create_address(sender, nonce):
    """Ethereum CREATE address = keccak256(rlp([sender, nonce]))[12:]. The OP receipt does not
    carry contractAddress (known gap), so derive it for getCode/eth_call here."""
    sender_bytes = bytes.fromhex(sender[2:])
    return "0x" + keccak256(rlp_encode([sender_bytes, to_bytes_min(nonce)]))[12:].hex()


def main():
    rpc = Rpc()
    nonce = int(rpc.call("eth_getTransactionCount", [SENDER, "latest"]), 16)
    print(f"sender nonce: {nonce}")

    # Minimal storage contract: runtime = PUSH1 2 PUSH1 0 SSTORE (writes slot0=2, no revert).
    # init = CODECOPY(runtime) + RETURN. runtime sits at offset 12 (11-byte init prefix).
    store_runtime = "6002600055"
    store_init = "6005600c60003960056000f3" + store_runtime
    # Revert contract: runtime = PUSH1 0 PUSH1 0 REVERT (5 bytes, always reverts), offset 12.
    rev_runtime = "60006000fd"
    rev_init = "6005600c60003960056000f3" + rev_runtime

    th, rec = deploy(rpc, nonce, store_init)
    check("storage deploy receipt", rec is not None)
    if rec:
        check("storage deploy status=0x1", rec["status"] == "0x1", str(rec["status"]))
        # contractAddress now populated by the OP receipt (evmc create_address); verify it equals
        # the CREATE derivation keccak(rlp([sender, nonce]))[12:].
        addr = rec.get("contractAddress")
        check("receipt contractAddress present", isinstance(addr, str) and addr.startswith("0x"),
              str(addr))
        if isinstance(addr, str) and addr.startswith("0x"):
            check("contractAddress == CREATE derived", addr.lower() == create_address(SENDER, nonce),
                  f"{addr} vs {create_address(SENDER, nonce)}")
            code = rpc.call("eth_getCode", [addr, "latest"])
            check("getCode == runtime", code == "0x" + store_runtime, f"{code} vs 0x{store_runtime}")
        try:
            out = rpc.call("eth_call", [{"to": addr, "data": "0x"}, "latest"])
            check("eth_call storage contract succeeds", True, str(out))
        except AssertionError as e:
            check("eth_call storage contract succeeds", False, str(e))

    th2, rec2 = deploy(rpc, nonce + 1, rev_init)
    check("revert deploy receipt", rec2 is not None)
    if rec2:
        check("revert deploy status=0x1", rec2["status"] == "0x1", str(rec2["status"]))
        addr2 = rec2.get("contractAddress")
        check("revert contractAddress present", isinstance(addr2, str) and addr2.startswith("0x"),
              str(addr2))
        if isinstance(addr2, str) and addr2.startswith("0x"):
            check("revert contractAddress == CREATE derived",
                  addr2.lower() == create_address(SENDER, nonce + 1),
                  f"{addr2} vs {create_address(SENDER, nonce + 1)}")
            code2 = rpc.call("eth_getCode", [addr2, "latest"])
        check("revert getCode == runtime", code2 == "0x" + rev_runtime, f"{code2} vs 0x{rev_runtime}")
        # Known limitation (recorded): OP mode has no historical-state snapshot (no MPT /
        # checkpoint), so SchedulerInterface::callAtBlock defaults to call() == latest — a
        # historical blockTag does NOT honor block-N state. The revert path is asserted at
        # latest only; blockTag history honoring is a documented gap (spec A.2 partial).
        try:
            rpc.call("eth_call", [{"to": addr2, "data": "0x"}, "latest"])
            check("eth_call REVERT contract fails (latest)", False, "expected revert/error")
        except AssertionError as e:
            check("eth_call REVERT contract fails (latest)", True, str(e)[:70])

    print(f"\n{len(PASSED)} passed, {len(FAILED)} failed")
    if FAILED:
        print("Failed:", FAILED)
        sys.exit(1)


if __name__ == "__main__":
    main()
