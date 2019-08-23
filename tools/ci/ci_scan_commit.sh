#!/bin/bash

set -e

scan_code_script="python ~/cobra/cobra.py -t "

LOG_ERROR() {
    content=${1}
    echo -e "\033[31m${content}\033[0m"
}

LOG_INFO() {
    content=${1}
    echo -e "\033[32m${content}\033[0m"
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

scan_code()
{
    # Redirect output to stderr.
    exec 1>&2
    for file in $(git diff-index --name-status HEAD^ | grep -v .ci | awk '{print $2}'); do
        execute_cmd "${scan_code_script} $file -f json -o /tmp/report.json"
        trigger_rules=$(jq -r '.' /tmp/report.json | grep 'trigger_rules' | awk '{print $2}' | sed 's/,//g')
        echo "trigger_rules is ${trigger_rules}"
        rm /tmp/report.json
        if [ ${trigger_rules} -ne 0 ]; then
            echo "######### ERROR: Scan code failed, please adjust them before commit"
            exit 1
        fi
    done
}

install_cobra() {
   git clone https://github.com/WhaleShark-Team/cobra.git ~/cobra
   pip install -r ~/cobra/requirements.txt
   cp ~/cobra/config.template ~/cobra/config
}

install_cobra
scan_code
