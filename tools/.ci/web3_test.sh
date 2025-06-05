#!/bin/bash

pem_file="$1"
hex_privatekey=$(openssl ec -in "$pem_file" -text -noout | 
           grep -A3 'priv:' | 
           tail -n +2 | 
           tr -d ': \n' | 
           sed 's/^00//; s/$.\{64\}$$/\\1/')

git clone https://github.com/FISCO-BCOS/bcos-testing
cd bcos-testing
npm install
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