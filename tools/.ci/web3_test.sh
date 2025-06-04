#!/bin/bash

git clone https://github.com/FISCO-BCOS/bcos-testing
cd bcos-testing
npm install
npm install ethereum-cryptography
npm install node@22.14.0

cat << EOF > .env
PRIVATE_KEY=b0b9d33d8558ffb74cfa501426a1652bd4cb3452d49faad9b235c42f66d39e33
BCOS_HOST_URL=http://127.0.0.1:8545
LOCAL_HOST_URL=http://127.0.0.1:8545
INFURA_API_KEY=your_infura_api_key_here
MAINNET_URL=https://mainnet.infura.io/v3/your_infura_api_key_here
SEPOLIA_URL=https://sepolia.infura.io/v3/your_infura_api_key_here
GOERLI_URL=https://goerli.infura.io/v3/your_infura_api_key_here
EOF

npx hardhat test --network bcosnet