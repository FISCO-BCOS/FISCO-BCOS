#!/bin/bash
java_sdk_branch="master"
current_path=`pwd`

LOG_ERROR() {
    local content=${1}
    echo -e "\033[31m ${content}\033[0m"
}

LOG_INFO() {
    local content=${1}
    echo -e "\033[32m ${content}\033[0m"
}

download_java_sdk()
{
    cd ${current_path}
    
    LOG_INFO "Pull java sdk, branch: ${java_sdk_branch} ..."
    local java_sdk_file=java-sdk
    if [ -d ${java_sdk_file} ] && [ -d "${java_sdk_file}/.git" ]; then
        LOG_INFO "Use download cache"
        cd ${java_sdk_file}
        git fetch --all
        rm -rf build log
        git reset --hard
        if [ -n "$(git branch -a | grep origin/${java_sdk_branch})" ]; then
            git checkout origin/${java_sdk_branch}
        else
            git checkout origin/master
        fi
        if [ $? -ne 0 ]; then
            cd ..
            rm -rf ${java_sdk_file}
            git clone -b ${java_sdk_branch} https://github.com/FISCO-BCOS/java-sdk.git
        fi
    else
        rm -rf ${java_sdk_file}
        git clone https://github.com/FISCO-BCOS/java-sdk.git || exit 1
        cd java-sdk || exit 1
        if [ -n "$(git branch -a | grep origin/${java_sdk_branch})" ]; then
            git checkout origin/${java_sdk_branch}
        else
            git checkout origin/master
        fi
    fi
}

config_java_sdk()
{
    cd "${current_path}/java-sdk/"
    local use_sm="${1}"
    local node_path="${2}"
    local sed_cmd="sed -i"
    if [ "$(uname)" == "Darwin" ];then
        sed_cmd="sed -i .bkp"
    fi
    mkdir -p "./src/integration-test/resources/conf"
    cp -r ${node_path}/sdk/* ./src/integration-test/resources/conf
    cp ./src/test/resources/config-example.toml ./src/integration-test/resources/config.toml
    cp ./src/test/resources/clog.ini ./src/integration-test/resources/conf
    rm -rf src/integration-test/resources/abi
    rm -rf src/integration-test/resources/bin
    local not_use_sm="true"
    if [ "${use_sm}" == "true" ]; then
        not_use_sm="false"
        cp -r ./src/test/resources/gm/abi ./src/integration-test/resources/abi
        cp -r ./src/test/resources/gm/bin ./src/integration-test/resources/bin
    else
        cp -r ./src/test/resources/ecdsa/abi ./src/integration-test/resources/abi
        cp -r ./src/test/resources/ecdsa/bin ./src/integration-test/resources/bin
    fi
    
    use_sm_str="useSMCrypto = \"${use_sm}\""
    ${sed_cmd} "s/useSMCrypto = \"${not_use_sm}\"/${use_sm_str}/g" ./src/integration-test/resources/config.toml
    ${sed_cmd} "s/useSMCrypto = \"${not_use_sm}\"/${use_sm_str}/g" ./src/integration-test/resources/amop/config-subscriber-for-test.toml
    ${sed_cmd} "s/useSMCrypto = \"${not_use_sm}\"/${use_sm_str}/g" ./src/integration-test/resources/amop/config-publisher-for-test.toml
    LOG_INFO "Build and Config java sdk success ..."
}

java_sdk_integration_test()
{
    bash gradlew integrationTest --info
    local re=${?}
    if [ ${re} == "0" ]; then
        LOG_INFO "java_sdk_integration_test success"
    else
        echo "java_sdk_integration_test error"
        exit 1
    fi
}

if [ $# != 3 ]; then
    echo "USAGE: ${0} java_sdk_branch is_sm node_path"
    echo " e.g.: ${0} master true ./nodes"
    exit 1;
fi

java_sdk_branch="${1}"
download_java_sdk
config_java_sdk "${2}" "${3}"
java_sdk_integration_test