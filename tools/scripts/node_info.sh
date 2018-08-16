#/bin/bash
set -e
node_dir=
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
    echo "Usage:"
    echo "    -d  <node dir>  Node dir you want to see"
    echo "Optional:"
    echo "    -g              Guomi Info"
    echo "    -h              This help"
    echo "Example:"
    echo "    bash $this_script -d /mydata/node0 "
    echo "Guomi Example:"
    echo "    bash $this_script -d /mydata/node0 -g"
exit -1
}
while getopts "d:gh" option;do
	case $option in
    d) node_dir=$OPTARG;;
    g) enable_guomi=1;;
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

node_genesis_info() {
   file="${1}"
   god_address=`get_key_value ${file} god`
   
   LOG_INFO "God address:\t\t${god_address}"
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
if [ ${enable_guomi} -eq 1 ];then
    node_basic ${node_dir}/data/gmnode.json
    node_config_info ${node_dir}/config.json
else
node_basic $node_dir/data/node.json
node_config_info $node_dir/config.json
fi
node_genesis_info ${node_dir}/genesis.json
node_state $node_dir
LOG_INFO "-----------------------------------------------------------------"

