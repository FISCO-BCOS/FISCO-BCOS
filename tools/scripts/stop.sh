#!/bin/bash
dirpath="$(cd "$(dirname "$0")" && pwd)"
cd $dirpath

#stop tail -f log
name=`pwd`/log/info
agent_pid=`ps aux|grep "$name"|grep -v grep|awk '{print $2}'`
kill_cmd="kill -9 ${agent_pid}"
if [ ! -z "${agent_pid}" ];then
    echo "${kill_cmd}"
    eval ${kill_cmd}
fi

# stop node
node=$(basename ${dirpath})
pid=`ps aux|grep "${dirpath}/config.json"|grep "fisco-bcos"|grep -v grep|awk '{print $2}'`

if [ -z "$pid" ];then
    echo " ${node} is not running."
    exit 0
fi

kill_cmd="kill -2 ${pid}"
try_times=10
i=0
while [ $i -lt ${try_times} ]
do
    eval ${kill_cmd}
    sleep 1
    pid=`ps aux|grep "${dirpath}/config.json"|grep "fisco-bcos"|grep -v grep|awk '{print $2}'`
    if [ -z $pid ];then
        echo " stop ${node} successfully. "
        exit 0
    fi
    ((i=i+1))
done

echo " stop ${node} timeout, pid is ${pid}. "
