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

echo -e "--------------Nodes info in docker--------------"
echo -e "Nodes info:"
echo -e "  node name \tIP\t\trpcport\t\tp2pport\t\tchannelPort\tlog dir"
echo -e "  node0\t\t"$local_ip"\t\t"$rpcport1"\t\t"$p2pport1"\t\t"$channelPort1"\t\t"$log_dir/node0
echo -e "  node1\t\t"$local_ip"\t\t"$rpcport2"\t\t"$p2pport2"\t\t"$channelPort2"\t\t"$log_dir/node1
echo -e "To check whether the nodes are started:"
echo -e "	# ps -ef |grep fisco-bcos"
echo -e "To check whether the nodes are connected each other:"
echo -e "	# cat "$log_dir"/node0/* | grep peers"
echo -e "To check whether the nodes can seal: "
echo -e "	# tail -f "$log_dir"/node0/* | grep ++++"
echo -e ""
echo -e "Trying to start nodes in docker..."
docker run -v $log_dir:/bcos-data/log -p 35500:35500 -p 35501:35501 -p 53300:53300 -p 53301:53301 -p 54300:54300 -p 54301:54301 -i docker.io/fiscoorg/fiscobcos:latest /bcos-data/start_node.sh &

