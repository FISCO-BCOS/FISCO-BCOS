#
#         一键安装脚本说明
#1 build.sh在centos和Ubuntu版本测试成功；
#2 所有Linux发行版本请确保yum或apt、git已安装，并能正常使用；
#3 如遇到中途依赖库下载失败，一般和网络状况有关，请到https://github.com/bcosorg/lib找到相应的库，手动安装成功后，再执行此脚本
#

#!/bin/bash
SHELL_FOLDER=$(cd $(dirname $0);pwd)
current_dir=`pwd`

enable_guomi=0
build_source=0
version=`cat ${SHELL_FOLDER}/release_note.txt| sed "s/^[vV]//"`
package_name="fisco-bcos.tar.gz"
binary_link=https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/v${version}/${package_name}
Ubuntu_Platform=0
Centos_Platform=1

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

# get platform: now support debain/ubuntu, fedora/centos, oracle
get_platform()
{
    uname -v > /dev/null 2>&1 || { echo >&2 "ERROR - FISCO-BCOS requires 'uname' to identify the platform."; exit 1; }
    case $(uname -s) in
    Darwin)
        LOG_ERROR "FISCO-BCOS doesn't Support MAC OS Yet!"
        exit 1;;
    FreeBSD)
        LOG_ERROR "FISCO-BCOS doesn't Support FreeBSD Yet!"
        exit 1;;
    Linux)
        if [ -f "/etc/arch-release" ]; then
            LOG_ERROR "FISCO-BCOS doesn't Support arch-linux Yet!"
        elif [ -f "/etc/os-release" ];then
            DISTRO_NAME=$(. /etc/os-release; echo $NAME)
            case $DISTRO_NAME in
            Debian*|Ubuntu)
                LOG_INFO "Debian*|Ubuntu Platform"
                return ${Ubuntu_Platform};; #ubuntu type
            Fedora|CentOS*)
                LOG_INFO "Fedora|CentOS* Platform"
                return ${Centos_Platform};; #centos type
            Oracle*)
                LOG_INFO "Oracle Platform"
                return ${Centos_Platform};; #oracle type
            esac
        else
            LOG_ERROR "Unsupported Platform"
        fi
    esac
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
		execute_cmd "sudo yum -y install ${i}";
	done
}

#install ubuntu package
install_ubuntu_deps()
{
	#delete the default nodejs of ubuntu
        execute_cmd "sudo apt -y remove nodejs"
	install_ubuntu_package "cmake" "openssl" "libssl-dev" "libkrb5-dev" "jq"
	execute_cmd "curl -sL https://deb.nodesource.com/setup_6.x | sudo -E bash -"
        execute_cmd "sudo apt-get -y install nodejs"
	check_nodejs
	cnpm_path=`which cnpm`
        if [ -f "${cnpm_path}" ];then
            execute_cmd "sudo rm -rf ${cnpm_path}"
        fi
    execute_cmd "sudo npm set registry https://registry.npm.taobao.org && sudo npm set disturl https://npm.taobao.org/dist && sudo npm cache clean"
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
	get_platform
        platform=`echo $?`
	if [ ${platform} -eq ${Ubuntu_Platform} ];then
		install_ubuntu_deps
		if [ $enable_guomi -eq 0 ];then
			execute_cmd "sudo cp fisco-solc-ubuntu  /usr/bin/fisco-solc && sudo chmod +x /usr/bin/fisco-solc"
		else
			execute_cmd "sudo cp fisco-solc-guomi-ubuntu  /usr/bin/fisco-solc-guomi && sudo chmod +x  /usr/bin/fisco-solc-guomi"
		fi
	elif [ ${platform} -eq ${Centos_Platform} ];then
		install_centos_deps
		if [ $enable_guomi -eq 0 ];then
			execute_cmd "sudo cp fisco-solc  /usr/bin/fisco-solc && sudo chmod +x /usr/bin/fisco-solc"
		else
			execute_cmd "sudo cp fisco-solc-guomi-centos  /usr/bin/fisco-solc-guomi && sudo chmod +x /usr/bin/fisco-solc-guomi"
		fi
	else
		LOG_ERROR "Unsupported Platform"
		exit 1
	fi
	execute_cmd "chmod +x scripts/install_deps.sh && sudo ./scripts/install_deps.sh"
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
		execute_cmd "cmake .. "
	else
		execute_cmd "cmake -DENCRYPTTYPE=ON .. "
	fi
	execute_cmd "make && sudo make install"
}

build_centos_source()
{
	# build source
	execute_cmd "mkdir -p build && cd build/"
	if [ ${enable_guomi} -eq 0 ];then
		execute_cmd "cmake3 .. "
	else
		execute_cmd "cmake3 -DENCRYPTTYPE=ON .. "
	fi
	execute_cmd "make && sudo make install && cd ${current_dir}"
}

build_source()
{
	cd ${current_dir}
	get_platform
    platform=`echo $?`
	if [ ${platform} -eq ${Ubuntu_Platform} ];then
		build_ubuntu_source
	elif [ ${platform} -eq ${Centos_Platform} ];then
		build_centos_source
	else
		LOG_ERROR "Unsupported Platform, Exit"
		exit 1
	fi
}

download_binary()
{
	if [ ${enable_guomi} -eq 0 ];then
		execute_cmd "curl -LO ${binary_link}"
		execute_cmd "tar -zxf ${package_name}"
	else
		build_source
		# execute_cmd "curl -Lo fisco-bcos ${binary_link}-gm"
		return 0
	fi
	execute_cmd "chmod a+x fisco-bcos"
	execute_cmd "sudo mv fisco-bcos /usr/local/bin/"
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
    	LOG_ERROR "Current Openssl doesn't Support secp256k1 ! Please Upgrade Openssl To  OpenSSL 1.0.2k-fips"
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
	echo $1
	cat << EOF
Usage:
Optional:
    -b       Compile from source code 
    -g       Compile fisco-bcos with guomi algorithms
    -h       Help
Example: 
    bash build.sh 
    bash build.sh -g
EOF
	exit 0
}

parse_param()
{
	while getopts "hgb" option;do
		case $option in
		b) build_source=1;;
		g) enable_guomi=1;;
		h) Usage;;
		esac
	done
}

install_all()
{
	install_all_deps
	if [ ${build_source} -eq 0 ];then
		download_binary
	else
		build_source
	fi
	nodejs_init
	check
}
parse_param "$@"
install_all
