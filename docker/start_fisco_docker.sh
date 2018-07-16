#! /bin/sh
set -e
docker pull docker.io/fiscoorg/fiscobcos:latest

chmod +x `pwd`/node0/start.sh
chmod +x `pwd`/node1/start.sh

rpcport1=8301
rpcport2=8302
p2pport1=30901
p2pport2=30902
channelPort1=40001
channelPort2=40002
local_ip=`ifconfig | sed -En 's/127.0.0.1//;s/.*inet (addr:)?(([0-9]*\.){3}[0-9]*).*/\2/p' | awk 'END{print}' `
local_dir=`pwd`

if [ "" = "`grep 0.0.0.0 -rl $local_dir/node1/data/bootstrapnodes.json`" ];
then
    echo "Please Confirm Host Is Right Which In $local_dir/node1/data/bootstrapnodes.json !"
else
    sed -i "s/0.0.0.0/$local_ip/g" `grep 0.0.0.0 -rl $local_dir/node1/data/bootstrapnodes.json `
fi

echo -e "--------------FISCO-BCOS Docker Node Info--------------"
echo -e "Local Ip:"$local_ip
echo -e "Local Dir:"$local_dir
echo -e "Info："
echo -e "  Name \tIP\t\trpcport\t\tp2pport\t\tchannelPort\tLogDir"
echo -e "  node0\t\t"$local_ip"\t\t"$rpcport1"\t\t"$p2pport1"\t\t"$channelPort1"\t\t"$local_dir/node0/log/
echo -e "  node1\t\t"$local_ip"\t\t"$rpcport2"\t\t"$p2pport2"\t\t"$channelPort2"\t\t"$local_dir/node1/log/

echo -e "try to start docker node0..."

docker run  -v `pwd`/node0:/fisco-bcos/node -p 8301:8301 -p 30901:30901 -p 40001:40001 -i docker.io/fiscoorg/fiscobcos:latest /fisco-bcos/start_node.sh &
sleep 10
echo -e "try to start docker node1..."
docker run  -v `pwd`/node1:/fisco-bcos/node -p 8302:8302 -p 30902:30902 -p 40002:40002 -i docker.io/fiscoorg/fiscobcos:latest /fisco-bcos/start_node.sh &
sleep 5

echo -e "Check Node Is Running："
echo -e "	# ps -ef |grep fisco-bcos"
echo -e "Check Node Has Connect Other Node："
echo -e "	# tail -f `pwd`/node1/log/* | grep topics"
echo -e "Check Node Is Working： "
echo -e "	# tail -f `pwd`/node0/log/* | grep ++++"
echo -e ""
