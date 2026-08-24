#!/usr/bin/env python3
"""Decisive probe: send a NORMAL signed tx (opTransition path, NOT the deposit path) to the
L1Block predeploy with setL1BlockValues calldata, seal it via FCU, then read slot1/3/7/8.

If slot1 changes -> the code executes fine via the normal path; the deposit path (runDeposit)
is where the code is not loaded / diff not applied.
If slot1 unchanged -> the code is not executable in block execution at all (state-view gap).

Reuses the b3_contracts signing (libsecp256k1, keccak, EIP-1559 0x02).
"""
import json
import subprocess
import sys
import os
import time
import urllib.request

SIGN_SECP = os.environ.get("SIGN_SECP", "/tmp/op-spike/sign_secp")
SENDER = "0x6afa9580383E6627dA926B6f6ed9Ab2B9c8cC693"
PRIVKEY = "cdf753782bdb981198eab72e09b6c0ad780a9858ea4f3a8fe8b257016e2e0e29"
CHAIN_ID = 11155111
L1_BLOCK = "0x4200000000000000000000000000000000000015"

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


def make_call_tx(privkey, nonce, to, data, gas=500_000):
    fields = [to_bytes_min(CHAIN_ID), to_bytes_min(nonce), to_bytes_min(1_000_000_000),
              to_bytes_min(1_000_000_000), to_bytes_min(gas), bytes.fromhex(to[2:]),
              to_bytes_min(0), data, []]
    msg_hash = keccak256(b'\x02' + rlp_encode(fields))
    out = subprocess.run([SIGN_SECP, privkey, msg_hash.hex()], capture_output=True, text=True).stdout
    lines = out.strip().split('\n')
    r = int(lines[1], 16)
    s = int(lines[2].split('=')[1], 16)
    recid = int(lines[3].split('=')[1])
    full = fields + [to_bytes_min(recid & 1), to_bytes_min(r), to_bytes_min(s)]
    return '0x' + (b'\x02' + rlp_encode(full)).hex()


def build_jovian_calldata(l1_base_fee=0xabc, base_fee_scalar=0x100, blob_scalar=0x200,
                          blob_base_fee=0xdef, da=0x3, op_fee_scalar=0x4, op_fee_const=0x5):
    """Jovian setL1BlockValues layout (178B, selector 0x3db6be2b):
    [0:4]   selector
    [4:8]   baseFeeScalar
    [8:12]  blobBaseFeeScalar
    [12:36] unknown1 (20B)
    [36:68] l1BaseFee (32B)
    [68:100] l1BlobBaseFee (32B)
    [100:124] unknown2 (24B)
    [124:128] l2BaseFee? (4B)
    [128:160] l1FeeScalars (32B)
    [160:164] opFeeScalar? per gen_l1block offset
    [164:168] ... (x5 = CALLDATALOAD(160), opFeeScalar=(x5>>192)&0xffffffff -> calldata[164:168])
    [168:176] opFeeConstant = (x5>>128)&0xffffffffffffffff -> calldata[168:176]
    [176:178] da footprint (2B)
    """
    cd = bytearray(178)
    cd[0:4] = bytes.fromhex('3db6be2b')
    cd[4:8] = base_fee_scalar.to_bytes(4, 'big')
    cd[8:12] = blob_scalar.to_bytes(4, 'big')
    cd[36:68] = l1_base_fee.to_bytes(32, 'big')
    cd[68:100] = blob_base_fee.to_bytes(32, 'big')
    # opFeeScalar at [164:168] => bytes 160..168 contain (scalar<<... ). Put scalar at [164:168].
    cd[164:168] = op_fee_scalar.to_bytes(4, 'big')
    cd[168:176] = op_fee_const.to_bytes(8, 'big')
    cd[176:178] = da.to_bytes(2, 'big')
    return bytes(cd)


class Rpc:
    def __init__(self, port=8563, engine_port=8564,
            jwt=os.environ.get("B3A_JWT", "/tmp/op-spike/b3a/jwt.hex")):
        self._opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.url = f"http://127.0.0.1:{port}"
        self.eng_url = f"http://127.0.0.1:{engine_port}"
        self._jwt = open(jwt).read().strip()
        self._nonce = {}

    def call(self, url, m, p=None, auth=False):
        headers = {"Content-Type": "application/json"}
        if auth:
            import base64, hashlib, hmac
            def b64url(d):
                return base64.urlsafe_b64encode(d).rstrip(b"=").decode()
            hd = b64url(json.dumps({"alg": "HS256", "typ": "JWT"}).encode())
            pl = b64url(json.dumps({"iat": int(time.time())}).encode())
            sig = b64url(hmac.new(bytes.fromhex(self._jwt), f"{hd}.{pl}".encode(), hashlib.sha256).digest())
            headers["Authorization"] = f"Bearer {hd}.{pl}.{sig}"
        body = json.dumps({"jsonrpc": "2.0", "method": m, "params": p or [], "id": 1}).encode()
        req = urllib.request.Request(url, data=body, headers=headers)
        with self._opener.open(req, timeout=15) as r:
            out = json.load(r)
        if "error" in out:
            raise AssertionError(f"{m} RPC error: {out['error']}")
        return out.get("result")

    def eth(self, m, p=None):
        return self.call(self.url, m, p)

    def eng(self, m, p=None):
        return self.call(self.eng_url, m, p, auth=True)


def fcu_seal(rpc, want_head_after):
    """FCU V4 (attrs) -> getPayload -> newPayload. Returns after the head reaches want_head_after."""
    head = rpc.eth("eth_getBlockByNumber", ["latest", False])
    fcs = {"headBlockHash": head["hash"], "safeBlockHash": head["hash"],
           "finalizedBlockHash": head["hash"]}
    attrs = {"timestamp": hex(int(time.time())), "prevRandao": "0x" + "00" * 32,
             "suggestedFeeRecipient": "0x4200000000000000000000000000000000000011",
             "gasLimit": hex(int(head["gasLimit"], 16)),
             "eip1559Params": "0x0000000800000002",
             "withdrawals": [],
             "parentBeaconBlockRoot": "0x" + "00" * 32,
             "minBaseFee": "0x0"}  # B3a/C2 均为 Jovian 链；Isthmus 链删此行
    fc = rpc.eng("engine_forkchoiceUpdatedV4", [fcs, attrs])
    pid = fc["payloadId"]
    pl = rpc.eng("engine_getPayloadV4", [pid])
    ep = pl["executionPayload"]
    root = "0x" + "00" * 32
    np = rpc.eng("engine_newPayloadV4", [ep, [], root])
    assert np.get("status") == "VALID", f"newPayload not VALID: {np}"
    # move head
    ep_hash = ep["blockHash"]
    fcs2 = {"headBlockHash": ep_hash, "safeBlockHash": ep_hash, "finalizedBlockHash": ep_hash}
    fc2 = rpc.eng("engine_forkchoiceUpdatedV4", [fcs2, None])
    assert fc2["payloadStatus"]["status"] == "VALID", f"final FCU not VALID: {fc2}"
    return ep


def main():
    rpc = Rpc()
    nonce = int(rpc.eth("eth_getTransactionCount", [SENDER, "latest"]), 16)
    cd = build_jovian_calldata()
    print(f"nonce={nonce} calldata len={len(cd)} selector={cd[:4].hex()}")
    raw = make_call_tx(PRIVKEY, nonce, L1_BLOCK, cd)
    tx_hash = rpc.eth("eth_sendRawTransaction", [raw])
    print("tx hash:", tx_hash)
    time.sleep(2)
    ep = fcu_seal(rpc, 0)
    print("sealed block:", ep.get("blockNumber"), "txs:", len(ep.get("transactions", [])))

    time.sleep(2)
    for slot in [1, 3, 7, 8]:
        v = rpc.eth("eth_getStorageAt", [L1_BLOCK, "0x" + format(slot, "064x"), "latest"])
        print(f"slot{slot}: {v}")

    r = rpc.eth("eth_getTransactionReceipt", [tx_hash])
    if r:
        print("tx receipt status:", r.get("status"), "gasUsed:", r.get("gasUsed"))
    else:
        print("tx receipt: NOT FOUND")


if __name__ == "__main__":
    main()
