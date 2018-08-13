#
#         一键安装脚本说明
#1 build.sh在centos和Ubuntu版本测试成功；
#2 所有Linux发行版本请确保yum或apt、git已安装，并能正常使用；
#3 如遇到中途依赖库下载失败，一般和网络状况有关，请到https://github.com/bcosorg/lib找到相应的库，手动安装成功后，再执行此脚本
#

#!/bin/sh
set -e

current_dir=`pwd`

enable_guomi=0

LOG_ERROR()
{
    content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

LOG_INFO()
{
    content=${1}
    echo -e "\033[32m"${content}"\033[0m"
}

execute_cmd()
{
    command="${1}"
    #LOG_INFO "RUN: ${command}"
    eval ${command}
    ret=$?
    if [ $ret -ne 0 ];then
        LOG_ERROR "FAILED execution of command: ${command}"
        exit 1
    else
        LOG_INFO "SUCCESS execution of command: ${command}"
    fi
}

install_ubuntu_package()
{
	for i in $@ ;
	do 
		LOG_INFO "install ${i}";
		execute_cmd "sudo apt-get -y install ${i}";
	done
}

install_centos_package()
{
	for i in $@ ;
	do
		LOG_INFO "install ${i}";
		execute_cmd "yum -y install ${i}";
	done
}

#install ubuntu package
install_ubuntu_deps()
{
	install_ubuntu_package "cmake" "npm" "openssl" "libssl-dev" "libkrb5-dev" "nodejs-legacy" "jq"
	check_nodejs
	execute_cmd "sudo npm install -g cnpm --registry=https://registry.npm.taobao.org"
	execute_cmd "sudo cnpm install -g babel-cli babel-preset-es2017"
	echo '{ "presets": ["es2017"] }' > ~/.babelrc
	execute_cmd "sudo npm install -g secp256k1 --unsafe-perm"
}

# install centos package
install_centos_deps()
{
	install_centos_package "cmake3" "gcc-c++" "openssl" "openssl-devel" "nodejs" "npm" "jq"
	check_nodejs
	execute_cmd "sudo npm install -g cnpm --registry=https://registry.npm.taobao.org"
	execute_cmd "sudo cnpm install -g babel-cli babel-preset-es2017"
	echo '{ "presets": ["es2017"] }' > ~/.babelrc
}


install_all_deps()
{
	if grep -Eqi "Ubuntu" /etc/issue || grep -Eq "Ubuntu" /etc/*-release; then
		install_ubuntu_deps
		if [ $enable_guomi -eq 0 ];then
			execute_cmd "sudo cp fisco-solc-ubuntu  /usr/bin/fisco-solc"
		else
			execute_cmd "sudo cp fisco-solc-guomi-ubuntu  /usr/bin/fisco-solc-guomi && chmod +x  /usr/bin/fisco-solc-guomi"
		fi
	else
		install_centos_deps
		if [ $enable_guomi -eq 0 ];then
			execute_cmd "sudo cp fisco-solc  /usr/bin/fisco-solc"
		else
			execute_cmd "sudo cp fisco-solc-guomi-centos  /usr/bin/fisco-solc-guomi && chmod +x /usr/bin/fisco-solc-guomi"
		fi
	fi
	execute_cmd "chmod +x scripts/install_deps.sh && ./scripts/install_deps.sh"
	execute_cmd "sudo chmod +x /usr/bin/fisco-solc"
	#install console
	execute_cmd "sudo cnpm install -g ethereum-console"
}

update_configjs()
{
	execute_cmd "cd ${current_dir}/web3lib"
	sed -i 's/var encryptType = 0;/var encryptType = 1;/g' config.js
	execute_cmd "cd ${current_dir}/tools/web3lib"
	sed -i 's/var encryptType = 0;/var encryptType = 1;/g' config.js
}

install_nodejs()
{
	cd ${current_dir}
	execute_cmd "cd ${current_dir}/web3lib && cnpm install"
	execute_cmd "cd ${current_dir}/tool && cnpm install"
	execute_cmd "cd ${current_dir}/systemcontract && cnpm install"

	execute_cmd "cd ${current_dir}/tools/contract && cnpm install"
	execute_cmd "cd ${current_dir}/tools/systemcontract && cnpm install"
	execute_cmd "cd ${current_dir}/tools/web3lib && cnpm install"
}


init_guomi_nodejs()
{
	cd ${current_dir}
	execute_cmd "cd ${current_dir}/web3lib && bash guomi.sh"
	execute_cmd "cd ${current_dir}/tool && bash guomi.sh"
	execute_cmd "cd ${current_dir}/systemcontract && bash guomi.sh"

	execute_cmd "cd ${current_dir}/tools/contract && bash guomi.sh"
	execute_cmd "cd ${current_dir}/tools/systemcontract && bash guomi.sh"
	execute_cmd "cd ${current_dir}/tools/web3lib && bash guomi.sh"
	update_configjs
}

build_ubuntu_source()
{
	# build source
	execute_cmd "mkdir -p build && cd build/"
	if [ ${enable_guomi} -eq 0 ];then
		execute_cmd "cmake -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF .. "
	else
		execute_cmd "cmake -DENCRYPTTYPE=ON -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF .. "
	fi
	execute_cmd "make && make install"
}

build_centos_source()
{
	# build source
	execute_cmd "mkdir -p build && cd build/"
	if [ ${enable_guomi} -eq 0 ];then
		execute_cmd "cmake3 -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF .. "
	else
		execute_cmd "cmake3 -DENCRYPTTYPE=ON -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF .. "
	fi
	execute_cmd "make -j2 && make install && cd ${current_dir}"
}

build_source()
{
	cd ${current_dir}
	if grep -Eqi "Ubuntu" /etc/issue || grep -Eq "Ubuntu" /etc/*-release; then
		build_ubuntu_source
	else
		build_centos_source
	fi
}

nodejs_init()
{
	cd ${current_dir}
	# install nodejs
	install_nodejs
	if [ ${enable_guomi} -eq 1 ];then
		init_guomi_nodejs
	fi
}

#### check fisco-bcos
check_fisco()
{
	if [ ! -f "/usr/local/bin/fisco-bcos" ]; then
		LOG_ERROR "fisco-bcos build fail!"
	else
		LOG_INFO "fisco-bcos build SUCCESS! path: /usr/local/bin/fisco-bcos"
	fi
}

#### check nodejs
check_nodejs()
{
	node=$(node -v)
	echo | awk  '{if(node < "v6") print "WARNING : fisco need nodejs verion newer than v6(refer to: https://gaoliming123.github.io/2017/07/30/node/) , current version is '$node'"}' node="$node"
	if [ "" = "`openssl ecparam -list_curves | grep secp256k1`" ];
	then
    	LOG_ERROR "Current Openssl Don't Support secp256k1 ! Please Upgrade Openssl To  OpenSSL 1.0.2k-fips"
    	exit;
	fi
}


check()
{
	check_fisco
	check_nodejs
}	

Usage()
{
	LOG_INFO "sudo ./build.sh: \tfisco-bcos build and install"
	LOG_INFO "sudo ./build.sh -g: \tguomi-fisco-bcos build and install"
	LOG_INFO "sudo ./build.sh -h: \tprint help info"
	exit 0
}

check_input_param()
{
	if [ $# -ge 1 ] && [ "${1}" != "-g" ] && [ "${1}" != "-g" ] && [ "${1}" != "-h" ] && [ "${1}" != "--help" ];then
		Usage
	fi

	if [ "${1}" = "-h" ] || [ "${1}" = "--help" ];then
		Usage
	fi

	if [ "${1}" = "-g" ] || [ "${1}" = "--guomi" ];then
		enable_guomi=1
	fi
}

install_all()
{
	### install all deps
	install_all_deps
	### build source
	build_source
	### init nodejs
	nodejs_init
	### check
	check
}
check_input_param "$@"
install_all
