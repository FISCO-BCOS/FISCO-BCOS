#!/bin/bash

set -e
LOG_ERROR()
{
    local content=${1}
    echo -e "\033[31m ${content}\033[0m"
}

LOG_INFO()
{
    local content=${1}
    echo -e "\033[32m ${content}\033[0m"
}
pip install requests
bash build_chain.sh -l "127.0.0.1:4" -e ../build/bin/fisco-bcos -v 2.0.0-rc3
cd nodes/127.0.0.1
bash start_all.sh
sleep 1

LOG_INFO "[round1]==============send a transaction"
if [ $(bash .transTest.sh | grep "result" | wc -l) -ne 1 ];then
    LOG_ERROR "send transaction failed!"
    exit 1
fi
LOG_INFO "[round1]==============send a transaction is ok"

LOG_INFO "[round1]==============check report block"
sleep 2
num=$(cat node*/log/* | grep Report | wc -l)
if [ ${num} -ne 8 ];then
    LOG_ERROR "check report block failed! ${num} != 8"
    cat node*/log/* 
    exit 1
fi
LOG_INFO "[round1]==============check report block is ok"

LOG_INFO "[round1]==============check sync block"
bash stop_all.sh
rm -rf node0/data node0/log
bash start_all.sh
sleep 5
num=$(cat node*/log/* | grep Report | wc -l)
if [ ${num} -ne 11 ];then
    LOG_ERROR "[round1] sync block failed! ${num} != 11"
    cat node*/log/* 
    exit 1
fi
LOG_INFO "[round1]==============check sync block is ok"

LOG_INFO "[round2]==============restart all node"
bash stop_all.sh
bash start_all.sh

LOG_INFO "[round2]==============send a transaction"
if [ $(bash .transTest.sh | grep "result" | wc -l) -ne 1 ];then
    LOG_ERROR "[round1] send transaction failed!"
    exit 1
fi
LOG_INFO "[round2]==============send a transaction is ok"

LOG_INFO "[round2]==============check report block"
sleep 2
num=$(cat node*/log/* | grep Report | wc -l)
if [ ${num} -ne 8 ];then
    LOG_ERROR "[round2] check report block failed! ${num} != 8"
    cat node*/log/* 
    exit 1
fi
LOG_INFO "[round2]==============check report block is ok"

LOG_INFO "[round2]==============check sync block"
bash stop_all.sh
rm -rf node0/data node0/log
bash start_all.sh
sleep 5
num=$(cat node*/log/* | grep Report | wc -l)
if [ ${num} -ne 11 ];then
    LOG_ERROR "[round2] sync block failed! ${num} != 11"
    cat node*/log/* 
    exit 1
fi
LOG_INFO "[round2]==============check sync block is ok"

cd ../..
python ci/ci_check.py

bash nodes/127.0.0.1/stop_all.sh 2 > /dev/null
