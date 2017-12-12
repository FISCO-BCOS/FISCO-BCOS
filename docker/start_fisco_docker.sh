#! /bin/sh
set -e
log_dir="/log-fisco-bcos"
mkdir -p $log_dir
mkdir -p $log_dir/node0
mkdir -p $log_dir/node1

rpcport1=35500
rpcport2=35501
p2pport1=53300
p2pport2=53301
channelPort1=54300
channelPort2=54301
local_ip="0.0.0.0"

echo -e "--------------区块链docker节点信息--------------"
echo -e "节点信息："
echo -e "  节点名 \tIP\t\trpcport\t\tp2pport\t\tchannelPort\t日志目录"
echo -e "  node0\t\t"$local_ip"\t\t"$rpcport1"\t\t"$p2pport1"\t\t"$channelPort1"\t\t"$log_dir/node0
echo -e "  node1\t\t"$local_ip"\t\t"$rpcport2"\t\t"$p2pport2"\t\t"$channelPort2"\t\t"$log_dir/node1
echo -e "验证区块链节点是否启动："
echo -e "	# ps -ef |grep fisco-bcos"
echo -e "验证一个区块链节点是否连接了另一个："
echo -e "	# cat "$log_dir"/node0/* | grep peers"
echo -e "验证区块链节点是否能够进行共识： "
echo -e "	# tail -f "$log_dir"/node0/* | grep ++++"
echo -e ""
echo -e "尝试启动区块链docker节点..."
docker run -v $log_dir:/bcos-data/log -p 35500:35500 -p 35501:35501 -p 53300:53300 -p 53301:53301 -p 54300:54300 -p 54301:54301 -i docker.io/fiscoorg/fiscobcos:latest /bcos-data/start_node.sh &

