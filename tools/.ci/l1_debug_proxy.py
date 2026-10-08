#!/usr/bin/env python3
"""L1 debug RPC proxy for kona-host native replay.

C2's L1 is anvil, which lacks geth's `debug_getRawHeader` / `debug_getRawReceipts`
— the two methods kona-host's `L1BlockHeader` / `L1Receipts` hints call. This proxy
fronts anvil and implements both from standard RPCs plus canonical RLP encoding
(the exact bytes whose trie root the L1 header commits to), transparently
forwarding every other method.

Encoding is byte-verified against geth:
  - header  -> keccak(rlp(21-field Prague header)) == block hash
  - receipt -> `txType || rlp([status, cumulativeGasUsed, bloom, logs])` (EIP-2718),
               and kona's ordered trie over these == the header's receiptsRoot

Usage:
  l1_debug_proxy.py --l1 <anvil-url> --port <listen-port>

Env aliases: L1_RPC (anvil url), L1_DEBUG_PROXY_PORT.
"""
import argparse
import json
import os
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import requests
import rlp


def hx(x):
    """0x-prefixed hex string -> bytes."""
    if x is None:
        return b""
    x = x[2:] if x.startswith("0x") else x
    if len(x) % 2:
        x = "0" + x
    return bytes.fromhex(x)


def enc_scalar(q):
    """JSON-RPC QUANTITY -> minimal big-endian RLP scalar (0 -> empty string)."""
    if q in ("0x0", "0x00", "0x", ""):
        return b""
    return hx(q)


def encode_header(block):
    """Encode an anvil block's header as geth's canonical 21-field Prague RLP.

    Field order matches geth Header.EncodeRLP (and FISCO's EthBlockHeaderData
    codec): the 15 pre-London mandatory fields, then baseFee, withdrawalsRoot,
    blobGasUsed, excessBlobGas, parentBeaconBlockRoot, requestsHash.
    """
    fields = [
        hx(block["parentHash"]),
        hx(block["sha3Uncles"]),
        hx(block["miner"]),
        hx(block["stateRoot"]),
        hx(block["transactionsRoot"]),
        hx(block["receiptsRoot"]),
        hx(block["logsBloom"]),
        enc_scalar(block["difficulty"]),
        enc_scalar(block["number"]),
        enc_scalar(block["gasLimit"]),
        enc_scalar(block["gasUsed"]),
        enc_scalar(block["timestamp"]),
        hx(block["extraData"]),
        hx(block["mixHash"]),
        hx(block["nonce"]),
    ]
    # Fork-gated optional fields, in order. C2's anvil runs Prague from genesis, so
    # all six are present; the presence checks keep the encoder correct if the
    # fork schedule ever lags a field.
    if block.get("baseFeePerGas") is not None:
        fields.append(enc_scalar(block["baseFeePerGas"]))
    if block.get("withdrawalsRoot") is not None:
        fields.append(hx(block["withdrawalsRoot"]))
    if block.get("blobGasUsed") is not None:
        fields.append(enc_scalar(block["blobGasUsed"]))
    if block.get("excessBlobGas") is not None:
        fields.append(enc_scalar(block["excessBlobGas"]))
    if block.get("parentBeaconBlockRoot") is not None:
        fields.append(hx(block["parentBeaconBlockRoot"]))
    if block.get("requestsHash") is not None:
        fields.append(hx(block["requestsHash"]))
    return rlp.encode(fields)


def encode_receipt(rcpt):
    """Encode one receipt as geth's EIP-2718 form: txType || rlp(receipt fields).

    The logs field is a NESTED list (list of [address, [topics], data] triples),
    matching geth types.Receipt.MarshalBinary — encoding each log to bytes first
    would turn the inner list into a byte-string item (b8.. prefix) instead of a
    list (f8.. prefix) and diverge the receipts trie root.
    """
    status = hx(rcpt["status"])  # 0x1 -> b'\x01', 0x0 -> b'\x00'
    cumulative_gas_used = enc_scalar(rcpt["cumulativeGasUsed"])
    bloom = hx(rcpt["logsBloom"])
    logs = [[hx(log["address"]), [hx(t) for t in log.get("topics", [])],
             hx(log.get("data", "0x"))] for log in rcpt.get("logs", [])]
    inner = rlp.encode([status, cumulative_gas_used, bloom, logs])

    tx_type = rcpt.get("type", "0x0")
    type_byte = int(tx_type, 16)
    if type_byte == 0:
        return inner  # legacy: no type prefix
    return bytes([type_byte]) + inner


class L1Proxy:
    def __init__(self, l1_url):
        self.l1_url = l1_url
        # Reuse one keep-alive session instead of opening a fresh TCP connection per
        # request. kona-host walks every L1 header during boot (thousands of calls),
        # and a connect/close per call floods the loopback with TIME_WAIT sockets,
        # eventually stalling new connects. trust_env=False bypasses any ambient
        # http_proxy (localhost must never route through a proxy).
        self.session = requests.Session()
        self.session.trust_env = False

    def forward(self, payload):
        """Forward a JSON-RPC request to anvil and return the parsed result."""
        resp = self.session.post(self.l1_url, json=payload, timeout=120)
        resp.raise_for_status()
        return resp.json()

    def rpc(self, method, params):
        return self.forward({"jsonrpc": "2.0", "method": method, "params": params, "id": 1})

    def _handle_one(self, payload):
        if not isinstance(payload, dict):
            return {"jsonrpc": "2.0", "id": None,
                    "error": {"code": -32600, "message": "Invalid Request"}}
        method = payload.get("method")
        params = payload.get("params", [])

        if method == "debug_getRawHeader":
            block = self.rpc("eth_getBlockByHash", [params[0], False])["result"]
            if block is None:
                return {"jsonrpc": "2.0", "id": payload.get("id"),
                        "error": {"code": -32602, "message": "Block not found"}}
            return {"jsonrpc": "2.0", "id": payload.get("id"),
                    "result": "0x" + encode_header(block).hex()}

        if method == "debug_getRawReceipts":
            receipts = self.rpc("eth_getBlockReceipts", [params[0]])["result"]
            if receipts is None:
                return {"jsonrpc": "2.0", "id": payload.get("id"),
                        "error": {"code": -32602, "message": "Block not found"}}
            return {"jsonrpc": "2.0", "id": payload.get("id"),
                    "result": ["0x" + encode_receipt(r).hex() for r in receipts]}

        # Everything else: transparent passthrough.
        return self.forward(payload)

    def handle(self, payload):
        # JSON-RPC batch: dispatch per item — debug_* methods are answered here, the rest
        # transparently forwarded — so an array body does not crash the handler thread
        # (payload.get on a list would raise AttributeError and drop the connection).
        if isinstance(payload, list):
            return [self._handle_one(item) for item in payload]
        return self._handle_one(payload)


def make_handler(proxy):
    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length)
            try:
                payload = json.loads(body)
            except json.JSONDecodeError:
                resp = json.dumps({"jsonrpc": "2.0", "id": None,
                                   "error": {"code": -32700, "message": "Parse error"}}).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(resp)))
                self.end_headers()
                self.wfile.write(resp)
                return
            result = proxy.handle(payload)
            resp = json.dumps(result).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(resp)))
            self.end_headers()
            self.wfile.write(resp)

        def log_message(self, *args):
            pass

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--l1", default=os.environ.get("L1_RPC", "http://127.0.0.1:8549"))
    parser.add_argument("--port", type=int,
                        default=int(os.environ.get("L1_DEBUG_PROXY_PORT", "0")))
    args = parser.parse_args()

    proxy = L1Proxy(args.l1)
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(proxy))
    actual_port = server.server_address[1]
    # Print the resolved port FIRST so a launcher can read it off stdout; kona_verify
    # starts this with --port 0 and parses this line.
    print(f"PORT={actual_port}", flush=True)
    print(f"[l1-debug-proxy] listening on 127.0.0.1:{actual_port} -> {args.l1}",
          flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
