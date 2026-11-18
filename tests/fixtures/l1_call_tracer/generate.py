#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Rebuild public, deterministic EVM/transaction fixtures with Foundry cast.

The key is the public Ethereum test key 1. It has no funds outside this fixture.
The parity runner itself uses only Python's standard library.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

CHAIN_ID = 133712
KEY = "0x" + "00" * 31 + "01"
SENDER = "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"


def address(number):
    return "0x" + format(number, "040x")


def push(number):
    data = number.to_bytes(max(1, (number.bit_length() + 7) // 8), "big")
    return bytes([0x5f + len(data)]) + data


def call(kind, to, value=0, output_size=32, input_size=0):
    # The caller ignores the success flag, allowing failed child frames to survive.
    code = push(output_size) + push(0) + push(input_size) + push(0)
    if kind in (0xf1, 0xf2):
        code += push(value)
    return code + push(int(to, 16)) + bytes.fromhex("5a") + bytes([kind, 0x50])


def return_word(opcode):
    return bytes([opcode]) + bytes.fromhex("60005260206000f3")


def init_code(runtime):
    assert len(runtime) < 256
    # Copy the runtime immediately after this twelve-byte constructor.
    return push(len(runtime)) + push(12) + push(0) + bytes([0x39]) + push(len(runtime)) + push(0) + bytes([0xf3]) + runtime


def creation(opcode, init, salt=1):
    # MSTORE right-aligns bytes; put the initcode at memory[0..len).
    assert len(init) <= 32
    code = push(int.from_bytes(init.ljust(32, b"\0"), "big")) + push(0) + bytes([0x52])
    if opcode == 0xf5:
        code += push(salt)
    return code + push(len(init)) + push(0) + push(0) + bytes([opcode, 0x50])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cast", default="cast")
    args = ap.parse_args()
    folder = Path(__file__).resolve().parent
    state, reader, leaf, reverter, router, rollback, destroy, factory, spin, big = map(address, range(0x1001, 0x100b))
    # Empty calldata reads slot 0, non-empty calldata writes slot 0.
    state_code = bytes.fromhex("3615600c57600035600055005b60005460005260206000f3")
    reader_code = call(0xfa, state) + bytes.fromhex("60206000f3")
    leaf_code = return_word(0x33)  # CALLER exposes delegatecall sender differences.
    revert_code = bytes.fromhex("63deadbeef6000526004601cfd")
    destroy_code = push(0xbeef) + bytes([0xff])
    # Successful CREATE/CREATE2, then identical CREATE2 collision; both failures visible.
    init = init_code(bytes.fromhex("602a60005260206000f3"))
    factory_code = creation(0xf0, init) + creation(0xf5, init) + creation(0xf5, init) + bytes([0x00])
    nested = address(0x100c)
    delegated = "0x2b5ad5c4795c026514f8317c7a215e218dccd6cf"
    router_code = (call(0xf1, leaf, 3) + call(0xf4, leaf) + call(0xfa, reader) + call(0xf1, reverter)
                   + call(0xfa, nested) + call(0xf1, leaf, 10**30) + call(0xf2, leaf, 10**30)
                   + call(0xf1, factory) + call(0xf1, destroy) + bytes.fromhex("60206000f3"))
    rollback_code = call(0xf1, router) + bytes.fromhex("60006000fd")
    # CREATE initcode selfdestructs in the creation transaction (EIP-6780 deletion case).
    ephemeral = creation(0xf0, push(0xbeef) + bytes([0xff])) + bytes([0x00])
    def revert_data(data):
        return init_code(data)[:11] + bytes([0xfd]) + data
    error_string = bytes.fromhex("08c379a0") + (32).to_bytes(32, "big") + (4).to_bytes(32, "big") + b"boom".ljust(32, b"\0")
    panic = bytes.fromhex("4e487b71") + (0x11).to_bytes(32, "big")
    alloc = {SENDER[2:]: {"balance": hex(10**24)}}
    for addr, code in [(state, state_code), (reader, reader_code), (leaf, leaf_code),
                       (reverter, revert_code), (router, router_code), (rollback, rollback_code),
                       (destroy, destroy_code), (factory, factory_code), (spin, bytes.fromhex("5b600056")),
                       (big, call(0xf1, address(0x100d), output_size=0) * 6 + bytes([0x00])),
                       (address(0x100d), push(800000) + push(0) + bytes([0xf3])),
                       (nested, call(0xf1, leaf) + bytes([0x00])), (address(0x100b), ephemeral),
                       (address(0x100f), call(0xf1, delegated, 2) + bytes([0x00])),
                       (address(0x1010), revert_data(error_string)), (address(0x1011), revert_data(panic)),
                       (address(0x1012), bytes.fromhex("600156")), (address(0x1013), bytes.fromhex("01")),
                       (address(0x1014), bytes.fromhex("fe")),
                       (address(0x100e), bytes.fromhex("42600052") +
                        call(0xfa, "0x000f3df6d732807ef1319fb7b8bb8522d0beac02", input_size=32) + bytes.fromhex("60206000f3"))]:
        alloc[addr[2:]] = {"balance": hex(10**18), "nonce": "0x1", "code": "0x" + code.hex()}
    alloc.update({addr[2:]: account for addr, account in
        json.loads((folder / "system-contracts.json").read_text())["contracts"].items()})
    genesis = {
        "config": {"chainId": CHAIN_ID, "homesteadBlock": 0, "eip150Block": 0,
                   "eip155Block": 0, "eip158Block": 0, "byzantiumBlock": 0,
                   "constantinopleBlock": 0, "petersburgBlock": 0, "istanbulBlock": 0,
                   "berlinBlock": 0, "londonBlock": 0, "terminalTotalDifficulty": 0,
                   "shanghaiTime": 0, "cancunTime": 0, "pragueTime": 0, "osakaTime": 0,
                   "blobSchedule": {"cancun": {"target": 3, "max": 6, "baseFeeUpdateFraction": 3338477},
                                    "prague": {"target": 6, "max": 9, "baseFeeUpdateFraction": 5007716},
                                    "osaka": {"target": 6, "max": 9, "baseFeeUpdateFraction": 5007716}}},
        "nonce": "0x0", "timestamp": "0x1", "extraData": "0x", "gasLimit": "0x3938700",
        "difficulty": "0x0", "mixHash": "0x" + "00" * 32,
        "coinbase": address(0), "baseFeePerGas": "0x3b9aca00", "alloc": alloc,
    }
    # First block's reader MUST see 42, even though parent state is 0 and later state 99.
    cases = [
        ("same_block_setter", state, "0x" + format(42, "064x"), 1, 100000, 0),
        ("same_block_reader", reader, "0x", 1, 1000000, 0),
        ("block_system_call_reader", address(0x100e), "0x", 1, 1000000, 0),
        ("call_tree", router, "0x", 1, 3000000, 7),
        ("ancestor_revert", rollback, "0x", 1, 3000000, 0),
        ("create_and_destroy", address(0x100b), "0x", 1, 1000000, 0),
        ("top_create", None, "0x" + init.hex(), 1, 1000000, 0),
        ("top_create_revert", None, "0x" + revert_code.hex(), 1, 1000000, 0),
        ("top_revert", reverter, "0x", 1, 1000000, 0),
        ("error_string", address(0x1010), "0x", 1, 1000000, 0),
        ("panic_reason", address(0x1011), "0x", 1, 1000000, 0),
        ("invalid_jump", address(0x1012), "0x", 1, 1000000, 0),
        ("stack_underflow", address(0x1013), "0x", 1, 1000000, 0),
        ("invalid_opcode", address(0x1014), "0x", 1, 1000000, 0),
        ("create_deposit_oog", None, "0x" + (push(600) + push(0) + bytes([0xf3])).hex(), 1, 100000, 0),
        ("create_invalid_prefix", None, "0x" + init_code(bytes([0xef])).hex(), 1, 1000000, 0),
        ("create_maxcode", None, "0x" + (push(24577) + push(0) + bytes([0xf3])).hex(), 1, 1000000, 0),
        ("blake_invalid_length", address(9), "0x", 1, 1000000, 0),
        ("blake_invalid_final_flag", address(9), "0x" + "00" * 212 + "02", 1, 1000000, 0),
        ("delegated_root", delegated, "0x", 2, 1000000, 0),
        ("delegated_internal", address(0x100f), "0x", 2, 1000000, 0),
        ("later_state_setter", state, "0x" + format(99, "064x"), 2, 100000, 0),
        ("timeout_loop", spin, "0x", 3, 16000000, 0),
        ("output_limit", big, "0x", 4, 16000000, 0),
    ]
    fp_modulus = int("30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47", 16)
    zero_commitment = bytes([0xc0]) + bytes(47)  # KZG commitment/proof to the zero polynomial.
    zero_hash = bytes([1]) + hashlib.sha256(zero_commitment).digest()[1:]
    malformed_hash = bytes([1]) + hashlib.sha256(bytes(48)).digest()[1:]
    non_subgroup = bytes([0x80]) + bytes(47)  # BLS G1 (0,2), order three.
    subgroup_hash = bytes([1]) + hashlib.sha256(non_subgroup).digest()[1:]
    scalar_modulus = int("73eda753299d7d483339d80809a1d80553bda402fffe5bfeffffffff00000001", 16).to_bytes(32, "big")
    generator = bytes.fromhex("97f1d3a73197d7942695638c4fa9ac0fc3688c4f9774b905a14e3a3f171bac586c55e83ff97a1aeffb3af00adb22c6bb")
    modexp_1025 = (1025).to_bytes(32, "big") + (1).to_bytes(32, "big") * 2
    modexp_1024 = (1024).to_bytes(32, "big") + (1).to_bytes(32, "big") * 2
    precompiles = [
        ("kzg_invalid_length", 10, b""),
        ("kzg_versioned_hash", 10, bytes(192)),
        ("kzg_invalid_proof", 10, malformed_hash + bytes(160)),
        ("kzg_zero_polynomial", 10, zero_hash + bytes(64) + zero_commitment * 2),
        ("kzg_scalar_z", 10, zero_hash + scalar_modulus + bytes(32) + zero_commitment * 2),
        ("kzg_scalar_y", 10, zero_hash + bytes(32) + scalar_modulus + zero_commitment * 2),
        ("kzg_commitment_subgroup", 10, subgroup_hash + bytes(64) + non_subgroup + zero_commitment),
        ("kzg_proof_subgroup", 10, zero_hash + bytes(64) + zero_commitment + non_subgroup),
        ("kzg_proof_equation", 10, zero_hash + bytes(64) + zero_commitment + generator),
        ("bn_add_coordinate", 6, fp_modulus.to_bytes(32, "big") + (1).to_bytes(32, "big") + bytes(64)),
        ("bn_mul_not_on_curve", 7, (1).to_bytes(32, "big") * 3),
        ("bn_pairing_length", 8, bytes([1])),
        ("bn_add_infinity", 6, bytes(128)),
        ("bls_g1_add_length", 11, b""),
        ("bls_g1_add_field_top", 11, bytes([1]) + bytes(255)),
        ("bls_g1_add_not_on_curve", 11, (1).to_bytes(64, "big") * 2 + bytes(128)),
        ("bls_g1_msm_length", 12, b""),
        ("bls_g1_msm_subgroup", 12, bytes(64) + (2).to_bytes(64, "big") + (1).to_bytes(32, "big")),
        ("bls_g2_add_length", 13, b""),
        ("bls_g2_msm_length", 14, b""),
        ("bls_pairing_length", 15, b""),
        ("bls_map_g1_length", 16, b""),
        ("bls_map_g2_length", 17, b""),
        ("bls_g1_add_infinity", 11, bytes(256)),
        ("modexp_length_1025", 5, modexp_1025),
        ("modexp_length_1024", 5, modexp_1024),
    ]
    cases.extend((name, address(to), "0x" + data.hex(), 2, 1000000, 0) for name, to, data in precompiles)
    # Osaka's existing nominal gas formula gives 33,282; supply one less after
    # transaction intrinsic gas, so OOG must precede the oversized-base reason.
    intrinsic = 21000 + sum(4 if byte == 0 else 16 for byte in modexp_1025)
    cases.append(("modexp_length_1025_lowgas", address(5), "0x" + modexp_1025.hex(), 2, intrinsic + 33281, 0))
    cases.sort(key=lambda case: case[3])
    transactions = []
    for nonce, (name, to, data, block, gas, value) in enumerate(cases):
        cmd = [args.cast, "mktx", "--private-key", KEY, "--chain", str(CHAIN_ID),
               "--nonce", str(nonce), "--gas-price", "2000000000", "--gas-limit", str(gas),
               "--value", str(value)]
        if name == "delegated_root":
            auth = subprocess.check_output([args.cast, "wallet", "sign-auth", leaf,
                "--private-key", "0x" + "00" * 31 + "02", "--chain", str(CHAIN_ID), "--nonce", "0"], text=True).strip()
            cmd += ["--priority-gas-price", "1000000000", "--auth", auth]
        else:
            cmd += ["--legacy"]
        cmd += [to, data] if to else ["--create", data]
        raw = subprocess.check_output(cmd, text=True).strip()
        tx_hash = subprocess.check_output([args.cast, "keccak", raw], text=True).strip()
        transactions.append({"name": name, "block": block, "hash": tx_hash, "raw": raw})
    (folder / "genesis.json").write_text(json.dumps(genesis, indent=2) + "\n")
    (folder / "transactions.json").write_text(json.dumps({"chainId": CHAIN_ID, "sender": SENDER,
        "castVersion": subprocess.check_output([args.cast, "--version"], text=True).strip(),
        "transactions": transactions}, indent=2) + "\n")


if __name__ == "__main__":
    main()
