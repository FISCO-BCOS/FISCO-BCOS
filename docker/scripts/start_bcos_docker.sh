#!/bin/bash

# example: ./start_docker.sh /nodedata/node-1 node1
set -e

configPath="~/nodedata"
dockerImage="bcosorg/bcos:latest"
containerName="bcos"
containerDataPath="/nodedata"

if [ $# = 1 ];then
    configPath=$1
    containerName=$containerName"-"${configPath##*/}
elif [ $# = 2 ];then
    configPath=$1
    containerName=$2
else
    echo "Please input path of config.json"
    echo "Usage: $0 [full/path/to/config.json & genesis.json] [container name]"
    exit -1
fi

if [ ! -f $configPath/genesis.json ];then
    echo "$configPath/genesis.json doesn't exists."
    exit -1
fi

if [ ! -f $configPath/config.json ];then
    echo "$configPath/config.json doesn't exists."
    exit -1
fi

rpcport=`cat $configPath/config.json | grep -i "rpcport" | awk -F ':' '{print $2}' | sed 's/\([^0-9]\)//g'`
p2pport=`cat $configPath/config.json | grep -i "p2pport" | awk -F ':' '{print $2}' | sed 's/\([^0-9]\)//g'`

echo "--------------------------------------"
echo "configPath:    $configPath"
echo "rpcport:       $rpcport"
echo "p2pport:       $p2pport"
echo "dockerImage:   $dockerImage"
echo "containerName: $containerName"
echo "--------------------------------------"

docker run -d --rm --name $containerName -v $configPath:$containerDataPath  -p $rpcport:$rpcport -p $p2pport:$p2pport $dockerImage 

