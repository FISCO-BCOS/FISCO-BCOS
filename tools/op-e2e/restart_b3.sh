#!/usr/bin/env bash
# Restart the B3 node WITHOUT wiping storage — the persistence check (spec B.4) needs the same
# RocksDB across restarts. setup_b3.sh rm -rf's the data dirs, so it must NOT be used here.
set -u
WORK=${WORK:-/tmp/op-spike/b3}
BINARY=${BINARY:-$(cd "$(dirname "$0")/../.." && pwd)/build/fisco-bcos-air/fisco-bcos}

[ -f "$WORK/node.pid" ] && kill "$(cat "$WORK/node.pid")" 2>/dev/null
# Wait for the old process to free the RocksDB lock before relaunching.
for _ in $(seq 1 20); do
  kill -0 "$(cat "$WORK/node.pid" 2>/dev/null)" 2>/dev/null || break
  sleep 0.5
done

cd "$WORK"
rm -f nohup.out
nohup "$BINARY" -c config.genesis -g config.genesis > nohup.out 2>&1 &
echo $! > node.pid
sleep 8
kill -0 "$(cat node.pid)" 2>/dev/null && echo "RUNNING (storage preserved)" || echo "EXITED"
