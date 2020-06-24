#!/bin/bash

latest_version=$(curl -s https://api.github.com/repos/FISCO-BCOS/FISCO-BCOS/releases | grep "\"v2\.[0-9]\.[0-9]\"" | sort -V | tail -n 1 | cut -d \" -f 4)
curl -LO https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/${latest_version}/build_chain.sh && chmod u+x build_chain.sh
