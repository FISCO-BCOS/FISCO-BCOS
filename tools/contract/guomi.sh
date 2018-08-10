#!/bin/bash
rm -rf node_modules/web3/lib/utils/sha3.js
cp node_modules_sha3.js node_modules/web3/lib/utils/sha3.js
echo "execute success"