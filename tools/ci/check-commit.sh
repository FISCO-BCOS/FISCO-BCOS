# "Copyright [2018] <fisco-bcos>"
# @ function: check code format of {.h, .hpp and .cpp} files
# @ require : Make sure your machine is linux (centos/ubuntu), yum or apt is ready
# @ author  : wheatli
# @ file    : check-commit.sh
# @ date    : 2018

# !/bin/bash
SHELL_FOLDER=$(cd $(dirname $0);pwd)
check_script=${SHELL_FOLDER}/run-clang-format.py
Ubuntu_Platform=0
Centos_Platform=1

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

function check()
{
    if git rev-parse --verify HEAD >/dev/null 2>&1;then
        against=HEAD^
    else
        # Initial commit: diff against an empty tree object
        against=4b825dc642cb6eb9a060e54bf8d69288fbee4904
    fi
    LOG_INFO "against: ${against}"
    # Redirect output to stderr.
    exec 1>&2
    sum=0
    git diff-index --name-status $against -- | grep -v D | grep -E '\.[ch](pp)?$' | awk '{print $2}'
    # for check-script
    for file in $(git diff-index --name-status $against -- | grep -v D | grep -E '\.[ch](pp)?$' | awk '{print $2}'); do
        execute_cmd "$check_script $file"
        LOG_INFO "=== file: ${file}"
        sum=$(expr ${sum} + $?)
    done

    if [ ${sum} -eq 0 ]; then
        exit 0
    else
        echo "######### ERROR: Format check failed, please adjust them before commit"
        exit 1
    fi
}

check
