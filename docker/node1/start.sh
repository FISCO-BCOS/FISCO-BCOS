#!/bin/bash
fisco-bcos   --genesis /fisco-bcos/node/genesis.json --config /fisco-bcos/node/config.json > fisco-bcos.log 2>&1 
echo "waiting..."
sleep 5
