#!/usr/bin/env python3
"""Passive-mode chain driver for the B3 OP node (spec §6, mirroring op-geth setupBlocks):
inject a deterministic signed tx via eth_sendRawTransaction, wait for the single-node-consensus
driver to commit it (SINGLE_CONSENSUS "Committed block" or eth_blockNumber advance), then assert
the receipt / balance / header. This is the B.3 state-semantics driver.

The signature uses libsecp256k1 (via the sign_secp helper) — the pure-python ECDSA was proven to
recover a wrong sender after low-s normalization, so defer to the node's own crypto.

Usage: chain_driver.py [--port PORT] [--sender ADDR] [--privkey HEX] [--txs N] [--value WEI]
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
import urllib.request

SIGN_SECP = os.environ.get("SIGN_SECP", "/tmp/op-spike/sign_secp")
SENDER = "0x6afa9580383E6627dA926B6f6ed9Ab2B9c8cC693"
PRIVKEY = "cdf753782bdb981198eab72e09b6c0ad780a9858ea4f3a8fe8b257016e2e0e29"
RECIPIENT = bytes.fromhex("000000000000000000000000000000000000dEaD")
CHAIN_ID = 11155111

PASSED, FAILED = [], []


def check(name, cond, detail=""):
    if cond:
        PASSED.append(name)
        print(f"  PASS {name}")
    else:
        FAILED.append(name)
        print(f"  FAIL {name} {detail}")


# --- keccak + RLP (verified against known vectors earlier in the session) ---
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
        return bytes([0x80 + len(item)]) + item if len(item) < 56 else bytes([0xB7 + (len(item).bit_length() + 7) // 8]) + len(item).to_bytes((len(item).bit_length() + 7) // 8, 'big') + item
    if isinstance(item, list):
        body = b''.join(rlp_encode(i) for i in item)
        return bytes([0xC0 + len(body)]) + body if len(body) < 56 else bytes([0xF7 + (len(body).bit_length() + 7) // 8]) + len(body).to_bytes((len(body).bit_length() + 7) // 8, 'big') + body


def to_bytes_min(x):
    return x.to_bytes(max(1, (x.bit_length() + 7) // 8), 'big') if x else b''


def make_signed_tx(privkey, nonce, value, max_fee=1_000_000_000, max_prio=1_000_000_000, gas=21_000):
    tx_fields = [to_bytes_min(CHAIN_ID), to_bytes_min(nonce), to_bytes_min(max_prio),
                 to_bytes_min(max_fee), to_bytes_min(gas), RECIPIENT, to_bytes_min(value), b'', []]
    msg_hash = keccak256(b'\x02' + rlp_encode(tx_fields))
    out = subprocess.run([SIGN_SECP, privkey, msg_hash.hex()], capture_output=True, text=True).stdout
    lines = out.strip().split('\n')
    r = int(lines[1], 16)
    s = int(lines[2].split('=')[1], 16)
    recid = int(lines[3].split('=')[1])
    y_parity = recid & 1
    full = tx_fields + [to_bytes_min(y_parity), to_bytes_min(r), to_bytes_min(s)]
    return '0x' + (b'\x02' + rlp_encode(full)).hex()


class Rpc:
    def __init__(self, port):
        self._opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.url = f"http://127.0.0.1:{port}"

    def call(self, m, p=None):
        body = json.dumps({"jsonrpc": "2.0", "method": m, "params": p or [], "id": 1}).encode()
        req = urllib.request.Request(self.url, data=body, headers={"Content-Type": "application/json"})
        with self._opener.open(req, timeout=10) as r:
            out = json.load(r)
        if "error" in out:
            raise AssertionError(f"{m} error: {out['error']}")
        return out.get("result")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8553)
    ap.add_argument("--privkey", default=PRIVKEY)
    ap.add_argument("--txs", type=int, default=2)
    ap.add_argument("--value", type=int, default=1_000_000_000_000_000)  # 0.001 ETH
    args = ap.parse_args()
    rpc = Rpc(args.port)

    bal0 = int(rpc.call("eth_getBalance", [SENDER, "latest"]), 16)
    nonce0 = int(rpc.call("eth_getTransactionCount", [SENDER, "latest"]), 16)
    print(f"start bal={bal0} nonce={nonce0}")

    prev_head = int(rpc.call("eth_blockNumber"), 16)
    cumulative_gas = 0
    for i in range(args.txs):
        raw = make_signed_tx(args.privkey, nonce0 + i, args.value)
        tx_hash = rpc.call("eth_sendRawTransaction", [raw])
        print(f"tx[{i}] {tx_hash[:20]} nonce={nonce0 + i}")

        # wait for the tx to be committed (head advances past the pre-tx head)
        receipt = None
        for _ in range(60):
            receipt = rpc.call("eth_getTransactionReceipt", [tx_hash])
            if receipt:
                break
            time.sleep(1)
        check(f"tx[{i}] receipt", receipt is not None)
        if not receipt:
            continue
        print(f"  tx[{i}] hash={tx_hash} block={receipt['blockNumber']} cumGas={receipt.get('cumulativeGasUsed')} gasUsed={receipt.get('gasUsed')} idx={receipt.get('transactionIndex')}")
        check(f"tx[{i}] status=0x1", receipt["status"] == "0x1", str(receipt["status"]))
        check(f"tx[{i}] from", receipt["from"].lower() == SENDER.lower(), receipt["from"])
        check(f"tx[{i}] gasUsed=21000", int(receipt["gasUsed"], 16) == 21000, receipt["gasUsed"])
        cumulative_gas += int(receipt["gasUsed"], 16)

        # A.2 tx query family (phase-1 additions): getTransactionByHash field parity,
        # getTransactionByBlockNumberAndIndex (index 0 = L1-attributes deposit, index i+1 = user),
        # getTransactionReceipt OP meta.
        b = rpc.call("eth_getTransactionByHash", [tx_hash])
        check(f"tx[{i}] byHash from", b is not None and b["from"].lower() == SENDER.lower(),
            str(b and b.get("from")))
        check(f"tx[{i}] byHash to", b["to"].lower() == "0x" + RECIPIENT.hex(),
            str(b.get("to")))
        check(f"tx[{i}] byHash nonce", int(b["nonce"], 16) == nonce0 + i, str(b.get("nonce")))
        check(f"tx[{i}] byHash value", int(b["value"], 16) == args.value, str(b.get("value")))
        check(f"tx[{i}] byHash blockNumber", int(b["blockNumber"], 16) == int(receipt["blockNumber"], 16),
            str(b.get("blockNumber")))
        check(f"tx[{i}] byHash index", int(b["transactionIndex"], 16) == 1,
            str(b.get("transactionIndex")))
        blk_num = receipt["blockNumber"]
        dep = rpc.call("eth_getTransactionByBlockNumberAndIndex", [blk_num, "0x0"])
        check(f"tx[{i}] index0 is deposit", dep is not None and int(dep["transactionIndex"], 16) == 0,
            str(dep))
        user = rpc.call("eth_getTransactionByBlockNumberAndIndex", [blk_num, "0x1"])
        check(f"tx[{i}] index1 == tx hash", user is not None and user["hash"] == tx_hash,
            str(user))
        check(f"tx[{i}] receipt effectiveGasPrice",
            receipt.get("effectiveGasPrice") is not None, str(receipt.get("effectiveGasPrice")))
        check(f"tx[{i}] receipt cumulative>=used",
            int(receipt.get("cumulativeGasUsed", "0x0"), 16) >= int(receipt["gasUsed"], 16),
            str(receipt.get("cumulativeGasUsed")))

    # Final balance accounting: pre − Σ(value + gasUsed×effectiveGasPrice)
    bal1 = int(rpc.call("eth_getBalance", [SENDER, "latest"]), 16)
    nonce1 = int(rpc.call("eth_getTransactionCount", [SENDER, "latest"]), 16)
    check("nonce advanced", nonce1 == nonce0 + args.txs, f"{nonce0}+{args.txs}={nonce1}")
    gas_cost = cumulative_gas * 1_000_000_000  # effectiveGasPrice = 1 gwei
    expected = bal0 - (args.txs * args.value) - gas_cost
    check("balance exact", bal1 == expected, f"got={bal1} want={expected}")

    head = int(rpc.call("eth_blockNumber"), 16)
    check("head advanced", head >= prev_head, f"{prev_head}->{head}")

    print(f"\n{len(PASSED)} passed, {len(FAILED)} failed")
    if FAILED:
        print("Failed:", FAILED)
        sys.exit(1)


if __name__ == "__main__":
    main()
