#!/bin/bash
# This script depends nc awk tr head tail

set -e

node_path=./
cut_range="3-130"

LOG_WARN() {
    local content=${1}
    echo -e "\033[31m[WARN] ${content}\033[0m"
}

LOG_INFO() {
    local content=${1}
    echo -e "\033[32m[INFO] ${content}\033[0m"
}

help() {
    cat <<EOF
Usage:
    -p <node path>              Node's path default is ./
    -h Help
e.g
    $0 -p node0
    $0 -p node_127.0.0.1_30300
EOF

    exit 0
}

check_env() {
    # check if nc is installed
    if [[ ! -f /bin/nc && ! -f /usr/bin/nc ]]; then
        LOG_WARN "Please install nc, this script use nc to check network accessibility."
        exit 1
    fi
    # check openssl version
    echo "openssl version : $(openssl version)"
    [ ! -z "$(openssl version | grep 1.0.2)" ] || [ ! -z "$(openssl version | grep 1.1)" ] || [ ! -z "$(openssl version | grep 3.)" ] || [ ! -z "$(openssl version | grep reSSL)" ] || {
        LOG_WARN "OpenSSL is too old, please install openssl 1.0.2 or higher!"
    }
    if [ ! -z "$(openssl version | grep reSSL)" ];then
        export PATH="/usr/local/opt/openssl/bin:$PATH"
    fi
    if [ ! -z "$(openssl version | grep 1.1.1)" ];then
        cut_range="6-133"
    fi
    if openssl version | grep -q 1.0.2t ;then
        cut_range="6-133"
    fi
    # TODO: check hardware requirement
}

print_config_info() {
    local config_file=$1
    # TODO: parse config.ini
    # cat ${config_file}
}

check_cert() {
    local path=$1
    # use ca_cert verify node_cert
    ca_name=($(awk -F "=" '/ca_cert/ {print $2}' ${path}/config.ini | tr -d ' '))
    node_name=($(awk -F "=" '/^[[:space:]]*cert/ {print $2}' ${path}/config.ini | tr -d ' '))
    data_path=($(awk -F "=" '/^[[:space:]]*data_path/ {print $2}' ${path}/config.ini | tr -d ' '))
    if [ "${data_path:0:1}" != "/" ]; then
        data_path=${path}/${data_path}
    fi
    ca_cert=${data_path}/${ca_name}
    node_cert=${data_path}/${node_name}
    agency_start=$(grep -n "BEGIN CERTIFICATE" ${node_cert} | cut -d ":" -f 1 | sed -n "2,1p")
    if [ -z "${agency_start}" ];then
        LOG_WARN "${node_name} type check failed, this certificate doesn't contain agency."
        return 1
    fi
    agency_end=$(grep -n "END CERTIFICATE" ${node_cert} | cut -d ":" -f 1 | sed -n "2,1p")
    if [ -z "${agency_end}" ];then
        LOG_WARN "${node_name} type check failed, this certificate doesn't contain agency."
        return 1
    fi
    sed -n "${agency_start},${agency_end}p" ${node_cert} > ./.agency.crt
    sed -n "1,${agency_start}p" ${node_cert} > ./.node.crt
    if ! openssl verify -CAfile ${ca_cert} -untrusted ./.agency.crt ./.node.crt &>/dev/null; then
        LOG_WARN "use ${ca_cert} verify ${node_cert} failed"
        return 1
    fi
    rm ./.agency.crt
    rm ./.node.crt
    LOG_INFO "use ${ca_cert} verify ${node_cert} successful"

    # check if the node.nodeid match with node.crt
    nodeid_from_cert=$(openssl x509  -text -in ${node_cert} | sed -n "15,20p" |  sed "s/://g" | tr "\n" " " | sed "s/ //g" | cut -c ${cut_range})
    nodeid_from_file=$(cat ${data_path}/node.nodeid)
    if [[ "${nodeid_from_cert}" != "${nodeid_from_file}" ]]; then
        LOG_WARN "nodeid match failed!"
        LOG_WARN "nodeid from ${node_cert} is ${nodeid_from_cert}"
        LOG_WARN "nodeid from ${data_path}/node.nodeid is ${nodeid_from_file}"
        return 1
    fi
    LOG_INFO "The contents of ${data_path}/node.nodeid and ${node_cert} are same."
}

check_node_reachable() {
    local config_file=$1
    ip_ports=($(awk -F "=" '/node\.[0-9]+/ {print $2}' ${config_file} | tr -d ' '))
    for ip_port in ${ip_ports[*]}; do
        local host=${ip_port%:*}
        local port=${ip_port#*:}
        if nc -z -w 5 ${host} ${port} &>/dev/null; then
            LOG_INFO "${host}:${port} is ready to connect."
        else
            LOG_WARN "${host}:${port} is unreachable."
        fi
    done
}

check_local_ip_port() {
    local host=$1
    local port=$2
    if nc -z -w 5 ${host} ${port} &>/dev/null; then
        LOG_WARN "${host}:${port} is used."
    else
        LOG_INFO "${host}:${port} is valid."
    fi
}

check_port_available() {
    # RPC config must in front of p2p config items
    local config_file=$1
    rpc_ip=($(awk -F "=" '/listen_ip/ {print $2}' ${config_file} | head -n 1 | tr -d ' '))
    channel_listen_port=($(awk -F "=" '/channel_listen_port/ {print $2}' ${config_file} | tr -d ' '))
    jsonrpc_listen_port=($(awk -F "=" '/jsonrpc_listen_port/ {print $2}' ${config_file} | tr -d ' '))
    p2p_ip=($(awk -F "=" '/listen_ip/ {print $2}' ${config_file} | tail -n 1 | tr -d ' '))
    p2p_listen_port=($(awk -F "=" '/listen_port/ {print $2}' ${config_file} | tail -n 1 | tr -d ' '))

    check_local_ip_port ${rpc_ip} ${channel_listen_port}
    check_local_ip_port ${rpc_ip} ${jsonrpc_listen_port}
    check_local_ip_port ${p2p_ip} ${p2p_listen_port}
}

check_ip_available() {
    local config_file=$1
    local ip_addrs=($(awk -F "=" '/listen_ip/ {print $2}' ${config_file} | tr -d ' '))
    local local_addr=($(ifconfig | grep inet | awk -F " " '{print $2}' | grep -v inet6 | tr -d ' ') "0.0.0.0")
    for ip in ${ip_addrs[*]}; do
        if echo "${local_addr[*]}" | grep "${ip}" &>/dev/null; then
            LOG_INFO "${ip} is valid listen IP."
        else
            LOG_WARN "${ip} is not found in local network cards."
        fi
    done
}

check_process() {
    local node_name=$(basename $1)
    local node_pid=$(ps aux | grep ${node_name} | grep "fisco-bcos" | grep -v grep | awk '{print $2}')
    if [ ! -z "${node_pid}" ]; then
        LOG_INFO "${node_name} is running, pid is ${node_pid}"
    else
        LOG_INFO "${node_name} is stopped"
    fi
}

check_node() {
    local path=$1
    local config_file=${path}/config.ini
    check_ip_available ${config_file}
    check_port_available ${config_file}
    check_node_reachable ${config_file}
    check_cert ${path}
    check_process ${path}
    print_config_info ${config_file}
}

main() {
    while getopts "p:h" option; do
        case ${option} in
        p) node_path=${OPTARG} ;;
        h) help ;;
        *) help ;;
        esac
    done
    check_env
    check_node ${node_path}
}

main "$@"
