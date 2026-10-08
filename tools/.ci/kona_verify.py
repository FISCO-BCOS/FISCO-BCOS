#!/usr/bin/env python3
"""Native fault-proof replay verification against the C2 devnet.

Reads the claimed output root / L2 block number / L1 head straight from the
on-chain FaultDisputeGame (the root claim a permissioned game commits to), then
runs kona-host in native mode to re-derive the L2 chain from the agreed genesis
head and assert kona's independent (revm) derivation reproduces FISCO's claimed
output root.

The verifier does NOT use cannon (no MIPS VM): it runs kona-client natively
inside kona-host. This is the L1 milestone of the integration plan — proving
FISCO's outputRoot is reproducible by a fully independent implementation.

Exit 0 iff kona's recomputed output root == the on-chain claimed output root.

The FaultDisputeGame is a clones-with-immutable-args proxy; the three getters
used here map directly onto kona-host's inputs:
    rootClaim()  -> --l2-claim        (bytes32 output root)
    extraData()  -> --l2-block-number (uint256 L2 block packed into 32 bytes)
    l1Head()     -> --l1-head         (L1 parent hash at game creation)

Env (all overridable):
  C2_L1_RPC    anvil L1 RPC (eth namespace)          [default http://127.0.0.1:8549]
  C2_L2_WEB3   FISCO L2 RPC (eth + debug namespace)  [default http://127.0.0.1:8555]
  C2_OP_NODE   op-node RPC (optimism_outputAtBlock)  [default http://127.0.0.1:9545]
  C2_STATE     op-deployer state.json (DGF address)  [default /tmp/c2/state.json]
  C2_ROLLUP    rollup.json path                      [default /tmp/c2/rollup.json]
  KONA_HOST    kona-host binary path                 [default kona-host on PATH]
  C2_ANVIL_CHAIN anvil L1 chain id                   [default 900900]
"""
import json
import os
import subprocess
import sys
import tempfile
import threading
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer

from eth_hash.auto import keccak

os.environ.setdefault("NO_PROXY", "127.0.0.1,localhost")
os.environ.setdefault("no_proxy", "127.0.0.1,localhost")

L1 = os.environ.get("C2_L1_RPC", "http://127.0.0.1:8549")
L2 = os.environ.get("C2_L2_WEB3", "http://127.0.0.1:8555")
OP_NODE = os.environ.get("C2_OP_NODE", "http://127.0.0.1:9545")
STATE = os.environ.get("C2_STATE", "/tmp/c2/state.json")
ROLLUP = os.environ.get("C2_ROLLUP", "/tmp/c2/rollup.json")
KONA_HOST = os.environ.get("KONA_HOST", "kona-host")
ANVIL_CHAIN = int(os.environ.get("C2_ANVIL_CHAIN", "900900"))

# C2 runs with no real beacon (op-node uses --l1.beacon.ignore): batches are posted
# as calldata, so the blob hint never fires. But OnlineBlobProvider::init STILL
# eagerly fetches the beacon's genesis time and slot interval at startup, so
# kona-host needs a beacon endpoint that answers those two calls. A tiny local
# mock satisfies it; the blob-fetch endpoint is never reached on a calldata chain.


def _quantity(n):
    """alloy_serde::quantity wire format: 0x-prefixed lowercase hex, no leading zeros."""
    return "0x" + hex(n)[2:]


class _BeaconHandler(BaseHTTPRequestHandler):
    """Answers the two endpoints OnlineBlobProvider::init queries at startup."""

    def do_GET(self):
        if self.path == "/eth/v1/beacon/genesis":
            body = json.dumps(
                {"data": {"genesis_time": _quantity(_BeaconHandler.genesis_time)}}).encode()
        elif self.path == "/eth/v1/config/spec":
            body = json.dumps(
                {"data": {"SECONDS_PER_SLOT": _quantity(_BeaconHandler.seconds_per_slot)}}).encode()
        else:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def start_beacon_mock(genesis_time):
    """Start a thread-local beacon mock and return its base URL."""
    _BeaconHandler.genesis_time = genesis_time
    _BeaconHandler.seconds_per_slot = 12
    server = HTTPServer(("127.0.0.1", 0), _BeaconHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return f"http://127.0.0.1:{server.server_address[1]}", server


def rpc(url, method, params):
    body = json.dumps({"jsonrpc": "2.0", "method": method, "params": params,
                       "id": 1}).encode()
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(urllib.request.Request(
            url, body, {"Content-Type": "application/json"}), timeout=60) as r:
        data = json.load(r)
    if "error" in data:
        raise SystemExit(f"rpc {method} failed: {data['error']}")
    return data["result"]


def cast(*args):
    out = subprocess.run(["cast", *args], capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit(f"cast {' '.join(args)} failed: {out.stderr.strip()[:300]}")
    return out.stdout.strip()


def dgf_address():
    s = json.load(open(STATE))
    return s["opChainDeployments"][0]["DisputeGameFactoryProxy"]


def game_count(dgf):
    return int(cast("call", dgf, "gameCount()(uint256)", "--rpc-url", L1).split()[0])


def game_at(dgf, index):
    return cast("call", dgf,
                "gameAtIndex(uint256)(uint32,uint64,address)", str(index),
                "--rpc-url", L1).split()[-1]


def find_honest_game(dgf):
    """Find the game whose rootClaim matches the independently recomputed output
    root at its claimed L2 block.

    The contest leg creates TWO games: the honest one (rootClaim = the real
    output root FISCO committed) and a dishonest one (rootClaim = FAKE_ROOT).
    The dishonest one is always created LAST, so blindly taking gameCount-1 would
    hand kona a fake claim and its replay would trivially (and correctly) fail.
    Instead, walk every game and pick the one whose rootClaim equals
    optimism_outputAtBlock(extraData) recomputed via keccak — i.e. the honest
    game whose rootClaim kona should reproduce.
    """
    count = game_count(dgf)
    if count == 0:
        raise SystemExit("no dispute game exists yet (CONTEST=1 must create one)")
    for idx in range(count - 1, -1, -1):
        game = game_at(dgf, idx)
        root_claim, l2_block, l1_head = read_game_claim(game)
        out = rpc(OP_NODE, "optimism_outputAtBlock", [hex(l2_block)])
        # outputRoot == keccak(version|stateRoot|messagePasserStorageRoot|blockHash)
        version = out["version"]
        state_root = out["stateRoot"]
        withdrawal_root = out["withdrawalStorageRoot"]
        block_hash = out["blockRef"]["hash"]
        recomputed = "0x" + keccak(bytes.fromhex("".join(
            h[2:] for h in (version, state_root, withdrawal_root, block_hash)))).hex()
        if recomputed.lower() == root_claim.lower():
            return game, root_claim, l2_block, l1_head
    raise SystemExit("no honest game found (no rootClaim matches its recomputed output root)")


def read_game_claim(game):
    """rootClaim() / extraData() / l1Head() from the FaultDisputeGame proxy."""
    root_claim = cast("call", game, "rootClaim()(bytes32)", "--rpc-url", L1)
    extra = cast("call", game, "extraData()(bytes)", "--rpc-url", L1)
    l1_head = cast("call", game, "l1Head()(bytes32)", "--rpc-url", L1)

    # extraData is uint256(l2Block) packed into 32 bytes (C2's create packs the
    # claimed L2 block number into extraData, see withdraw_claim.create_game).
    extra_hex = extra[2:] if extra.startswith("0x") else extra
    l2_block = int(extra_hex, 16) if extra_hex else 0
    if l2_block == 0:
        raise SystemExit(f"game extraData has no L2 block number: {extra}")
    return root_claim, l2_block, l1_head


def agreed_genesis():
    """Genesis head + output root — the agreed derivation start.

    kona re-derives from genesis (block 0): its hash comes from the L2 node and
    its output root from op-node's optimism_outputAtBlock(0), which is the
    trusted anchor FISCO committed at chain start.
    """
    genesis = rpc(L2, "eth_getBlockByNumber", ["0x0", False])
    head_hash = genesis["hash"]
    out = rpc(OP_NODE, "optimism_outputAtBlock", ["0x0"])
    return head_hash, out["outputRoot"]


def write_l1_config(path):
    """kona-host's --l1-config-path expects alloy_genesis::ChainConfig (camelCase).

    op-node's l1_chain_config.json wraps the same struct in a top-level "config"
    key (geth's chain-config file shape); alloy's ChainConfig is the bare object.
    Fork schedule mirrors C2: everything active from genesis (all-zero), Cancun
    blob schedule only. The deposit contract / TTD fields are irrelevant to
    derivation and left default.
    """
    cfg = {
        "chainId": ANVIL_CHAIN,
        "homesteadBlock": 0, "eip150Block": 0, "eip155Block": 0, "eip158Block": 0,
        "byzantiumBlock": 0, "constantinopleBlock": 0, "petersburgBlock": 0,
        "istanbulBlock": 0, "muirGlacierBlock": 0, "berlinBlock": 0,
        "londonBlock": 0, "arrowGlacierBlock": 0, "grayGlacierBlock": 0,
        "shanghaiTime": 0, "cancunTime": 0,
        "clique": {"period": 0, "epoch": 30000},
        "blobSchedule": {
            "cancun": {"target": 3, "max": 6, "baseFeeUpdateFraction": 332827},
        },
    }
    with open(path, "w") as f:
        json.dump(cfg, f, indent=2)


def main():
    dgf = dgf_address()
    game, root_claim, l2_block, l1_head = find_honest_game(dgf)
    agreed_head, agreed_output = agreed_genesis()

    print(f"[kona-verify] game {game[:18]}…")
    print(f"[kona-verify] claimed outputRoot {root_claim[:18]}… @ L2 #{l2_block}")
    print(f"[kona-verify] l1Head {l1_head[:18]}…, agreed genesis {agreed_head[:18]}…")

    if l2_block == 0:
        raise SystemExit("claimed block is genesis; nothing to re-derive")

    # The beacon mock's genesis time is the L1 block-0 timestamp; the value is
    # never actually used to derive a slot on a calldata chain, but it must parse.
    l1_genesis = rpc(L1, "eth_getBlockByNumber", ["0x0", False])
    l1_genesis_time = int(l1_genesis["timestamp"], 16)
    beacon_url, beacon_server = start_beacon_mock(l1_genesis_time)

    # C2's L1 is anvil, which lacks geth's debug_getRawHeader / debug_getRawReceipts
    # that kona-host's L1BlockHeader / L1Receipts hints require. Front anvil with the
    # debug proxy so kona-host can read raw L1 headers and receipts.
    proxy_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "l1_debug_proxy.py")
    proxy_proc = subprocess.Popen(
        [sys.executable, proxy_script, "--l1", L1, "--port", "0"],
        stdout=subprocess.PIPE, text=True)
    port_line = proxy_proc.stdout.readline().strip()
    if not port_line.startswith("PORT="):
        raise SystemExit(f"l1_debug_proxy failed to report its port: {port_line!r}")
    l1_node = f"http://127.0.0.1:{port_line[len('PORT='):]}"
    print(f"[kona-verify] l1 debug proxy at {l1_node} -> {L1}")

    with tempfile.TemporaryDirectory(prefix="kona-verify.") as tmp:
        l1_cfg = os.path.join(tmp, "l1_chain_config.json")
        write_l1_config(l1_cfg)

        cmd = [
            KONA_HOST, "single",
            "--l1-head", l1_head,
            "--l2-head", agreed_head,
            "--l2-output-root", agreed_output,
            "--l2-claim", root_claim,
            "--l2-block-number", str(l2_block),
            "--l1-node-address", l1_node,
            "--l2-node-address", L2,
            "--l1-beacon-address", beacon_url,
            "--rollup-config-path", ROLLUP,
            "--l1-config-path", l1_cfg,
            "--native",
        ]
        print(f"[kona-verify] running: {KONA_HOST} single --native "
              f"(l2 #{l2_block}, claim {root_claim[:18]}…, beacon {beacon_url})")
        env = dict(os.environ)
        # Keep the derive driver's retry loop visible at warn/error level (the default
        # info level already surfaces "Failed to prefetch hint" and pipeline warnings,
        # which is enough to diagnose a stall without flooding gigabytes of trace logs).
        env.setdefault(
            "RUST_LOG",
            "kona_derive=info,single_hint_handler=info,host_backend=info,l1_traversal=info")
        # Native replay finishes in seconds; a hint-prefetch stall or derive retry loop can
        # hang forever, so bound it well above the normal case (900s) and treat a timeout as
        # a verification failure rather than letting the CI job-level timeout (45min) kill it.
        # The finally block guarantees the beacon mock and the L1 debug proxy are torn down on
        # every path (success, non-zero, or timeout) instead of leaking until CI VM recycle.
        try:
            result = subprocess.run(cmd, env=env, timeout=900)
        except subprocess.TimeoutExpired:
            print("[kona-verify] FAIL: kona-host did not finish within 900s "
                  "(hint-prefetch stall or derive retry loop)")
            return 1
        finally:
            beacon_server.shutdown()
            proxy_proc.terminate()
        if result.returncode != 0:
            print("[kona-verify] FAIL: kona's independent derivation did not "
                  "reproduce FISCO's claimed output root")
            return 1

    print("[kona-verify] PASS: kona (revm) reproduced FISCO's output root "
          f"at L2 #{l2_block}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
