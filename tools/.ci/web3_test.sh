#!/bin/bash

pem_file="$1"
hex_privatekey=$(openssl ec -in "$pem_file" -text -noout | 
           grep -A3 'priv:' | 
           tail -n +2 | 
           tr -d ': \n' | 
           sed 's/^00//; s/$.\{64\}$$/\\1/')

# The hardhat suite is a hard gate (its exit code is propagated below), so its corpus must be
# reproducible: a floating HEAD means an upstream commit can turn this PR red, or silently stop
# covering what it used to. Pin the revision and never `git pull` on top of it.
# Bump deliberately, together with a node change that requires it.
BCOS_TESTING_REF="${BCOS_TESTING_REF:-f9b8338a46a2857f5ba64b4df74e981c9297fabc}"
git clone https://github.com/FISCO-BCOS/bcos-testing
cd bcos-testing
git checkout --quiet "$BCOS_TESTING_REF" || {
    echo "[ERROR] cannot check out bcos-testing at ${BCOS_TESTING_REF}" >&2; exit 1; }

# 缓存 node_modules，避免每次重复安装
if [ -d "node_modules" ] && [ -f "package.json" ]; then
    echo "[INFO] Using cached node_modules, running npm install --prefer-offline..."
    npm install --prefer-offline
else
    npm install
fi
npm install ethereum-cryptography
npm install node@22.14.0

cat << EOF > .env
PRIVATE_KEY=$hex_privatekey
BCOS_HOST_URL=http://127.0.0.1:8545
LOCAL_HOST_URL=http://127.0.0.1:8545
INFURA_API_KEY=your_infura_api_key_here
MAINNET_URL=https://mainnet.infura.io/v3/your_infura_api_key_here
SEPOLIA_URL=https://sepolia.infura.io/v3/your_infura_api_key_here
GOERLI_URL=https://goerli.infura.io/v3/your_infura_api_key_here
EOF

npx hardhat test --network bcosnet
# Propagate the hardhat result: everything below (the dormant websocket-env rewrite)
# would otherwise reset $? to 0 and turn 22 failing tests into "web3 test success".
hardhat_rc=$?


# websocket test
rm -rf .env

cat << EOF > .env
PRIVATE_KEY=$hex_privatekey
BCOS_HOST_URL=ws://127.0.0.1:8545
LOCAL_HOST_URL=ws://127.0.0.1:8545
INFURA_API_KEY=your_infura_api_key_here
MAINNET_URL=https://mainnet.infura.io/v3/your_infura_api_key_here
SEPOLIA_URL=https://sepolia.infura.io/v3/your_infura_api_key_here
GOERLI_URL=https://goerli.infura.io/v3/your_infura_api_key_here
EOF

# npx hardhat test test/tx/eth_sendRawTransaction* --network bcosnet

exit "${hardhat_rc}"