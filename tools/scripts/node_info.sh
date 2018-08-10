#/bin/bash
set -e
node_dir=
web3lib_dir=`readlink -f ../web3lib`
systemcontract_dir=`readlink -f ../systemcontract`

this_script=$0

function LOG_ERROR()
{
    local content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

function LOG_INFO()
{
    local content=${1}
    echo -e "\033[34m"${content}"\033[0m"
}

function execute_cmd()
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
    LOG_INFO "Usage:"
    LOG_INFO "    -d  <node dir>  Node dir you want to see"
    LOG_INFO "Optional:"
    LOG_INFO "    -h              This help"
    LOG_INFO "Example:"
    LOG_INFO "    bash $this_script -d /mydata/node0 "
exit -1
}
while getopts "d:h" option;do
	case $option in
    d) node_dir=$OPTARG;;
	h) help;;
	esac
done

[ -z $node_dir ] && help "Error: Please specify <node dir> using -d"
[ ! -f "$node_dir/config.json" ] && help "Error: Missing $node_dir/config.json"

node_state() {
    node_dir=$1
    pre_dir=`pwd`
    cd $node_dir
    name=`pwd`/config.json
    agent_pid=`ps aux|grep "$name"|grep -v grep|awk '{print $2}'`

    if [ $agent_pid ]; then
        LOG_INFO "State:\t\t\tRunning (pid: "$agent_pid")"
    else
        LOG_INFO "State:\t\t\tStop"
    fi
    cd $pre_dir
}

get_key_value() {
    file=$1
    key=$2
    LOG_INFO `cat $file |jq .$key |sed 's/\"//g' `
}

node_config_info() {
    file=$1
    listenip=`get_key_value $file listenip`
    rpcport=`get_key_value $file rpcport`
    p2pport=`get_key_value $file p2pport`
    channelPort=`get_key_value $file channelPort`
    systemproxyaddress=`get_key_value $file systemproxyaddress`

    LOG_INFO "RPC address:\t\t$listenip:$rpcport"
    LOG_INFO "P2P address:\t\t$listenip:$p2pport"
    LOG_INFO "Channel address:\t$listenip:$channelPort"
    LOG_INFO "SystemProxy address:\t$systemproxyaddress"
}

node_basic() {
    file=$1
    id=`get_key_value $file id`
    name=`get_key_value $file name`
    agency=`get_key_value $file agency`
    caHash=`get_key_value $file caHash`
    
    LOG_INFO "Name:\t\t\t$name"
    LOG_INFO "Node dir:\t\t$node_dir"
    LOG_INFO "Agency:\t\t\t$agency"
    LOG_INFO "CA hash:\t\t$caHash"
    LOG_INFO "Node ID:\t\t$id"
}


LOG_INFO "-----------------------------------------------------------------"
node_basic $node_dir/data/node.json
node_config_info $node_dir/config.json
node_state $node_dir
LOG_INFO "-----------------------------------------------------------------"

