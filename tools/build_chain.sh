#!/bin/bash

set -e

# default value
ca_path= #CA key
gmca_path= #gm CA key
node_num=1
ip_file=
ip_param=
use_ip_param=
agency_array=
group_array=
ports_array=
ip_array=
output_dir=nodes
port_start=(30300 20200 8545)
state_type=storage
storage_type=rocksdb
supported_storage=(rocksdb mysql external scalable)
conf_path="conf"
bin_path=
make_tar=
binary_log="false"
sm_crypto_channel="false"
log_level="info"
logfile=${PWD}/build.log
listen_ip="127.0.0.1"
default_listen_ip="0.0.0.0"
bcos_bin_name=fisco-bcos
guomi_mode=
docker_mode=
gm_conf_path="gmconf/"
current_dir=$(pwd)
consensus_type="pbft"
supported_consensus=(pbft raft rpbft)
TASSL_CMD="${HOME}"/.fisco/tassl
auto_flush="true"
enable_statistic="false"
enable_free_storage="false"
deploy_mode=
root_crt=
gmroot_crt=
copy_cert=
no_agency=
days=36500 # 100 years
# trans timestamp from seconds to milliseconds
timestamp=$(($(date '+%s')*1000))
chain_id=1
compatibility_version=""
default_version="2.7.1"
macOS=""
x86_64_arch="true"
download_timeout=240
cdn_link_header="https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS"
use_ipv6=
help() {
    cat << EOF
Usage:
    -l <IP list>                        [Required] "ip1:nodeNum1,ip2:nodeNum2" e.g:"192.168.0.1:2,192.168.0.2:3"
    -f <IP list file>                   [Optional] split by line, every line should be "ip:nodeNum agencyName groupList p2p_port,channel_port,jsonrpc_port". eg "127.0.0.1:4 agency1 1,2 30300,20200,8545"
    -v <FISCO-BCOS binary version>      Default is the latest v${default_version}
    -e <FISCO-BCOS binary path>         Default download fisco-bcos from GitHub. If set -e, use the binary at the specified location
    -o <Output Dir>                     Default ./nodes/
    -p <Start Port>                     Default 30300,20200,8545 means p2p_port start from 30300, channel_port from 20200, jsonrpc_port from 8545
    -q <List FISCO-BCOS releases>       List FISCO-BCOS released versions
    -i <Host ip>                        Default 127.0.0.1. If set -i, listen 0.0.0.0
    -s <DB type>                        Default rocksdb. Options can be rocksdb / mysql / scalable, rocksdb is recommended
    -d <docker mode>                    Default off. If set -d, build with docker
    -c <Consensus Algorithm>            Default PBFT. Options can be pbft / raft /rpbft, pbft is recommended
    -C <Chain id>                       Default 1. Can set uint.
    -g <Generate guomi nodes>           Default no
    -z <Generate tar packet>            Default no
    -t <Cert config file>               Default auto generate
    -6 <Use ipv6>                       Default no. If set -6, treat IP as IPv6
    -k <The path of ca root>            Default auto generate, the ca.crt and ca.key must in the path, if use intermediate the root.crt must in the path
    -K <The path of sm crypto ca root>  Default auto generate, the gmca.crt and gmca.key must in the path, if use intermediate the gmroot.crt must in the path
    -D <Use Deployment mode>            Default false, If set -D, use deploy mode directory struct and make tar
    -G <channel use sm crypto ssl>      Default false, only works for guomi mode
    -X <Certificate expiration time>    Default 36500 days
    -T <Enable debug log>               Default off. If set -T, enable debug log
    -S <Enable statistics>              Default off. If set -S, enable statistics
    -F <Disable log auto flush>         Default on. If set -F, disable log auto flush
    -E <Enable free_storage_evm>        Default off. If set -E, enable free_storage_evm
    -h Help
e.g
    $0 -l "127.0.0.1:4"
EOF

exit 0
}

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

get_value()
{
    local var_name="${1}"
    var_name="${var_name//./}"
    var_name="var_${var_name//-/}"
    local res=$(eval echo '$'"${var_name}")
    echo ${res}
}

set_value()
{
    local var_name="${1}"
    var_name="${var_name//./}"
    var_name="var_${var_name//-/}"
    local var_value=${2}
    eval "${var_name}=${var_value}"
}

exit_with_clean()
{
    local content=${1}
    echo -e "\033[31m[ERROR] ${content}\033[0m"
    if [ -d "${output_dir}" ];then
        rm -rf ${output_dir}
    fi
    exit 1
}

parse_params()
{
while getopts "f:l:o:p:e:t:v:s:C:c:k:K:X:izhgGTNFSdEDZ6q" option;do
    case $option in
    f) ip_file=$OPTARG
       use_ip_param="false"
    ;;
    l) ip_param=$OPTARG
       use_ip_param="true"
    ;;
    o) output_dir=$OPTARG;;
    i) listen_ip="0.0.0.0" && LOG_WARN "jsonrpc_listen_ip linstens 0.0.0.0 is unsafe.";;
    q) LOG_INFO "Show the history releases of FISCO-BCOS " && curl -sS https://gitee.com/api/v5/repos/FISCO-BCOS/FISCO-BCOS/tags | grep -oe "\"name\":\"v[2-9]*\.[0-9]*\.[0-9]*\"" | cut -d \" -f 4 | sort -V && exit 0;;
    v) compatibility_version="${OPTARG//[vV]/}";;
    p) port_start=(${OPTARG//,/ })
    if [ ${#port_start[@]} -ne 3 ];then LOG_WARN "start port error. e.g: 30300,20200,8545" && exit 1;fi
    ;;
    e) bin_path=$OPTARG;;
    k) ca_path="$OPTARG" && ca_key="$OPTARG/ca.key" && ca_crt="$OPTARG/ca.crt"
        if [[ ! -f "${ca_key}" ]];then
            LOG_WARN "${ca_key} not exist."
            exit 1;
        fi
        if [[ ! -f "${ca_crt}" ]];then
            LOG_WARN "${ca_crt} not exist."
            exit 1;
        fi
    ;;
    K) gmca_path="$OPTARG" && gmca_key="$OPTARG/gmca.key" && gmca_crt="$OPTARG/gmca.crt"
        if [[ ! -f "${gmca_key}" ]];then
            LOG_WARN "${gmca_key} not exist."
            exit 1;
        fi
        if [[ ! -f "${gmca_crt}" ]];then
            LOG_WARN "${gmca_crt} not exist."
            exit 1;
        fi
    ;;
    s) storage_type=$OPTARG
        if ! echo "${supported_storage[*]}" | grep -i "${storage_type}" &>/dev/null; then
            LOG_WARN "${storage_type} is not supported. Please set one of ${supported_storage[*]}"
            exit 1;
        fi
    ;;
    t) CertConfig=$OPTARG;;
    c) consensus_type=$OPTARG
        if ! echo "${supported_consensus[*]}" | grep -i "${consensus_type}" &>/dev/null; then
            LOG_WARN "${consensus_type} is not supported. Please set one of ${supported_consensus[*]}"
            exit 1;
        fi
    ;;
    C) chain_id=$OPTARG
        if [ -z $(grep '^[[:digit:]]*$' <<< "${chain_id}") ];then
            LOG_WARN "${chain_id} is not a positive integer."
            exit 1;
        fi
    ;;
    X) days="$OPTARG";;
    G) sm_crypto_channel="true";;
    T) log_level="debug";;
    F) auto_flush="false";;
    N) no_agency="true";;
    S) enable_statistic="true";;
    E) enable_free_storage="true";;
    D) deploy_mode="true" && make_tar="true";;
    Z) copy_cert="true";;
    z) make_tar="true";;
    g) guomi_mode="true";;
    6) use_ipv6="true" && default_listen_ip="::";;
    d) docker_mode="true"
        if [ "$(uname)" == "Darwin" ];then LOG_WARN "Docker desktop of macOS can't support docker mode of FISCO BCOS!" && exit 1;fi
    ;;
    h) help;;
    esac
done
if [ "${storage_type}" == "scalable" ]; then
    echo "use scalable storage, so turn on binary log"
    binary_log="true"
fi
# sm_crypto_channel only works in guomi mode
if [[ -n "${guomi_mode}" && "${sm_crypto_channel}" == "true" ]];then
    sm_crypto_channel="true"
else
    sm_crypto_channel="false"
fi
}

print_result()
{
echo "=============================================================="
[ -z "${docker_mode}" ] && [ -f "${bin_path}" ] && LOG_INFO "FISCO-BCOS Path : $bin_path"
[ -n "${docker_mode}" ] && LOG_INFO "Docker tag      : latest"
[ -n "${ip_file}" ] && LOG_INFO "IP List File    : $ip_file"
LOG_INFO "Start Port      : ${port_start[*]}"
LOG_INFO "Server IP       : ${ip_array[*]}"
LOG_INFO "Output Dir      : ${output_dir}"
LOG_INFO "CA Path         : $ca_path"
[ -n "${guomi_mode}" ] && LOG_INFO "Guomi CA Path   : $gmca_path"
[ -n "${guomi_mode}" ] && LOG_INFO "Guomi mode      : $guomi_mode"
echo "=============================================================="
LOG_INFO "Execute the download_console.sh script in directory named by IP to get FISCO-BCOS console."
echo "e.g.  bash ${output_dir}/${ip_array[0]%:*}/download_console.sh -f"
echo "=============================================================="
LOG_INFO "All completed. Files in ${output_dir}"
}

check_env() {
    if [ "$(uname)" == "Darwin" ];then
        export PATH="/usr/local/opt/openssl/bin:$PATH"
        macOS="macOS"
    fi
    [ ! -z "$(openssl version | grep 1.0.2)" ] || [ ! -z "$(openssl version | grep 1.1)" ] || {
        echo "please install openssl!"
        #echo "download openssl from https://www.openssl.org."
        echo "use \"openssl version\" command to check."
        exit 1
    }

    if [ "$(uname -m)" != "x86_64" ];then
        x86_64_arch="false"
    fi
}

check_and_install_tassl(){
if [ -n "${guomi_mode}" ]; then
    if [ ! -f "${TASSL_CMD}" ];then
        local tassl_link_perfix="${cdn_link_header}/FISCO-BCOS/tools/tassl-1.0.2"
        LOG_INFO "Downloading tassl binary from ${tassl_link_perfix}..."
        if [[ -n "${macOS}" ]];then
            curl -#LO "${tassl_link_perfix}/tassl_mac.tar.gz"
            mv tassl_mac.tar.gz tassl.tar.gz
        else
            if [[ "$(uname -p)" == "aarch64" ]];then
                curl -#LO "${tassl_link_perfix}/tassl-aarch64.tar.gz"
                mv tassl-aarch64.tar.gz tassl.tar.gz
            elif [[ "$(uname -p)" == "x86_64" ]];then
                curl -#LO "${tassl_link_perfix}/tassl.tar.gz"
            else
                LOG_ERROR "Unsupported platform"
                exit 1
            fi
        fi
        tar zxvf tassl.tar.gz && rm tassl.tar.gz
        chmod u+x tassl
        mkdir -p "${HOME}"/.fisco
        mv tassl "${HOME}"/.fisco/tassl
    fi
fi
}

check_name() {
    local name="$1"
    local value="$2"
    [[ "$value" =~ ^[a-zA-Z0-9._-]+$ ]] || {
        exit_with_clean "$name name [$value] invalid, it should match regex: ^[a-zA-Z0-9._-]+\$"
    }
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

gen_chain_cert() {
    local path="${1}"
    name=$(basename "$path")
    echo "$path --- $name"
    dir_must_not_exists "$path"
    check_name chain "$name"

    chaindir=$path
    mkdir -p $chaindir
    # openssl genrsa -out "$chaindir/ca.key" 2048
    openssl ecparam -out "$chaindir/secp256k1.param" -name secp256k1 2> /dev/null
    openssl genpkey -paramfile "$chaindir/secp256k1.param" -out "$chaindir/ca.key" 2> /dev/null
    openssl req -new -x509 -days "${days}" -subj "/CN=$name/O=fisco-bcos/OU=chain" -key "$chaindir/ca.key" -out "$chaindir/ca.crt"
    rm -f "$chaindir/secp256k1.param"
}

gen_agency_cert() {
    local chain="${1}"
    local agencypath="${2}"
    name=$(basename "$agencypath")

    dir_must_exists "$chain"
    file_must_exists "$chain/ca.key"
    check_name agency "$name"
    agencydir=$agencypath
    dir_must_not_exists "$agencydir"
    mkdir -p $agencydir

    # openssl genrsa -out "$agencydir/agency.key" 2048 2> /dev/null
    openssl ecparam -out "$agencydir/secp256k1.param" -name secp256k1 2> /dev/null
    openssl genpkey -paramfile "$agencydir/secp256k1.param" -out "$agencydir/agency.key" 2> /dev/null
    openssl req -new -sha256 -subj "/CN=$name/O=fisco-bcos/OU=agency" -key "$agencydir/agency.key" -out "$agencydir/agency.csr" 2> /dev/null
    openssl x509 -req -days 3650 -sha256 -CA "$chain/ca.crt" -CAkey "$chain/ca.key" -CAcreateserial\
        -in "$agencydir/agency.csr" -out "$agencydir/agency.crt"  -extensions v4_req -extfile "$chain/cert.cnf" 2> /dev/null
    # cat "$chain/ca.crt" >> "$agencydir/agency.crt"
    cp $chain/ca.crt $chain/cert.cnf $agencydir/
    rm -f "$agencydir/agency.csr" "$agencydir/secp256k1.param"

    echo "build $name cert successful!"
}

gen_cert_secp256k1() {
    agpath="$1"
    certpath="$2"
    name="$3"
    type="$4"
    openssl ecparam -out "$certpath/secp256k1.param" -name secp256k1 2> /dev/null
    openssl genpkey -paramfile "$certpath/secp256k1.param" -out "$certpath/${type}.key" 2> /dev/null
    openssl pkey -in "$certpath/${type}.key" -pubout -out "$certpath/${type}.pubkey" 2> /dev/null
    openssl req -new -sha256 -subj "/CN=${name}/O=fisco-bcos/OU=${type}" -key "$certpath/${type}.key" -out "$certpath/${type}.csr" 2> /dev/null
    if [ -n "${no_agency}" ];then
        echo "not use $(basename $agpath) to sign $(basename $certpath) ${type}" >>"${logfile}"
        openssl x509 -req -days "${days}" -sha256 -in "$certpath/${type}.csr" -CAkey "$agpath/../ca.key" -CA "$agpath/../ca.crt" \
            -force_pubkey "$certpath/${type}.pubkey" -out "$certpath/${type}.crt" -CAcreateserial -extensions v3_req -extfile "$agpath/cert.cnf" 2> /dev/null
    else
        openssl x509 -req -days "${days}" -sha256 -in "$certpath/${type}.csr" -CAkey "$agpath/agency.key" -CA "$agpath/agency.crt" \
            -force_pubkey "$certpath/${type}.pubkey" -out "$certpath/${type}.crt" -CAcreateserial -extensions v3_req -extfile "$agpath/cert.cnf" 2> /dev/null
        # openssl ec -in $certpath/${type}.key -outform DER | tail -c +8 | head -c 32 | xxd -p -c 32 | cat >$certpath/${type}.private
        cat "${agpath}/agency.crt" >> "$certpath/${type}.crt"
    fi
    cat "${agpath}/../ca.crt" >> "$certpath/${type}.crt"
    if [[ -n "${root_crt}" ]];then
        echo "Use user specified root cert as ca.crt, $certpath" >> "${logfile}"
        cp "${root_crt}" "$certpath/ca.crt"
    else
        cp "${agpath}/../ca.crt" "$certpath/"
    fi

    rm -f "$certpath/${type}.csr" "$certpath/${type}.pubkey" "$certpath/secp256k1.param"
}

gen_cert() {
    if [ "" == "$(openssl ecparam -list_curves 2>&1 | grep secp256k1)" ]; then
        exit_with_clean "openssl don't support secp256k1, please upgrade openssl!"
    fi

    agpath="${1}"
    agency=$(basename "$agpath")
    ndpath="${2}"
    local cert_name="${3}"
    node=$(basename "$ndpath")
    dir_must_exists "$agpath"
    file_must_exists "$agpath/agency.key"
    check_name agency "$agency"
    dir_must_not_exists "$ndpath"
    check_name node "$node"

    mkdir -p $ndpath
    gen_cert_secp256k1 "$agpath" "$ndpath" "$node" "${cert_name}"
    #nodeid is pubkey
    openssl ec -text -noout -in "$ndpath/${cert_name}.key" 2> /dev/null | sed -n '7,11p' | tr -d ": \n" | awk '{print substr($0,3);}' | cat >"$ndpath"/node.nodeid
    # openssl x509 -serial -noout -in $ndpath/node.crt | awk -F= '{print $2}' | cat >$ndpath/node.serial
    cd "$ndpath"
    if [ "${cert_name}" == "node" ];then
        mkdir -p "${conf_path}/"
        mv ./*.* "${conf_path}/"
        cd "${output_dir}"
    fi
}

generate_gmsm2_param()
{
    local output=$1
    cat << EOF > "${output}"
-----BEGIN EC PARAMETERS-----
BggqgRzPVQGCLQ==
-----END EC PARAMETERS-----

EOF
}

gen_chain_cert_gm() {
    local path="${1}"
    name=$(basename "$path")
    echo "$path --- $name"
    dir_must_not_exists "$path"
    check_name chain "$name"

    chaindir=$path
    mkdir -p $chaindir

	$TASSL_CMD genpkey -paramfile gmsm2.param -out "$chaindir/gmca.key" 2> /dev/null
	$TASSL_CMD req -config gmcert.cnf -x509 -days "${days}" -subj "/CN=${name}/O=fisco-bcos/OU=chain" -key "$chaindir/gmca.key" -extensions v3_ca -out "$chaindir/gmca.crt" 2> /dev/null
}

gen_agency_cert_gm() {
    local chain="${1}"
    local agencypath="${2}"
    name=$(basename "$agencypath")

    dir_must_exists "$chain"
    file_must_exists "$chain/gmca.key"
    check_name agency "$name"
    agencydir=$agencypath
    dir_must_not_exists "$agencydir"
    mkdir -p $agencydir

    $TASSL_CMD genpkey -paramfile "$chain/gmsm2.param" -out "$agencydir/gmagency.key" 2> /dev/null
    $TASSL_CMD req -new -subj "/CN=${name}_son/O=fisco-bcos/OU=agency" -key "$agencydir/gmagency.key" -config "$chain/gmcert.cnf" -out "$agencydir/gmagency.csr" 2> /dev/null
    $TASSL_CMD x509 -sm3 -req -CA "$chain/gmca.crt" -CAkey "$chain/gmca.key" -days 3650 -CAcreateserial -in "$agencydir/gmagency.csr" -out "$agencydir/gmagency.crt" -extfile "$chain/gmcert.cnf" -extensions v3_agency_root 2> /dev/null
    # cat "$chain/gmca.crt" >> "$agencydir/gmagency.crt"
    cp "$chain/gmca.crt" "$chain/gmcert.cnf" "$chain/gmsm2.param" "$agencydir/"
    rm -f "$agencydir/gmagency.csr"
}

gen_node_cert_with_extensions_gm() {
    capath="$1"
    certpath="$2"
    name="$3"
    type="$4"
    extensions="$5"

    $TASSL_CMD genpkey -paramfile "$capath/gmsm2.param" -out "$certpath/gm${type}.key" 2> /dev/null
    $TASSL_CMD req -new -subj "/CN=$name/O=fisco-bcos/OU=${type}" -key "$certpath/gm${type}.key" -config "$capath/gmcert.cnf" -out "$certpath/gm${type}.csr" 2> /dev/null
    if [ -n "${no_agency}" ];then
        echo "not use $(basename $capath) to sign $(basename $certpath) ${type}" >>"${logfile}"
        $TASSL_CMD x509 -sm3 -req -CA "$capath/../gmca.crt" -CAkey "$capath/../gmca.key" -days "${days}" -CAcreateserial -in "$certpath/gm${type}.csr" -out "$certpath/gm${type}.crt" -extfile "$capath/gmcert.cnf" -extensions "$extensions" 2> /dev/null
    else
        $TASSL_CMD x509 -sm3 -req -CA "$capath/gmagency.crt" -CAkey "$capath/gmagency.key" -days "${days}" -CAcreateserial -in "$certpath/gm${type}.csr" -out "$certpath/gm${type}.crt" -extfile "$capath/gmcert.cnf" -extensions "$extensions" 2> /dev/null
    fi
    rm -f $certpath/gm${type}.csr
}

gen_node_cert_gm() {

    agpath="${1}"
    agency=$(basename "$agpath")
    ndpath="${2}"
    node=$(basename "$ndpath")
    dir_must_exists "$agpath"
    file_must_exists "$agpath/gmagency.key"
    check_name agency "$agency"

    mkdir -p $ndpath
    dir_must_exists "$ndpath"
    check_name node "$node"

    mkdir -p $ndpath
    gen_node_cert_with_extensions_gm "$agpath" "$ndpath" "$node" node v3_req
    if [ -z "${no_agency}" ];then cat "${agpath}/gmagency.crt" >> "$ndpath/gmnode.crt";fi
    cat "${agpath}/../gmca.crt" >> "$ndpath/gmnode.crt"
    gen_node_cert_with_extensions_gm "$agpath" "$ndpath" "$node" ennode v3enc_req
    #nodeid is pubkey
    $TASSL_CMD ec -in "$ndpath/gmnode.key" -text -noout 2> /dev/null | sed -n '7,11p' | sed 's/://g' | tr "\n" " " | sed 's/ //g' | awk '{print substr($0,3);}'  | cat > "$ndpath/gmnode.nodeid"

    #serial
    if [ "" != "$($TASSL_CMD version 2> /dev/null | grep 1.0.2)" ];
    then
        $TASSL_CMD x509 -text -in "$ndpath/gmnode.crt" 2> /dev/null | sed -n '5p' |  sed 's/://g' | tr "\n" " " | sed 's/ //g' | sed 's/[a-z]/\u&/g' | cat > "$ndpath/gmnode.serial"
    else
        $TASSL_CMD x509 -text -in "$ndpath/gmnode.crt" 2> /dev/null | sed -n '4p' |  sed 's/ //g' | sed 's/.*(0x//g' | sed 's/)//g' |sed 's/[a-z]/\u&/g' | cat > "$ndpath/gmnode.serial"
    fi
    if [[ -n "${gmroot_crt}" ]];then
        echo "Use user specified gmroot cert as gmca.crt, $ndpath" >>"${logfile}"
        cp "${gmroot_crt}" "$certpath/gmca.crt"
    else
        cp "$agpath/../gmca.crt" "$ndpath"
    fi
    cd "$ndpath"
    mkdir -p "${gm_conf_path}/"
    mv ./*.* "${gm_conf_path}/"
    cd "${output_dir}"
}

generate_config_ini()
{
    local output=${1}
    local ip=${2}
    local offset=0
    offset=$(get_value "${ip//[\.:]/_}_port_offset")
    local node_groups=(${3//,/ })
    local port_array=
    read -r -a port_array <<< "${port_start[*]}"
    local node_index="${5}"
    if [[ -n "${4}" ]];then
        read -r -a port_array <<< "${4//,/ }"
        [ "${node_index}" == "0" ] && { offset=0 && set_value "${ip//[\.:]/_}_port_offset" 0; }
    fi

    sm_crypto="false"
    local prefix=""
    if [ -n "${guomi_mode}" ]; then
        sm_crypto="true"
        prefix="gm"
    fi

    cat << EOF > "${output}"
[rpc]
    channel_listen_ip=${default_listen_ip}
    channel_listen_port=$(( offset + port_array[1] ))
    jsonrpc_listen_ip=${listen_ip}
    jsonrpc_listen_port=$(( offset + port_array[2] ))
[p2p]
    listen_ip=${default_listen_ip}
    listen_port=$(( offset + port_array[0] ))
    ; nodes to connect
    $ip_list

[certificate_blacklist]
    ; crl.0 should be nodeid, nodeid's length is 128
    ;crl.0=

[certificate_whitelist]
    ; cal.0 should be nodeid, nodeid's length is 128
    ;cal.0=

[group]
    group_data_path=data/
    group_config_path=${conf_path}/

[network_security]
    ; directory the certificates located in
    data_path=${conf_path}/
    ; the node private key file
    key=${prefix}node.key
    ; the node certificate file
    cert=${prefix}node.crt
    ; the ca certificate file
    ca_cert=${prefix}ca.crt

[storage_security]
    enable=false
    key_manager_ip=
    key_manager_port=
    cipher_data_key=

[chain]
    id=${chain_id}
    ; use SM crypto or not, should nerver be changed
    sm_crypto=${sm_crypto}
    sm_crypto_channel=${sm_crypto_channel}

[compatibility]
    ; supported_version should nerver be changed
    supported_version=${compatibility_version}

[log]
    enable=true
    log_path=./log
    ; enable/disable the statistics function
    enable_statistic=${enable_statistic}
    ; network statistics interval, unit is second, default is 60s
    stat_flush_interval=60
    ; info debug trace
    level=${log_level}
    ; MB
    max_log_file_size=200
    flush=${auto_flush}

[flow_control]
    ; restrict QPS of the node
    ;limit_req=1000
    ; restrict the outgoing bandwidth of the node
    ; Mb, can be a decimal
    ; when the outgoing bandwidth exceeds the limit, the block synchronization operation will not proceed
    ;outgoing_bandwidth_limit=2
EOF
    printf "  [%d] p2p:%-5d  channel:%-5d  jsonrpc:%-5d\n" "${node_index}" $(( offset + port_array[0] )) $(( offset + port_array[1] )) $(( offset + port_array[2] )) >>"${logfile}"
}

generate_group_genesis()
{
    local output=$1
    local index=$2
    local node_list=$3
    local sealer_size=$4
    cat << EOF > "${output}"
[consensus]
    ; consensus algorithm now support PBFT(consensus_type=pbft), Raft(consensus_type=raft)
    ; rpbft(consensus_type=rpbft)
    consensus_type=${consensus_type}
    ; the max number of transactions of a block
    max_trans_num=1000
    ; in seconds, block consensus timeout, at least 3s
    consensus_timeout=3
    ; rpbft related configuration
    ; the working sealers num of each consensus epoch
    epoch_sealer_num=${sealer_size}
    ; the number of generated blocks each epoch
    epoch_block_num=1000
    ; the node id of consensusers
    ${node_list}
[state]
    type=${state_type}
[tx]
    ; transaction gas limit
    gas_limit=300000000
[group]
    id=${index}
    timestamp=${timestamp}
[evm]
    enable_free_storage=${enable_free_storage}
EOF
}

function generate_group_ini()
{
    local output="${1}"
    cat << EOF > "${output}"
[consensus]
    ; the ttl for broadcasting pbft message
    ttl=2
    ; min block generation time(ms)
    min_block_generation_time=500
    enable_dynamic_block_size=true
    enable_ttl_optimization=true
    enable_prepare_with_txsHash=true
    ; The following is the relevant configuration of rpbft
    ; set true to enable broadcast prepare request by tree
    broadcast_prepare_by_tree=true
    ; percent of nodes that broadcast prepare status to, must be between 25 and 100
    prepare_status_broadcast_percent=33
    ; max wait time before request missed transactions, ms, must be between 5ms and 1000ms
    max_request_missedTxs_waitTime=100
    ; maximum wait time before requesting a prepare, ms, must be between 10ms and 1000ms
    max_request_prepare_waitTime=100

[storage]
    ; storage db type, rocksdb / mysql / scalable, rocksdb is recommended
    type=${storage_type}
    ; set true to turn on binary log
    binary_log=${binary_log}
    ; scroll_threshold=scroll_threshold_multiple*1000, only for scalable
    scroll_threshold_multiple=2
    ; set fasle to disable CachedStorage
    cached_storage=true
    ; max cache memeory, MB
    max_capacity=32
    max_forward_block=10
    ; only for external, deprecated in v2.3.0
    max_retry=60
    topic=DB
    ; only for mysql
    db_ip=127.0.0.1
    db_port=3306
    db_username=
    db_passwd=
    db_name=
[tx_pool]
    limit=150000
    ; transaction pool memory size limit, MB
    memory_limit=512
    ; number of threads responsible for transaction notification,
    ; default is 2, not recommended for more than 8
    notify_worker_num=2
[sync]
    ; max memory size used for block sync, must >= 32MB
    max_block_sync_memory_size=512
    idle_wait_ms=200
    ; send block status by tree-topology, only supported when use pbft
    sync_block_by_tree=true
    ; send transaction by tree-topology, only supported when use pbft
    ; recommend to use when deploy many consensus nodes
    send_txs_by_tree=true
    ; must between 1000 to 3000
    ; only enabled when sync_by_tree is true
    gossip_interval_ms=1000
    gossip_peers_number=3
    ; max number of nodes that broadcast txs status to, recommended less than 5
    txs_max_gossip_peers_num=5
[flow_control]
    ; restrict QPS of the group
    ;limit_req=1000
    ; restrict the outgoing bandwidth of the group
    ; Mb, can be a decimal
    ; when the outgoing bandwidth exceeds the limit, the block synchronization operation will not proceed
    ;outgoing_bandwidth_limit=2

[sdk_allowlist]
    ; When sdk_allowlist is empty, all SDKs can connect to this node
    ; when sdk_allowlist is not empty, only the SDK in the allowlist can connect to this node
    ; public_key.0 should be nodeid, nodeid's length is 128
    ;public_key.0=
EOF
}

generate_cert_conf()
{
    local output=$1
    cat << EOF > "${output}"
[ca]
default_ca=default_ca
[default_ca]
default_days = 365
default_md = sha256

[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
[req_distinguished_name]
countryName = CN
countryName_default = CN
stateOrProvinceName = State or Province Name (full name)
stateOrProvinceName_default =GuangDong
localityName = Locality Name (eg, city)
localityName_default = ShenZhen
organizationalUnitName = Organizational Unit Name (eg, section)
organizationalUnitName_default = fisco-bcos
commonName =  Organizational  commonName (eg, fisco-bcos)
commonName_default = fisco-bcos
commonName_max = 64

[ v3_req ]
basicConstraints = CA:FALSE
keyUsage = nonRepudiation, digitalSignature, keyEncipherment

[ v4_req ]
basicConstraints = CA:TRUE

EOF
}

generate_script_template()
{
    local filepath=$1
    mkdir -p $(dirname $filepath)
    cat << EOF > "${filepath}"
#!/bin/bash
SHELL_FOLDER=\$(cd \$(dirname \$0);pwd)

LOG_ERROR() {
    content=\${1}
    echo -e "\033[31m[ERROR] \${content}\033[0m"
}

LOG_INFO() {
    content=\${1}
    echo -e "\033[32m[INFO] \${content}\033[0m"
}

EOF
    chmod +x ${filepath}
}

generate_cert_conf_gm()
{
    local output=$1
    cat << EOF > "${output}"
HOME			= .
RANDFILE		= $ENV::HOME/.rnd
oid_section		= new_oids

[ new_oids ]
tsa_policy1 = 1.2.3.4.1
tsa_policy2 = 1.2.3.4.5.6
tsa_policy3 = 1.2.3.4.5.7

####################################################################
[ ca ]
default_ca	= CA_default		# The default ca section

####################################################################
[ CA_default ]

dir		= ./demoCA		# Where everything is kept
certs		= $dir/certs		# Where the issued certs are kept
crl_dir		= $dir/crl		# Where the issued crl are kept
database	= $dir/index.txt	# database index file.
#unique_subject	= no			# Set to 'no' to allow creation of
					# several ctificates with same subject.
new_certs_dir	= $dir/newcerts		# default place for new certs.

certificate	= $dir/cacert.pem 	# The CA certificate
serial		= $dir/serial 		# The current serial number
crlnumber	= $dir/crlnumber	# the current crl number
					# must be commented out to leave a V1 CRL
crl		= $dir/crl.pem 		# The current CRL
private_key	= $dir/private/cakey.pem # The private key
RANDFILE	= $dir/private/.rand	# private random number file

x509_extensions	= usr_cert		# The extensions to add to the cert

name_opt 	= ca_default		# Subject Name options
cert_opt 	= ca_default		# Certificate field options

default_days	= 365			# how long to certify for
default_crl_days= 30			# how long before next CRL
default_md	= default		# use public key default MD
preserve	= no			# keep passed DN ordering

policy		= policy_match

[ policy_match ]
countryName		= match
stateOrProvinceName	= match
organizationName	= match
organizationalUnitName	= optional
commonName		= supplied
emailAddress		= optional

[ policy_anything ]
countryName		= optional
stateOrProvinceName	= optional
localityName		= optional
organizationName	= optional
organizationalUnitName	= optional
commonName		= supplied
emailAddress		= optional

####################################################################
[ req ]
default_bits		= 2048
default_md		= sm3
default_keyfile 	= privkey.pem
distinguished_name	= req_distinguished_name
x509_extensions	= v3_ca	# The extensions to add to the self signed cert

string_mask = utf8only

# req_extensions = v3_req # The extensions to add to a certificate request

[ req_distinguished_name ]
countryName = CN
countryName_default = CN
stateOrProvinceName = State or Province Name (full name)
stateOrProvinceName_default =GuangDong
localityName = Locality Name (eg, city)
localityName_default = ShenZhen
organizationalUnitName = Organizational Unit Name (eg, section)
organizationalUnitName_default = fisco
commonName =  Organizational  commonName (eg, fisco)
commonName_default =  fisco
commonName_max = 64

[ usr_cert ]
basicConstraints=CA:FALSE
nsComment			= "OpenSSL Generated Certificate"

subjectKeyIdentifier=hash
authorityKeyIdentifier=keyid,issuer

[ v3_req ]

# Extensions to add to a certificate request

basicConstraints = CA:FALSE
keyUsage = nonRepudiation, digitalSignature

[ v3enc_req ]

# Extensions to add to a certificate request
basicConstraints = CA:FALSE
keyUsage = keyAgreement, keyEncipherment, dataEncipherment

[ v3_agency_root ]
subjectKeyIdentifier=hash
authorityKeyIdentifier=keyid:always,issuer
basicConstraints = CA:true
keyUsage = cRLSign, keyCertSign

[ v3_ca ]
subjectKeyIdentifier=hash
authorityKeyIdentifier=keyid:always,issuer
basicConstraints = CA:true
keyUsage = cRLSign, keyCertSign

EOF
}

generate_node_scripts()
{
    local output=$1
    local docker_tag="v${compatibility_version}"
    generate_script_template "$output/start.sh"
    local ps_cmd="\$(ps aux|grep \${fisco_bcos}|grep -v grep|awk '{print \$2}')"
    local start_cmd="nohup \${fisco_bcos} -c config.ini >>nohup.out 2>&1 &"
    local stop_cmd="kill \${node_pid}"
    local pid="pid"
    local log_cmd="tail -n20  nohup.out"
    local check_success="\$(${log_cmd} | grep running)"
    local fisco_bin_path="../${bcos_bin_name}"
    if [ -n "${deploy_mode}" ];then fisco_bin_path="${bcos_bin_name}"; fi
    if [ -n "${docker_mode}" ];then
        ps_cmd="\$(docker ps |grep \${SHELL_FOLDER//\//} | grep -v grep|awk '{print \$1}')"
        start_cmd="docker run -d --rm --name \${SHELL_FOLDER//\//} -v \${SHELL_FOLDER}:/data --network=host -w=/data fiscoorg/fiscobcos:${docker_tag} -c config.ini"
        stop_cmd="docker kill \${node_pid} 2>/dev/null"
        pid="container id"
        log_cmd="tail -n20 \$(docker inspect --format='{{.LogPath}}' \${SHELL_FOLDER//\//})"
        check_success="success"
    fi
    cat << EOF >> "$output/start.sh"
fisco_bcos=\${SHELL_FOLDER}/${fisco_bin_path}
cd \${SHELL_FOLDER}
node=\$(basename \${SHELL_FOLDER})
node_pid=${ps_cmd}
if [ -n "\${node_pid}" ];then
    echo " \${node} is running, ${pid} is \$node_pid."
    exit 0
else
    ${start_cmd}
    sleep 1.5
fi
try_times=4
i=0
while [ \$i -lt \${try_times} ]
do
    node_pid=${ps_cmd}
    success_flag=${check_success}
    if [[ -n "\${node_pid}" && -n "\${success_flag}" ]];then
        echo -e "\033[32m \${node} start successfully\033[0m"
        exit 0
    fi
    sleep 0.5
    ((i=i+1))
done
echo -e "\033[31m  Exceed waiting time. Please try again to start \${node} \033[0m"
${log_cmd}
exit 1
EOF
    generate_script_template "$output/stop.sh"
    cat << EOF >> "$output/stop.sh"
fisco_bcos=\${SHELL_FOLDER}/${fisco_bin_path}
node=\$(basename \${SHELL_FOLDER})
node_pid=${ps_cmd}
try_times=20
i=0
if [ -z \${node_pid} ];then
    echo " \${node} isn't running."
    exit 0
fi
[ -n "\${node_pid}" ] && ${stop_cmd} > /dev/null
while [ \$i -lt \${try_times} ]
do
    sleep 0.6
    node_pid=${ps_cmd}
    if [ -z \${node_pid} ];then
        echo -e "\033[32m stop \${node} success.\033[0m"
        exit 0
    fi
    ((i=i+1))
done
echo "  Exceed maximum number of retries. Please try again to stop \${node}"
exit 1
EOF
    generate_script_template "$output/scripts/load_new_groups.sh"
    cat << EOF >> "$output/scripts/load_new_groups.sh"
cd \${SHELL_FOLDER}/../
NODE_FOLDER=\$(pwd)
fisco_bcos=\${NODE_FOLDER}/${fisco_bin_path}
node=\$(basename \${NODE_FOLDER})
node_pid=${ps_cmd}
if [ -n "\${node_pid}" ];then
    echo "\${node} is trying to load new groups. Check log for more information."
    DATA_FOLDER=\${NODE_FOLDER}/data
    for dir in \$(ls \${DATA_FOLDER})
    do
        if [[ -d "\${DATA_FOLDER}/\${dir}" ]] && [[ -n "\$(echo \${dir} | grep -E "^group\d+$")" ]]; then
            STATUS_FILE=\${DATA_FOLDER}/\${dir}/.group_status
            if [ ! -f "\${STATUS_FILE}" ]; then
                echo "STOPPED" > \${STATUS_FILE}
            fi
        fi
    done
    touch config.ini.append_group
    exit 0
else
    echo "\${node} is not running, use start.sh to start all group directlly."
fi
EOF
    generate_script_template "$output/scripts/reload_whitelist.sh"
    cat << EOF >> "$output/scripts/reload_whitelist.sh"
check_cal_line()
{
    line=\$1;
    if [[ \$line =~ cal.[0-9]*=[0-9A-Fa-f]{128,128}\$ ]]; then
        echo "true";
    else
        echo "false";
    fi
}

check_cal_lines()
{
    # print Illegal line
    config_file=\$1
    error="false"
    for line in \$(grep -v "^[ ]*[;]" \$config_file | grep "cal\."); do
        if [[ "true" != \$(check_cal_line \$line) ]]; then
            LOG_ERROR "Illigal whitelist line: \$line"
            error="true"
        fi
    done

    if [[ "true" == \$error ]]; then
        LOG_ERROR "[certificate_whitelist] reload error for illigal lines"
        exit 1
    fi
}

check_duplicate_key()
{
    config_file=\$1;
    dup_key=\$(grep -v '^[ ]*[;]' \$config_file |grep "cal\."|awk -F"=" '{print \$1}'|awk '{print \$1}' |sort |uniq -d)

    if [[ "" != \$dup_key ]]; then
        LOG_ERROR "[certificate_whitelist] has duplicate keys:"
        LOG_ERROR "\$dup_key"
        exit 1
    fi
}

check_whitelist()
{
    config_file=\$1
    check_cal_lines \$config_file
    check_duplicate_key \$config_file
}

check_whitelist \${SHELL_FOLDER}/../config.ini

cd \${SHELL_FOLDER}/../
NODE_FOLDER=\$(pwd)
fisco_bcos=\${NODE_FOLDER}/${fisco_bin_path}
node=\$(basename \${NODE_FOLDER})
node_pid=${ps_cmd}
if [ -n "\${node_pid}" ];then
    echo "\${node} is trying to reset certificate whitelist. Check log for more information."
    touch config.ini.reset_certificate_whitelist
    exit 0
else
    echo "\${node} is not running, use start.sh to start and enable whitelist directlly."
fi
EOF

generate_script_template "$output/scripts/monitor.sh"
    cat << EOF >> "$output/scripts/monitor.sh"

# * * * * * bash monitor.sh -d /data/nodes/127.0.0.1/ > monitor.log 2>&1
cd \$SHELL_FOLDER

alarm() {
        alert_ip=\$(/sbin/ifconfig eth0 | grep inet | awk '{print \$2}')
        time=\$(date "+%Y-%m-%d %H:%M:%S")
        echo "[\${time}] \$1"
}

# restart the node
restart() {
        local nodedir=\$1
        bash \$nodedir/stop.sh
        sleep 5
        bash \$nodedir/start.sh
}

info() {
        time=\$(date "+%Y-%m-%d %H:%M:%S")
        echo "[\$time] \$1"
}

# check if nodeX is work well
function check_node_work_properly() {
        nodedir=\$1
        # node name
        node=\$(basename \$nodedir)
        # fisco-bcos path
        fiscopath=\${nodedir}/
        config=\$1/config.ini
        # rpc url
        config_ip="127.0.0.1"
        config_port=\$(cat \$config | grep 'jsonrpc_listen_port' | egrep -o "[0-9]+")

        # check if process id exist
        pid=\$(ps aux | grep "\$fiscopath" | egrep "fisco-bcos" | grep -v "grep" | awk -F " " '{print \$2}')
        [ -z "\${pid}" ] && {
                alarm " ERROR! \$config_ip:\$config_port \$node does not exist"
                restart \$nodedir
                return 1
        }

        # get group_id list
        groups=\$(ls \${nodedir}/conf/group*genesis | grep -o "group.*.genesis" | grep -o "group.*.genesis" | cut -d \. -f 2)
        for group in \${groups}; do
                # get blocknumber
                heightresult=\$(curl -s "http://\$config_ip:\$config_port" -X POST --data "{\"jsonrpc\":\"2.0\",\"method\":\"getBlockNumber\",\"params\":[\${group}],\"id\":67}")
                echo \$heightresult
                height=\$(echo \$heightresult | awk -F'"' '{if(\$2=="id" && \$4=="jsonrpc" && \$8=="result") {print \$10}}')
                [[ -z "\$height" ]] && {
                        alarm " ERROR! Cannot connect to \$config_ip:\$config_port \$node:\$group "
                        continue
                }

                height_file="\$nodedir/\$node_\$group.height"
                prev_height=0
                [ -f \$height_file ] && prev_height=\$(cat \$height_file)
                heightvalue=\$(printf "%d\n" "\$height")
                prev_heightvalue=\$(printf "%d\n" "\$prev_height")

                viewresult=\$(curl -s "http://\$config_ip:\$config_port" -X POST --data "{\"jsonrpc\":\"2.0\",\"method\":\"getPbftView\",\"params\":[\$group],\"id\":68}")
                echo \$viewresult
                view=\$(echo \$viewresult | awk -F'"' '{if(\$2=="id" && \$4=="jsonrpc" && \$8=="result") {print \$10}}')
                [[ -z "\$view" ]] && {
                        alarm " ERROR! Cannot connect to \$config_ip:\$config_port \$node:\$group "
                        continue
                }

                view_file="\$nodedir/\$node_\$group.view"
                prev_view=0
                [ -f \$view_file ] && prev_view=\$(cat \$view_file)
                viewvalue=\$(printf "%d\n" "\$view")
                prev_viewvalue=\$(printf "%d\n" "\$prev_view")

                # check if blocknumber of pbft view already change, if all of them is the same with before, the node may not work well.
                [ \$heightvalue -eq \$prev_heightvalue ] && [ \$viewvalue -eq \$prev_viewvalue ] && {
                        alarm " ERROR! \$config_ip:\$config_port \$node:\$group is not working properly: height \$height and view \$view no change"
                        continue
                }

                echo \$height >\$height_file
                echo \$view >\$view_file
                info " OK! \$config_ip:\$config_port \$node:\$group is working properly: height \$height view \$view"

        done

        return 0
}

# check all node of this server, if all node work well.
function check_all_node_work_properly() {
        local work_dir=\$1
        for configfile in \$(ls \${work_dir}/node*/config.ini); do
                check_node_work_properly \$(dirname \$configfile)
        done
}

function help() {
        echo "Usage:"
        echo "Optional:"
        echo "    -d  <path>          work dir(default: \\$SHELL_FOLDER/../). "
        echo "    -h                  Help."
        echo "Example:"
        echo "    bash monitor.sh -d /data/nodes/127.0.0.1 "
        exit 0
}

work_dir=\${SHELL_FOLDER}/../../

while getopts "d:r:c:s:h" option; do
        case \$option in
        d) work_dir=\$OPTARG ;;
        h) help ;;
        esac
done

[ ! -d \${work_dir} ] && {
        LOG_ERROR "work_dir(\$work_dir) not exist "
        exit 0
}

check_all_node_work_properly \${work_dir}

EOF
generate_script_template "$output/scripts/reload_sdk_allowlist.sh"
    cat << EOF >> "$output/scripts/reload_sdk_allowlist.sh"
cd \${SHELL_FOLDER}/../
NODE_FOLDER=\$(pwd)
fisco_bcos=\${NODE_FOLDER}/${fisco_bin_path}
node=\$(basename \${NODE_FOLDER})
node_pid=${ps_cmd}
if [ -n "\${node_pid}" ];then
    LOG_INFO "\${node} is trying to reload sdk allowlist. Check log for more information."
    touch config.ini.reset_allowlist
    exit 0
else
    echo "\${node} is not running, use start.sh to start all group directlly."
fi
EOF
}

genDownloadConsole() {
    local output=$1
    local file="${output}/download_console.sh"
    generate_script_template "${file}"
    cat << EOF >> "${file}"
set -e
sed_cmd="sed -i"
solc_suffix=""
supported_solc_versions=(0.4 0.5 0.6)
package_name="console.tar.gz"
sm_crypto=false

if [ "\$(uname)" == "Darwin" ];then
    sed_cmd="sed -i .bkp"
fi
while getopts "v:V:f" option;do
    case \$option in
    v) solc_suffix="\${OPTARG//[vV]/}"
        if ! echo "\${supported_solc_versions[*]}" | grep -i "\${solc_suffix}" &>/dev/null; then
            LOG_WARN "\${solc_suffix} is not supported. Please set one of \${supported_solc_versions[*]}"
            exit 1;
        fi
        package_name="console-\${solc_suffix}.tar.gz"
        if [ "\${solc_suffix}" == "0.4" ]; then package_name="console.tar.gz";fi
    ;;
    V) version="\$OPTARG";;
    f) config="true";;
    esac
done

if [[ -z "\${version}" ]];then
    version=\$(curl -s https://api.github.com/repos/FISCO-BCOS/console/releases | grep "tag_name" | cut -d \" -f 4 | sort -V | tail -n 1 | sed "s/^[vV]//")
fi
sm_crypto=\$(cat "\${SHELL_FOLDER}"/node*/config.ini | grep sm_crypto= | cut -d = -f 2 | head -n 1)
download_link=https://github.com/FISCO-BCOS/console/releases/download/v\${version}/\${package_name}
cos_download_link=${cdn_link_header}/console/releases/v\${version}/\${package_name}
echo "Downloading console \${version} from \${download_link}"
if [ \$(curl -IL -o /dev/null -s -w %{http_code}  \${cos_download_link}) == 200 ];then
    curl -#LO \${download_link} --speed-time 30 --speed-limit 102400 -m 450 || {
        echo -e "\033[32m Download speed is too low, try \${cos_download_link} \033[0m"
        curl -#LO \${cos_download_link}
    }
else
    curl -#LO \${download_link}
fi
tar -zxf \${package_name} && cd console && chmod +x *.sh

if [[ -n "\${config}" ]];then
    if  [ "\${sm_crypto}" == "false" ];then
        cp ../sdk/* conf/
    else
        cp ../sdk/gm/* conf/
    fi
    channel_listen_port=\$(cat "\${SHELL_FOLDER}"/node*/config.ini | grep channel_listen_port | cut -d = -f 2 | head -n 1)
    channel_listen_ip=\$(cat "\${SHELL_FOLDER}"/node*/config.ini | grep channel_listen_ip | cut -d = -f 2 | head -n 1)
    if [ "\${version:0:1}" == "1" ];then
        cp conf/applicationContext-sample.xml conf/applicationContext.xml
        \${sed_cmd} "s/127.0.0.1:20200/127.0.0.1:\${channel_listen_port}/" conf/applicationContext.xml
    else
        cp conf/config-example.toml conf/config.toml
        \${sed_cmd} "s/127.0.0.1:20200/127.0.0.1:\${channel_listen_port}/" conf/config.toml
    fi
    echo -e "\033[32m console configuration completed successfully. \033[0m"
fi
EOF
}

genTransTest()
{
    local output=$1
    local file="${output}/.transTest.sh"
    generate_script_template "${file}"
    cat << EOF >> "${file}"
# This script only support for block number smaller than 65535 - 256

ip_port=http://127.0.0.1:$(( port_start[2] ))
trans_num=1
target_group=1
version=
if [ \$# -ge 1 ];then
    trans_num=\$1
fi
if [ \$# -ge 2 ];then
    target_group=\$2
fi

getNodeVersion()
{
    result="\$(curl -X POST --data '{"jsonrpc":"2.0","method":"getClientVersion","params":[],"id":1}' \${ip_port})"
    version="\$(echo \${result} | cut -c250- | cut -d \" -f3)"
}

block_limit()
{
    result=\$(curl -s -X POST --data '{"jsonrpc":"2.0","method":"getBlockNumber","params":['\${target_group}'],"id":83}' \${ip_port})
    if [ \$(echo \${result} | grep -i failed | wc -l) -gt 0 ] || [ -z \${result} ];then
        echo "getBlockNumber error!"
        exit 1
    fi
    blockNumber=\$(echo \${result}| cut -d \" -f 10)
    printf "%04x" \$((\$blockNumber+0x100))
}

send_a_tx()
{
    limit=\$(block_limit)
    random_id="\$(date +%s)\$(printf "%09d" \${RANDOM})"
    if [ \${#limit} -gt 4 ];then echo "blockLimit exceed 0xffff, this scripts is unavailable!"; exit 0;fi
    if [ "\${version}" == "2.0.0-rc1" ];then
        txBytes="f8f0a02ade583745343a8f9a70b40db996fbe69c63531832858\${random_id}85174876e7ff8609184e729fff82\${limit}94d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7f7395028658d0e01b86a371ca0e33891be86f781ebacdafd543b9f4f98243f7b52d52bac9efa24b89e257a354da07ff477eb0ba5c519293112f1704de86bd2938369fbf0db2dff3b4d9723b9a87d"
    else
        txBytes="f8eca003eb675ec791c2d19858c91d0046821c27d815e2e9c15\${random_id}0a8402faf08082\${limit}948c17cf316c1063ab6c89df875e96c9f0f5b2f74480b8644ed3885e0000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000000000000000a464953434f2042434f53000000000000000000000000000000000000000000000101801ba09edf7c0cb63645442aff11323916d51ec5440de979950747c0189f338afdcefda02f3473184513c6a3516e066ea98b7cfb55a79481c9db98e658dd016c37f03dcf"
    fi
    #echo \$txBytes
    curl -s -X POST --data '{"jsonrpc":"2.0","method":"sendRawTransaction","params":['\${target_group}', "'\$txBytes'"],"id":83}' \${ip_port}
}

send_many_tx()
{
    for j in \$(seq 1 \$1)
    do
        echo 'Send transaction: ' \$j
        send_a_tx \${ip_port}
    done
}
getNodeVersion
echo "Use version:\${version}"
send_many_tx \${trans_num}

EOF
}

generate_server_scripts()
{
    local output=$1
    genTransTest "${output}"
    generate_script_template "$output/start_all.sh"
    # echo "ip_array=(\$(ifconfig | grep inet | grep -v inet6 | awk '{print \$2}'))"  >> "$output/start_all.sh"
    # echo "if echo \${ip_array[@]} | grep -w \"${ip}\" &>/dev/null; then echo \"start node_${ip}_${i}\" && bash \${SHELL_FOLDER}/node_${ip}_${i}/start.sh; fi" >> "${output_dir}/start_all.sh"
    cat << EOF >> "$output/start_all.sh"
dirs=(\$(ls -l \${SHELL_FOLDER} | awk '/^d/ {print \$NF}'))
for directory in \${dirs[*]}
do
    if [[ -f "\${SHELL_FOLDER}/\${directory}/config.ini" && -f "\${SHELL_FOLDER}/\${directory}/start.sh" ]];then
        echo "try to start \${directory}"
        bash \${SHELL_FOLDER}/\${directory}/start.sh &
    fi
done
wait
EOF
    generate_script_template "$output/stop_all.sh"
    cat << EOF >> "$output/stop_all.sh"
dirs=(\$(ls -l \${SHELL_FOLDER} | awk '/^d/ {print \$NF}'))
for directory in \${dirs[*]}
do
    if [[ -d "\${SHELL_FOLDER}/\${directory}" && -f "\${SHELL_FOLDER}/\${directory}/stop.sh" ]];then
        echo "try to stop \${directory}"
        bash \${SHELL_FOLDER}/\${directory}/stop.sh &
    fi
done
wait
EOF
    generate_script_template "$output/download_bin.sh"
    cat << EOF >> "$output/download_bin.sh"
#!/bin/bash

project=FISCO-BCOS/FISCO-BCOS
cdn_link_header="${cdn_link_header}"
download_branch=
output_dir=bin/
download_mini=
download_version=
latest_version=
download_timeout=${download_timeout}

help() {
    echo "
Usage:
    -v <Version>           Download binary of spectfic version, default latest
    -b <Branch>            Download binary of spectfic branch
    -o <Output Dir>        Default ./bin
    -l                     List released FISCO-BCOS versions
    -m                     Download mini binary, only works with -b option
    -h Help
e.g
    \$0 -v ${default_version}
"
exit 0
}

parse_params(){
while getopts "b:o:v:hml" option;do
    case \$option in
    o) output_dir=\$OPTARG;;
    b) download_branch="\$OPTARG";;
    v) download_version="\${OPTARG//[vV]/}";;
    m) download_mini="true";;
    l) LOG_INFO "Show the history releases of FISCO-BCOS " && curl -sS https://gitee.com/api/v5/repos/\${project}/tags | grep -oe "\"name\":\"v[2-9]*\.[0-9]*\.[0-9]*\"" | cut -d \" -f 4 | sort -V && exit 0;;
    h) help;;
    *) help;;
    esac
done
}

download_artifact_linux(){
    local branch="\$1"
    build_num=\$(curl https://circleci.com/api/v1.1/project/github/\${project}/tree/\${branch}\?circle-token\=\&limit\=1\&offset\=0\&filter\=successful 2>/dev/null| grep build_num | sed "s/ //g"| cut -d ":" -f 2| sed "s/,//g" | sort -u | tail -n1)

    local response="\$(curl https://circleci.com/api/v1.1/project/github/\${project}/\${build_num}/artifacts?circle-token= 2>/dev/null)"
    if [ -z "\${download_mini}" ];then
        link="\$(echo \${response}| grep -o 'https://[^"]*' | grep -v mini)"
    else
        link="\$(echo \${response}| grep -o 'https://[^"]*' | grep mini)"
    fi
    if [ -z "\${link}" ];then
        build_num=\$(( build_num - 1 ))
        response="\$(curl https://circleci.com/api/v1.1/project/github/\${project}/\${build_num}/artifacts?circle-token= 2>/dev/null)"
        if [ -z "\${download_mini}" ];then
            link="\$(echo \${response}| grep -o 'https://[^"]*' | grep -v mini| tail -n 1)"
        else
            link="\$(echo \${response}| grep -o 'https://[^"]*' | grep mini| tail -n 1)"
        fi
    fi
    if [ -z "\${link}" ];then
        echo "CircleCI build_num:\${build_num} can't find artifacts."
        exit 1
    fi
    LOG_INFO "Downloading binary from \${link} "
    curl -#LO \${link} && tar -zxf fisco*.tar.gz && rm fisco*.tar.gz
    result=\$?
    if [[ "\${result}" != "0" ]];then LOG_ERROR "Download failed, please try again" && exit 1;fi

}

download_artifact_macOS(){
    echo "unsupported for now."
    exit 1
    local branch="\$1"
    # TODO: https://developer.github.com/v3/actions/artifacts/#download-an-artifact
    local workflow_artifacts_url="\$(curl https://api.github.com/repos/\${project}/actions/runs\?branch\=\${branch}\&status\=success\&event\=push | grep artifacts_url | head -n 1 | cut -d \" -f 4)"
    local archive_download_url="\$(curl \${workflow_artifacts_url} | grep archive_download_url | cut -d \" -f 4)"
    if [ -z "\${archive_download_url}" ];then
        echo "GitHub action \${workflow_artifacts_url} can't find artifact."
        exit 1
    fi
    LOG_INFO "Downloading macOS binary from \${archive_download_url} "
    # FIXME: https://github.com/actions/upload-artifact/issues/51
    curl -#LO "\${archive_download_url}" && tar -zxf fisco-bcos-macOS.tar.gz && rm fisco-bcos-macOS.tar.gz
}

download_branch_artifact(){
    local branch="\$1"
    if [ "\$(uname)" == "Darwin" ];then
        LOG_INFO "Downloading binary of \${branch} on macOS "
        download_artifact_macOS "\${branch}"
    else
        LOG_INFO "Downloading binary of \${branch} on Linux "
        download_artifact_linux "\${branch}"
    fi
}

download_released_artifact(){
    local version="\$1"
    local package_name="fisco-bcos.tar.gz"
    if [ "\$(uname)" == "Darwin" ];then
        package_name="fisco-bcos-macOS.tar.gz"
    fi
    local download_link="https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/\${version}/\${package_name}"
    local cdn_download_link="\${cdn_link_header}/FISCO-BCOS/releases/\${version}/\${package_name}"
    LOG_INFO "Downloading binary of \${version}, from \${download_link}"
    if [ \$(curl -IL -o /dev/null -s -w %{http_code} \${cdn_download_link}) == 200 ];then
        curl -#LO \${download_link} --speed-time 20 --speed-limit 102400 -m \${download_timeout} || {
            echo "Download speed is too low, try \${cdn_download_link}"
            curl -#LO "\${cdn_download_link}"
        }
    else
        curl -#LO \${download_link}
    fi
    tar -zxf "\${package_name}" && rm "\${package_name}"
}

main() {
    [ -f "\${output_dir}/fisco-bcos" ] && mv "\${output_dir}/fisco-bcos" "\${output_dir}/fisco-bcos.bak"
    mkdir -p "\${output_dir}" && cd "\${output_dir}"
    latest_version=\$(curl -sS https://gitee.com/api/v5/repos/\${project}/tags | grep -oe "\"name\":\"v[2-9]*\.[0-9]*\.[0-9]*\"" | cut -d \" -f 4 | sort -V | tail -n 1)
    if [ -n "\${download_version}" ];then
        download_released_artifact "v\${download_version}"
    elif [ -n "\${download_branch}" ];then
        download_branch_artifact "\${download_branch}"
    else
        download_released_artifact "\${latest_version}"
    fi
    LOG_INFO "Finished. Please check \${output_dir}"
}

parse_params "\$@"
main

EOF
}

parse_ip_config()
{
    local config=$1
    local n=0
    while read line;do
        ip_array[n]=$(echo ${line} | awk '{print $1}')
        agency_array[n]=$(echo ${line} | awk '{print $2}')
        group_array[n]=$(echo ${line} | awk '{print $3}')
        if [ -z "${ip_array[$n]}" -o -z "${agency_array[$n]}" -o -z "${group_array[$n]}" ];then
            exit_with_clean "Please check ${config}, make sure there is no empty line!"
        fi
        ports_array[n]=$(echo "${line}" | awk '{print $4}')
        ((++n))
    done < ${config}
}

download_bin()
{
    if [ "${x86_64_arch}" != "true" ];then exit_with_clean "We only offer x86_64 precompiled fisco-bcos binary, your OS architecture is not x86_64. Please compile from source."; fi
    bin_path=${output_dir}/${bcos_bin_name}
    package_name="fisco-bcos.tar.gz"
    if [ -n "${macOS}" ];then
        package_name="fisco-bcos-macOS.tar.gz"
    fi

    Download_Link="https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/v${compatibility_version}/${package_name}"
    local cdn_download_link="${cdn_link_header}/FISCO-BCOS/releases/v${compatibility_version}/${package_name}"
    LOG_INFO "Downloading fisco-bcos binary from ${Download_Link} ..."
    if [ $(curl -IL -o /dev/null -s -w %{http_code} "${cdn_download_link}") == 200 ];then
        curl -#LO "${Download_Link}" --speed-time 20 --speed-limit 102400 -m "${download_timeout}" || {
            LOG_INFO "Download speed is too low, try ${cdn_download_link}"
            curl -#LO "${cdn_download_link}"
        }
    else
        curl -#LO "${Download_Link}"
    fi
    if [[ "$(ls -al . | grep tar.gz | awk '{print $5}')" -lt "1048576" ]];then
        exit_with_clean "Download fisco-bcos failed, please try again. Or download and extract it manually from ${Download_Link} and use -e option."
    fi
    tar -zxf ${package_name} && mv fisco-bcos ${bin_path} && rm ${package_name}
    chmod a+x ${bin_path}
}

function version_gt() { test "$(echo "$@" | tr " " "\n" | sort -V | head -n 1)" != "$1"; }

check_bin()
{
    echo "Checking fisco-bcos binary..."
    bin_version=$()
    if ! ${bin_path} -v | grep -q 'FISCO-BCOS';then
        exit_with_clean "${bin_path} is wrong. Please correct it and try again."
    fi
    bin_version=$(${bin_path} -v | grep -oe "[2-9]*\.[0-9]*\.[0-9]*")
    if version_gt "${compatibility_version}" "${bin_version}";then
        exit_with_clean "${bin_version} is less than ${compatibility_version}. Please correct it and try again."
    fi
    echo "Binary check passed."
}

prepare_ca(){

    if [ -z "${CertConfig}" ] || [ ! -e "${CertConfig}" ];then
        # CertConfig="${output_dir}/cert.cnf"
        generate_cert_conf "cert.cnf"
    else
        cp "${CertConfig}" .
    fi
    dir_must_not_exists "${output_dir}/chain"
    if [ -z "$ca_path" ]; then
        echo "Generating CA key..."
        gen_chain_cert "${output_dir}/chain" >"${logfile}" 2>&1 || exit_with_clean "openssl error!"
        mv "${output_dir}/chain" "${output_dir}/cert"
        ca_path="${output_dir}/cert/"
    else
        echo "Use user specified ca key ${ca_key}"
        mkdir -p "${output_dir}/cert"
        cp "${ca_key}" "${output_dir}/cert"
        cp "${ca_crt}" "${output_dir}/cert"
        [ -f "$ca_path/root.crt" ] && cp "$ca_path/root.crt" "${output_dir}/cert/" && root_crt="${output_dir}/cert/root.crt"
    fi
    ca_key="${output_dir}/cert/ca.key"
    mv cert.cnf "${output_dir}/cert/"
    if [ "${use_ip_param}" == "false" ];then
        for agency_name in ${agency_array[*]};do
            if [ ! -d "${output_dir}/cert/${agency_name}" ];then
                gen_agency_cert "${output_dir}/cert" "${output_dir}/cert/${agency_name}" >>"${logfile}" 2>&1
            fi
        done
    else
        gen_agency_cert "${output_dir}/cert" "${output_dir}/cert/agency" >"${logfile}" 2>&1
    fi

    if [[ -n "${guomi_mode}" ]];then
        dir_must_not_exists "${output_dir}/gmchain"
        generate_cert_conf_gm "gmcert.cnf"
        generate_gmsm2_param "gmsm2.param"
        if [[ -z "${gmca_path}" ]]; then
            echo "Generating Guomi CA key..."
            gen_chain_cert_gm "${output_dir}/gmchain" >"${logfile}" 2>&1 || exit_with_clean "openssl gm error!"
            mv "${output_dir}/gmchain" "${output_dir}/gmcert"
            gmca_path="${output_dir}/gmcert/"
        elif [[ -n "${gmca_path}" ]]; then
            echo "Use user specified sm_crypto ca key ${gmca_key}"
            mkdir -p "${output_dir}/gmcert"
            cp "${gmca_key}" "${output_dir}/gmcert"
            cp "${gmca_crt}" "${output_dir}/gmcert"
            [ -f "$gmca_path/gmroot.crt" ] && cp "$gmca_path/gmroot.crt" "${output_dir}/gmcert/" && gmroot_crt="${output_dir}/gmcert/gmroot.crt"
        fi
        gmca_key="${output_dir}/gmcert/ca.key"

        if ! cp gmcert.cnf gmsm2.param "${output_dir}/gmcert/";then
            exit_with_clean "cp gmcert.cnf gmsm2.param failed"
        fi
        if [ "${use_ip_param}" == "false" ];then
            for agency_name in ${agency_array[*]};do
                if [ ! -d "${output_dir}/gmcert/${agency_name}" ];then
                    gen_agency_cert_gm "${output_dir}/gmcert" "${output_dir}/gmcert/${agency_name}" >"${logfile}" 2>&1
                fi
            done
        else
            gen_agency_cert_gm "${output_dir}/gmcert" "${output_dir}/gmcert/agency" >"${logfile}" 2>&1
        fi
    fi
}

main()
{
output_dir="$(pwd)/${output_dir}"
[ -z $use_ip_param ] && help 'ERROR: Please set -l or -f option.'
if [ "${use_ip_param}" == "true" ];then
    ip_array=(${ip_param//,/ })
elif [ "${use_ip_param}" == "false" ];then
    if ! parse_ip_config "$ip_file" ;then
        exit_with_clean "Parse $ip_file error!"
    fi
else
    help
fi


dir_must_not_exists "${output_dir}"
mkdir -p "${output_dir}"

# if [ -z "${compatibility_version}" ];then
#     set +e
#     compatibility_version=$(curl -s https://api.github.com/repos/FISCO-BCOS/FISCO-BCOS/releases | grep "tag_name" | grep "\"v2\.[0-9]*\.[0-9]*\"" | sort -V | tail -n 1 | cut -d \" -f 4 | sed "s/^[vV]//")
#     set -e
# fi

# use default version as compatibility_version
if [ -z "${compatibility_version}" ];then
    compatibility_version="${default_version}"
fi

# download fisco-bcos and check it
if [ -z ${docker_mode} ];then
    if [[ -z ${bin_path} ]];then
        download_bin
    else
        check_bin
    fi
fi

if [ "${use_ip_param}" == "true" ];then
    for i in $(seq 0 ${#ip_array[*]});do
        agency_array[i]="agency"
        group_array[i]=1
    done
fi

# prepare CA
echo "=============================================================="
echo "">"${logfile}"
prepare_ca

echo "=============================================================="
echo "Generating keys and certificates ..."
nodeid_list=""
ip_list=""
count=0
server_count=0
groups=
groups_count=
for line in ${ip_array[*]};do
    ip=${line%:*}
    num=${line##*:}
    if [[ $(echo "$ip" | grep -qE "^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$") && -z "${use_ipv6}" ]];then
        LOG_WARN "Please check IP address: ${ip}, if you use domain name please ignore this."
    fi
    [ "$num" == "$ip" ] || [ -z "${num}" ] && num=${node_num}
    echo "Processing IP=${ip} Total=${num} Agency=${agency_array[${server_count}]} Groups=${group_array[server_count]}"
    [ -z "$(get_value "${ip//[\.:]/_}_count")" ] && set_value "${ip//[\.:]/_}_count" 0
    sdk_path="${output_dir}/${ip}/sdk"
    if [ ! -d "${sdk_path}" ];then
        gen_cert "${output_dir}/cert/${agency_array[${server_count}]}" "${sdk_path}" "sdk"
        # FIXME: delete the below unbelievable ugliest operation in future
        cp sdk.crt node.crt
        cp sdk.key node.key
        # FIXME: delete the upside unbelievable ugliest operation in future
        mv node.nodeid sdk.publickey
        cd "${output_dir}"
        if [ -n "${guomi_mode}" ];then
            mkdir -p "${sdk_path}/gm"
            gen_node_cert_with_extensions_gm "${output_dir}/gmcert/${agency_array[${server_count}]}" "${sdk_path}/gm" "sdk" sdk v3_req
            if [ -z "${no_agency}" ];then cat "${output_dir}/gmcert/${agency_array[${server_count}]}/gmagency.crt" >> "${sdk_path}/gm/gmsdk.crt";fi
            cat "${output_dir}/gmcert/${agency_array[${server_count}]}/../gmca.crt" >> "${sdk_path}/gm/gmsdk.crt"
            gen_node_cert_with_extensions_gm "${output_dir}/gmcert/${agency_array[${server_count}]}" "${sdk_path}/gm" "sdk" ensdk v3enc_req
            cp "${output_dir}/gmcert/gmca.crt" "${sdk_path}/gm/"
            $TASSL_CMD ec -in "$sdk_path/gm/gmsdk.key" -text -noout 2> /dev/null | sed -n '7,11p' | sed 's/://g' | tr "\n" " " | sed 's/ //g' | awk '{print substr($0,3);}'  | cat > "$ndpath/gmsdk.publickey"
            mv "$sdk_path/gmsdk.publickey" "$sdk_path/gm"
        fi
    fi
    for ((i=0;i<num;++i));do
        echo "Processing IP=${ip} ID=${i} node's key" >>"${logfile}"
        local node_count="$(get_value "${ip//[\.:]/_}_count")"
        local node_dir="${output_dir}/${ip}/node${node_count}"
        [ -d "${node_dir}" ] && exit_with_clean "${node_dir} exist! Please delete!"

        while :
        do
            gen_cert "${output_dir}/cert/${agency_array[${server_count}]}" "${node_dir}" "node"
            privateKey=$(openssl ec -in "${node_dir}/${conf_path}/node.key" -text 2> /dev/null| sed -n '3,5p' | sed 's/://g'| tr "\n" " "|sed 's/ //g')
            len=${#privateKey}
            head2=${privateKey:0:2}
            #private key should not start with 00
            if [ "64" != "${len}" ] || [ "00" == "$head2" ];then
                rm -rf ${node_dir}
                echo "continue because of length=${len} head=$head2" >>"${logfile}"
                continue;
            fi

            if [ -n "$guomi_mode" ]; then
                gen_node_cert_gm "${output_dir}/gmcert/${agency_array[${server_count}]}" "${node_dir}"
                privateKey=$($TASSL_CMD ec -in "${node_dir}/${gm_conf_path}/gmnode.key" -text 2> /dev/null| sed -n '3,5p' | sed 's/://g'| tr "\n" " "|sed 's/ //g')
                len=${#privateKey}
                head2=${privateKey:0:2}
                #private key should not start with 00
                if [ "64" != "${len}" ] || [ "00" == "$head2" ];then
                    rm -rf ${node_dir}
                    echo "continue gm because of length=${len} head=$head2" >>"${logfile}"
                    continue;
                fi
            fi
            break;
        done

        if [ -n "${guomi_mode}" ]; then
            rm ${node_dir}/${conf_path}/node.nodeid
            mv ${node_dir}/${conf_path} ${node_dir}/${gm_conf_path}/origin_cert
            nodeid="$(cat ${node_dir}/${gm_conf_path}/gmnode.nodeid)"
            #remove original cert files
            mv ${node_dir}/${gm_conf_path} ${node_dir}/${conf_path}
        else
            nodeid="$(cat ${node_dir}/${conf_path}/node.nodeid)"
        fi
        if [ -n "${copy_cert}" ];then cp -r "${sdk_path}" "${node_dir}/" ; fi
        if [ "${use_ip_param}" == "false" ];then
            node_groups=(${group_array[server_count]//,/ })
            for j in ${node_groups[@]};do
                if [ -z "${groups_count[${j}]}" ];then groups_count[${j}]=0;fi
        groups[${j}]=$"${groups[${j}]}node.${groups_count[${j}]}=${nodeid}
    "
                ((++groups_count[j]))
            done
        else
        nodeid_list=$"${nodeid_list}node.${count}=${nodeid}
    "
        fi
        local node_ports=
        read -r -a node_ports <<< "${port_start[*]}"
        if [ -n "${ports_array[server_count]}" ];then
            read -r -a node_ports <<< "${ports_array[server_count]//,/ }"
            echo "node_ports ${node_ports[*]}" >>"${logfile}"
            [ "${i}" == "0" ] && { set_value "${ip//[\.:]/_}_port_offset" 0; }
        fi
        if [ -n "${use_ipv6}" ];then
        ip_list="${ip_list}node.${count}=[${ip}]:$(( $(get_value ${ip//[\.:]/_}_port_offset) + node_ports[0] ))
    "
        else
        ip_list="${ip_list}node.${count}=${ip}:$(( $(get_value ${ip//[\.:]/_}_port_offset) + node_ports[0] ))
    "
        fi
        set_value "${ip//[\.:]/_}_count" $(( $(get_value "${ip//[\.:]/_}_count") + 1 ))
        set_value "${ip//[\.:]/_}_port_offset" $(( $(get_value "${ip//[\.:]/_}_port_offset") + 1 ))
        ((++count))
    done
    ((++server_count))
done

# clean
for line in ${ip_array[*]};do
    ip=${line%:*}
    set_value "${ip//[\.:]/_}_count" 0
    set_value "${ip//[\.:]/_}_port_offset" 0
done

echo "=============================================================="
echo "Generating configuration files ..."
cd ${current_dir}
server_count=0
for line in ${ip_array[*]};do
    ip=${line%:*}
    num=${line##*:}
    [ "$num" == "$ip" ] || [ -z "${num}" ] && num=${node_num}
    echo "Processing IP=${ip} Total=${num} Agency=${agency_array[${server_count}]} Groups=${group_array[server_count]}"
    for ((i=0;i<num;++i));do
        local node_count="$(get_value "${ip//[\.:]/_}_count")"
        local node_dir="${output_dir}/${ip}/node${node_count}"
        echo "Processing IP=${ip} ID=${i} ${node_dir} config files..." >> "${logfile}"
        generate_config_ini "${node_dir}/config.ini" "${ip}" "${group_array[server_count]}" "${ports_array[server_count]}" ${i}
        if [ "${use_ip_param}" == "false" ];then
            node_groups=(${group_array[${server_count}]//,/ })
            for j in ${node_groups[@]};do
                generate_group_genesis "$node_dir/${conf_path}/group.${j}.genesis" "${j}" "${groups[${j}]}" "${groups_count[${j}]}"
                generate_group_ini "$node_dir/${conf_path}/group.${j}.ini"
            done
        else
            generate_group_genesis "$node_dir/${conf_path}/group.1.genesis" "1" "${nodeid_list}" "${count}"
            generate_group_ini "$node_dir/${conf_path}/group.1.ini"
        fi
        generate_node_scripts "${node_dir}"
        if [[ -n "${deploy_mode}" ]];then
            if [ -z ${docker_mode} ];then cp "$bin_path" "${node_dir}/fisco-bcos"; fi
            p2p_port=$(grep listen_port < "${node_dir}"/config.ini | grep -v _listen_port | cut -d = -f 2)
            mv "${node_dir}" "${output_dir}/${ip}/node_${ip}_${p2p_port}"
        fi
        set_value "${ip//[\.:]/_}_count" $(( $(get_value "${ip//[\.:]/_}_count") + 1 ))
        set_value "${ip//[\.:]/_}_port_offset" $(( $(get_value "${ip//[\.:]/_}_port_offset") + 1 ))
    done
    generate_server_scripts "${output_dir}/${ip}"
    genDownloadConsole "${output_dir}/${ip}"
    if [[ -z "${deploy_mode}" && -z "${docker_mode}" ]];then cp "$bin_path" "${output_dir}/${ip}/fisco-bcos"; fi
    if [ -n "$make_tar" ];then cd ${output_dir} && tar zcf "${ip}.tar.gz" "${ip}" && cd ${current_dir};fi
    ((++server_count))
done
rm "${logfile}"
if [ -f "${output_dir}/${bcos_bin_name}" ];then rm "${output_dir}/${bcos_bin_name}";fi
if [ "${use_ip_param}" == "false" ];then
echo "=============================================================="
    for l in $(seq 0 ${#groups_count[@]});do
        if [ -n "${groups_count[${l}]}" ];then echo "Group:${l} has ${groups_count[${l}]} nodes";fi
    done
fi

if [ -n "${no_agency}" ];then
# delete agency crt
    for agency_name in ${agency_array[*]};do
        if [ -d "${output_dir}/cert/${agency_name}" ];then rm -rf "${output_dir}/cert/${agency_name}";fi
        if [ -d "${output_dir}/gmcert/${agency_name}" ];then rm -rf "${output_dir}/gmcert/${agency_name}";fi
    done
fi
}

check_env
parse_params $@
check_and_install_tassl
main
print_result
