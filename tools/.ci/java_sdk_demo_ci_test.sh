#!/bin/bash
console_branch=""
current_path=`pwd`

LOG_ERROR() {
    local content=${1}
    echo -e "\033[31m ${content}\033[0m"
}

LOG_INFO() {
    local content=${1}
    echo -e "\033[32m ${content}\033[0m"
}

download_console()
{
    cd ${current_path}

    LOG_INFO "Pull console, branch: ${console_branch} ..."
    console_file=console
    if [ -d ${console_file} ] && [ -d "${console_file}/.git" ]; then
        LOG_INFO "Use download cache"
        cd ${console_file}
        git fetch --all
        rm -rf build log dist
        git reset --hard
        # check if console_branch exists
        if [ -n "$(git branch -a | grep origin/${console_branch})" ]; then
            git checkout origin/${console_branch}
        else
            git checkout origin/master
        fi
        if [ $? -ne 0 ]; then
            cd ..
            rm -rf ${console_file}
            git clone https://github.com/FISCO-BCOS/console.git
        fi
    else
        rm -rf ${console_file}
        git clone https://github.com/FISCO-BCOS/console.git
        cd console
        if [ -n "$(git branch -a | grep origin/${console_branch})" ]; then
            git checkout origin/${console_branch}
        else
            git checkout origin/master
        fi
    fi
}

download_java_sdk_demo()
{
    cd ${current_path}
    local demo_branch=${console_branch}

    LOG_INFO "Pull java-sdk-demo, branch: ${demo_branch} ..."
    demo_dir=java-sdk-demo
    if [ -d ${demo_dir} ] && [ -d "${demo_dir}/.git" ]; then
        LOG_INFO "Use download cache"
        cd ${demo_dir}
        git fetch --all
        git reset --hard
        # check if demo_branch exists
        if [ -n "$(git branch -a | grep origin/${demo_branch})" ]; then
            git checkout origin/${demo_branch}
        else
            git checkout origin/master
        fi
        if [ $? -ne 0 ]; then
            cd ..
            rm -rf ${demo_dir}
            git clone https://github.com/FISCO-BCOS/java-sdk-demo.git
        fi
    else
        rm -rf ${demo_dir}
        git clone https://github.com/FISCO-BCOS/java-sdk-demo.git
        cd console
        if [ -n "$(git branch -a | grep origin/${demo_branch})" ]; then
            git checkout origin/${demo_branch}
        else
            git checkout origin/master
        fi
    fi
}

config_console()
{
    cd "${current_path}/console/"
    local use_sm="${1}"
    local node_path="${2}"
    local sed_cmd="sed -i"
    if [ "$(uname)" == "Darwin" ];then
        sed_cmd="sed -i .bkp"
    fi

    rm -rf dist
    # use 0.6 solc
    ${sed_cmd} "s/org.fisco-bcos:solcJ:0.8.11.1/org.fisco-bcos:solcJ:0.6.10.1/g" build.gradle
    bash gradlew ass
    git reset --hard # modify back

    cp -r ${node_path}/sdk/* ./dist/conf/
    cp ./dist/conf/config-example.toml ./dist/conf/config.toml
    ${sed_cmd} "s/peers=\[\"127.0.0.1:20200\", \"127.0.0.1:20201\"\]/peers=\[\"127.0.0.1:20201\"\]/g" ./dist/conf/config.toml
    ${sed_cmd} "s/messageTimeout = \"10000\"/messageTimeout = \"10000000\"/g" ./dist/conf/config.toml

    # config test contract
    cp -r  ${current_path}/java-sdk-demo/src/main/java/org/fisco/bcos/sdk/demo/contract/sol/* dist/contracts/solidity/
    if [ "${use_sm}" == "true" ]; then
        ${sed_cmd} "s/ECRecoverTest/ECRecoverSMTest/g" dist/contracts/solidity/ContractTestAll.sol
    fi

    # config admin
    [ -d ${node_path}/../ca/accounts ] && echo "copy account" && mkdir -p ./dist/account/ecdsa/ && cp -r ${node_path}/../ca/accounts/* ./dist/account/ecdsa/
    [ -d ${node_path}/../ca/accounts_gm ] && echo "copy account_gm" && mkdir -p ./dist/account/gm/ && cp -r ${node_path}/../ca/accounts_gm/* ./dist/account/gm/

    local not_use_sm="true"
    if [ "${use_sm}" == "true" ]; then
        not_use_sm="false"
    fi
    use_sm_str="useSMCrypto = \"${use_sm}\""
    ${sed_cmd} "s/useSMCrypto = \"${not_use_sm}\"/${use_sm_str}/g" ./dist/conf/config.toml
    LOG_INFO "Build and Config console success ..."
}

config_java_sdk_demo()
{
    cd "${current_path}/java-sdk-demo/"
    local use_sm="${1}"
    local node_path="${2}"
    local sed_cmd="sed -i"
    if [ "$(uname)" == "Darwin" ];then
        sed_cmd="sed -i .bkp"
    fi

    rm -rf dist
    bash gradlew ass

    cp -r ${node_path}/sdk/* ./dist/conf/
    cp ./dist/conf/config-example.toml ./dist/conf/config.toml
    ${sed_cmd} "s/peers=\[\"127.0.0.1:20200\", \"127.0.0.1:20201\"\]/peers=\[\"127.0.0.1:20201\"\]/g" ./dist/conf/config.toml
    ${sed_cmd} "s/messageTimeout = \"10000\"/messageTimeout = \"10000000\"/g" ./dist/conf/config.toml

    # config admin
    [ -d ${node_path}/../ca/accounts ] && echo "copy account" && mkdir -p ./dist/account/ecdsa/ && cp -r ${node_path}/../ca/accounts/* ./dist/account/ecdsa/
    [ -d ${node_path}/../ca/accounts_gm ] && echo "copy account_gm" && mkdir -p ./dist/account/gm/ && cp -r ${node_path}/../ca/accounts_gm/* ./dist/account/gm/

    local not_use_sm="true"
    if [ "${use_sm}" == "true" ]; then
        not_use_sm="false"
    fi
    use_sm_str="useSMCrypto = \"${use_sm}\""
    ${sed_cmd} "s/useSMCrypto = \"${not_use_sm}\"/${use_sm_str}/g" ./dist/conf/config.toml
    LOG_INFO "Build and Config java-sdk-demo success ..."
}

check_all_contract() {

    local test_contract_address=$(bash console.sh deploy ContractTestAll |grep "contract address" |awk '{print $3}')

    if [ -z "$test_contract_address" ]; then
        LOG_ERROR  "java-sdk-demo contract test, deploy contract failed:"
        bash console.sh deploy ContractTestAll
        exit 1;
    fi

    LOG_INFO "check block gasUsed"
    local current_block_number=$(bash console.sh getBlockNumber|grep -v error)
    LOG_INFO "check block gasUsed, current number is ${current_block_number}"
    local gas_used=$(bash console.sh getBlockByNumber ${current_block_number} |grep gasUsed |awk -F "'" '{print $2}')
    if [ ${gas_used} -ne 0 ]; then
        LOG_INFO "check block gasUsed success, current gas is ${gas_used}"
    else
        LOG_ERROR "check block gasUsed failed, gas is ${gas_used}"
        exit 1;
    fi
    
    LOG_INFO "addBalance to contract ${test_contract_address}"
    bash console.sh addBalance ${test_contract_address} 10000000

    LOG_INFO "run ContractTestAll " ${test_contract_address}
    local output=$(bash console.sh call ContractTestAll ${test_contract_address} check)

    if [[ $output == *"transaction status: 0"* ]]; then
        LOG_INFO "java-sdk-demo contract test success"
    else
        LOG_ERROR "java-sdk-demo contract test error:"
        bash console.sh call ContractTestAll ${test_contract_address} check
        exit 1
    fi
}

check_all_dmc_transfer()
{
    cd "${current_path}/java-sdk-demo/dist/"

    java -cp 'conf/:lib/*:apps/*' org.fisco.bcos.sdk.demo.perf.DMCTransferDag group0 4 50 10 true
    java -cp 'conf/:lib/*:apps/*' org.fisco.bcos.sdk.demo.perf.DMCTransferDag group0 4 50 10 false
    java -cp 'conf/:lib/*:apps/*' org.fisco.bcos.sdk.demo.perf.DMCTransferMyself group0 3 50 10 true
    java -cp 'conf/:lib/*:apps/*' org.fisco.bcos.sdk.demo.perf.DMCTransferMyself group0 3 50 10 false
    java -cp 'conf/:lib/*:apps/*' org.fisco.bcos.sdk.demo.perf.DMCTransferStar group0 5 50 10 true
    java -cp 'conf/:lib/*:apps/*' org.fisco.bcos.sdk.demo.perf.DMCTransferStar group0 5 50 10 false
}

contract_test()
{
    cd "${current_path}/console/dist/"

    # get current account
    local account=$(bash console.sh listAccount |grep "current account" |awk -F '(' '{print $1}')

    LOG_INFO "set feature_balance"
    bash console.sh setSystemConfigByKey feature_balance 1

    LOG_INFO "set feature_balance_precompiled"
    bash console.sh setSystemConfigByKey feature_balance_precompiled 1

    LOG_INFO "addBalance to account ${account}"
    bash console.sh addBalance ${account} 10000000000

    LOG_INFO "\n=====> Test default mode"
    check_all_contract

    LOG_INFO "\n=====> Test serial: set feature_dmc2serial"
    bash console.sh setSystemConfigByKey feature_dmc2serial 1
    check_all_contract

    LOG_INFO "\n=====> Test sharding: set feature_sharding"
    bash console.sh setSystemConfigByKey feature_sharding 1
    check_all_contract
}


if [ $# != 3 ]; then
    echo "USAGE: ${0} console_branch is_sm node_path"
    echo " e.g.: ${0} master true ./nodes"
    exit 1;
fi

console_branch="${1}"
download_java_sdk_demo
download_console
config_console "${2}" "${3}"
config_java_sdk_demo "${2}" "${3}"
contract_test
check_all_dmc_transfer