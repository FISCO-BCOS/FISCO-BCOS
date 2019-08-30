#!/bin/bash

set -e

# default value 
ca_file= #CA key
node_num=1 
ip_file=
ip_param=
use_ip_param=
agency_array=
group_array=
ip_array=
output_dir=nodes
port_start=(30300 20200 8545)
state_type=storage 
storage_type=rocksdb
conf_path="conf"
bin_path=
make_tar=
debug_log="false"
log_level="info"
logfile=${PWD}/build.log
listen_ip="127.0.0.1"
bcos_bin_name=fisco-bcos
guomi_mode=
docker_mode=
gm_conf_path="gmconf/"
current_dir=$(pwd)
consensus_type="pbft"
TASSL_CMD="${HOME}"/.tassl
enable_parallel=true
auto_flush="true"
# trans timestamp from seconds to milliseconds
timestamp=$(($(date '+%s')*1000))
chain_id=1
compatibility_version=""
default_version="2.1.0"
macOS=""
x86_64_arch="true"
download_timeout=60
cdn_link_header="https://www.fisco.com.cn/cdn/fisco-bcos/releases/download"

help() {
    echo $1
    cat << EOF
Usage:
    -l <IP list>                        [Required] "ip1:nodeNum1,ip2:nodeNum2" e.g:"192.168.0.1:2,192.168.0.2:3"
    -f <IP list file>                   [Optional] split by line, every line should be "ip:nodeNum agencyName groupList". eg "127.0.0.1:4 agency1 1,2"
    -e <FISCO-BCOS binary path>         Default download fisco-bcos from GitHub. If set -e, use the binary at the specified location
    -o <Output Dir>                     Default ./nodes/
    -p <Start Port>                     Default 30300,20200,8545 means p2p_port start from 30300, channel_port from 20200, jsonrpc_port from 8545
    -i <Host ip>                        Default 127.0.0.1. If set -i, listen 0.0.0.0
    -v <FISCO-BCOS binary version>      Default get version from https://github.com/FISCO-BCOS/FISCO-BCOS/releases. If set use specificd version binary
    -s <DB type>                        Default rocksdb. Options can be rocksdb / mysql / external, rocksdb is recommended
    -d <docker mode>                    Default off. If set -d, build with docker
    -c <Consensus Algorithm>            Default PBFT. If set -c, use Raft
    -m <MPT State type>                 Default storageState. if set -m, use mpt state
    -C <Chain id>                       Default 1. Can set uint.
    -g <Generate guomi nodes>           Default no
    -z <Generate tar packet>            Default no
    -t <Cert config file>               Default auto generate
    -T <Enable debug log>               Default off. If set -T, enable debug log
    -F <Disable log auto flush>         Default on. If set -F, disable log auto flush
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
    local var_name=${1}
    var_name=var_${var_name//./}
    local res=$(eval echo '$'"${var_name}")
    echo ${res}
}

set_value()
{
    local var_name=${1}
    var_name=var_${var_name//./}
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
while getopts "f:l:o:p:e:t:v:s:C:iczhgmTFd" option;do
    case $option in
    f) ip_file=$OPTARG
       use_ip_param="false"
    ;;
    l) ip_param=$OPTARG
       use_ip_param="true"
    ;;
    o) output_dir=$OPTARG;;
    i) listen_ip="0.0.0.0";;
    v) compatibility_version="$OPTARG";;
    p) port_start=(${OPTARG//,/ })
    if [ ${#port_start[@]} -ne 3 ];then LOG_WARN "start port error. e.g: 30300,20200,8545" && exit 1;fi
    ;;
    e) bin_path=$OPTARG;;
    m) state_type=mpt;;
    s) storage_type=$OPTARG
        if [ -z "${storage_type}" ];then
            LOG_WARN "${storage_type} is not supported storage."
            exit 1;
        fi
    ;;
    t) CertConfig=$OPTARG;;
    c) consensus_type="raft";;
    C) chain_id=$OPTARG
        if [ -z $(grep '^[[:digit:]]*$' <<< "${chain_id}") ];then
            LOG_WARN "${chain_id} is not a positive integer."
            exit 1;
        fi
    ;;
    T) debug_log="true"
    log_level="debug"
    ;;
    F) auto_flush="false";;
    z) make_tar="yes";;
    g) guomi_mode="yes";;
    d) docker_mode="yes"
    if [ ! -z "${macOS}" ];then LOG_WARN "Docker desktop of macOS can't support docker mode of FISCO BCOS!" && exit 1;fi
    ;;
    h) help;;
    esac
done
}

print_result()
{
echo "================================================================"
[ -z ${docker_mode} ] && [ -f "${bin_path}" ] && LOG_INFO "FISCO-BCOS Path   : $bin_path"
[ ! -z ${docker_mode} ] && LOG_INFO "Docker tag        : latest"
[ ! -z $ip_file ] && LOG_INFO "IP List File      : $ip_file"
# [ ! -z $ip_file ] && LOG_INFO -e "Agencies/groups : ${#agency_array[@]}/${#groups[@]}"
LOG_INFO "Start Port        : ${port_start[*]}"
LOG_INFO "Server IP         : ${ip_array[*]}"
LOG_INFO "State Type        : ${state_type}"
LOG_INFO "RPC listen IP     : ${listen_ip}"
LOG_INFO "Output Dir        : ${output_dir}"
LOG_INFO "CA Key Path       : $ca_file"
[ ! -z $guomi_mode ] && LOG_INFO "Guomi mode        : $guomi_mode"
echo "================================================================"
if [ "${listen_ip}" == "127.0.0.1" ];then LOG_WARN "RPC listens 127.0.0.1 will cause nodes' JSON-RPC and Channel service to be inaccessible form other machines";fi
LOG_INFO "Execute the following command to get FISCO-BCOS console"
echo " bash <(curl -s https://raw.githubusercontent.com/FISCO-BCOS/console/master/tools/download_console.sh)"
echo "================================================================"
LOG_INFO "All completed. Files in ${output_dir}"
}

check_env() {
    [ ! -z "$(openssl version | grep 1.0.2)" ] || [ ! -z "$(openssl version | grep 1.1)" ] || [ ! -z "$(openssl version | grep reSSL)" ] || {
        echo "please install openssl!"
        #echo "download openssl from https://www.openssl.org."
        echo "use \"openssl version\" command to check."
        exit 1
    }
    if [ ! -z "$(openssl version | grep reSSL)" ];then
        export PATH="/usr/local/opt/openssl/bin:$PATH"
    fi
    if [ "$(uname)" == "Darwin" ];then
        macOS="macOS"
    fi
    if [ "$(uname -m)" != "x86_64" ];then
        x86_64_arch="false"
    fi
}

# TASSL env
check_and_install_tassl()
{
    if [ ! -f "${HOME}/.tassl" ];then
        curl -LO https://github.com/FISCO-BCOS/LargeFiles/raw/master/tools/tassl.tar.gz
        LOG_INFO "Downloading tassl binary ..."
        tar zxvf tassl.tar.gz
        chmod u+x tassl
        mv tassl ${HOME}/.tassl
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
    openssl genrsa -out $chaindir/ca.key 2048
    openssl req -new -x509 -days 3650 -subj "/CN=$name/O=fisco-bcos/OU=chain" -key $chaindir/ca.key -out $chaindir/ca.crt
    mv cert.cnf $chaindir
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

    openssl genrsa -out $agencydir/agency.key 2048
    openssl req -new -sha256 -subj "/CN=$name/O=fisco-bcos/OU=agency" -key $agencydir/agency.key -config $chain/cert.cnf -out $agencydir/agency.csr
    openssl x509 -req -days 3650 -sha256 -CA $chain/ca.crt -CAkey $chain/ca.key -CAcreateserial\
        -in $agencydir/agency.csr -out $agencydir/agency.crt  -extensions v4_req -extfile $chain/cert.cnf
    
    cp $chain/ca.crt $chain/cert.cnf $agencydir/
    rm -f $agencydir/agency.csr

    echo "build $name agency cert successful!"
}

gen_cert_secp256k1() {
    agpath="$1"
    certpath="$2"
    name="$3"
    type="$4"
    openssl ecparam -out $certpath/${type}.param -name secp256k1
    openssl genpkey -paramfile $certpath/${type}.param -out $certpath/${type}.key
    openssl pkey -in $certpath/${type}.key -pubout -out $certpath/${type}.pubkey
    openssl req -new -sha256 -subj "/CN=${name}/O=fisco-bcos/OU=${type}" -key $certpath/${type}.key -config $agpath/cert.cnf -out $certpath/${type}.csr
    openssl x509 -req -days 3650 -sha256 -in $certpath/${type}.csr -CAkey $agpath/agency.key -CA $agpath/agency.crt\
        -force_pubkey $certpath/${type}.pubkey -out $certpath/${type}.crt -CAcreateserial -extensions v3_req -extfile $agpath/cert.cnf
    # openssl ec -in $certpath/${type}.key -outform DER | tail -c +8 | head -c 32 | xxd -p -c 32 | cat >$certpath/${type}.private
    cat ${agpath}/agency.crt >> $certpath/${type}.crt
    rm -f $certpath/${type}.csr $certpath/${type}.pubkey $certpath/${type}.param
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
    openssl ec -in $ndpath/node.key -text -noout | sed -n '7,11p' | tr -d ": \n" | awk '{print substr($0,3);}' | cat >$ndpath/node.nodeid
    # openssl x509 -serial -noout -in $ndpath/node.crt | awk -F= '{print $2}' | cat >$ndpath/node.serial
    cp $agpath/ca.crt $ndpath
    cd $ndpath
}

generate_gmsm2_param()
{
    local output=$1
    cat << EOF > ${output} 
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

    generate_gmsm2_param "gmsm2.param"
	$TASSL_CMD genpkey -paramfile gmsm2.param -out $chaindir/gmca.key
	$TASSL_CMD req -config gmcert.cnf -x509 -days 3650 -subj "/CN=$name/O=fiscobcos/OU=chain" -key $chaindir/gmca.key -extensions v3_ca -out $chaindir/gmca.crt

    cp gmcert.cnf gmsm2.param $chaindir

    if $(cp gmcert.cnf gmsm2.param $chaindir)
    then
        echo "build chain ca succussful!"
    else
        echo "please input at least Common Name!"
    fi
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

    $TASSL_CMD genpkey -paramfile $chain/gmsm2.param -out $agencydir/gmagency.key
    $TASSL_CMD req -new -subj "/CN=$name/O=fiscobcos/OU=agency" -key $agencydir/gmagency.key -config $chain/gmcert.cnf -out $agencydir/gmagency.csr
    $TASSL_CMD x509 -req -CA $chain/gmca.crt -CAkey $chain/gmca.key -days 3650 -CAcreateserial -in $agencydir/gmagency.csr -out $agencydir/gmagency.crt -extfile $chain/gmcert.cnf -extensions v3_agency_root

    cp $chain/gmca.crt $chain/gmcert.cnf $chain/gmsm2.param $agencydir/
    rm -f $agencydir/gmagency.csr
}

gen_node_cert_with_extensions_gm() {
    capath="$1"
    certpath="$2"
    name="$3"
    type="$4"
    extensions="$5"

    $TASSL_CMD genpkey -paramfile $capath/gmsm2.param -out $certpath/gm${type}.key
    $TASSL_CMD req -new -subj "/CN=$name/O=fiscobcos/OU=agency" -key $certpath/gm${type}.key -config $capath/gmcert.cnf -out $certpath/gm${type}.csr
    $TASSL_CMD x509 -req -CA $capath/gmagency.crt -CAkey $capath/gmagency.key -days 3650 -CAcreateserial -in $certpath/gm${type}.csr -out $certpath/gm${type}.crt -extfile $capath/gmcert.cnf -extensions $extensions

    rm -f $certpath/gm${type}.csr
}

gen_node_cert_gm() {
    if [ "" = "$(openssl ecparam -list_curves 2>&1 | grep secp256k1)" ]; then
        exit_with_clean "openssl don't support secp256k1, please upgrade openssl!"
    fi

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
    gen_node_cert_with_extensions_gm "$agpath" "$ndpath" "$node" ennode v3enc_req
    #nodeid is pubkey
    $TASSL_CMD ec -in $ndpath/gmnode.key -text -noout | sed -n '7,11p' | sed 's/://g' | tr "\n" " " | sed 's/ //g' | awk '{print substr($0,3);}'  | cat > $ndpath/gmnode.nodeid

    #serial
    if [ "" != "$($TASSL_CMD version | grep 1.0.2)" ];
    then
        $TASSL_CMD x509  -text -in $ndpath/gmnode.crt | sed -n '5p' |  sed 's/://g' | tr "\n" " " | sed 's/ //g' | sed 's/[a-z]/\u&/g' | cat > $ndpath/gmnode.serial
    else
        $TASSL_CMD x509  -text -in $ndpath/gmnode.crt | sed -n '4p' |  sed 's/ //g' | sed 's/.*(0x//g' | sed 's/)//g' |sed 's/[a-z]/\u&/g' | cat > $ndpath/gmnode.serial
    fi

    cp $agpath/gmca.crt $agpath/gmagency.crt $ndpath
    cd $ndpath
}

generate_config_ini()
{
    local output=${1}
    local ip=${2}
    local offset=$(get_value ${ip//./}_count)
    local node_groups=(${3//,/ })
    local prefix=""
    if [ -n "$guomi_mode" ]; then
        prefix="gm"
    fi
    cat << EOF > ${output}
[rpc]
    listen_ip=${listen_ip}
    channel_listen_port=$(( offset + port_start[1] ))
    jsonrpc_listen_port=$(( offset + port_start[2] ))
[p2p]
    listen_ip=0.0.0.0
    listen_port=$(( offset + port_start[0] ))
    ;enable_compress=true
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
[compatibility]
    ; supported_version should nerver be changed
    supported_version=${compatibility_version}
[log]
    enable=true
    log_path=./log
    ; info debug trace 
    level=${log_level}
    ; MB
    max_log_file_size=200
    flush=${auto_flush}
    log_flush_threshold=100
EOF
}

generate_group_genesis()
{
    local output=$1
    local index=$2
    local node_list=$3
    cat << EOF > ${output} 
[consensus]
    ; consensus algorithm type, now support PBFT(consensus_type=pbft) and Raft(consensus_type=raft)
    consensus_type=${consensus_type}
    ; the max number of transactions of a block
    max_trans_num=1000
    ; the node id of consensusers
    ${node_list}
[state]
    ; support mpt/storage
    type=${state_type}
[tx]
    ; transaction gas limit
    gas_limit=300000000
[group]
    id=${index}
    timestamp=${timestamp}
EOF
}

function generate_group_ini()
{
    local output="${1}"
    cat << EOF > ${output}
[consensus]
    ; the ttl for broadcasting pbft message
    ;ttl=2
    ; min block generation time(ms), the max block generation time is 1000 ms
    ;min_block_generation_time=500
    ;enable_dynamic_block_size=true
[storage]
    ; storage db type, rocksdb / mysql / external, rocksdb is recommended
    type=${storage_type}
    ; max cache memeory, MB
    max_capacity=32
    max_forward_block=10
    ; only for external
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
[tx_execute]
    enable_parallel=${enable_parallel}
EOF
}

generate_cert_conf()
{
    local output=$1
    cat << EOF > ${output} 
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
    cat << EOF > ${output} 
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

x509_extensions	= usr_cert		# The extentions to add to the cert

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
x509_extensions	= v3_ca	# The extentions to add to the self signed cert

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
    if [ ! -z ${docker_mode} ];then
        ps_cmd="\$(docker ps |grep \${SHELL_FOLDER//\//} | grep -v grep|awk '{print \$1}')"
        start_cmd="docker run -d --rm --name \${SHELL_FOLDER//\//} -v \${SHELL_FOLDER}:/data --network=host -w=/data fiscoorg/fiscobcos:${docker_tag} -c config.ini"
        stop_cmd="docker kill \${node_pid} 2>/dev/null"
        pid="container id"
        log_cmd="tail -n20 \$(docker inspect --format='{{.LogPath}}' \${SHELL_FOLDER//\//})"
        check_success="success"
    fi
    cat << EOF >> "$output/start.sh"
fisco_bcos=\${SHELL_FOLDER}/../${bcos_bin_name}
cd \${SHELL_FOLDER}
node=\$(basename \${SHELL_FOLDER})
node_pid=${ps_cmd}
if [ ! -z \${node_pid} ];then
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
    if [[ ! -z \${node_pid} && ! -z "\${success_flag}" ]];then
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
fisco_bcos=\${SHELL_FOLDER}/../${bcos_bin_name}
node=\$(basename \${SHELL_FOLDER})
node_pid=${ps_cmd}
try_times=10
i=0
if [ -z \${node_pid} ];then
    echo " \${node} isn't running."
    exit 0
fi
[ ! -z \${node_pid} ] && ${stop_cmd} > /dev/null
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
fisco_bcos=\${NODE_FOLDER}/../${bcos_bin_name}
node=\$(basename \${NODE_FOLDER})
node_pid=${ps_cmd}
if [ ! -z \${node_pid} ];then
    echo "\${node} is trying to load new groups. Check log for more information."
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
fisco_bcos=\${NODE_FOLDER}/../${bcos_bin_name}
node=\$(basename \${NODE_FOLDER})
node_pid=${ps_cmd}
if [ ! -z \${node_pid} ];then
    echo "\${node} is trying to reset certificate whitelist. Check log for more information."
    touch config.ini.reset_certificate_whitelist
    exit 0
else 
    echo "\${node} is not running, use start.sh to start and enable whitelist directlly."
fi
EOF
}


genTransTest()
{
    local output=$1
    local file="${output}/.transTest.sh"
    generate_script_template "${file}"
    cat << EOF > "${file}"
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
}

parse_ip_config()
{
    local config=$1
    n=0
    while read line;do
        ip_array[n]=$(echo ${line} | awk '{print $1}')
        agency_array[n]=$(echo ${line} | awk '{print $2}')
        group_array[n]=$(echo ${line} | awk '{print $3}')
        if [ -z "${ip_array[$n]}" -o -z "${agency_array[$n]}" -o -z "${group_array[$n]}" ];then
            exit_with_clean "Please check ${config}, make sure there is no empty line!"
        fi
        ((++n))
    done < ${config}
}

download_bin()
{
    if [ "${x86_64_arch}" != "true" ];then exit_with_clean "We only offer x86_64 precompiled fisco-bcos binary, your OS architecture is not x86_64. Please compile from source."; fi
    bin_path=${output_dir}/${bcos_bin_name}
    package_name="fisco-bcos.tar.gz"
    [ ! -z "${macOS}" ] && package_name="fisco-bcos-macOS.tar.gz"
    [ ! -z "$guomi_mode" ] && package_name="fisco-bcos-gm.tar.gz"
    if [[ ! -z "$guomi_mode" && ! -z ${macOS} ]];then
        exit_with_clean "We don't provide binary of GuoMi on macOS. Please compile source code and use -e option to specific fisco-bcos binary path"
    fi
    Download_Link="https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/v${compatibility_version}/${package_name}"
    LOG_INFO "Downloading fisco-bcos binary from ${Download_Link} ..." 
    if [ $(curl -IL -o /dev/null -s -w %{http_code}  ${cdn_link_header}/v${compatibility_version}/${package_name}) == 200 ];then
        curl -LO ${Download_Link} --speed-time 20 --speed-limit 102400 -m ${download_timeout} || {
            LOG_INFO "Download speed is too low, try ${cdn_link_header}/v${compatibility_version}/${package_name}"
            curl -LO ${cdn_link_header}/v${compatibility_version}/${package_name}
        }
    else
        curl -LO ${Download_Link}
    fi
    tar -zxf ${package_name} && mv fisco-bcos ${bin_path} && rm ${package_name}
    chmod a+x ${bin_path}
}

check_bin()
{
    echo "Checking fisco-bcos binary..."
    bin_version=$(${bin_path} -v)
    if [ -z "$(echo ${bin_version} | grep 'FISCO-BCOS')" ];then
        exit_with_clean "${bin_path} is wrong. Please correct it and try again."
    fi
    if [[ ! -z ${guomi_mode} && -z $(echo ${bin_version} | grep 'gm') ]];then
        exit_with_clean "${bin_path} isn't gm version. Please correct it and try again."
    fi
    if [[ -z ${guomi_mode} && ! -z $(echo ${bin_version} | grep 'gm') ]];then
        exit_with_clean "${bin_path} isn't standard version. Please correct it and try again."
    fi
    echo "Binary check passed."
}

main()
{
output_dir="$(pwd)/${output_dir}"
[ -z $use_ip_param ] && help 'ERROR: Please set -l or -f option.'
if [ "${use_ip_param}" == "true" ];then
    ip_array=(${ip_param//,/ })
elif [ "${use_ip_param}" == "false" ];then
    if ! parse_ip_config $ip_file ;then 
        exit_with_clean "Parse $ip_file error!"
    fi
else 
    help 
fi


dir_must_not_exists ${output_dir}
mkdir -p "${output_dir}"

if [ -z "${compatibility_version}" ];then
    compatibility_version=$(curl -s https://api.github.com/repos/FISCO-BCOS/FISCO-BCOS/releases | grep "tag_name" | grep "\"v2\.[0-9]\.[0-9]\"" | sort -u | tail -n 1 | cut -d \" -f 4 | sed "s/^[vV]//")
fi
# in case network is broken
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
if [ -z ${CertConfig} ] || [ ! -e ${CertConfig} ];then
    # CertConfig="${output_dir}/cert.cnf"
    generate_cert_conf "cert.cnf"
else 
   cp ${CertConfig} .
fi

if [ "${use_ip_param}" == "true" ];then
    for i in $(seq 0 ${#ip_array[*]});do
        agency_array[i]="agency"
        group_array[i]=1
    done
fi

# prepare CA
echo "=============================================================="
if [ ! -e "$ca_file" ]; then
    echo "Generating CA key..."
    dir_must_not_exists ${output_dir}/chain
    gen_chain_cert ${output_dir}/chain >${logfile} 2>&1 || exit_with_clean "openssl error!"
    mv ${output_dir}/chain ${output_dir}/cert
    if [ "${use_ip_param}" == "false" ];then
        for agency_name in ${agency_array[*]};do
            if [ ! -d ${output_dir}/cert/${agency_name} ];then 
                gen_agency_cert ${output_dir}/cert ${output_dir}/cert/${agency_name} >${logfile} 2>&1
            fi
        done
    else
        gen_agency_cert ${output_dir}/cert ${output_dir}/cert/agency >${logfile} 2>&1
    fi
    ca_file="${output_dir}/cert/ca.key"
fi

if [ -n "$guomi_mode" ]; then
    check_and_install_tassl

    generate_cert_conf_gm "gmcert.cnf"

    echo "Generating Guomi CA key..."
    dir_must_not_exists ${output_dir}/gmchain
    gen_chain_cert_gm ${output_dir}/gmchain >${output_dir}/build.log 2>&1 || exit_with_clean "openssl error!"  #生成secp256k1算法的CA密钥
    mv ${output_dir}/gmchain ${output_dir}/gmcert
    gen_agency_cert_gm ${output_dir}/gmcert ${output_dir}/gmcert/agency >${output_dir}/build.log 2>&1
    ca_file="${output_dir}/gmcert/ca.key"    
fi


echo "=============================================================="
echo "Generating keys ..."
nodeid_list=""
ip_list=""
count=0
server_count=0
groups=
groups_count=
for line in ${ip_array[*]};do
    ip=${line%:*}
    num=${line#*:}
    if [ -z $(echo $ip | grep -E "^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$") ];then
        LOG_WARN "Please check IP address: ${ip}, if you use domain name please ignore this."
    fi
    [ "$num" == "$ip" ] || [ -z "${num}" ] && num=${node_num}
    echo "Processing IP:${ip} Total:${num} Agency:${agency_array[${server_count}]} Groups:${group_array[server_count]}"
    [ -z "$(get_value ${ip//./}_count)" ] && set_value ${ip//./}_count 0
    for ((i=0;i<num;++i));do
        echo "Processing IP:${ip} ID:${i} node's key" >> ${logfile}
        local node_coount=$(get_value ${ip//./}_count)
        node_dir="${output_dir}/${ip}/node${node_coount}"
        [ -d "${node_dir}" ] && exit_with_clean "${node_dir} exist! Please delete!"
        
        while :
        do
            gen_cert ${output_dir}/cert/${agency_array[${server_count}]} ${node_dir} "node" >${logfile} 2>&1
            mkdir -p ${conf_path}/
            mv *.* ${conf_path}/

            #private key should not start with 00
            cd ${output_dir}
            privateKey=$(openssl ec -in "${node_dir}/${conf_path}/node.key" -text 2> /dev/null| sed -n '3,5p' | sed 's/://g'| tr "\n" " "|sed 's/ //g')
            len=${#privateKey}
            head2=${privateKey:0:2}
            if [ "64" != "${len}" ] || [ "00" == "$head2" ];then
                rm -rf ${node_dir}
                continue;
            fi

            if [ -n "$guomi_mode" ]; then
                gen_node_cert_gm ${output_dir}/gmcert/agency ${node_dir} >${output_dir}/build.log 2>&1
                mkdir -p ${gm_conf_path}/
                mv ./*.* ${gm_conf_path}/

                #private key should not start with 00
                cd ${output_dir}
                privateKey=$($TASSL_CMD ec -in "${node_dir}/${gm_conf_path}/gmnode.key" -text 2> /dev/null| sed -n '3,5p' | sed 's/://g'| tr "\n" " "|sed 's/ //g')
                len=${#privateKey}
                head2=${privateKey:0:2}
                if [ "64" != "${len}" ] || [ "00" == "$head2" ];then
                    rm -rf ${node_dir}
                    continue;
                fi
            fi
            break;
        done

        if [ -n "$guomi_mode" ]; then
            cat ${output_dir}/gmcert/agency/gmagency.crt >> ${node_dir}/${gm_conf_path}/gmnode.crt
            cat ${output_dir}/gmcert/gmca.crt >> ${node_dir}/${gm_conf_path}/gmnode.crt

            #move origin conf to gm conf
            rm ${node_dir}/${conf_path}/node.nodeid
            cp ${node_dir}/${conf_path} ${node_dir}/${gm_conf_path}/origin_cert -r
        fi

        if [ -n "$guomi_mode" ]; then
            nodeid="$(cat ${node_dir}/${gm_conf_path}/gmnode.nodeid)"
        else
            nodeid="$(cat ${node_dir}/${conf_path}/node.nodeid)"
        fi

        if [ -n "$guomi_mode" ]; then
            #remove original cert files
            rm ${node_dir:?}/${conf_path} -rf
            mv ${node_dir}/${gm_conf_path} ${node_dir}/${conf_path}
        fi


        if [ "${use_ip_param}" == "false" ];then
            node_groups=(${group_array[server_count]//,/ })
            for j in ${node_groups[@]};do
                if [ -z "${groups_count[${j}]}" ];then groups_count[${j}]=0;fi
                echo "groups_count[${j}]=${groups_count[${j}]}"  >> ${logfile}
        groups[${j}]=$"${groups[${j}]}node.${groups_count[${j}]}=${nodeid}
    "
                ((++groups_count[j]))
            done
        else
        nodeid_list=$"${nodeid_list}node.${count}=${nodeid}
    "
        fi
        
        ip_list=$"${ip_list}node.${count}="${ip}:$(( $(get_value ${ip//./}_count) + port_start[0] ))"
    "
        set_value ${ip//./}_count $(( $(get_value ${ip//./}_count) + 1 ))
        ((++count))
    done
    sdk_path="${output_dir}/${ip}/sdk"
    if [ ! -d ${sdk_path} ];then
        gen_cert ${output_dir}/cert/${agency_array[${server_count}]} "${sdk_path}" "sdk" >${logfile} 2>&1
        # FIXME: delete the below unbelievable ugliest operation in future
        cp sdk.crt node.crt
        cp sdk.key node.key
        # FIXME: delete the upside unbelievable ugliest operation in future
        rm node.nodeid
        cp ${output_dir}/cert/ca.crt ${sdk_path}/
        cd ${output_dir}
    fi
    ((++server_count))
done 

# clean 
for line in ${ip_array[*]};do
    ip=${line%:*}
    set_value ${ip//./}_count 0
done

echo "=============================================================="
echo "Generating configurations..."
cd ${current_dir}
server_count=0
for line in ${ip_array[*]};do
    ip=${line%:*}
    num=${line#*:}
    [ "$num" == "$ip" ] || [ -z "${num}" ] && num=${node_num}
    echo "Processing IP:${ip} Total:${num} Agency:${agency_array[${server_count}]} Groups:${group_array[server_count]}"
    for ((i=0;i<num;++i));do
        echo "Processing IP:${ip} ID:${i} config files..." >> ${logfile}
        local node_coount=$(get_value ${ip//./}_count)
        node_dir="${output_dir}/${ip}/node${node_coount}"
        generate_config_ini "${node_dir}/config.ini" ${ip} "${group_array[server_count]}"
        if [ "${use_ip_param}" == "false" ];then
            node_groups=(${group_array[${server_count}]//,/ })
            for j in ${node_groups[@]};do
                generate_group_genesis "$node_dir/${conf_path}/group.${j}.genesis" "${j}" "${groups[${j}]}"
                generate_group_ini "$node_dir/${conf_path}/group.${j}.ini"
            done
        else
            generate_group_genesis "$node_dir/${conf_path}/group.1.genesis" "1" "${nodeid_list}"
            generate_group_ini "$node_dir/${conf_path}/group.1.ini"
        fi
        generate_node_scripts "${node_dir}"
        set_value ${ip//./}_count $(( $(get_value ${ip//./}_count) + 1 ))
    done
    generate_server_scripts "${output_dir}/${ip}"
    if [ -z ${docker_mode} ];then cp "$bin_path" "${output_dir}/${ip}/fisco-bcos"; fi
    if [ -n "$make_tar" ];then cd ${output_dir} && tar zcf "${ip}.tar.gz" "${ip}" && cd ${current_dir};fi
    ((++server_count))
done 
rm ${logfile}
if [ -f "${output_dir}/${bcos_bin_name}" ];then rm ${output_dir}/${bcos_bin_name};fi 
if [ "${use_ip_param}" == "false" ];then
echo "=============================================================="
    for l in $(seq 0 ${#groups_count[@]});do
        if [ ! -z "${groups_count[${l}]}" ];then echo "Group:${l} has ${groups_count[${l}]} nodes";fi
    done
fi

}

check_env
parse_params $@
main
print_result
