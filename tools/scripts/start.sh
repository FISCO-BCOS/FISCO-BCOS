#!/bin/bash

dirpath="$(cd "$(dirname "$0")" && pwd)"
cd $dirpath
node=$(basename ${dirpath})
ulimit -c unlimited 2>/dev/null
pid=`ps aux|grep "${dirpath}/config.json"|grep "fisco-bcos"|grep -v grep|awk '{print $2}'`
if [ ! -z $pid ];then
    echo " ${node} is running, pid is $pid."
else 
    echo " start ${node} ..."
    setsid fisco-bcos  --genesis ${dirpath}/genesis.json  --config ${dirpath}/config.json  >> fisco-bcos.log 2>&1 &
fi