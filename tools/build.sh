 # "Copyright [2018] <fisco-bcos>"
 # @ function : one-click install shell script(appliable for centos and ubuntu)
 # @ Require  : yum or apt, git are ready
 # @ attention: if dependecies are downloaded failed, 
 #              please fetch packages into "deps/src" from https://github.com/bcosorg/lib manually
 #              and execute this shell script later
 # @ author   : yujiechen  
 # @ file     : build.sh
 # @ date     : 2018

#!/bin/sh

current_dir=`pwd`"/.."

Ubuntu_Platform=0
Centos_Platform=1
Oracle_Platform=2

cd ${current_dir}

LOG_ERROR() {
    content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

LOG_INFO() {
    content=${1}
    echo -e "\033[32m"${content}"\033[0m"
}

execute_cmd() {
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
get_platform() {
    uname -v > /dev/null 2>&1 || { echo >&2 "ERROR - FISCO-BCOS requires 'uname' to identify the platform."; exit 1; }
    case $(uname -s) in
    Darwin)
        LOG_ERROR "FISCO-BCOS V2.0 Don't Support MAC OS Yet!"
        exit 1;;
    FreeBSD)
        LOG_ERROR "FISCO-BCOS V2.0 Don't Support FreeBSD Yet!"
        exit 1;;
    Linux)
        if [ -f "/etc/arch-release" ]; then
            LOG_ERROR "FISCO-BCOS V2.0 Don't Support arch-linux Yet!"
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
                return ${Oracle_Platform};; #oracle type
            esac
        else
            LOG_ERROR "Unsupported Platform"
        fi
    esac
}

install_ubuntu_package() {
    for i in $@ ;
    do 
        LOG_INFO "install ${i}";
        execute_cmd "sudo apt-get -y install ${i}";
    done
}

install_centos_package() {
    for i in $@ ;
    do
        LOG_INFO "install ${i}";
        execute_cmd "sudo yum -y install ${i}";
    done
}

#install ubuntu package
install_ubuntu_deps() {
    execute_cmd "sudo apt-get update"
    install_ubuntu_package "make" "build-essential"  \
                            "openssl" "libssl-dev" "libkrb5-dev" \
                            "libcurl4-openssl-dev" "libgmp-dev" \
                            "libleveldb-dev" "libmicrohttpd-dev" \
                            "libminiupnpc-dev" "uuid-dev"
}


# install centos package
install_centos_deps() {
    execute_cmd "sudo yum upgrade"
    install_centos_package "make" "gcc-c++" "leveldb-devel" \
                            "curl-devel" "openssl" "openssl-devel" \
                            "libmicrohttpd-devel" "gmp-devel" "libuuid-devel"
}

# install oracle package
install_oracle_deps() {
    install_centos_package "epel-release"
    install_centos_deps
}
               
#=== install all deps
install_all_deps()
{
    get_platform
    platform=`echo $?`
    if [ ${platform} -eq ${Ubuntu_Platform} ];then
        install_ubuntu_deps
    elif [ ${platform}  -eq ${Centos_Platform} ];then
        install_centos_deps
    elif [ ${platform} -eq ${Oracle_Platform} ];then
        install_oracle_deps
    else
        LOG_ERROR "unsupported platform!"
    fi
}

build_ubuntu_source() {
    # build source
    execute_cmd "mkdir -p build && cd build/"
    execute_cmd "cmake .. -DCOVERAGE=ON"
    execute_cmd "make"
    #execute_cmd "make install && cd ${current_dir}"
}

build_centos_source() {
    # build source
    execute_cmd "mkdir -p build && cd build/"
    execute_cmd "cmake3 .. -DCOVERAGE=ON"
    execute_cmd "make"
    #execute_cmd "make install && cd ${current_dir}"
}

build_source() {
    get_platform
    platform=`echo $?`
    execute_cmd "cd ${current_dir}"
    if [ ${platform} -eq ${Ubuntu_Platform} ];then
        build_ubuntu_source
    elif [ ${platform} -eq ${Centos_Platform} ] || [ ${platform} -eq ${Oracle_Platform} ];then
        build_centos_source
    else
        LOG_ERROR "unsupported platform!"
    fi
}

install_all_deps
build_source
