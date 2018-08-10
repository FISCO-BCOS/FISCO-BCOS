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

output_dir=
name=
listenip=127.0.0.1
rpcport=
p2pport=
channelPort=
peers=
agency_name=
agency_dir=
mflag=
gflag=

this_script=$0
help() {
    LOG_ERROR "${1}"
    LOG_INFO "Usage:"
    LOG_INFO "    -o  <output dir>        Where node files generate "
    LOG_INFO "    -n  <node name>         Name of node"
    LOG_INFO "    -l  <listen ip>         Node's listen IP"
    LOG_INFO "    -r  <RPC port>          Node's RPC port"
    LOG_INFO "    -p  <P2P port>          Node's P2P port"
    LOG_INFO "    -c  <channel port>      Node's channel port"
    LOG_INFO "    -a  <agency name>       The agency name that the node belongs to"
    LOG_INFO "    -d  <agency dir>        The agency cert dir that the node belongs to"
    LOG_INFO "Optional:"
    LOG_INFO "    -r  <GM shell path>     The path of GM shell scripts directory"
    LOG_INFO "    -s  <sdk name>          The sdk name to connect with the node "
    LOG_INFO "    -g 			          Generate guomi cert"
    LOG_INFO "    -m                      Input agency information manually"
    LOG_INFO "    -h                      This help"
    LOG_INFO "Example:"
    LOG_INFO "    bash $this_script -o /mydata -n node0 -l 127.0.0.1 -r 8545 -p 30303 -c 8891 -d /mydata/test_agency -a test_agency "

exit -1
}

while getopts "o:n:l:r:p:c:a:d:gmh" option;do
	case $option in
	o) output_dir=$OPTARG;;
    n) name=$OPTARG;;
    l) listenip=$OPTARG;;
    r) rpcport=$OPTARG;;
    p) p2pport=$OPTARG;;
    c) channelPort=$OPTARG;;
    a) agency_name=$OPTARG;;
    d) agency_dir=$OPTARG;;
    g) gflag=-g;;
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
[ -z $agency_name ] && help 'Error: Please specify <agency dir> using -a'
[ -z $agency_dir ] && LOG_INFO 'Error: Please specify <agency dir> using -d, using ${agency_name} by default' && agency_dir="${agency_name}"

echo "---------- Generate node basic files ----------" && sleep 1
execute_cmd "sh generate_node_basic.sh -o $output_dir -n $name -l $listenip -r $rpcport -p $p2pport -c $channelPort -e $listenip:$p2pport "
echo 

echo "---------- Generate node cert files ----------" && sleep 1
execute_cmd "sh generate_node_cert.sh -a $agency_name -d $agency_dir -n $name -o $output_dir/$name/data $mflag $gflag"
echo

echo "---------- Generate node genesis file ----------" && sleep 1
execute_cmd "sh generate_genesis.sh -d $output_dir/$name -o $output_dir/$name "

echo "---------- Deploy system contract ----------" && sleep 1
execute_cmd "sh deploy_systemcontract.sh -d $output_dir/$name"
echo
echo  "Genesis node generate success!" && sleep 1
bash node_info.sh -d $output_dir/$name