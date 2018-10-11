#/bin/bash
set -e
genesis_node_dir=
output_dirs=
god_address=0xf78451eb46e20bc5336e279c52bda3a3e92c09b6
init_miners=
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

help() {
    LOG_ERROR "${1}"
    echo "Usage:"
    echo "    -o    <output dir>            Where genesis.json generate"
    echo "    -i/-d <genesis node id/dir>   Genesis node id or dir"
    echo "Optional:"
    echo "    -i    <genesis node id>       Genesis node id  of the blockchain"
    echo "    -d    <genesis node dir>      Genesis node dir of the blockchain"
    echo "    -s    <god address>           Address of god account(default: 0xf78451eb46e20bc5336e279c52bda3a3e92c09b6)"
    echo "    -g                            Generate genesis node for guomi-FISCO-BCOS"
    echo "    -h                            This help"
    
    echo "Example:"
    echo "    bash $this_script -d /mydata/node0 -o /mydata/node1"
    echo "    bash $this_script -i xxxxxxxxxxxxx -o /mydata/node1"
    echo "    bash $this_script -d /mydata/node0 -o /mydata/node1 -s 0xf78451eb46e20bc5336e279c52bda3a3e92c09b6"
    echo "    bash $this_script -i xxxxxxxxxxxxx -o /mydata/node1 -s 0xf78451eb46e20bc5336e279c52bda3a3e92c09b6"

    echo "GUOMI Example:"
    echo "    bash $this_script -d ~/mydata/node0 -o ~/mydata/node0 -g"
    echo "    bash $this_script -i xxxxxxxxxxxxx -o ~/mydata/node0 -g"
    echo "    bash $this_script -d ~/mydata/node0 -o ~/mydata/node0 -s 0x3b5b68db7502424007c6e6567fa690c5afd71721 -g"
    echo "    bash $this_script -i xxxxxxxxxxxxx -o ~/mydata/node0 -s 0x3b5b68db7502424007c6e6567fa690c5afd71721 -g"
exit -1
}
guomi_support=0
while getopts "d:o:i:s:gh" option;do
	case $option in
	o) output_dirs=$OPTARG;;
    d) genesis_node_dir=$OPTARG;;
    i) init_miners=$OPTARG;;
    s) god_address=$OPTARG;;
    g) guomi_support=1;;
	h) help;;
	esac
done

[ -z $genesis_node_dir ] && [ -z $init_miners ] && help 'Error: Please specify <genesis node dir> or <genesis node id> using -d or -i'
[ -z $output_dirs ] && help 'Error: Please specify <output dirs> using -o'
[ -z $god_address ] && help 'Error: Please specify <god account> using -s'


if [ $genesis_node_dir ]; then
    data_dir=$genesis_node_dir/data
    if [ ${guomi_support} -eq 0 ];then
        nodeid_file=$data_dir/node.nodeid
    else
        nodeid_file=$data_dir/gmnode.nodeid
        if [ ${god_address} == "0xf78451eb46e20bc5336e279c52bda3a3e92c09b6" ];then
            god_address=0x3b5b68db7502424007c6e6567fa690c5afd71721
        fi
    fi

    if [ ! -d "$data_dir" ]; then
        help "Error: $data_dir is not exist! Please generate node first."
    elif [ ! -f "$nodeid_file" ]; then
        help "Error $nodeid_file is not exist! Please generate node first."
    fi
fi

#Generate god account
LOG_INFO "God account address: "$god_address

#Get genesis account if not set
[ -z $init_miners ] && init_miners=`cat $nodeid_file`

IFS=',' read -ra NODE_DIRS <<< "$output_dirs"
for NODE_DIR in "${NODE_DIRS[@]}"; do
    #Generate genesis.json
    genesis_file=$NODE_DIR/genesis.json
    if [ -f $genesis_file ]; then
        echo "Attention! Duplicate generation of \"$genesis_file\". Overwrite?"
        if [ `yes_or_no` != "Yes" ]; then
            continue
        fi
    fi
        echo '
{
     "nonce": "0x0",
     "difficulty": "0x0",
     "mixhash": "0x0",
     "coinbase": "0x0",
     "timestamp": "0x0",
     "parentHash": "0x0",
     "extraData": "0x0",
     "gasLimit": "0x13880000000000",
     "god":"'$god_address'",
     "alloc": {}, 	
     "initMinerNodes":["'$init_miners'"]
}
' > $genesis_file
LOG_INFO "`readlink -f $genesis_file` is generated"
done
