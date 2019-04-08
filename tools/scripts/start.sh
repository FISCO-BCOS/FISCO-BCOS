#!/bin/bash

dirpath="$(cd "$(dirname "$0")" && pwd)"
cd $dirpath
node=$(basename ${dirpath})
ulimit -c unlimited 2>/dev/null
pid=`ps aux|grep "${dirpath}/config.json"|grep "fisco-bcos"|grep -v grep|awk '{print $2}'`
[ ! -z $pid ] &&  {
    echo " ${node} is running, pid is $pid."
    exit 0
}
    
echo " start ${node} ..."
setsid fisco-bcos  --genesis ${dirpath}/genesis.json  --config ${dirpath}/config.json  >> fisco-bcos.log 2>&1 &

sleep 1

# check if start node successfully
try_times=3
i=0
while [ $i -lt ${try_times} ]
do
    pid=`ps aux|grep "${dirpath}/config.json"|grep "fisco-bcos"|grep -v grep|awk '{print $2}'`
    if [ ! -z $pid ];then
        echo " start ${node} successfully. "
        exit 0
    fi

    sleep 1

    ((i=i+1))
done

echo " start ${node} failed. "