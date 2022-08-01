#!/bin/bash
dirpath="$(cd "$(dirname "$0")" && pwd)"
cd ${dirpath}

service_name='@SERVICE_NAME@'
service=${dirpath}/${service_name}

pid=$(ps aux|grep ${service}|grep -v grep|awk '{print $2}')

if [ ! -z ${pid} ];then
    echo " ${service_name} is running, pid is ${pid}."
    exit 0
else
    nohup ${service} --config=conf/tars.conf  >>nohup.out 2>&1 &
    sleep 1.5
fi

try_times=4
i=0
while [ $i -lt ${try_times} ]
do
    pid=$(ps aux|grep ${service}|grep -v grep|awk '{print $2}')
    if [[ ! -z ${pid} ]];then
        echo -e "\033[32m ${service_name} start successfully pid=${pid}\033[0m"
        exit 0
    fi
    sleep 0.5
    ((i=i+1))
done
echo -e "\033[31m  Exceed waiting time. Please try again to start ${service_name} \033[0m"
tail -n20  nohup.out

