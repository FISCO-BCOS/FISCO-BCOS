#/bin/bash
set -e

function LOG_ERROR()
{
    local content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

function LOG_INFO()
{
    local content=${1}
    echo -e "\033[32m"${content}"\033[0m"
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

info_file=
output_dir=
name=
listenip=127.0.0.1
rpcport=
p2pport=
channelPort=
peers=
agency_name=
agency_dir=
genesis_node_id=
god_address=
system_proxy_address=
enable_guomi=0
mflag=

this_script=$0
help() {
    LOG_ERROR "${1}"
    echo "Usage:"
    echo "    -o  <output dir>            Where node files generate "
    echo "    -n  <node name>             Name of node"
    echo "    -l  <listen ip>             Node's listen IP"
    echo "    -r  <RPC port>              Node's RPC port"
    echo "    -p  <P2P port>              Node's P2P port"
    echo "    -c  <channel port>          Node's channel port"
    echo "    -e  <bootstrapnodes>        The list of running nodes' P2P url on blockchain(separate by \",\")"
    echo "    -d  <agency dir>            The agency cert dir that the node belongs to"
    echo "    -a  <agency name>           The agency name that the node belongs to"
    echo "Optional:"
    echo "    -x  <system proxy address>  System proxy address of the blockchain"
    echo "    -s  <god address>           God address"
    echo "    -i  <genesis node id>       Genesis node id"
    echo "    -f  <other node info file>  Use info file of other node to set <system proxy address>, <god address> and <genesis node id>"
    echo "    -m                          Input agency information manually"
    echo "    -g                          Create guomi node"
    echo "    -h                          This help"
    echo "Example:"
    echo "    bash $this_script -o /mydata -n node1 -l 127.0.0.1 -r 8546 -p 30304 -c 8892 -e 127.0.0.1:30303,127.0.0.1:30304 -d /mydata/test_agency -a test_agency -x 0x919868496524eedc26dbb81915fa1547a20f8998 -s 0xf78451eb46e20bc5336e279c52bda3a3e92c09b6 -i xxxxxx"
    echo "    bash $this_script -o /mydata -n node1 -l 127.0.0.1 -r 8546 -p 30304 -c 8892 -e 127.0.0.1:30303,127.0.0.1:30304 -d /mydata/test_agency -a test_agency -f node0.info"
    echo "GuomiExample:"
    echo "    bash $this_script -o /mydata -n node1 -l 127.0.0.1 -r 8546 -p 30304 -c 8892 -e 127.0.0.1:30303,127.0.0.1:30304 -x 0x919868496524eedc26dbb81915fa1547a20f8998 -s 0xf78451eb46e20bc5336e279c52bda3a3e92c09b6 -i xxxxxx -g"
    echo "    bash $this_script -o /mydata -n node1 -l 127.0.0.1 -r 8546 -p 30304 -c 8892 -e 127.0.0.1:30303,127.0.0.1:30304 -f node0.info -g"

exit -1
}

while getopts "o:n:l:r:p:c:e:d:a:x:s:i:f:gmh" option;do
	case $option in
	o) output_dir=$OPTARG;;
    n) name=$OPTARG;;
    l) listenip=$OPTARG;;
    r) rpcport=$OPTARG;;
    p) p2pport=$OPTARG;;
    c) channelPort=$OPTARG;;
    e) peers=$OPTARG;;
    d) agency_dir=$OPTARG;;
    a) agency_name=$OPTARG;;
    x) systemproxyaddress=$OPTARG;;
    s) god_address=${OPTARG};;
    i) genesis_node_id=$OPTARG;;
    f) info_file=$OPTARG;;
    g) enable_guomi=1;;
    m) mflag=-m;;
	h) help;;
	esac
done

[ -z $output_dir ] && help 'Error! Please specify <output dir> using -o'
[ -z $name ] && help 'Error! Please specify <node name> using -z'
[ -z $listenip ] && help 'Error! Please specify <listen ip> using -l'
[ -z $rpcport ] && help 'Error! Please specify <RPC port> using -r'
[ -z $p2pport ] && help 'Error! Please specify <P2P port> using -p'
[ -z $channelPort ] && help 'Error! Please specify <channel port> using -c'
[ -z $peers ] && help 'Error! Please specify <bootstrapnodes> using -e'
[ -z $systemproxyaddress ] && [ -z $info_file ] && help 'Error! Please specify <system proxy address> using -x or -f'
[ -z $god_address ] && [ -z $info_file ] && help 'Error! Please specify <god address> using -s or -f'
[ -z $genesis_node_id ] && [ -z $info_file ] && help 'Error! Please specify <genesis node id> using -i or -f'
[ "$info_file" ] && [ ! -f "$info_file" ] &&  help 'Error! Info file '$info_file' not found'

get_key_value() {
    file=$1
    key=$2
    cat $file |jq .$key |sed 's/\"//g'
}

load_info() {
    file=$1
    systemproxyaddress=`get_key_value $file systemproxyaddress`
    god_address=`get_key_value $file god`
    genesis_node_id=`get_key_value $file id`
}

if [ "$info_file" ];then
    load_info $info_file
fi

if [ ${enable_guomi} -eq 0 ];then
    [ -z $agency_name ] && help 'Error! Please specify <agency dir> using -a'
    [ -z $agency_dir ] && help 'Error! Please specify <agency dir> using -d, using ${agency_name} by default' && agency_dir="${agency_name}"
    
    echo "---------- Generate node basic files ----------" && sleep 1
    execute_cmd "sh generate_node_basic.sh -o $output_dir -n $name -l $listenip -r $rpcport -p $p2pport -c $channelPort -e $peers -x $systemproxyaddress "
    echo 

    echo "---------- Generate node cert files ----------" && sleep 1
    execute_cmd "sh generate_node_cert.sh -a $agency_name -d $agency_dir -n $name -o $output_dir/$name/data $mflag"
    echo

    echo "---------- Generate node genesis file ----------" && sleep 1
    execute_cmd "sh generate_genesis.sh -o $output_dir/$name -i $genesis_node_id -s ${god_address}"

    echo
    echo  "Node generate success!" && sleep 1
else
    LOG_INFO "---------- Generate node basic files ----------"
    execute_cmd "sh generate_node_basic.sh -o $output_dir -n $name -l $listenip -r $rpcport -p $p2pport -c $channelPort -e $peers -x $systemproxyaddress -g"
    LOG_INFO "---------- Generate node genesis file ----------"
    execute_cmd "sh generate_genesis.sh -o $output_dir/$name -i $genesis_node_id -r ${god_address} -g"
fi


if [ ${enable_guomi} -eq 0 ];then
    bash node_info.sh -d $output_dir/$name
else
    bash node_info.sh -d $output_dir/$name -g
fi
