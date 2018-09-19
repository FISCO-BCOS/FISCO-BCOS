#!/bin/bash
function LOG_ERROR()
{
    local content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

function LOG_INFO()
{
    local content=${1}
    echo -e "\033[32m"${content}"\033[0m"
}

function execute_cmd()
{
    local command="${1}"
    eval ${command}
    local ret=$?
    if [ $ret -ne 0 ];then
        LOG_ERROR "FAILED execution of command: ${command}"
        exit 1
    else
        LOG_INFO "SUCCESS execution of command: ${command}"
    fi
}

CUR_DIR=`pwd`
TARGET_DIR="${HOME}/TASSL"
function need_install()
{
    if [ ! -f "${TARGET_DIR}/bin/openssl" ];then
	LOG_INFO "== TASSL HAS NOT BEEN INSTALLED, INSTALL NOW =="
	return 1
    fi
    LOG_INFO "== TASSL HAS BEEN INSTALLED, NO NEED TO INSTALL ==="
    return 0
}

function download_and_install()
{
    need_install
    local required=$?
    if [ $required -eq 1 ];then
        local url=${1}
        local pkg_name=${2}
        local install_cmd=${3}

        local PKG_PATH=${CUR_DIR}/${pkg_name}
        git clone ${url}/${pkg_name}
        execute_cmd "cd ${pkg_name}"
        local shell_list=`find . -name *.sh`
        execute_cmd "chmod a+x ${shell_list}"
        execute_cmd "chmod a+x ./util/pod2mantest"
        #cd ${PKG_PATH}
        execute_cmd "${install_cmd}"

        execute_cmd "rm -rf ${PKG_PATH}"
        cd "${CUR_DIR}"
    fi
}


function install_deps_centos()
{
    execute_cmd "sudo yum -y install flex"
    execute_cmd "sudo yum -y install bison"
    execute_cmd "sudo yum -y install gcc"
    execute_cmd "sudo yum -y install gcc-c++"
}


function install_deps_ubuntu()
{
    execute_cmd "sudo apt-get install -y flex"
    execute_cmd "sudo apt-get install -y bison"
    execute_cmd "sudo apt-get install -y gcc"
    execute_cmd "sudo apt-get install -y g++"
}

function ca_exists()
{
    local ca_dir=.
    if [ $# -gt 0 ];then
        ca_dir="${1}"
    fi
    if [  -f "${ca_dir}/gmca.key" ]; then
        LOG_ERROR "gmca.key exist! please clean all old file!"
        exit 1
    elif [  -f "${ca_dir}/gmca.crt" ]; then
        LOG_ERROR "gmca.crt exist! please clean all old file!"
        exit 1
    fi
}

ca_exists "$@"

###install pre-packagses
if grep -Eqi "Ubuntu" /etc/issue || grep -Eq "Ubuntu" /etc/*-release; then
    install_deps_ubuntu
else
    install_deps_centos
fi
##install tassl
tassl_name="TASSL"
tassl_url=" https://github.com/jntass"
tassl_install_cmd="bash config --prefix=${TARGET_DIR} no-shared && make -j2 && make install"
download_and_install "${tassl_url}" "${tassl_name}" "${tassl_install_cmd}"
OPENSSL_CMD=${TARGET_DIR}/bin/openssl

function openssl_check()
{
    if [ "" = "`$OPENSSL_CMD ecparam -list_curves | grep SM2`" ];
    then
        echo "Current Openssl Don't Support SM2 ! Please Upgrade tassl"
        exit;
    fi
}

function gen_ca() {
    local dst_dir=.
    local manual="yes"
    if [ $# -gt 1 ];then
        dst_dir="${1}"
        manual="${2}"
    fi
    execute_cmd "mkdir -p ${dst_dir}"
	execute_cmd "$OPENSSL_CMD genpkey -paramfile gmsm2.param -out ${dst_dir}/gmca.key"
    if [ "${manual}" != "yes"  ];then
	    execute_cmd "$OPENSSL_CMD req -x509 -days 3650 -key ${dst_dir}/gmca.key -extensions v3_ca -out ${dst_dir}/gmca.crt -config cert.cnf  -batch "
    else
        execute_cmd "$OPENSSL_CMD req -x509 -days 3650 -key ${dst_dir}/gmca.key -extensions v3_ca -out ${dst_dir}/gmca.crt "
    fi
    if [ ! -f "${dst_dir}/gmsm2.param" ];then
        execute_cmd "cp gmsm2.param ${dst_dir}"
    fi
    LOG_INFO "GENERATE ca SUCC!"
}
openssl_check
gen_ca "$@"
