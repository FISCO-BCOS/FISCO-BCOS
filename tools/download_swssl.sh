#!/bin/bash

set -e

# SHELL_FOLDER=$(cd $(dirname $0);pwd)
current_dir=$(pwd)
key_path=""
gmkey_path=""
output_dir="newNode"
logfile="build.log"
conf_path="conf"
gm_conf_path="gmconf/"
SWSSL_CMD="${HOME}"/.fisco/swssl/bin/swssl
hsm_config_array=
guomi_mode=
sdk_cert=

LOG_WARN()
{
    local content=${1}
    echo -e "\033[31m[WARN] ${content}\033[0m"
}

LOG_INFO()
{
    local content=${1}
    echo -e "\033[32m[INFO] ${content}\033[0m"
}

help() {
    cat << EOF
Usage:
    -c <cert path>              [Required] cert key path
    -g <gm cert path>           gmcert key path, if generate gm node cert
    -s                          If set -s, generate certificate for sdk
    -o <Output Dir>             Default ${output_dir}
    -H <HSM swssl config>       Ip,port,password to connect hardware secure module
    -h Help
e.g
//    $0 -c nodes/cert/agency -o newNode                                #generate node certificate
//    $0 -c nodes/cert/agency -o newSDK -s                              #generate sdk certificate
    $0 -c nodes/cert/agency -g nodes/gmcert/agency -o newNode_GM      #generate gm node certificate
    $0 -c nodes/cert/agency -g nodes/gmcert/agency -o newSDK_GM -s    #generate gm sdk certificate
EOF

exit 0
}

check_and_install_swssl(){
if [ ! -f "${SWSSL_CMD}" ];then
    local swssl_link_perfix="https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/FISCO-BCOS/deps/swssl"
    if [[ "$(uname -p)" == "aarch64" ]];then
        curl -#LO "${swssl_link_perfix}/swssl-20011-linaro-6.5.0-aarch64-linux-gnu.tgz"
        LOG_INFO "Downloading tassl binary from ${swssl_link_perfix}/swssl-20011-linaro-6.5.0-aarch64-linux-gnu.tgz"
        mv swssl-20011-linaro-6.5.0-aarch64-linux-gnu.tgz swssl.tgz
    elif [[ "$(uname -p)" == "x86_64" ]];then
        curl -#LO "${swssl_link_perfix}/swssl-20011-3.10.0-1062.el7.x86_64.tgz"
        LOG_INFO "Downloading tassl binary from ${swssl_link_perfix}/swssl-20011-3.10.0-1062.el7.x86_64.tgz"
        mv swssl-20011-3.10.0-1062.el7.x86_64.tgz swssl.tgz
    else
        LOG_WARN "Unsupported platform"
        exit 1
    fi
    tar zxvf swssl.tgz && rm swssl.tgz
    chmod u+x swssl
    mkdir -p "${HOME}"/.fisco
    mv swssl "${HOME}"/.fisco/
    export OPENSSL_CONF="${HOME}"/.fisco/swssl/ssl/swssl.cnf
    export LD_LIBRARY_PATH="${HOME}"/.fisco/swssl/lib
    echo $LD_LIBRARY_PATH
fi
if [[ -n ${hsm_config_array} ]];then
    generate_swssl_ini "${HOME}/.fisco/swssl/bin/swsds.ini"
    generate_swssl_ini "swsds.ini"
fi
}

parse_params()
{
while getopts "c:o:g:H:hs" option;do
    case $option in
    c) [ ! -z $OPTARG ] && key_path=$OPTARG
    ;;
    o) [ ! -z $OPTARG ] && output_dir=$OPTARG
    ;;
    g) guomi_mode="yes" && gmkey_path=$OPTARG
        check_and_install_tassl
    ;;
    H) hsm_config_array=(${OPTARG//,/ });;
    s) sdk_cert="true";;
    h) help;;
    esac
done
}

print_result()
{
echo "=============================================================="
LOG_INFO "SWSSL Path   : ${HOME}/.fisco/swssl"
if [[ -n ${hsm_config_array} ]];then
    LOG_INFO "SWSSL Config Dir  : ${PWD}/swsds.ini"
fi
echo "=============================================================="
if [[ -n ${hsm_config_array} ]];then
    LOG_INFO "Please move ${PWD}/swsds.ini to /etc/swsds.ini to make the config available."
    LOG_INFO "Example command: sudo mv ${PWD}/swsds.ini /etc/swsds.ini"
fi
LOG_INFO "All completed. "
}


file_must_exists() {
    if [ ! -f "$1" ]; then
        exit_with_clean "$1 file does not exist, please check!"
    fi
}

dir_must_exists() {
    if [ ! -d "$1" ]; then
        exit_with_clean "$1 DIR does not exist, please check!"
    fi
}

dir_must_not_exists() {
    if [ -e "$1" ]; then
        LOG_WARN "$1 DIR exists, please clean old DIR!"
        exit 1
    fi
}
generate_swssl_ini()
{
    local output=${1}
    local ip=${hsm_config_array[0]}
    local port=${hsm_config_array[1]}
    local passwd=${hsm_config_array[2]}
    cat << EOF > "${output}"
[ErrorLog]
level=3
logfile=swsds.log
maxsize=10
[HSM1]
ip=${ip} 
port=${port} 
passwd=${passwd}
[Timeout]
connect=30
service=60
[ConnectionPool]
poolsize=2
EOF
    printf "  [%d] p2p:%-5d  channel:%-5d  jsonrpc:%-5d\n" "${node_index}" $(( offset + port_array[0] )) $(( offset + port_array[1] )) $(( offset + port_array[2] )) >>"${logfile}"
}

parse_params $@
check_and_install_swssl
print_result
