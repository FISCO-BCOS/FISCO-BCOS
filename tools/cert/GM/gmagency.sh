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

function usage()
{
    if [ $# -lt 1 ];then
        LOG_ERROR "Usage: bash gmagency.sh agency_name whether_add_to_agency_list(1: add to agency list; 0: doesn't add to agency list; defaut is 1)"
        exit 1
    fi
}

function openssl_check()
{
    if [ "" = "`$OPENSSL_CMD ecparam -list_curves | grep SM2`" ];
    then
        LOG_ERROR "Current Openssl Don't Support SM2 ! Please Upgrade tassl"
        exit;
    fi
}

function agency_exist()
{
    local agency_dir="${1}"
    if [ $# -gt 1 ];then
        agency_dir="${2}/${1}"
    fi
    if [ -d "${agency_dir}" ];then
        LOG_ERROR "${agency_dir} exists already, please clean all old dir!"
        exit 1;
    fi
}

function ca_exist()
{
    local ca_dir=.
    if [ $# -gt 1 ];then
        ca_dir="${3}"
    fi
    if [ ! -f "${ca_dir}/gmca.crt" ];then
        LOG_ERROR "Must create ca certificate ${ca_dir}/gmca.crt first!"
        exit 1
    fi
}

function gen_agency()
{
    local agency_name="${1}"
    local agency_dir=${agency_name}
    local ca_dir=.
    local manual="yes"
    if [ $# -gt 1 ];then
        agency_dir="${2}/${agency_name}"
        ca_dir="${3}"
        manual=${4}
    fi
    execute_cmd "mkdir -p ${agency_dir}"
    execute_cmd "$OPENSSL_CMD genpkey -paramfile ${ca_dir}/gmsm2.param -out ${agency_dir}/gmagency.key"  
    if [ "${manual}" != "yes" ];then
        execute_cmd "$OPENSSL_CMD req -new -key ${agency_dir}/gmagency.key -config cert.cnf -out ${agency_dir}/gmagency.csr -batch"
    else
        execute_cmd "$OPENSSL_CMD req -new -key ${agency_dir}/gmagency.key -out ${agency_dir}/gmagency.csr"
    fi
    execute_cmd "$OPENSSL_CMD x509 -req -CA ${ca_dir}/gmca.crt -CAkey ${ca_dir}/gmca.key -days 3650 -CAcreateserial -in ${agency_dir}/gmagency.csr -out ${agency_dir}/gmagency.crt -extfile cert.cnf -extensions v3_agency_root"
    execute_cmd "cp ${ca_dir}/gmsm2.param ${ca_dir}/gmca.crt ${agency_dir}"
    execute_cmd "cd ${agency_dir} && rm -rf gmagency.csr gmca.srl"
    LOG_INFO "GEN AGENCY CERT FOR CERT SUCC!"
}

usage "$@"
ca_exist "$@"
agency_exist "$@"
OPENSSL_CMD=${TARGET_DIR}/bin/openssl
###install pre-packages
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

openssl_check
gen_agency "$@"
