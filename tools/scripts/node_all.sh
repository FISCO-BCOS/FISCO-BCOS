#/bin/bash
set -e
node_dir=
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


help() {
    LOG_ERROR "${1}"
    echo "Usage:"
    echo "Optional:"
    echo "    -w  <web3lib dir>         Directory of web3lib"
    echo "    -s  <systemcontract dir>  Directory of systemcontract"
    echo "    -h                        This help"
    echo "Example:"
    echo "    bash $this_script -d /mydata/node0 "
    echo "    bash $this_script -d /mydata/node0 -w ../web3lib/ -s ../systemcontract/ "
exit -1
}
while getopts "d:w:s:h" option;do
	case $option in
    w) web3lib_dir=$OPTARG;;
    s) systemcontract_dir=$OPTARG;;
	h) help;;
	esac
done



show_node() {
    cd ${systemcontract_dir}
    execute_cmd "babel-node tool.js NodeAction all"
}

show_node
