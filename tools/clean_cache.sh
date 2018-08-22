 # "Copyright [2018] <fisco-bcos>"
 # @ function: clean built package when rebuilding
 # @ author  : yujiechen
 # @ file    : clean_cache.sh
 # @ date    : 2018

#!/bin/bash

current_dir=`pwd`"/.."
LOG_ERROR() {
    content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

LOG_INFO() {
    content=${1}
    echo -e "\033[32m"${content}"\033[0m"
}

execute_cmd() {
    command="${1}"
    #LOG_INFO "RUN: ${command}"
    eval ${command}
    ret=$?
    if [ $ret -ne 0 ];then
        LOG_ERROR "FAILED execution of command: ${command}"
        exit 1
    else
        LOG_INFO "SUCCESS execution of command: ${command}"
    fi
}

function clean_deps() {
    execute_cmd "cd ${current_dir}"
    execute_cmd "rm -rf deps/src/boost"
    execute_cmd "rm -rf deps/src/*build"
    execute_cmd "rm -rf deps/src/*stamp"
}

function clean_build() {
    execute_cmd "cd ${current_dir}"
    execute_cmd "rm -rf build"
}

clean_build
clean_deps
