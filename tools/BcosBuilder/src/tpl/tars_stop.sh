#!/bin/bash
dirpath="$(cd "$(dirname "$0")" && pwd)"
cd ${dirpath}

service_name='@SERVICE_NAME@'
service=${dirpath}/${service_name}

pid=$(ps aux|grep ${service}|grep -v grep|awk '{print $2}')

if [ -z ${pid} ];then
    echo " ${service_name} isn't running."
    exit 0
fi

kill ${pid} > /dev/null

i=0
try_times=10
while [ $i -lt ${try_times} ]
do
    sleep 1
    pid=$(ps aux|grep ${service}|grep -v grep|awk '{print $2}')
    if [ -z ${pid} ];then
        echo -e "\033[32m stop ${service_name} success.\033[0m"
        exit 0
    fi
    ((i=i+1))
done
echo "  Exceed maximum number of retries. Please try again to stop ${service_name}"
exit 1
