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

init() {
    cat >ipconf <<EOF
127.0.0.1:2 agencyA 1,2
127.0.0.1:2 agencyB 1
EOF
    fisco_version=$(../build/bin/fisco-bcos -v | grep -o "2\.[0-9]\.[0-9]")
    bash build_chain.sh -f ipconf -e ../build/bin/fisco-bcos -v ${fisco_version}
    bash check_node_config.sh -p nodes/127.0.0.1/node0
    cd nodes/127.0.0.1
}

send_transaction()
{
    LOG_INFO "==============send a transaction"
    if [ $(bash .transTest.sh | grep "result" | wc -l) -ne 1 ]; then
        LOG_ERROR "send transaction failed!"
        exit 1
    fi
    sleep 1.5
    LOG_INFO "==============send a transaction is ok"
}

check_reports()
{
    local num=$1
    local target=$2
    local errorMessage=$3
    local successdMessage=$4
    local total=$(cat node*/log/* | grep Report | grep "num=${num}" | wc -l)
    if [ ${total} -ne 4 ]; then
        LOG_ERROR "${errorMessage} ${total} != ${target}"
        cat node*/log/*
        exit 1
    else
        LOG_INFO "${successdMessage}"
    fi
}

check_sync_consensus()
{
    LOG_INFO "***************check_sync_consensus"
    bash start_all.sh
    bash ../../check_node_config.sh -p node0
    sleep 1
    send_transaction
    LOG_INFO "[round1]==============send a transaction is ok"
    LOG_INFO "[round1]==============check report block"
    check_reports 1 4 "check report block failed!" "[round1]==============check report block is ok"

    LOG_INFO "[round1]==============check sync block"
    bash stop_all.sh
    rm -rf node0/data node*/log
    bash start_all.sh && sleep 5
    check_reports 1 4 "[round1] sync block failed!" "[round1]==============check sync block is ok"

    LOG_INFO "[round2]==============restart all node"
    bash stop_all.sh
    rm -rf node*/log
    bash start_all.sh

    send_transaction
    LOG_INFO "[round2]==============send a transaction is ok"

    LOG_INFO "[round2]==============check report block"
    check_reports 2 4 "[round2] check report block failed!" "[round2]==============check report block is ok"

    LOG_INFO "[round2]==============check sync block"
    bash stop_all.sh
    rm -rf node0/data node*/log
    bash start_all.sh && sleep 5
    check_reports 2 4 "[round2] sync block failed!" "[round2]==============check sync block is ok"

    bash stop_all.sh
}

check_binarylog()
{
    LOG_INFO "***************check_binarylog"
    rm -rf node*/data node*/log
    local sed_cmd="sed -i"
    if [ "$(uname)" == "Darwin" ];then
        sed_cmd="sed -i .bkp"
    fi
    ${sed_cmd} "s/binary_log=false/binary_log=true/" node0/conf/group.1.ini
    ${sed_cmd} "s/binary_log=false/binary_log=true/" node1/conf/group.1.ini
    bash start_all.sh
    send_transaction
    check_reports 1 4 "check report block failed!" "==============check report block is ok"
    bash stop_all.sh
    rm -rf node0/data/ node*/log
    rm -rf node1/data/block
    bash start_all.sh && sleep 6
    check_reports 1 4 "sync block failed!" "==============check sync block is ok"
    bash stop_all.sh
}

init
check_sync_consensus
check_binarylog
