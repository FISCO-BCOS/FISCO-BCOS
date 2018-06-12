#
#         一键安装脚本说明
#1 build.sh在centos和Ubuntu版本测试成功；
#2 所有Linux发行版本请确保yum或apt、git已安装，并能正常使用；
#3 如遇到中途依赖库下载失败，一般和网络状况有关，请到https://github.com/bcosorg/lib找到相应的库，手动安装成功后，再执行此脚本
#

#!/bin/sh
set -e


#install package
if grep -Eqi "Ubuntu" /etc/issue || grep -Eq "Ubuntu" /etc/*-release; then
sudo apt-get -y install cmake
sudo apt-get -y install npm
sudo apt-get -y install openssl
sudo apt-get -y install libssl-dev libkrb5-dev
sudo apt-get -y install nodejs-legacy
sudo npm install -g cnpm --registry=https://registry.npm.taobao.org
sudo cnpm install -g babel-cli babel-preset-es2017
echo '{ "presets": ["es2017"] }' > ~/.babelrc
sudo npm install -g secp256k1
else
sudo yum -y install cmake3
sudo yum -y install gcc-c++
sudo yum -y install openssl openssl-devel
sudo yum -y install nodejs
sudo yum -y install npm
sudo npm install -g cnpm --registry=https://registry.npm.taobao.org
sudo cnpm install -g babel-cli babel-preset-es2017
echo '{ "presets": ["es2017"] }' > ~/.babelrc
fi

#install deps
chmod +x scripts/install_deps.sh
./scripts/install_deps.sh

#install fisco-solc
if grep -Eqi "Ubuntu" /etc/issue || grep -Eq "Ubuntu" /etc/*-release; then
sudo cp fisco-solc-ubuntu  /usr/bin/fisco-solc
else
sudo cp fisco-solc  /usr/bin/fisco-solc
fi
sudo chmod +x /usr/bin/fisco-solc

#install console
sudo cnpm install -g ethereum-console

#build bcos
mkdir -p build
cd build/
if grep -Eqi "Ubuntu" /etc/issue || grep -Eq "Ubuntu" /etc/*-release; then
cmake -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF .. 
else
cmake3 -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF .. 
fi

make

sudo make install

cd ..
cd ./web3lib
cnpm install

cd ..
cd ./tool
cnpm install



cd ..
cd ./systemcontract
cnpm install

if [ ! -f "/usr/local/bin/fisco-bcos" ]; then
	echo 'fisco-bcos build fail!'
	else
	echo 'fisco-bcos build succ! path: /usr/local/bin/fisco-bcos'
 fi

#nodejs 版本检查
node=$(node -v)
echo | awk  '{if(node < "v6") print "WARNING : fisco need nodejs verion newer than v6 , current version is '$node'"}' node="$node"

if [ "" = "`openssl ecparam -list_curves | grep secp256k1`" ];
then
    echo "Current Openssl Don't Support secp256k1 ! Please Upgrade Openssl To  OpenSSL 1.0.2k-fips"
    exit;
fi