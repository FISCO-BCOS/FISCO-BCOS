#!/bin/sh

if [ -d "/bcos-data" ]; then
    sudo rm -rf /bcos-data
fi

if [ `whoami` = "root" ];then  
    export PATH="${PATH}:/usr/local/bin"
else
    user=`whoami`
    group=`groups`
    sudo mkdir /bcos-data
    sudo chown ${user}:${group} /bcos-data
fi 

node init.js node0.sample node1.sample
chmod +x /bcos-data/node0/start0.sh
chmod +x /bcos-data/node1/start1.sh
echo "尝试启动节点..."
sh /bcos-data/node0/start0.sh &
sh /bcos-data/node1/start1.sh &
