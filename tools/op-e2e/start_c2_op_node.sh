#!/usr/bin/env bash
# C2 devnet op-node launcher — daemonized properly (no tee pipeline, disowned).
# Usage: bash start_c2_op_node.sh   (restarts if already running)
# Env overrides (defaults = the /tmp/c2 layout, same names as setup_c2.sh):
#   C2, ANVIL_PORT (8549), FISCO_ENGINE (8566)
set -u

C2="${C2:-/tmp/c2}"
ANVIL_PORT="${ANVIL_PORT:-8549}"
FISCO_ENGINE="${FISCO_ENGINE:-8566}"
OPNODE=$C2/op-node
LOG=$C2/op-node.log
PIDFILE=$C2/op-node.pid

# Stop existing instance if any
if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
    kill "$(cat "$PIDFILE")"
    sleep 1
fi
# Also catch orphans by command line
pgrep -f "op-node.*rollup.config $C2/rollup.json" | xargs kill 2>/dev/null
sleep 1

# Pre-flight: FISCO engine RPC must be up
if ! curl -s -o /dev/null "http://127.0.0.1:$FISCO_ENGINE"; then
    echo "FISCO engine RPC ($FISCO_ENGINE) not reachable — start FISCO first" >&2
    exit 1
fi
# Pre-flight: anvil L1 must be up
if ! curl -s -o /dev/null -X POST "http://127.0.0.1:$ANVIL_PORT" \
       -H 'Content-Type: application/json' \
       -d '{"jsonrpc":"2.0","method":"eth_chainId","params":[],"id":1}'; then
    echo "anvil L1 ($ANVIL_PORT) not reachable — start anvil first" >&2
    exit 1
fi

nohup "$OPNODE" \
  --rollup.config "$C2/rollup.json" \
  --rollup.l1-chain-config "$C2/l1_chain_config.json" \
  --l1 "http://127.0.0.1:$ANVIL_PORT" \
  --l2 "http://127.0.0.1:$FISCO_ENGINE" \
  --l2.jwt-secret "$C2/fisco/jwt.hex" \
  --l2.enginekind geth \
  --l1.beacon.ignore \
  --sequencer.enabled \
  --sequencer.l1-confs 1 \
  --p2p.sequencer.key "$(cat "$C2/sequencer.key")" \
  --log.level info \
  --log.format json \
  > "$LOG" 2>&1 &

echo $! > "$PIDFILE"
disown
echo "op-node started: PID $(cat "$PIDFILE"), log: $LOG"
