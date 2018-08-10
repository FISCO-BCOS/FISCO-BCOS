#!/bin/sh

if [ $# -lt 2 ]; then
    echo "用法:$0 <链的数目> <每条链的节点数目> <ip0> <ip1>..."
    echo "说明:同一条链的所有节点都在相同ip;如果缺少ip参数，则默认采用127.0.0.1"
    exit 0
fi

declare -i numChain=`expr $1`
declare -i numNode=`expr $2`

if [ $numChain -lt 4 ]; then
    echo "链的数目需要>=4"
    exit
fi

if [ $numNode -lt 1 ]; then
    echo "节点的数目需要>=1"
    exit
fi

shift
shift

#get ip
ipArr=()

declare -i length=${numChain}-1
if [ $# == 0 ]; then
    for i in `seq 0 ${length}`
    do
        ipArr[$i]="127.0.0.1"
    done

else
    for i in `seq 0 ${length}`
    do
        if [ $1"" == "" ];then
            echo "ip数目与链的数目不一致"
            exit 0
        fi

        ipArr[$i]=$1
        shift
    done
fi

ethName="fisco-bcos"
defaultRpcPort=20306
defaultP2pPort=30306
defaultChannelPort=40306
defaultNetworkid="12345"
defaultDataDir="data"

declare -A p2pPortDict
declare -A channelPortDict
declare -A channelPortDictCount
declare -A rpcPortDict

for ip in ${ipArr[@]}
do
    rpcPortDict[$ip]=${defaultRpcPort}
    p2pPortDict[$ip]=${defaultP2pPort}
    channelPortDict[$ip]=${defaultChannelPort}
    if [[ -z ${channelPortDictCount[$ip]} ]];then
        declare -i init=1
        channelPortDictCount[$ip]=${init}
    else
        channelPortDictCount[$ip]=`expr ${channelPortDictCount[$ip]} + 1`
    fi
    
done


if [ ! -f ../build/eth/${ethName} ]; then
    echo "../build/eth/${ethName} binary file not found."
    exit 0
fi

if [ ! -f ./config.json.template ];then
    echo "./config.json.template not found."
    exit 0
fi

if [ ! -f ./genesis.json.template ];then
    echo "./genesis.json.template not found."
    exit 0
fi

echo ${ipArr[@]:0}

function generateChain()
{
    #生成config.json配置文件
    declare -i num=`expr $1`
    ip=$2
    suffix=$3

    nodedir=${defaultDataDir}
    networkid=${defaultNetworkid}

    declare -i beginP2pPort=`expr ${p2pPortDict[$ip]}`
    for i in `seq 1 $num`
    do
        idx=$i

        mkdir -p node_${idx}
        cp config.json.template node_${idx}/config.json

        declare -i rpcport=`expr ${rpcPortDict[$ip]}`
        rpcPortDict[$ip]=`expr ${rpcport} + 1`

        declare -i p2pport=`expr ${p2pPortDict[$ip]}`
        p2pPortDict[$ip]=`expr ${p2pport} + 1`

        declare -i channelPort=`expr ${channelPortDict[$ip]}`
        channelPortDict[$ip]=`expr ${channelPort} + 1`

        sed -i "s/{ip}/${ip}/g" node_${idx}/config.json
        sed -i "s/{rpcport}/${rpcport}/g" node_${idx}/config.json
        sed -i "s/{p2pport}/${p2pport}/g" node_${idx}/config.json
        sed -i "s/{channelPort}/${channelPort}/g" node_${idx}/config.json
        sed -i "s/{nodedir}/.\//g" node_${idx}/config.json
        sed -i "s/{networkid}/${networkid}/g" node_${idx}/config.json

        #生成genesis.json配置文件
        echo '{
        "nonce": "0x0000000000000042",
        "difficulty": "0x1",
        "mixhash": "0x0000000000000000000000000000000000000000000000000000000000000000",
        "coinbase": "0x0000000000000000000000000000000000000000",
        "timestamp": "0x00",
        "parentHash": "0x0000000000000000000000000000000000000000000000000000000000000000",
        "extraData": "0x11bbe8db4e347b4e8c937c1c8370e4b5ed33adb3db69cbdb7a38e1e50b1b82fa",
        "gasLimit": "0x13880000000000",
        "god":"0x6c5d78ea78858d1ec0f5862474e074e980ce6f60",
        "alloc": {
            "3282791d6fd713f1e94f4bfd565eaa78b3a0599d": {
              "balance": "1337000000000000000000"
            },
            "17961d633bcf20a7b029a7d94b7df4da2ec5427f": {
              "balance": "229427000000000000000"
            }
          },
        "initMinerNodes":{initMinerNodes}
        }' > node_${idx}/genesis.json

        cp log.conf.template node_${idx}/log.conf

        sed -i "s/{nodedir}/.\//g" node_${idx}/log.conf

        #拷贝eth文件
        cp ../build/eth/${ethName} node_${idx}/
        cp ../fisco-solc node_${idx}/
        echo '{"cryptomod":"0","rlpcreatepath":"./network.rlp"}' > node_${idx}/cryptomod.json

        mkdir -p node_${idx}/data
        mkdir -p node_${idx}/keystore

        cd node_${idx}
        ./${ethName} --gennetworkrlp cryptomod.json
        mv network.rlp* data
        cp ../ca.crt data
        cp ../server.key data
        cp ../server.crt data
        cd ..
    done

    extrajson=""
    initMinerNodes=""

    for i in `seq 1 $num`
    do
        idx=`expr ${i} - 1`
        declare -i p2pport=${beginP2pPort}
        nodeid=`cat node_${i}/data/network.rlp.pub`
        if [ ${nodeid}"" = "" ];then
            echo "nodeid not found"
            exit
        fi
        extrajson=${extrajson}'{"Nodeid":"'${nodeid}'","Nodedesc": "node_'${idx}'","Agencyinfo": "node_'${idx}'","Peerip": "'${ip}'","Identitytype": 1,"Port":'${p2pport}',"Idx":'${idx}'},'
        initMinerNodes=${initMinerNodes}"\""${nodeid}"\","
        beginP2pPort=`expr ${p2pport} + 1`
    done

    declare -i extraLen=${#extrajson}-1
    declare -i minerLen=${#initMinerNodes}-1
    extrajson="["${extrajson:0:${extraLen}}"]"
    initMinerNodes="["${initMinerNodes:0:${minerLen}}"]"

    for i in `seq 1 $num`
    do
        idx=$i
        sed -i "s/{initMinerNodes}/${initMinerNodes}/g" node_${idx}/genesis.json
        sed -i "s/{nodeextrainfo}/${extrajson}/g" node_${idx}/config.json
    done

    dirName=${ip}_${suffix}
    mkdir -p ${dirName}
    mv node_* ${dirName}/
    cp ../systemcontractv2 ${dirName}/ -R
    cp ../tool ${dirName}/ -R
    cp ./start_meshchain.sh ${dirName}
    chmod a+x start_meshchain.sh
    tar -zcvf ${dirName}.tar.gz ${dirName}
    rm ${dirName} -rf
}

function generateProxyConfig()
{
    if [ ! -f ./applicationContext_meshchain.template.xml ];then
        echo "applicationContext_meshchain.template.xml not existed"
        exit
    fi

    if [ ! -f ./config_meshchain.template.xml ];then
        echo "config_meshchain.template.xml not existed"
        exit
    fi

    mkdir -p proxyConfig

    cp applicationContext_meshchain.template.xml proxyConfig/applicationContext.xml
    cp config_meshchain.template.xml proxyConfig/config.xml

    cd proxyConfig
    routeChainIp=${ipArr[0]}
    hotChainIp=${ipArr[1]}

    declare -i index=0
    setList=""

    sed -i 's/<\/beans>//g' applicationContext.xml
    sed -i 's/${routeIp}/'${routeChainIp}'/g' applicationContext.xml
    sed -i 's/${routeChannelPort}/'$defaultChannelPort'/g' applicationContext.xml

    if [ ${routeChainIp}"" = ${hotChainIp}"" ];then
        sed -i 's/${hotIp}/'${hotChainIp}'/g' applicationContext.xml
        declare -i hotPort=defaultChannelPort+${numNode}
        sed -i 's/${hotChannelPort}/'${hotPort}'/g' applicationContext.xml
    else
        sed -i 's/${hotIp}/'${hotChainIp}'/g' applicationContext.xml
        sed -i 's/${hotChannelPort}/'$defaultChannelPort'/g' applicationContext.xml
    fi

    declare -i len=${#ipArr[@]}-1
    declare -i index=0
    declare -i first=0

    for ip in ${ipArr[@]:2:${len}}
    do
        if [ ${ip} = ${routeChainIp} -o ${ip} = ${hotChainIp} ];then
            if [ ${routeChainIp} = ${hotChainIp} -a ${first} = 0 ];then
                first=1
                channelPortDictCount[${ip}]=`expr ${channelPortDictCount[${ip}]} - 2`
                declare -i lastPort=`expr ${channelPortDict[${ip}]} - ${numNode} \* ${channelPortDictCount[${ip}]}`
            else
                channelPortDictCount[${ip}]=`expr ${channelPortDictCount[${ip}]} - 1`
                declare -i lastPort=`expr ${channelPortDict[${ip}]} - ${numNode} \* ${channelPortDictCount[${ip}]}`
            fi
        else
            declare -i lastPort=`expr ${channelPortDict[${ip}]} - ${numNode} \* ${channelPortDictCount[${ip}]}`
            channelPortDictCount[${ip}]=`expr ${channelPortDictCount[${ip}]} - 1`
        fi
        echo '
            <bean id="set'${index}'Service" class="org.bcos.channel.client.Service">
                <property name="threadPool">
                    <bean class="org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor">
                        <property name="corePoolSize" value="50" />
                        <property name="maxPoolSize" value="100" />
                        <property name="queueCapacity" value="500" />
                        <property name="keepAliveSeconds" value="60" />
                        <property name="rejectedExecutionHandler">
                            <bean class="java.util.concurrent.ThreadPoolExecutor.AbortPolicy" />
                        </property>
                    </bean>
                </property>
                <property name="orgID" value="WB" />
                <property name="allChannelConnections">
                    <map>
                    <entry key="WB">
                        <bean class="org.bcos.channel.handler.ChannelConnections">
                            <property name="connectionsStr">
                                <list>
                                    <value>nodeid@'${ip}':'${lastPort}'</value>
                                </list>
                            </property>
                        </bean>
                    </entry>
                    </map>
                </property>
            </bean>
        ' >> applicationContext.xml
        setList=${setList}"set"${index}"Service,"
        index=${index}+1
    done

    echo "</beans>" >> applicationContext.xml
    declare -i len=${#setList}-1
    sed -i 's/${setList}/'${setList:0:${len}}'/g' config.xml

    declare -i index=0
    routes="["
    for ip in ${ipArr[@]}
    do
        if [ ${index} -ge 2 ];then
             declare -i setIdx=${index}-2
             routes=${routes}'{
                    "set_name":"set'${setIdx}'Service",
                    "set_warn_num":2,
                    "set_max_num":2,
                    "set_node_list":[]
              },'
        fi
        index=${index}+1
    done

    declare -i len=${#routes}-1
    routes=${routes:0:${len}}
    routes=${routes}"]"
    echo ${routes} > route.json
    cd ..
    tar -zcvf proxyConfig.tar.gz proxyConfig
    rm -rf proxyConfig
}

declare -i index=0

for ip in ${ipArr[@]}
do
    if [ ${index} = 0 ];then
        generateChain ${numNode} ${ip} "route"
    elif [ ${index} = 1 ];then
        generateChain ${numNode} ${ip} "hot"
    else
        declare -i setIdx=${index}-2
        generateChain ${numNode} ${ip} "set"${setIdx}
    fi
    index=${index}+1
done

generateProxyConfig
