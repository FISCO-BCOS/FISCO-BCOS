 # "Copyright [2018] <fisco-dev>"
 # @ function on-click install shell script(appliable for centos and ubuntu)
 # @ Require: yum or apt, git are ready
 # @ attention: if dependecies are downloaded failed, 
 #              please fetch packages into "deps/src" from https://github.com/bcosorg/lib manually
 #              and execute this shell script later
 # @ author: fisco-dev  
 # @ file: build.sh
 # @ date: 2018

#!/bin/sh
set -e

current_dir=`pwd`"/.."

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
        execute_cmd "yum -y install ${i}";
    done
}

#install ubuntu package
install_ubuntu_deps() {
    install_ubuntu_package "cmake" "npm" "openssl" "libssl-dev" "libkrb5-dev"
}

# install centos package
install_centos_deps() {
    install_centos_package "cmake3" "gcc-c++" "openssl" "openssl-devel"
}

#=== install all deps
install_all_deps()
{
    if grep -Eqi "Ubuntu" /etc/issue || grep -Eq "Ubuntu" /etc/*-release; then
        install_ubuntu_deps
    else
        install_centos_deps
    fi
}

build_ubuntu_source() {
    # build source
    execute_cmd "mkdir -p build && cd build/"
    execute_cmd "cmake .. "
    execute_cmd "make && make install"
}

build_centos_source() {
    # build source
    execute_cmd "mkdir -p build && cd build/"
    execute_cmd "cmake3 .. "
    execute_cmd "make -j2 && make install && cd ${current_dir}"
}

build_source() {
    cd ${current_dir}
    if grep -Eqi "Ubuntu" /etc/issue || grep -Eq "Ubuntu" /etc/*-release; then
        build_ubuntu_source
    else
        build_centos_source
    fi
}

install_all_deps
build_source
