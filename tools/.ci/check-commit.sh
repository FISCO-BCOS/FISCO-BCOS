#!/bin/bash
# "Copyright [2018] <fisco-bcos>"
# @ function: check code format of {.h, .hpp and .cpp} files
# @ require : Make sure your machine is linux (centos/ubuntu), yum or apt is ready
# @ author  : wheatli
# @ file    : check-commit.sh
# @ date    : 2018

SHELL_FOLDER=$(
    cd $(dirname $0)
    pwd
)

check_script="clang-format"
commit_limit=10
file_limit=35
insert_limit=300
license_line=20

skip_check_words="sync code"

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
    if [ $ret -ne 0 ]; then
        LOG_ERROR "FAILED of command: ${command}"
        exit 1
    else
        LOG_INFO "SUCCESS of command: ${command}"
    fi
}

function check_codeFormat() {
    # Redirect output to stderr.
    exec 1>&2
    sum=0
    for file in $(git diff-index --name-status HEAD^ -- | grep -v D | grep -E '\.[ch](pp)?$' | awk '{print $2}'); do
        execute_cmd "$check_script -style=file -i $file"
        sum=$(expr ${sum} + $?)
    done

    if [ ${sum} -ne 0 ]; then
        echo "######### ERROR: Format check failed, please adjust them before commit"
        exit 1
    fi
}

function check_PR_limit() {
    if [ "${PR_TITLE}" != "" ]; then
        local skip=$(echo ${PR_TITLE} | grep "${skip_check_words}")
        if [ ! -z "${skip}" ]; then
            LOG_INFO "sync code PR, skip PR limit check!"
            exit 0
        else
            LOG_INFO "PR: \"${PR_TITLE}\", checking limit..."
        fi
    else
        LOG_INFO "Could not get PR title"
        exit 1
    fi
    local files=$(git diff --shortstat HEAD^ | awk -F ' ' '{print $1}')
    # if [ ${file_limit} -lt ${files} ]; then
    #     LOG_ERROR "modify ${files} files, limit is ${file_limit}"
    #     exit 1
    # fi
    local need_check_files=$(git diff --numstat HEAD^ |awk '{print $3}'|grep -vE 'test|tools\/|fisco-bcos\/|.github\/')
    echo "need check files:"
    echo "${need_check_files}"

    if [ ! "${need_check_files}" ]; then
        LOG_INFO "No file changed. Ok!"
        exit 0
    fi

    local new_files=$(git diff HEAD^ $(echo "${need_check_files}") | grep "new file" | wc -l | xargs )
    local empty_lines=$(git diff HEAD^ $(echo "${need_check_files}") | grep -e '^+\s*$' | wc -l | xargs)
    local block_lines=$(git diff HEAD^ $(echo "${need_check_files}") | grep -e '^+\s*[\{\}]\s*$' | wc -l | xargs)
    local include_lines=$(git diff HEAD^ $(echo "${need_check_files}") | grep -e '^+\#include' | wc -l | xargs)
    local comment_lines=$(git diff HEAD^ $(echo "${need_check_files}") | grep -e "^+\s*\/\/" | wc -l | xargs)
    local insertions=$(git diff --shortstat HEAD^ $(echo "${need_check_files}")| awk -F ' ' '{print $4}')
    local valid_insertions=$((insertions - new_files * license_line - comment_lines - empty_lines - block_lines - include_lines))
    echo "valid_insertions: ${valid_insertions}, insertions(${insertions}) - new_files(${new_files}) * license_line(${license_line}) - comment_lines(${comment_lines}) - empty_lines(${empty_lines}) - block_lines(${block_lines}) - include_lines(${include_lines})"
    if [ ${insert_limit} -lt ${valid_insertions} ]; then
        LOG_ERROR "insert ${insertions} lines, valid is ${valid_insertions}, limit is ${insert_limit}"
        exit 1
    fi
    local deletions=$(git diff --shortstat HEAD^ | awk -F ' ' '{print $6}')
    #if [ ${delete_limit} -lt ${deletions} ];then
    #    LOG_ERROR "delete ${deletions} lines, limit is ${delete_limit}"
    #    exit 1
    #fi
    local commits=$(git rev-list --count HEAD^..HEAD)
    if [ ${commit_limit} -lt ${commits} ]; then
        LOG_ERROR "${commits} commits, limit is ${commit_limit}"
        exit 1
    fi
    local unique_commit=$(git log --format="%an %s" HEAD^..HEAD | sort -u | wc -l)
    if [ ${unique_commit} -ne ${commits} ]; then
        LOG_ERROR "${commits} != ${unique_commit}, please make commit message unique!"
        exit 1
    fi
    local merges=$(git log --format=%s HEAD^..HEAD | grep -i merge | wc -l)
    if [ ${merges} -gt 5 ]; then
        LOG_ERROR "PR contain merge : ${merges}, Please rebase!"
        exit 1
    fi
    LOG_INFO "modify ${files} files, insert ${insertions} lines, valid insertion ${valid_insertions}, delete ${deletions} lines. Total ${commits} commits."
    LOG_INFO "Ok!"
}

check_codeFormat
check_PR_limit
