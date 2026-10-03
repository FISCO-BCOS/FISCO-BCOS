#!/usr/bin/env bash
# Regenerate bcos-evm/test/opstack/op_geth_oracle.json from the pinned op-geth checkout.
# The values here are judged by op-geth's own testdata, not by this repo's implementation.
# (E4 will extend this contract with the EIP-2537 BLS vectors from the same source.)
set -euo pipefail
OP_GETH_REPO="${OP_GETH_REPO:-}"
[ -n "$OP_GETH_REPO" ] || { echo "OP_GETH_REPO is required (path to the pinned op-geth checkout; no machine default)" >&2; exit 1; }
PIN="${OP_GETH_PIN:-e8800cffe53d459cde8a07c8e8f1de9d86e79e07}"
OUT="${1:-$(cd "$(dirname "$0")/../.." && pwd -P)/bcos-evm/test/opstack/op_geth_oracle.json}"
python3 - "$OP_GETH_REPO" "$PIN" "$OUT" <<'PY'
import json,subprocess,sys
repo,pin,out=sys.argv[1],sys.argv[2],sys.argv[3]
def show(path):
    return subprocess.run(["git","-C",repo,"show",f"{pin}:{path}"],capture_output=True,text=True,check=True).stdout
# bn256: the G2 point used by repeatInfinityG1Pairs (bn256Pairing.json "two_point_match_3",
# pair-1 G2 = hex offset 128..384). Extracted, not hand-copied, so the contract regenerates.
pairing=json.loads(show("core/vm/testdata/precompiles/bn256Pairing.json"))
vec=[v for v in pairing if v.get("Name")=="two_point_match_3"]
assert len(vec)==1, [v.get("Name") for v in pairing]
g2=vec[0]["Input"][128:384]
assert len(g2)==256, len(g2)
doc={"source":{"repo":"blockchain-impl/op-geth","commit":pin,
               "files":["core/vm/testdata/precompiles/bn256Pairing.json"]},
     "bn256":{"g2_valid_point_hex":g2,
              "vector":"two_point_match_3","offset_hex":"128..384"}}
open(out,"w").write(json.dumps(doc,indent=2)+"\n")
print("wrote",out)
PY
