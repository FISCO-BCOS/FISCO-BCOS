"""One-off generator for the frozen anchor constants in
test_withdraw_claim_pure.py. Output is pasted verbatim into FROZEN; the test
then self-checks the constants against its own independent builders at every
run. Regenerate ONLY alongside a deliberate fixture-value change."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_withdraw_claim_pure import make_log
from eth_hash.auto import keccak

EMPTY_LOG = make_log(7, 10**18, 100_000, b"")
ODD_LOG = make_log(3, 1, 2, b"\xde\xad\xbe\xef")
wh_empty = bytes.fromhex(EMPTY_LOG["data"][2:][3 * 64:4 * 64])
wh_odd = bytes.fromhex(ODD_LOG["data"][2:][3 * 64:4 * 64])
tbl = "/apps/" + "4200000000000000000000000000000000000016"
print("FROZEN = {")
print(f'    "wh_empty_data": "{wh_empty.hex()}",')
print(f'    "wh_odd_data": "{wh_odd.hex()}",')
print(f'    "storage_key_empty": "{keccak(wh_empty + bytes(32)).hex()}",')
print(f'    "rocksdb_prefix": "{tbl.encode().hex() + "3a"}",')
print("}")
