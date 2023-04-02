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
    rm -rf console
    console_file=console
    if [ -d ${console_file} ]; then
        LOG_INFO "Use download cache"
        cd ${console_file}
        rm -rf build log
        git reset --hard
        git checkout ${console_branch}
        git pull
    else
        git clone -b ${console_branch} --depth 5  https://ghproxy.com/github.com/FISCO-BCOS/console.git
    fi
}

config_console()
{
    cd "${current_path}/console/"
    local use_sm="${1}"
    local node_path="${2}"
#    cp -r ${current_path}/nodes/127.0.0.1/sdk/* conf/
#    cp conf/config-example.toml conf/config.toml
    local sed_cmd="sed -i"
    if [ "$(uname)" == "Darwin" ];then
        sed_cmd="sed -i .bkp"
    fi
    mkdir -p "./src/integration-test/resources/conf"
    cp -r ${node_path}/127.0.0.1/sdk/* ./src/integration-test/resources/conf
    cp ./src/integration-test/resources/config-example.toml ./src/integration-test/resources/config.toml
    cp -r ./src/main/resources/contract ./contracts

    use_sm_str="useSMCrypto = \"${use_sm}\""
    ${sed_cmd} "s/useSMCrypto = \"false\"/${use_sm_str}/g" ./src/integration-test/resources/config.toml
    LOG_INFO "Build and Config console success ..."
}

console_integrationTest()
{
    bash gradlew integrationTest --info
    local re=${?}
    if [ ${re} == "0" ]; then
        LOG_INFO "console_integrationTest success"
    else
        echo "console_integrationTest error"
        exit 1
    fi
}

if [ $# != 3 ]; then
  echo "USAGE: ${0} console_branch is_sm node_path"
  echo " e.g.: ${0} master true ./nodes"
  exit 1;
fi

console_branch="${1}"
download_console
config_console "${2}" "${3}"
console_integrationTest