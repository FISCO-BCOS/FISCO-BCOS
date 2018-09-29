#!/bin/sh

setsid `pwd`/fisco-bcos  --config config.conf --genesis genesis.json > fisco-bcos.log 2>&1 &
echo "waiting..."
sleep 5
tail -f `pwd`/log/info* |grep ++++
