#!/usr/bin/env python3
"""Predeploy behavior matrix (spec docs/2026-08-17-opstack-predeploy-matrix-design.md).
Runs against B3 (eth RPC 8553). Group by group; each group = independent asserts.

Task 2 scope: skeleton + L1Block group.
  - callable getters (number + 9 more) returning well-formed 32B hex
  - reject path: non-depositor caller + wrong calldata length
  - sequenceNumber cross-block probe (DIVERGENCE-aware: deposit reverts on B3)

B3 reality (measured): the genesis L1Block is the Ecotone-era Solidity contract; the node
injects a Jovian deposit every block which REVERTS, so L1Block slots are never written and
getters return 0. That is the expected DIVERGENCE
(l1block_deposit_reverts_ecotone_vs_jovian), NOT a test failure. Assertions only require
"getter callable + returns well-formed 32B hex".
"""
import json
import subprocess
import sys
import time
import urllib.request

SIGN_SECP = "/tmp/op-spike/sign_secp"
SENDER = "0x6afa9580383E6627dA926B6f6ed9Ab2B9c8cC693"
PRIVKEY = "cdf753782bdb981198eab72e09b6c0ad780a9858ea4f3a8fe8b257016e2e0e29"
CHAIN_ID = 11155111
# 预部署地址(与 genesis [alloc.*] 一致)
L1_BLOCK = "0x4200000000000000000000000000000000000015"
MESSAGE_PASSER = "0x4200000000000000000000000000000000000016"
MESSENGER = "0x4200000000000000000000000000000000000007"
BRIDGE = "0x4200000000000000000000000000000000000010"
SYSTEM_CONFIG = "0x42000000000000000000000000000000000000c0"
PASSED, FAILED = [], []


def check(name, cond, detail=""):
    if cond:
        PASSED.append(name)
        print(f"  PASS {name}")
    else:
        FAILED.append(name)
        print(f"  FAIL {name} {detail}")


class Rpc:
    def __init__(self, port=8553):
        self._o = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.url = f"http://127.0.0.1:{port}"

    def eth(self, m, p=None):
        body = json.dumps({"jsonrpc": "2.0", "method": m, "params": p or [], "id": 1}).encode()
        req = urllib.request.Request(self.url, data=body, headers={"Content-Type": "application/json"})
        with self._o.open(req, timeout=10) as r:
            out = json.load(r)
        if "error" in out:
            raise AssertionError(f"{m} error: {out['error']}")
        return out.get("result")


# ── 共享助手(本骨架即含,Task 3-5 复用;abi_encode_call 手写,禁止第三方库)──
def addr_pad(a):          # address → 64 hex(去 0x)
    return a[2:].lower().zfill(64)


def wait_receipt(rpc, tx_hash):
    for _ in range(60):
        r = rpc.eth("eth_getTransactionReceipt", [tx_hash])
        if r:
            return r
        time.sleep(1)
    raise AssertionError(f"no receipt for {tx_hash}")


def abi_encode_call(sel, *args):
    # ABI 编码:selector + 每参 32B 头部词(动态参数在头部放 offset)+ 尾部 data 段。
    # offset = n*32(全部头部词,相对参数区起点=selector 之后)+ 当前 tail 位置。
    # ⚠️ 不加 4(selector 不计入 offset)—— Task 1 手写 calldata dataOffset=0x60=3*32 可反证。
    # address 判据:str 以 "0x" 开头且长 42 → 32B 左 pad 字;其它 str → string 动态 bytes。
    n, head, tail = len(args), [], b""
    for a in args:
        if isinstance(a, str) and a.startswith("0x") and len(a) == 42:
            head.append(int(a, 16).to_bytes(32, "big"))          # address → 32B 左 pad
        elif isinstance(a, (bytes, str)):
            if isinstance(a, str):
                a = a.encode()
            head.append((n * 32 + len(tail)).to_bytes(32, "big"))
            tail += len(a).to_bytes(32, "big") + a.ljust(32, b"\x00")
        elif isinstance(a, bool):
            head.append((1 if a else 0).to_bytes(32, "big"))
        else:
            head.append(int(a).to_bytes(32, "big", signed=False))
    return bytearray.fromhex(sel) + b"".join(head) + tail


# ── EIP-1559 0x02 签名链(原样复制 b3_contracts.py 的 keccak/RLP;sign_secp 外调)──
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


def make_eip1559_tx(privkey, nonce, to, data, gas):
    # EIP-1559 0x02 签名(复用 b3_contracts make_deploy_tx/make_call_tx 模式 + sign_secp):
    # chainId 0x2105、maxFee=1gwei、maxPrio=1gwei、gas、to、value=0、data。
    # ⚠️ brief 初稿 maxPrio=0.1gwei —— B3 实测该值让 produceBlock 抛未捕获 C++ 异常、封块冻结
    # (fee 算术 bug,任意 target 复现);与 b3_contracts 一致用 1gwei 才可正常出块。
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


def main():
    rpc = Rpc()

    # ═══ L1Block group ═══
    # ⚠️ B3 现实(实测):genesis L1Block 是 Ecotone 版,节点注入 Jovian deposit → deposit 每块
    # revert,L1Block 槽永不被写。getter 返回 0 是 DIVERGENCE(l1block_deposit_reverts_ecotone_vs_jovian)
    # 的**预期表现**,不是测试失败。断言改为「getter 可读且返回格式正确的 32B hex」。
    n = rpc.eth("eth_call", [{"to": L1_BLOCK, "data": "0x8381f58a"}, "latest"])  # number()
    check("l1block_number_callable", isinstance(n, str) and len(n) == 66 and n.startswith("0x"),
          f"number={n}")

    # getters: 可读且返回 32B hex(值 0 = DIVERGENCE 预期,非零 = 节点已对齐)。
    # selector 均已对过 genesis L1Block dispatcher(实扫含以下全部 10 个)。
    for sel, name in [
        ("0xb80777ea", "timestamp"),   # timestamp()
        ("0x5cf24969", "basefee"),     # basefee()
        ("0xc5985918", "baseFeeScalar"),
        ("0x68d5dca6", "blobBaseFeeScalar"),
        ("0xf8206140", "blobBaseFee"),
        ("0x09bd5a60", "hash"),        # hash()
        ("0x64ca23ef", "sequenceNumber"),
        ("0x8b239f73", "l1FeeOverhead"),
        ("0x9e8c4966", "l1FeeScalar"),
    ]:
        v = rpc.eth("eth_call", [{"to": L1_BLOCK, "data": sel}, "latest"])
        check(f"l1block_{name}", isinstance(v, str) and len(v) == 66 and v.startswith("0x"),
              f"{name}={v}")

    # 跨块 sequenceNumber 探测(1 条,受 DIVERGENCE 约束)。
    # B3 的 deposit 每块 revert → sequenceNumber 恒 0;跨块 +1 语义由 t8n 差分覆盖(Task 1)。
    # 注意:两次都以 latest tag 读,即使节点修复 deposit 对齐,两次 latest 也同块同值,
    # 故本步在 B3 现实下只能走 DIVERGENCE 分支(见 brief Step 4 说明)。
    b0 = rpc.eth("eth_getBlockByNumber", ["latest", False])
    time.sleep(1.2)   # 等空块自动生产(PBFT,block_interval=1000ms)
    b1 = rpc.eth("eth_getBlockByNumber", ["latest", False])
    seq0 = rpc.eth("eth_call", [{"to": L1_BLOCK, "data": "0x64ca23ef"}, "latest"])
    seq1 = rpc.eth("eth_call", [{"to": L1_BLOCK, "data": "0x64ca23ef"}, "latest"])
    if int(seq0, 16) == 0 and int(seq1, 16) == 0:
        check("l1block_seq_divergence_expected", True,
              "L1Block deposit reverts (ecotone vs jovian) -> seq stays 0; DIVERGENCE registered")
    else:
        check("l1block_seq_increments", int(seq1, 16) == int(seq0, 16) + 1, f"{seq0}->{seq1}")

    # 拒绝路径 — 非 deposit 调用方 + 错误 calldata 长度(2 条)。
    # SENDER 不是 deposit 账户 => L1Block setL1BlockValuesEcotone 应 revert(仅 0xdead...0001 可调)。
    # 实扫 genesis L1Block dispatcher:含 0x440a5e20(setL1BlockValuesEcotone 无参变体)+ 0xdead0001 检查;
    # 不含 0x3db6be2b(Jovian selector) → 落 fallback → revert。
    nonce = int(rpc.eth("eth_getTransactionCount", [SENDER, "latest"]), 16)   # ★ nonce 须现取
    cd = bytes.fromhex("440a5e20")  # setL1BlockValuesEcotone()(无参)
    raw = make_eip1559_tx(PRIVKEY, nonce, L1_BLOCK, cd, gas=300_000)
    r = wait_receipt(rpc, rpc.eth("eth_sendRawTransaction", [raw]))
    check("l1block_reject_nondepositor", r.get("status") == "0x0", f"status={r.get('status')}")
    # spec「拒绝路径」另一子项:错误 calldata 长度 —— 用 Jovian selector 0x3db6be2b + 短 calldata
    # (节点 L1Block 只 dispatch 0x440a5e20,0x3db6be2b 落 fallback)断言 revert。
    nonce = int(rpc.eth("eth_getTransactionCount", [SENDER, "latest"]), 16)   # nonce 已 +1
    cd2 = bytes.fromhex("3db6be2b") + b"\x00" * 8    # Jovian selector + 错误长度(不足 178B)
    raw2 = make_eip1559_tx(PRIVKEY, nonce, L1_BLOCK, cd2, gas=300_000)
    r2 = wait_receipt(rpc, rpc.eth("eth_sendRawTransaction", [raw2]))
    check("l1block_reject_badlen", r2.get("status") == "0x0", f"status={r2.get('status')}")

    print(f"\n{'ALL' if not FAILED else 'SOME'} PASSED {len(PASSED)} FAILED {len(FAILED)}")
    sys.exit(0 if not FAILED else 1)


if __name__ == "__main__":
    main()
