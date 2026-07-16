#!/usr/bin/env python3
# gen_7702_vectors.py — 生成 EIP-7702 授权签名金值（写入测试注释来源）
# 运行: python3 bcos-evm/test/opstack/scripts/gen_7702_vectors.py
# 依赖: pip install eth-account rlp eth-utils
#
# 签名哈希公式: keccak256(0x05 || rlp([chain_id, address, nonce]))
# 参考 op-geth tx_setcode.go SigHash: prefixedRlpHash(0x05, [ChainID, Address, Nonce])

from eth_utils import keccak
import rlp
from rlp.sedes import big_endian_int, Binary


def auth_hash(chain_id: int, address: bytes, nonce: int) -> bytes:
    """Compute EIP-7702 authorization signing hash."""
    # RLP encode: [chain_id, address, nonce]
    # chain_id and nonce are big_endian_int, address is Binary(20)
    encoded = rlp.encode([
        rlp.sedes.big_endian_int.serialize(chain_id),
        address,
        rlp.sedes.big_endian_int.serialize(nonce),
    ])
    return keccak(b"\x05" + encoded)


def sign(priv_hex: str, chain_id: int, address: bytes, nonce: int, label: str):
    """Sign an EIP-7702 authorization and print golden values."""
    try:
        from eth_account import Account
        acct = Account.from_key(priv_hex)
        h = auth_hash(chain_id, address, nonce)

        # Try different signing APIs depending on eth-account version
        try:
            # eth-account >= 0.10: use sign_message with raw bytes
            sig = Account._sign_hash(h, acct.key)
            yparity = sig.v - 27 if sig.v in (27, 28) else sig.v
            r, s = sig.r, sig.s
        except AttributeError:
            # Fallback: use eth_keys directly
            from eth_keys import keys
            pk = keys.PrivateKey(bytes.fromhex(priv_hex.removeprefix("0x")))
            sig = pk.sign_msg_hash(h)
            r = int.from_bytes(sig[:32], 'big')
            s = int.from_bytes(sig[32:64], 'big')
            yparity = sig[64]

        print(f"# === {label} ===")
        print(f"# chain_id={chain_id} addr=0x{address.hex()} nonce={nonce}")
        print(f"authority = {acct.address}")
        print(f"r = 0x{r:064x}")
        print(f"s = 0x{s:064x}")
        print(f"v = {yparity}")
        print()
    except Exception as e:
        print(f"Error for {label}: {e}")
        raise


def sign_fallback(priv_hex: str, chain_id: int, address: bytes, nonce: int, label: str):
    """Fallback signer using eth_keys directly (no eth-account needed)."""
    from eth_keys import keys
    from eth_utils import to_checksum_address
    h = auth_hash(chain_id, address, nonce)

    pk_bytes = bytes.fromhex(priv_hex.removeprefix("0x"))
    pk = keys.PrivateKey(pk_bytes)
    # Derive public key -> address
    pub = pk.public_key
    pub_bytes = pub.to_bytes()  # 64 bytes uncompressed
    addr_bytes = keccak(pub_bytes)[-20:]
    authority = to_checksum_address(addr_bytes)

    sig = pk.sign_msg_hash(h)
    r = int.from_bytes(sig.r.to_bytes(32, 'big'), 'big')
    s = int.from_bytes(sig.s.to_bytes(32, 'big'), 'big')
    yparity = sig.v

    print(f"# === {label} ===")
    print(f"# chain_id={chain_id} addr=0x{address.hex()} nonce={nonce}")
    print(f"authority = {authority}")
    print(f"r = 0x{r:064x}")
    print(f"s = 0x{s:064x}")
    print(f"v = {yparity}")
    print()


if __name__ == "__main__":
    # 固定测试私钥（仅测试用，非真实资产）
    PRIV = "0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d"
    DELEGATE = bytes.fromhex("00000000000000000000000000000000000000cc")  # 委托目标 addr

    try:
        sign(PRIV, 1, DELEGATE, 0, "成功用例 chain_id=1, nonce=0")
        sign(PRIV, 1, DELEGATE, 5, "nonce 不匹配用例 chain_id=1, nonce=5 (state nonce=0)")
        sign(PRIV, 999, DELEGATE, 0, "chain_id 不匹配用例 chain_id=999, nonce=0")
    except Exception:
        print("# Falling back to eth_keys direct signing...")
        sign_fallback(PRIV, 1, DELEGATE, 0, "成功用例 chain_id=1, nonce=0")
        sign_fallback(PRIV, 1, DELEGATE, 5, "nonce 不匹配用例 chain_id=1, nonce=5 (state nonce=0)")
        sign_fallback(PRIV, 999, DELEGATE, 0, "chain_id 不匹配用例 chain_id=999, nonce=0")
