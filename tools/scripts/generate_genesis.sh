#/bin/bash
set -e
genesis_node_dir=
output_dirs=
god_address=0xf78451eb46e20bc5336e279c52bda3a3e92c09b6

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
    LOG_INFO "Usage:"
    LOG_INFO "    -d <genesis node dir>   Genesis node dir of the blockchain"
    LOG_INFO "    -o <output dir>         Where genesis.json generate"
    LOG_INFO "    -r <god account>        Address of god account(default: 0xf78451eb46e20bc5336e279c52bda3a3e92c09b6)"
    LOG_INFO "    -d                      The Path of Guomi Directory"
    LOG_INFO "    -g                      Generate genesis node for guomi-FISCO-BCOS"
    LOG_INFO "    -h                      This help"
    LOG_INFO "Example:"
    LOG_INFO "    bash $this_script -d /mydata/node0 -o /mydata/node1"
    LOG_INFO " bash $this_script -d /mydata/node0 -o /mydata/node1 -g 0xf78451eb46e20bc5336e279c52bda3a3e92c09b6"
exit -1
}
goumi_support=0
while getopts "d:o:r:gh" option;do
	case $option in
    d) genesis_node_dir=$OPTARG;;
	o) output_dirs=$OPTARG;;
    r) god_address=$OPTARG;;
    g) goumi_support=1;;
	h) help;;
	esac
done

[ -z $genesis_node_dir ] && help 'Error: Please specify <genesis node dir> using -d'
[ -z $output_dirs ] && help 'Error: Please specify <output dirs> using -o'
[ -z $god_address ] && help 'Error: Please specify <god account> using -r'

data_dir=$genesis_node_dir/data
if [ ${goumi_support} -eq 0 ];then
    nodeid_file=$data_dir/node.nodeid
else
    nodeid_file=$data_dir/gmnode.nodeid
fi

if [ ! -d "$genesis_node_dir" ]; then
    help "Error: $genesis_node_dir is not exist! Please generate node first."
elif [ ! -d "$data_dir" ]; then
    help "Error: $data_dir is not exist! Please generate node first."
elif [ ! -f "$nodeid_file" ]; then
    help "Error $nodeid_file is not exist! Please generate node first."
else
    #Generate god account
    LOG_INFO "God account address: "$god_address

    #Get genesis account
    init_miners=`cat $nodeid_file`

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
fi
