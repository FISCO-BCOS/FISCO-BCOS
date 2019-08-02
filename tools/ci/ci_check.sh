#!/bin/bash

set -e
LOG_ERROR() {
    local content=${1}
    echo -e "\033[31m ${content}\033[0m"
}

LOG_INFO() {
    local content=${1}
    echo -e "\033[32m ${content}\033[0m"
}

cat >ipconf <<EOF
127.0.0.1:2 agencyA 1,2
127.0.0.1:2 agencyB 1
EOF

fisco_version=$(../build/bin/fisco-bcos -v | grep -o "2\.[0-9]\.[0-9]")
bash build_chain.sh -f ipconf -e ../build/bin/fisco-bcos -v ${fisco_version}
cd nodes/127.0.0.1
bash start_all.sh
sleep 1

LOG_INFO "[round1]==============send a transaction"
if [ $(bash .transTest.sh | grep "result" | wc -l) -ne 1 ]; then
    LOG_ERROR "send transaction failed!"
    exit 1
fi
LOG_INFO "[round1]==============send a transaction is ok"

LOG_INFO "[round1]==============check report block"
sleep 2
num=$(cat node*/log/* | grep Report | grep "num=1" | wc -l)
if [ ${num} -ne 4 ]; then
    LOG_ERROR "check report block failed! ${num} != 4"
    cat node*/log/*
    exit 1
fi
LOG_INFO "[round1]==============check report block is ok"

LOG_INFO "[round1]==============check sync block"
bash stop_all.sh
rm -rf node0/data node*/log
bash start_all.sh
sleep 5
num=$(cat node*/log/* | grep Report | grep "num=1" | wc -l)
if [ ${num} -ne 4 ]; then
    LOG_ERROR "[round1] sync block failed! ${num} != 4"
    cat node*/log/*
    exit 1
fi
LOG_INFO "[round1]==============check sync block is ok"

LOG_INFO "[round2]==============restart all node"
bash stop_all.sh
rm -rf node*/log
bash start_all.sh

LOG_INFO "[round2]==============send a transaction"
if [ $(bash .transTest.sh | grep "result" | wc -l) -ne 1 ]; then
    LOG_ERROR "[round1] send transaction failed!"
    exit 1
fi
LOG_INFO "[round2]==============send a transaction is ok"

LOG_INFO "[round2]==============check report block"
sleep 4
num=$(cat node*/log/* | grep Report | grep "num=2" | wc -l)
if [ ${num} -ne 4 ]; then
    LOG_ERROR "[round2] check report block failed! ${num} != 4"
    cat node*/log/*
    exit 1
fi
LOG_INFO "[round2]==============check report block is ok"

LOG_INFO "[round2]==============check sync block"
bash stop_all.sh
rm -rf node0/data node*/log
bash start_all.sh
sleep 10
num=$(cat node*/log/* | grep Report | grep "num=2" | wc -l)
if [ ${num} -ne 4 ]; then
    LOG_ERROR "[round2] sync block failed! ${num} != 4"
    cat node*/log/*
    exit 1
fi
LOG_INFO "[round2]==============check sync block is ok"

bash stop_all.sh
