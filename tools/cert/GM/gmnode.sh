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


function openssl_check()
{
    if [ "" = "`$OPENSSL_CMD ecparam -list_curves | grep SM2`" ];
    then
        echo "Current Openssl Don't Support SM2 ! Please Upgrade tassl"
        exit;
    fi
}

function usage()
{
    if [ $# -lt 2 ];then
        LOG_ERROR "Usage: bash gmnode.sh agency_name agency_dir node_name node_dir"
        exit 1
    fi
}

function prepare_check()
{
   local agency_name="${1}"
   local agency_dir="${agency_name}"
   if [ $# -gt 2 ];then
        if [ ! -f "${2}/gmagency.crt" ];then
           agency_dir="${2}/${1}"
        else
           agency_dir=${2}
        fi
   fi
   ##check existance of agency
   if [ ! -d "${agency_dir}" ];then
       LOG_ERROR "Must create certificates for ${agency_dir} first!"
       exit 1
   fi 
    ##check existance of agency certificate
   if [ ! -f "${agency_dir}/gmagency.crt" ];then
       LOG_ERROR "Must create ca certificate gmagency.crt first!"
       exit 1
   fi
}

function node_exists()
{
    local node_dir=""
    if [ $# -lt 4 ];then
        node_dir="${1}/${2}"
    else
        node_dir="${4}"
    fi
    if [ -f "$node_dir"/gmnode.crt ];then
        LOG_ERROR "node ${node_dir}/gmnode.crt already Exist, please clean the old data first!"
        exit 1
    fi
}


OPENSSL_CMD=${TARGET_DIR}/bin/openssl
function gen_node_cert() {
    local agency_name="${1}"
    local agency_dir="${agency_name}"
    local node_name="${2}"
    local node_dir="${agency_name}/${node_name}"
    local manual="yes"

    if [ $# -gt 4 ];then
       agency_name="${1}"
       if [ ! -f "${2}/gmagency.crt" ];then
           agency_dir="${2}/${1}"
       else
           agency_dir="${2}"
       fi
       node_name="${3}"
       node_dir="${4}"
       manual="${5}"
    fi
    
    execute_cmd "mkdir -p ${node_dir}"
    LOG_INFO "--------------Gen Signature certificate with Guomi Algorithm---------------"
    execute_cmd "${OPENSSL_CMD} genpkey -paramfile ${agency_dir}/gmsm2.param -out ${node_dir}/gmnode.key"
    if [ "${manual}" != "yes" ];then
        execute_cmd "$OPENSSL_CMD req -new -key ${node_dir}/gmnode.key -config cert.cnf -out ${node_dir}/gmnode.csr -batch"
    else
        execute_cmd "$OPENSSL_CMD req -new -key ${node_dir}/gmnode.key -out ${node_dir}/gmnode.csr"
    fi
    execute_cmd "$OPENSSL_CMD x509 -req -CA ${agency_dir}/gmagency.crt -CAkey ${agency_dir}/gmagency.key -days 3650 -CAcreateserial -in ${node_dir}/gmnode.csr -out ${node_dir}/gmnode.crt -extfile cert.cnf -extensions v3_req"

    LOG_INFO "-----------------Gen Encryption certificate with Guomi Algorithm-------------"
    execute_cmd "$OPENSSL_CMD genpkey -paramfile ${agency_dir}/gmsm2.param -out ${node_dir}/gmennode.key"
    if [ "${manual}" != "yes"  ];then
        execute_cmd "$OPENSSL_CMD req -new -key ${node_dir}/gmennode.key -config cert.cnf -out ${node_dir}/gmennode.csr -batch"
    else
        execute_cmd "$OPENSSL_CMD req -new -key ${node_dir}/gmennode.key -out ${node_dir}/gmennode.csr"
    fi
    execute_cmd "$OPENSSL_CMD x509 -req -CA ${agency_dir}/gmagency.crt -CAkey ${agency_dir}/gmagency.key -days 3650 -CAcreateserial -in ${node_dir}/gmennode.csr -out ${node_dir}/gmennode.crt -extfile cert.cnf -extensions v3enc_req"
    $OPENSSL_CMD ec -in ${node_dir}/gmnode.key -outform DER |tail -c +8 | head -c 32 | xxd -p -c 32 | cat >${node_dir}/gmnode.private
    $OPENSSL_CMD ec -in ${node_dir}/gmnode.key -text -noout | sed -n '7,11p' | sed 's/://g' | tr "\n" " " | sed 's/ //g' | awk '{print substr($0,3);}'  | cat >${node_dir}/gmnode.nodeid

    if [ "" != "`$OPENSSL_CMD version | grep 1.0.2`" ];
    then
        $OPENSSL_CMD x509  -text -in ${node_dir}/gmnode.crt | sed -n '5p' |  sed 's/://g' | tr "\n" " " | sed 's/ //g' | sed 's/[a-z]/\u&/g' | cat >${node_dir}/gmnode.serial
    else
        $OPENSSL_CMD x509  -text -in ${node_dir}/gmnode.crt | sed -n '4p' |  sed 's/ //g' | sed 's/.*(0x//g' | sed 's/)//g' |sed 's/[a-z]/\u&/g' | cat >${node_dir}/gmnode.serial
    fi
    cp ${agency_dir}/gmca.crt ${agency_dir}/gmagency.crt ${node_dir}
	rm -rf ${node_dir}/gmnode.csr ${node_dir}/gmennode.csr  ${agency_dir}/gmagency.srl
    cd ${node_dir}
    nodeid=`cat gmnode.nodeid | head`
    serial=`cat gmnode.serial | head`
    
    cat>gmnode.json <<EOF
 {
 "id":"$nodeid",
 "name":"$node",
 "agency":"$agency",
 "caHash":"$serial"
}
EOF

	cat>gmnode.ca <<EOF
	{
	"serial":"$serial",
	"pubkey":"$nodeid",
	"name":"$node"
	}
EOF
    echo "Build  $node Crt suc!!!"
}


usage "$@"
prepare_check "$@"
node_exists "$@"

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
gen_node_cert "$@"
