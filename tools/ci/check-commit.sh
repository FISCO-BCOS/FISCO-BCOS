# "Copyright [2018] <fisco-bcos>"
# @ function: check code format of {.h, .hpp and .cpp} files
# @ require : Make sure your machine is linux (centos/ubuntu), yum or apt is ready
# @ author  : wheatli
# @ file    : check-commit.sh
# @ date    : 2018

# !/bin/bash
SHELL_FOLDER=$(cd $(dirname $0);pwd)
check_script=${SHELL_FOLDER}/run-clang-format.py
file_limit=30
insert_limit=300
delete_limit=500
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
    eval ${command}
    ret=$?
    if [ $ret -ne 0 ];then
        LOG_ERROR "FAILED of command: ${command}"
        exit 1
    else
        LOG_INFO "SUCCESS of command: ${command}"
    fi
}

function init()
{
    if git rev-parse --verify HEAD >/dev/null 2>&1;then
        against=HEAD^
    else
        # Initial commit: diff against an empty tree object
        against=4b825dc642cb6eb9a060e54bf8d69288fbee4904
    fi
    LOG_INFO "against: ${against}"
}

function check_codeFormat()
{
    # Redirect output to stderr.
    exec 1>&2
    sum=0
    for file in $(git diff-index --name-status $against -- | grep -v D | grep -E '\.[ch](pp)?$' | awk '{print $2}'); do
        execute_cmd "$check_script $file"
        LOG_INFO "=== file: ${file}"
        sum=$(expr ${sum} + $?)
    done

    if [ ${sum} -ne 0 ]; then
        echo "######### ERROR: Format check failed, please adjust them before commit"
        exit 1
    fi
}

function check_PR_limit()
{
    local files=$(git diff --shortstat HEAD^ | awk  -F ' '  '{print $1}')
    if [ ${file_limit} -lt ${files} ];then
        LOG_ERROR "modify ${files} files, limit is ${file_limit}"
        exit 1
    fi
    local insertions=$(git diff --shortstat HEAD^ | awk  -F ' '  '{print $4}')
    if [ ${insert_limit} -lt ${insertions} ];then
        LOG_ERROR "insert ${insertions} lines, limit is ${insert_limit}"
        exit 1
    fi
    local deletions=$(git diff --shortstat HEAD^ | awk  -F ' '  '{print $6}')
    if [ ${delete_limit} -lt ${deletions} ];then
        LOG_ERROR "delete ${deletions} lines, limit is ${delete_limit}"
        exit 1
    fi
    LOG_INFO "modify ${files} files, insert ${insertions} lines, delete ${deletions} lines."
}

init
check_PR_limit
check_codeFormat
