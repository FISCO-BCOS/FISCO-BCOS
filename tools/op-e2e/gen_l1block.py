#!/usr/bin/env python3
"""Generate a minimal L1Block runtime bytecode implementing setL1BlockValues, aligned with:
  - FISCO makeL1AttributesDeposit calldata (OpEngineSeam.h): Isthmus 176B / Jovian 178B, selector
    0x098999be / 0x3db6be2b; fields at fixed byte offsets (CALLDATALOAD(k) = big-endian, byte k
    at bit [256-8*(k+1), 256-8*k)).
  - loadOpFeeParams (OpFeeParams.cpp): slot1=l1_base_fee, slot3 bytes[16:24]=
    baseFeeScalar|blobBaseFeeScalar, slot7=blob_base_fee, slot8 bytes[18:32]=
    daFootprintGasScalar|operatorFeeScalar|operatorFeeConstant.
Writes the 4 slots, returns empty. Selector-dispatches setL1BlockValues; other calls revert.
"""
import sys

def pv(n, v):
    return bytes([0x5f + n]) + v.to_bytes(n, 'big') if n > 0 else b''
def p1(v): return pv(1, v)
def p2(v): return pv(2, v)
def p4(v): return pv(4, v)
def p8(v): return pv(8, v)
PUSH28 = bytes([0x5f + 28]) + (b'\xff' * 28)  # 2^224 - 1, as a 28-byte immediate (PUSH28 = 0x7b).
# BUG NOTE: PUSH29 (0x7c) pushes 29 immediate bytes; the first delivery paired it with only 28
# bytes of 0xff, so evmone swallowed the next instruction's first byte into the mask, misaligning
# every subsequent instruction (observed: slot1 = 0x000000ff..16 instead of the computed 0).

body = bytearray()

# --- slot3 = (baseFeeScalar << 128) | (blob << 96)
#   baseFeeScalar = calldata[4:8]  = (x0 >> 192) & 0xffffffff   (x0=CALLDATALOAD(0))
#   blob          = calldata[8:12] = (x0 >> 160) & 0xffffffff
body += p1(0x00) + b'\x35'                                    # x0
body += b'\x80' + p1(0xc0) + b'\x1c' + p4(0xffffffff) + b'\x16' + p1(0x80) + b'\x1b'  # baseFeeScalar<<128
body += p1(0x00) + b'\x35' + p1(0xa0) + b'\x1c' + p4(0xffffffff) + b'\x16' + p1(0x60) + b'\x1b'  # blob<<96
body += b'\x17'
body += p1(0x03) + b'\x55'                                    # SSTORE(3)
body += b'\x50'                                               # POP x0

# --- slot1 = (x1 & M224) | ((x2 >> 224) << 224)
#   l1_base_fee = calldata[36:68] = x1.bytes[4:32] ++ x2.bytes[0:4]
body += p1(0x20) + b'\x35' + PUSH28 + b'\x16'                  # x1 & M224
body += p1(0x40) + b'\x35' + p1(0xe0) + b'\x1c' + p1(0xe0) + b'\x1b'  # (x2>>224)<<224
body += b'\x17'
body += p1(0x01) + b'\x55'                                    # SSTORE(1)

# --- slot7 = (x2 & M224) | ((x3 >> 224) << 224)
#   blob_base_fee = calldata[68:100] = x2.bytes[4:32] ++ x3.bytes[0:4]
body += p1(0x40) + b'\x35' + PUSH28 + b'\x16'
body += p1(0x60) + b'\x35' + p1(0xe0) + b'\x1c' + p1(0xe0) + b'\x1b'
body += b'\x17'
body += p1(0x07) + b'\x55'                                    # SSTORE(7)

# --- slot8 = (da << 96) | (opFeeScalar << 64) | opFeeConstant
#   da          = calldata[176:178] = (x6 >> 240) & 0xffff            (x6=CALLDATALOAD(176))
#   opFeeScalar = calldata[164:168] = (x5 >> 192) & 0xffffffff        (x5=CALLDATALOAD(160))
#   opFeeConst  = calldata[168:176] = (x5 >> 128) & 0xffffffffffffffff
body += p1(0xb0) + b'\x35' + p1(0xf0) + b'\x1c' + p2(0xffff) + b'\x16' + p1(0x60) + b'\x1b'
body += p1(0xa0) + b'\x35' + p1(0xc0) + b'\x1c' + p4(0xffffffff) + b'\x16' + p1(0x40) + b'\x1b' + b'\x17'
body += p1(0xa0) + b'\x35' + p1(0x80) + b'\x1c' + p8(0xffffffffffffffff) + b'\x16' + b'\x17'
body += p1(0x08) + b'\x55'                                    # SSTORE(8)

# return empty
body += p1(0x00) + p1(0x00) + b'\xf3'

# dispatch: calldatasize < 4 -> revert (min setL1BlockValues is 176+4 bytes, but guard cheap);
# selector == 0x098999be or 0x3db6be2b -> jump to body; else revert. IMPORTANT: EVM JUMPI/JUMP
# require the destination to be a JUMPDEST (0x5b) — jumping to a non-JUMPDEST byte is an
# exceptional halt that consumes all gas (deposit receipt reads status=0x0, gasUsed=gasLimit).
# Both jump targets below land on explicit JUMPDESTs.
# NOTE: EVM LT(0x10) is `top < second` (stack [a, b] with b on top => b < a). To test
# `calldatasize < 4` the PUSH1 4 must be pushed BEFORE CALLDATASIZE, so the stack is [4, cd]
# and LT = cd < 4. Writing CALLDATASIZE first gives LT = 4 < cd = "cd > 4" -- the guard's
# condition was inverted in the first delivery and reverted every real deposit (calldata 178B).
dispatch = bytearray()
dispatch += p1(0x04) + b'\x36' + b'\x10' + p1(0x00) + b'\x57'   # size guard: cd < 4 -> revert
dispatch += p1(0x00) + b'\x35' + p1(0xe0) + b'\x1c'
dispatch += p4(0x098999be) + b'\x14' + p1(0x00) + b'\x57'       # Isthmus
dispatch += p1(0x00) + b'\x35' + p1(0xe0) + b'\x1c'
dispatch += p4(0x3db6be2b) + b'\x14' + p1(0x00) + b'\x57'       # Jovian
dispatch += b'\x5b' + p1(0x00) + p1(0x00) + b'\xfd'             # JUMPDEST REVERT

REVERT_POS = len(dispatch) - 6  # the JUMPDEST before PUSH1 0 PUSH1 0 REVERT (6-byte block)
BODY_POS = len(dispatch)        # the body JUMPDEST (appended after the dispatch below)
dispatch[5] = REVERT_POS        # size guard: calldatasize < 4 -> revert
dispatch[20] = BODY_POS         # Isthmus setL1BlockValues -> body
dispatch[35] = BODY_POS         # Jovian setL1BlockValues -> body

final = bytes(dispatch) + b'\x5b' + bytes(body)  # body JUMPDEST at BODY_POS
print("L1BLOCK_CODE=0x" + final.hex())
print("len:", len(final))
