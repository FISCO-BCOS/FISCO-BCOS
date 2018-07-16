#!/bin/bash
sudo mkdir -p /bcos-data/
sudo chmod 777 /bcos-data
node init.js node0.sample node1.sample
chmod +x /bcos-data/node0/start0.sh
dos2unix /bcos-data/node0/start0.sh
chmod +x /bcos-data/node1/start1.sh
dos2unix /bcos-data/node1/start1.sh
echo "尝试启动节点..."
sh /bcos-data/node0/start0.sh &
sh /bcos-data/node1/start1.sh &
sleep 5
echo -e "--------------区块链节点已生成，并尝试启动--------------"
echo -e "验证区块链节点是否启动："
echo -e "	# ps -ef |grep fisco-bcos"
echo -e "验证一个区块链节点是否连接了另一个："
echo -e "	# cat /bcos-data/node0/log/* | grep \"topics Send to\""
echo -e "验证区块链节点是否能够进行共识： "
echo -e "	# tail -f /bcos-data/node0/log/* | grep ++++"