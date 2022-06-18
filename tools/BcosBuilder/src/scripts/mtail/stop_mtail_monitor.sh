#!/bin/bash
SHELL_FOLDER=$(cd $(dirname $0);pwd)

LOG_ERROR() {
    content=${1}
    echo -e "\033[31m[ERROR] ${content}\033[0m"
}

LOG_INFO() {
    content=${1}
    echo -e "\033[32m[INFO] ${content}\033[0m"
}

mtail=${SHELL_FOLDER}/mtail
node=$(basename ${SHELL_FOLDER})
node_pid=$(ps aux|grep ${mtail}|grep -v grep|awk '{print $2}')
try_times=10
i=0
if [ -z ${node_pid} ];then
    echo " ${node} isn't running."
    exit 0
fi
[ ! -z ${node_pid} ] && kill ${node_pid} > /dev/null

while [ $i -lt ${try_times} ]
do
    sleep 1
    node_pid=$(ps aux|grep ${mtail}|grep -v grep|awk '{print $2}')
    if [ -z ${node_pid} ];then
        echo -e "\033[32m stop ${node} success.\033[0m"
        exit 0
    fi
    ((i=i+1))
done
echo "  Exceed maximum number of retries. Please try again to stop ${node}"
exit 1
