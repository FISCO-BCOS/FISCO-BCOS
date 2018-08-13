#/bin/bash
set -e
genesis_node_dir=
enable_guomi=0
web3lib_dir=`readlink -f ../web3lib`
systemcontract_dir=`readlink -f ../systemcontract`

this_script=$0

LOG_ERROR()
{
    local content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

LOG_INFO()
{
    local content=${1}
    echo -e "\033[32m"${content}"\033[0m"
}

LOG_NORMAL()
{
    local content=${1}
    echo -e ${content}
}

execute_cmd()
{
    local command="${1}"
    LOG_INFO "RUN: ${command}"
    eval ${command}
    local ret=$?
    if [ $ret -ne 0 ];then
        LOG_ERROR "FAILED execution of command: ${command}"
        exit 1
    else
        LOG_INFO "SUCCESS execution of command: ${command}"
    fi
}

yes_or_no()
{
    read -r -p "[Y/n]: " input
    case $input in
        [yY][eE][sS]|[yY])
            echo "Yes";;

        [nN][oO]|[nN])
            echo "No"
                ;;

        *)
        echo "No"
        ;;
    esac    
}

yes_go_other_exit()
{
    read -r -p "[Y/n]: " input
    case $input in
        [yY][eE][sS]|[yY])
            ;;

        [nN][oO]|[nN])
            exit 1
                ;;

        *)
        exit 1
        ;;
    esac    
}

help() {
    LOG_ERROR "${1}"
    LOG_NORMAL "Usage:"
    LOG_NORMAL "    -d  <genesis node dir>   Genesis node dir of the blockchain"
    LOG_NORMAL "Optional:"
    LOG_NORMAL "    -w  <web3lib dir>        Directory of web3lib"
    LOG_NORMAL "    -s  <systemcontract dir> Directory of systemcontract"
    LOG_NORMAL "    -h                       This help"
    LOG_NORMAL "Example:"
    LOG_NORMAL "    bash $this_script -d /mydata/node0 "
    LOG_NORMAL "    bash $this_script -d /mydata/node0 -w ../web3lib/ -s ../systemcontract/ "
    LOG_NORMAL "Guomi Example:"
    LOG_NORMAL "    bash $this_script -d /mydata/node0 -g"
    LOG_NORMAL "    bash $this_script -d /mydata/node0 -w ../web3lib/ -s ../systemcontract/ -g"
exit -1
}
while getopts "d:w:s:gh" option;do
	case $option in
    d) genesis_node_dir=$OPTARG;;
    w) web3lib_dir=$OPTARG;;
    s) systemcontract_dir=$OPTARG;;
    g) enable_guomi=1;; 
	h) help;;
	esac
done

[ -z $genesis_node_dir ] && help "Error: Please specify <genesis node dir> using -d"
[ ! -f "$genesis_node_dir/config.json" ] && help "Error: Missing $genesis_node_dir/config.json"
[ ! -f "/usr/local/bin/fisco-bcos" ] && help "Error: FISCO-BCOS has not been installed in /usr/local/bin"

start_node() {
    node_dir=$1
    pre_dir=`pwd`
    cd $node_dir

    #Is the node running?
    name=`pwd`/config.json
    agent_pid=`ps aux|grep "$name"|grep -v grep|awk '{print $2}'`

    if [ $agent_pid ]; then
        echo "Attention! Node is running. Node will be stopped after deployment. Continue to deploy?"
        yes_go_other_exit
    else
        sh start.sh
    fi
    cd $pre_dir
}

deploy_systemcontract_using_nodejs() {
    #get genesis ip port
    listenip=`cat $genesis_node_dir/config.json |jq .listenip |sed 's/\"//g' `
    rpcport=`cat $genesis_node_dir/config.json |jq .rpcport |sed 's/\"//g' `
    config_file=$web3lib_dir/tmp_config_deploy_system_contract.js
    cp $web3lib_dir/config.js $config_file
    sed -i '/var proxy/c var proxy=\"http://'$listenip:$rpcport'\";' $config_file
    #start a thread to recover config.js
    echo "Start depoly system contract"
    rm -f $systemcontract_dir/output/SystemProxy.address
    cd $systemcontract_dir
    execute_cmd "babel-node deploy_systemcontract.js $config_file" & \
    (sleep 7 && rm $config_file)
    wait
    cd -
}

stop_node() {
    node_dir=$1
    cd $node_dir
    set +e && sh stop.sh && set -e
    cd -
}


LOG_INFO "Pre-start genesis node"
start_node $genesis_node_dir
sleep 10

LOG_INFO "Deploy System Contract"
deploy_systemcontract_using_nodejs


# guomi don't stop genesis 
    LOG_INFO "Stop genesis node"
    stop_node $genesis_node_dir
if [ ${enable_guomi} -eq 1 ];then
    LOG_INFO "Start genesis node"
    start_node "$genesis_node_dir"
fi

[ ! -f $systemcontract_dir/output/SystemProxy.address ] && help "Error: System Contract deploy failed."

LOG_INFO "Reconfig genesis node"
systemproxyaddress=`cat $systemcontract_dir/output/SystemProxy.address`
sed -i '/systemproxyaddress/c  \\t\"systemproxyaddress\":\"'$systemproxyaddress'\",'  $genesis_node_dir/config.json 
LOG_INFO "SystemProxy address: $systemproxyaddress"
LOG_INFO "Deploy System Contract Success!"

