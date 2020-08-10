#!/bin/bash
SHELL_FOLDER="$(cd "$(dirname "$0")" || exit;pwd)"

LOG_ERROR() {
    content=${1}
    echo -e "\033[31m[ERROR] ${content}\033[0m"
}

LOG_INFO() {
    content=${1}
    echo -e "\033[32m[INFO] ${content}\033[0m"
}

cd "${SHELL_FOLDER}/../" || exit
NODE_FOLDER="$(pwd)"
fisco_bcos="${NODE_FOLDER}/../fisco-bcos"
node="$(basename "${NODE_FOLDER}")"
node_pid=$(pgrep  -f "${fisco_bcos}"|awk '{print $1}')
if [ -n "${node_pid}" ];then
    LOG_INFO "${node} is trying to reload sdk allowlist. Check log for more information."
    touch config.ini.reset_allowlist
    exit 0
else
    echo "${node} is not running, use start.sh to start all group directlly."
fi
