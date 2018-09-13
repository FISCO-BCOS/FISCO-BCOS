#!/bin/sh

setsid ./fisco-bcos   --genesis `pwd`/genesis.json --config `pwd`/config.conf > fisco-bcos.log 2>&1 &
echo "waiting..."
sleep 5
tail -f `pwd`/log/info* |grep ++++
