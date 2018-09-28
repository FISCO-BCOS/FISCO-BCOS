#!/bin/bash
setsid fisco-bcos   --genesis `pwd`/genesis.json --config `pwd`/config.json > fisco-bcos.log 2>&1 &
