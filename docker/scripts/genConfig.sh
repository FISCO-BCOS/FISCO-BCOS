#!/bin/bash

set -e
dockerImage="bcosorg/bcos:latest"

genConfig()
{
    idx=0
    miner_path=$PWD/node-0
    genesis="false"
    minerInfo=""
    systemproxyaddress=0x0
    if [ $# = 0 ];then
        genesis="true"
    elif [ $# = 2 ];then
        idx=$1
        miner_path=$2
        if [ ! -f "${miner_path}/config.json" ];then
        echo "${miner_path}/config.json doesn't exist."
        return -1
        fi
        minerInfo=`cat ${miner_path}/config.json | tail -n +24|head -n 9`
        systemproxyaddress=`cat ${miner_path}/config.json |grep systemproxyaddress| awk -F ':' '{print $2}'|sed 's/\([^0-9a-z]\)//g'`
    else
        return -1
    fi

    if [ ${idx} -gt 99 ]; then 
    echo "Node numbers > 99 isn't supported."
    return -1
    fi

    output=$PWD/node-${idx}
    
    while ([ ${idx} -gt 0 ] && [ -d ${output} ]); do
        idx=`expr ${idx} + 1`
        output=$PWD/node-${idx}
    done
    if [ ! -d ${output} ]; then
    mkdir ${output}
    fi
    port=${idx}
    if [ ${idx} -lt 10 ]; then
    port=`printf "%02d" ${idx}`
    fi
    echo "------generate node-${idx}------"
    if ! id -nG $(whoami)|grep -qw "docker"; then SUDO='sudo'; else SUDO=''; fi
    $SUDO docker run -it --rm -v $output:/nodedata ${dockerImage} bcoseth --gennetworkrlp /nodedata/network.rlp
    sudo chown -R ${USER} node-${idx}
    nodeId=`sudo cat $output/network.rlp.pub`

echo "{
    \"sealEngine\": \"PBFT\",
    \"systemproxyaddress\":\"${systemproxyaddress}\",
    \"listenip\":\"0.0.0.0\",
    \"rpcport\":\"355${port}\",
    \"p2pport\":\"533${port}\",
    \"wallet\":\"/nodedata/keys.info\",
    \"keystoredir\":\"/nodedata/keystore/\",
    \"datadir\":\"/nodedata/\",
    \"vm\":\"interpreter\",
    \"networkid\":\"123456\",
    \"logverbosity\":\"4\",
    \"coverlog\":\"OFF\",
    \"eventlog\":\"ON\",
    \"logconf\":\"/nodedata/log.conf\",
    \"params\": {
            \"accountStartNonce\": \"0x0\",
            \"maximumExtraDataSize\": \"0x0\",
            \"tieBreakingGas\": false,
            \"blockReward\": \"0x0\",
            \"networkID\" : \"0x0\"
    },
    \"NodeextraInfo\":[" >$output/config.json
if [ ${genesis} == "false" ];then
    cp ${miner_path}/genesis.json node-${idx}
    echo "  ${minerInfo}," >>$output/config.json
fi
echo "  {
        \"Nodeid\":\"$nodeId\",
        \"Nodedesc\": \"node${idx}\",
        \"Agencyinfo\": \"node${idx}\",
        \"Peerip\": \"172.17.0.1\",
        \"Identitytype\": 1,
        \"Port\":533${port},
        \"Idx\":${idx}
    }
    ]
}" >>$output/config.json

echo "* GLOBAL:
    ENABLED                 =   true
    TO_FILE                 =   true
    TO_STANDARD_OUTPUT      =   false
    FORMAT                  =   \"%level|%datetime{%Y-%M-%d %H:%m:%s}|%msg\"
    FILENAME                =   \"/nodedata/logs/log_%datetime{%Y%M%d%H}.log\"
    MILLISECONDS_WIDTH      =   3
    PERFORMANCE_TRACKING    =   false
    MAX_LOG_FILE_SIZE       =   209715200 ## 200MB - Comment starts with two hashes (##)
    LOG_FLUSH_THRESHOLD     =   100  ## Flush after every 100 logs
* TRACE:
    ENABLED                 =   false
* DEBUG:
    ENABLED                 =   false
* FATAL:
    ENABLED                 =   false
* ERROR:
    FILENAME                =   \"/nodedata/logs/error_log_%datetime{%Y%M%d%H}.log\"
* WARNING:
    ENABLED                 =   false
* INFO:
    FILENAME                =   \"/nodedata/logs/info_log_%datetime{%Y%M%d%H}.log\"
* VERBOSE:
    ENABLED                 =   false" > $output/log.conf

echo "{
    \"id\":\"$nodeId\",
    \"ip\":\"172.17.0.1\",
    \"port\":533${port},
    \"category\":1,
    \"desc\":\"node${idx}\",
    \"CAhash\":\"\",
    \"agencyinfo\":\"node${idx}\",
    \"idx\":${idx}
}" > $output/node.json

    if [ ${genesis} == "true" ];then
echo "{
    \"nonce\": \"0x0\",
    \"difficulty\": \"0x0\",
    \"mixhash\": \"0x0\",
    \"coinbase\": \"0x0\",
    \"timestamp\": \"0x0\",
    \"parentHash\": \"0x0\",
    \"extraData\": \"0x0\",
    \"gasLimit\": \"0x13880000000000\",
    \"god\":\"0x4d23de3297034cdd4a58db35f659a9b61fc7577b\",
    \"alloc\": {}, 	
    \"initMinerNodes\":[\"$nodeId\"]
}" > $output/genesis.json
    fi
}

configPath=$PWD
numbers=1
minerPath=$PWD/node-0

if [ $# = 0 ];then
    echo "Generate config file to ./node-0"
    genConfig 
elif [ $# = 2 ];then
    numbers=$1
    minerPath=$2
    for id in $( seq 1 ${numbers} )
    do
    genConfig ${id} ${minerPath}
    done
else
    echo "Usage: $0 [numbers of node config files] [node-0 path]"
    exit -1
fi

echo "Config files in $configPath"
