# "Copyright [2018] <fisco-bcos>"
# @ function : one-click install shell script(appliable for centos and ubuntu)
# @ Require  : yum or apt, git are ready
# @ attention: if download dependecies failed, 
#              please fetch packages into "deps/src" from https://github.com/FISCO-BCOS/LargeFiles/tree/master/libs manually
#              and execute this shell script later
# @ author   : yujiechen  
# @ file     : build.sh
# @ date     : 2018

#!/bin/bash
SHELL_FOLDER=$(cd $(dirname $0);pwd)
project_dir="${SHELL_FOLDER}/.."
use_cores=1
test_mode=0
Ubuntu_Platform=0
Centos_Platform=1

clear_cache()
{ 
    cd ${project_dir}
    execute_cmd "rm -rf deps/src/*stamp"
}

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
        # clear_cache
        exit 1
    else
        LOG_INFO "SUCCESS execution of command: ${command}"
    fi
}

# get platform: now support debain/ubuntu, fedora/centos, oracle
get_platform()
{
    uname -v > /dev/null 2>&1 || { echo >&2 "ERROR - Require 'uname' to identify the platform."; exit 1; }
    case $(uname -s) in
    Darwin)
        LOG_ERROR "Not Support MAC OS Yet!"
        exit 1;;
    FreeBSD)
        LOG_ERROR "Not Support FreeBSD Yet!"
        exit 1;;
    Linux)
        if [ -f "/etc/arch-release" ];then
            LOG_ERROR "Not Support arch-linux Yet!"
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
install_ubuntu_package "cmake" "libssl-dev" "openssl"
}

# install centos package
install_centos_deps()
{
install_centos_package "cmake3" "gcc-c++" "openssl" "openssl-devel" 
}

install_all_deps()
{
    get_platform
    platform=`echo $?`
    if [ ${platform} -eq ${Ubuntu_Platform} ];then
        install_ubuntu_deps
    elif [ ${platform} -eq ${Centos_Platform} ];then
        install_centos_deps
    else
        LOG_ERROR "Unsupported Platform"
        exit 1
    fi
}

build_source()
{
    cd ${project_dir}
    get_platform
    platform=`echo $?`
    cmake_cmd="cmake"
    if [ ${platform} -eq ${Centos_Platform} ];then
        cmake_cmd="cmake3"
    fi
    execute_cmd "mkdir -p build && cd build/"
    if [ ${test_mode} -eq 1 ];then
        execute_cmd "${cmake_cmd} -DTESTS=ON .."
    else
        execute_cmd "${cmake_cmd} -DTESTS=OFF .."
    fi
    execute_cmd "make -j${use_cores}"
}

Usage()
{
	echo $1
	cat << EOF
Usage:
Optional:
    -j       Cores will be used to compile
    -t       Enable test mode (generate mini-consensus/mini-sync/mini-evm/mini-storage/test_verifier)
    -h       Help
Example: 
    bash install.sh 
    bash install.sh -j4
    bash install.sh -t
EOF
	exit 0
}

parse_param()
{
	while getopts "htj:" option;do
		case $option in
		j) use_cores=$OPTARG
            if [ ${use_cores} -gt $(nproc) ];then
                use_cores=$(nproc)
            elif [ ${use_cores} -lt 1 ];then
                use_cores=1
            fi
        ;;
        t) test_mode=1;;
		h) Usage;;
		esac
	done
}

install_all()
{
	install_all_deps
	build_source
    cd ${project_dir}
}

parse_param "$@"
install_all
