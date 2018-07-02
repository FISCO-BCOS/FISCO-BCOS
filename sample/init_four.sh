#! /bin/bash
if [ $# != 2 ] ; then 
echo "用法: $0 <本机 IP 地址> <另一台机器 IP 地址>"
echo "举例: $0 192.168.1.100 192.168.1.101"
exit 1
fi 

function generateSample()
{
    node_name=$1
    ip=$2
    rpcport=$3
    p2pport=$4
    channelPort=$5

    echo '{
    "networkid":"1234567",
    "nodedir":"'$nodes_dir$node_name'/",
    "ip":"'$ip'",
    "rpcport":'$rpcport',
    "p2pport":'$p2pport',    
    "channelPort":'$channelPort'
}' > $node_name.sample
}

function generateLocalRunScript()
{
    node_a=$1
    node_b=$2
    echo '#! /bin/sh
chmod +x '$nodes_dir'/'$node_a'/start0.sh
chmod +x '$nodes_dir'/'$node_b'/start1.sh
sh '$nodes_dir'/'$node_a'/start0.sh &
sh '$nodes_dir'/'$node_b'/start1.sh &   
echo -e "--------------区块链节点已生成，并尝试启动--------------"
echo -e "验证区块链节点是否启动："
echo -e "   # ps -ef |grep fisco-bcos"
echo -e "验证一个区块链节点是否连接了另一个："
echo -e "   # cat /bcos-data/node*/log/* | grep \"topics Send to\""
echo -e "验证区块链节点是否能够进行共识： "
echo -e "   # tail -f /bcos-data/node*/log/* | grep ++++" 
'
}

function generateRemoteRunScript()
{
    node_a=$1
    node_b=$2
    echo '#! /bin/sh
sudo cp fisco-bcos /usr/local/bin/fisco-bcos
sudo mkdir -p '$nodes_dir'
sudo chmod -R 777 '$nodes_dir'
ln -s $(pwd)/'$node_a' '$nodes_dir'/'$node_a'
ln -s $(pwd)/'$node_b' '$nodes_dir'/'$node_b'
chmod +x '$node_a'/start2.sh
chmod +x '$node_b'/start3.sh
sh '$nodes_dir'/'$node_a'/start2.sh &
sh '$nodes_dir'/'$node_b'/start3.sh &   
echo -e "--------------区块链节点已生成，并尝试启动--------------"
echo -e "验证区块链节点是否启动："
echo -e "   # ps -ef |grep fisco-bcos"
echo -e "验证一个区块链节点是否连接了另一个："
echo -e "   # cat /bcos-data/node*/log/* | grep \"topics Send to\""
echo -e "验证区块链节点是否能够进行共识： "
echo -e "   # tail -f /bcos-data/node*/log/* | grep ++++"  
'
}

local_ip=$1
remote_ip=$2
#nodes_dir=~/bcos-data/
nodes_dir=/bcos-data/
sudo mkdir -p $nodes_dir
sudo chmod -R 777 $nodes_dir

rpcport1=8847
rpcport2=8848
rpcport3=8849
rpcport4=8850
p2pport1=30405
p2pport2=30406
p2pport3=30407
p2pport4=30408
channelPort1=8991
channelPort2=8992
channelPort3=8993
channelPort4=8994

#生成4个节点的sample文件
generateSample "node4_0" $local_ip $rpcport1 $p2pport1 $channelPort1
generateSample "node4_1" $local_ip $rpcport2 $p2pport2 $channelPort2
generateSample "node4_2" $remote_ip $rpcport3 $p2pport3 $channelPort3
generateSample "node4_3" $remote_ip $rpcport4 $p2pport4 $channelPort4

#根据sample文件生成4个节点的全部文件
node init.js node4_0.sample node4_1.sample node4_2.sample node4_3.sample

#生成本地启动脚本
generateLocalRunScript "node4_0" "node4_1" > start_two.sh
chmod +x start_two.sh

#生成另一台机器的安装包
pkg_name=$2_install
mkdir $pkg_name
mv $nodes_dir/node4_2 $pkg_name/
mv $nodes_dir/node4_3 $pkg_name/
cp /usr/local/bin/fisco-bcos $pkg_name/
generateRemoteRunScript "node4_2" "node4_3" > $pkg_name/start_two.sh
chmod +x $pkg_name/start_two.sh
tar -zcf $pkg_name.tar.gz $pkg_name
rm -rf $pkg_name

#打印节点信息
echo -e "--------------区块链节点生成完成！--------------"
echo -e "节点信息："
echo -e "  节点名 \tIP\t\trpcport\t\tp2pport\t\tchannelPort\t\t启动时目录"
echo -e "  node4_0\t"$local_ip"\t"$rpcport1"\t\t"$p2pport1"\t\t"$channelPort1"\t\t"$local_ip":"$nodes_dir"node4_0/"
echo -e "  node4_1\t"$local_ip"\t"$rpcport2"\t\t"$p2pport2"\t\t"$channelPort2"\t\t"$local_ip":"$nodes_dir"node4_1/"
echo -e "  node4_2\t"$remote_ip"\t"$rpcport3"\t\t"$p2pport3"\t\t"$channelPort3"\t\t"$remote_ip":"$nodes_dir"node4_2/"
echo -e "  node4_3\t"$remote_ip"\t"$rpcport4"\t\t"$p2pport4"\t\t"$channelPort4"\t\t"$remote_ip":"$nodes_dir"node4_3/"
echo -e
#打印安装向导
echo -e "节点启动方法："
echo -e "  启动本机的两个节点"
echo -e "      # ./start_two.sh"
echo -e ""
echo -e "  启动另一台机器的两个节点："
echo -e "      将本目录下刚生成的 "$pkg_name.tar.gz" 拷贝到"$remote_ip"的任意目录下"
echo -e "      在"$remote_ip"上执行命令："
echo -e "      1. # tar -zxvf "$pkg_name.tar.gz
echo -e "      2. # cd "$pkg_name
echo -e "      3. # chmod +x start_two.sh"
echo -e "      4. # ./start_two.sh"
echo -e "      两个节点会在这另一台机器的目录"$remote_ip":"$nodes_dir"下被安装并运行"




