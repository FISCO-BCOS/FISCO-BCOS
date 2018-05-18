#!/usr/bin/env bash
set -e
function LOG_ERROR()
{
    local content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

function LOG_INFO()
{
    local content=${1}
    echo -e "\033[34m"${content}"\033[0m"
}

function execute_cmd()
{
    local command="${1}"
    eval ${command}
    local ret=$?
    if [ $ret -ne 0 ];then
        LOG_ERROR "execute command ${command} FAILED"
        exit 1
    else
        LOG_INFO "execute command ${command} SUCCESS"
    fi
}

CUR_DIR=`pwd`
function download_and_install()
{
    cd ${CUR_DIR}
    local url=${1}
    local pkg_name=${2}
    local install_cmd=${3}
    local patch_path=""
    if [ $# -eq 4 ];then
        patch_path=${CUR_DIR}"/patch/"${4}
    fi

    local PKG_NAME=${CUR_DIR}/${pkg_name}.tar.gz
    local PKG_PATH=${CUR_DIR}/${pkg_name}
    local command="wget "${url}
    execute_cmd "${command}"
    
    command="tar -xvf "${PKG_NAME}
    execute_cmd "${command}"
    
    if [ "${patch_path}" != "" ];then
        command="patch -f -p0 < ${patch_path}"
        LOG_INFO "patch cmd:"${command}
        execute_cmd "${command}"
    fi
    cd ${PKG_PATH}
    execute_cmd "${install_cmd}"

    execute_cmd "rm -rf ${PKG_NAME}"
    execute_cmd "rm -rf ${PKG_PATH}"
}

function get_cmake_cmd()
{
    if grep -Eqi "Ubuntu" /etc/issue || grep -Eq "Ubuntu" /etc/*-release; then
        cmake_cmd="cmake -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF .."
    else
        cmake_cmd="cmake3 -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF .."
    fi
}

function install_deps_centos()
{
    execute_cmd "sudo yum -y install cmake3"
    execute_cmd "sudo yum -y install patch"
    execute_cmd "sudo yum -y install flex"
    execute_cmd "sudo yum -y install bison"
    execute_cmd "sudo yum -y install gcc"
    execute_cmd "sudo yum -y install gcc-c++"
}


function install_deps_ubuntu()
{
    execute_cmd "sudo apt-get install -y cmake3"
    execute_cmd "sudo apt-get install -y patch"
    execute_cmd "sudo apt-get install -y flex"
    execute_cmd "sudo apt-get install -y bison"
    execute_cmd "sudo apt-get install -y gcc"
    execute_cmd "sudo apt-get install -y g++"
}

###install pre-packages
if grep -Eqi "Ubuntu" /etc/issue || grep -Eq "Ubuntu" /etc/*-release; then
    install_deps_ubuntu
else
    install_deps_centos
fi
##install pbc
pbc_pkg_name="pbc-0.5.14"
pbc_url="https://crypto.stanford.edu/pbc/files/pbc-0.5.14.tar.gz"
pbc_install_cmd="./configure && make -j2 && make install"
download_and_install "${pbc_url}" "${pbc_pkg_name}" "${pbc_install_cmd}"

sleep 10
##install pbc-sig
get_cmake_cmd
pbc_sig_pkg_name="pbc_sig-0.0.8"
pbc_sig_url="https://crypto.stanford.edu/pbc/sig/files/pbc_sig-0.0.8.tar.gz"
#pbc_sig_install_cmd="mkdir build && cd build && ${cmake_cmd} && make -j2 && make install"
pbc_sig_install_cmd="./configure && make -j2&& make install"
pbc_sig_path="pbc_sig.patch"
download_and_install "${pbc_sig_url}" "${pbc_sig_pkg_name}" "${pbc_sig_install_cmd}" "${pbc_sig_path}"


