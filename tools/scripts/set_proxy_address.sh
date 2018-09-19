#/bin/bash
set -e
web3lib_dir=`readlink -f ../web3lib`
rpc_url=

this_script=$0

LOG_ERROR()
{
    local content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

LOG_INFO()
{
    local content=${1}
    echo -e "\033[34m"${content}"\033[0m"
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
    echo "    -o  <rpcport url>         URL of a running node of the blockchain"
    echo "Optional:"
    echo "    -w  <web3lib dir>         Directory of web3lib"
    echo "    -h                        This help"
    echo "Example:"
    echo "    bash $this_script  -o 127.0.0.1:8545 "
    echo "    bash $this_script  -o 127.0.0.1:8545 -w ../web3lib/ "
exit -1
}
while getopts "o:w:h" option;do
	case $option in
    o) rpc_url=$OPTARG;;
    w) web3lib_dir=$OPTARG;;
	h) help;;
	esac
done

[ -z $rpc_url ] && help "Error: Please specify <rpcport url> of the blockchain using -o"
echo "Attention! This operation will change the target <rpcport url> of all tools under tools/. Continue?" && yes_go_other_exit

set_config_file() {
    rpc_url=${1##*//} #delete http:// or https://
    config_file=$web3lib_dir/config.js
    sed -i '/var proxy/c var proxy=\"http://'$rpc_url'\";' $config_file
}


set_config_file $rpc_url

new_proxy=`cat $web3lib_dir/config.js|grep 'var proxy'|awk '{print $2}'`
LOG_INFO "Proxy address has been set to: $new_proxy"

