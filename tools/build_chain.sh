#!/bin/bash

set -e

ca_file= #CA key
node_num=1 
ip_file=
agency_array=
group_array=
ip_param=
use_ip_param=
ip_array=
output_dir=nodes
port_start=(30300 20200 8545)
state_type=storage 
storage_type=LevelDB
conf_path="conf"
bin_path=
make_tar=
debug_log="false"
log_level="info"
logfile=build.log
listen_ip="127.0.0.1"
Download=false
bcos_bin_name=fisco-bcos
guomi_mode=
gm_conf_path="gmconf/"
CUR_DIR=$(pwd)
consensus_type="pbft"
TASSL_CMD="${HOME}"/.tassl
auto_flush="true"


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
    -c <Consensus Algorithm>            Default PBFT. If set -c, use Raft
    -s <State type>                     Default storage. if set -s, use mpt 
    -g <Generate guomi nodes>           Default no
    -z <Generate tar packet>            Default no
    -t <Cert config file>               Default auto generate
    -T <Enable debug log>               Default off. If set -T, enable debug log
    -d <Disable log auto flush>         Default on. If set -d, disable log auto flush
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

parse_params()
{
while getopts "f:l:o:p:e:t:icszhgTd" option;do
    case $option in
    f) ip_file=$OPTARG
       use_ip_param="false"
    ;;
    l) ip_param=$OPTARG
       use_ip_param="true"
    ;;
    o) output_dir=$OPTARG;;
    i) listen_ip="0.0.0.0";;
    p) port_start=(${OPTARG//,/ })
    if [ ${#port_start[@]} -ne 3 ];then LOG_WARN "start port error. e.g: 30300,20200,8545" && exit 1;fi
    ;;
    e) bin_path=$OPTARG;;
    s) state_type=mpt;;
    t) CertConfig=$OPTARG;;
    c) consensus_type="raft";;
    T) debug_log="true"
    log_level="debug"
    ;;
    d) auto_flush="false";;
    z) make_tar="yes";;
    g) guomi_mode="yes";;
    h) help;;
    esac
done
}

print_result()
{
echo "=============================================================="
LOG_INFO "FISCO-BCOS Path   : $bin_path"
[ ! -z $ip_file ] && LOG_INFO "IP List File      : $ip_file"
# [ ! -z $ip_file ] && LOG_INFO -e "Agencies/groups : ${#agency_array[@]}/${#groups[@]}"
LOG_INFO "Start Port        : ${port_start[*]}"
LOG_INFO "Server IP         : ${ip_array[*]}"
LOG_INFO "State Type        : ${state_type}"
LOG_INFO "RPC listen IP     : ${listen_ip}"
[ ! -z ${pkcs12_passwd} ] && LOG_INFO "SDK PKCS12 Passwd : ${pkcs12_passwd}"
LOG_INFO "Output Dir        : $output_dir"
LOG_INFO "CA Key Path       : $ca_file"
[ ! -z $guomi_mode ] && LOG_INFO "Guomi mode        : $guomi_mode"
echo "=============================================================="
LOG_INFO "All completed. Files in $output_dir"
}

fail_message()
{
    echo $1
    false
}

EXIT_CODE=-1

check_env() {
    [ ! -z "$(openssl version | grep 1.0.2)" ] || [ ! -z "$(openssl version | grep 1.1)" ] || [ ! -z "$(openssl version | grep reSSL)" ] || {
        echo "please install openssl 1.0.2k-fips!"
        #echo "download openssl from https://www.openssl.org."
        echo "use \"openssl version\" command to check."
        exit $EXIT_CODE
    }
    if [ ! -z "$(openssl version | grep reSSL)" ];then
        export PATH="/usr/local/opt/openssl/bin:$PATH"
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

getname() {
    local name="$1"
    if [ -z "$name" ]; then
        return 0
    fi
    [[ "$name" =~ ^.*/$ ]] && {
        name="${name%/*}"
    }
    name="${name##*/}"
    echo "$name"
}

check_name() {
    local name="$1"
    local value="$2"
    [[ "$value" =~ ^[a-zA-Z0-9._-]+$ ]] || {
        echo "$name name [$value] invalid, it should match regex: ^[a-zA-Z0-9._-]+\$"
        exit $EXIT_CODE
    }
}

file_must_exists() {
    if [ ! -f "$1" ]; then
        echo "$1 file does not exist, please check!"
        exit $EXIT_CODE
    fi
}

dir_must_exists() {
    if [ ! -d "$1" ]; then
        echo "$1 DIR does not exist, please check!"
        exit $EXIT_CODE
    fi
}

dir_must_not_exists() {
    if [ -e "$1" ]; then
        echo "$1 DIR exists, please clean old DIR!"
        exit $EXIT_CODE
    fi
}

gen_chain_cert() {
    path="$2"
    name=`getname "$path"`
    echo "$path --- $name"
    dir_must_not_exists "$path"
    check_name chain "$name"
    
    chaindir=$path
    mkdir -p $chaindir
    openssl genrsa -out $chaindir/ca.key 2048
    openssl req -new -x509 -days 3650 -subj "/CN=$name/O=fisco-bcos/OU=chain" -key $chaindir/ca.key -out $chaindir/ca.crt
    cp cert.cnf $chaindir

    if [ $? -eq 0 ]; then
        echo "build chain ca succussful!"
    else
        echo "please input at least Common Name!"
    fi
}

gen_agency_cert() {
    chain="$2"
    agencypath="$3"
    name=$(getname "$agencypath")

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
    cp $chain/ca.crt $agencydir/ca-agency.crt
    more $agencydir/agency.crt | cat >>$agencydir/ca-agency.crt
    rm -f $agencydir/agency.csr

    echo "build $name agency cert successful!"
}

gen_cert_secp256k1() {
    capath="$1"
    certpath="$2"
    name="$3"
    type="$4"
    openssl ecparam -out $certpath/${type}.param -name secp256k1
    openssl genpkey -paramfile $certpath/${type}.param -out $certpath/${type}.key
    openssl pkey -in $certpath/${type}.key -pubout -out $certpath/${type}.pubkey
    openssl req -new -sha256 -subj "/CN=${name}/O=fisco-bcos/OU=${type}" -key $certpath/${type}.key -config $capath/cert.cnf -out $certpath/${type}.csr
    openssl x509 -req -days 3650 -sha256 -in $certpath/${type}.csr -CAkey $capath/agency.key -CA $capath/agency.crt\
        -force_pubkey $certpath/${type}.pubkey -out $certpath/${type}.crt -CAcreateserial -extensions v3_req -extfile $capath/cert.cnf
    openssl ec -in $certpath/${type}.key -outform DER | tail -c +8 | head -c 32 | xxd -p -c 32 | cat >$certpath/${type}.private
    rm -f $certpath/${type}.csr
}

gen_node_cert() {
    if [ "" == "`openssl ecparam -list_curves 2>&1 | grep secp256k1`" ]; then
        echo "openssl don't support secp256k1, please upgrade openssl!"
        exit $EXIT_CODE
    fi

    agpath="$2"
    agency=$(getname "$agpath")
    ndpath="$3"
    node=$(getname "$ndpath")
    dir_must_exists "$agpath"
    file_must_exists "$agpath/agency.key"
    check_name agency "$agency"
    dir_must_not_exists "$ndpath"	
    check_name node "$node"

    mkdir -p $ndpath

    gen_cert_secp256k1 "$agpath" "$ndpath" "$node" node
    #nodeid is pubkey
    openssl ec -in $ndpath/node.key -text -noout | sed -n '7,11p' | tr -d ": \n" | awk '{print substr($0,3);}' | cat >$ndpath/node.nodeid
    # openssl x509 -serial -noout -in $ndpath/node.crt | awk -F= '{print $2}' | cat >$ndpath/node.serial
    cp $agpath/ca.crt $agpath/agency.crt $ndpath

    cd $ndpath

    echo "build $node node cert successful!"
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
    path="$2"
    name=$(getname "$path")
    echo "$path --- $name"
    dir_must_not_exists "$path"
    check_name chain "$name"

    chaindir=$path
    mkdir -p $chaindir

    generate_gmsm2_param "gmsm2.param"
	$TASSL_CMD genpkey -paramfile gmsm2.param -out $chaindir/gmca.key
	$TASSL_CMD req -config gmcert.cnf -x509 -days 3650 -subj "/CN=$name/O=fiscobcos/OU=chain" -key $chaindir/gmca.key -extensions v3_ca -out $chaindir/gmca.crt

    ls $chaindir

    cp gmcert.cnf gmsm2.param $chaindir

    if $(cp gmcert.cnf gmsm2.param $chaindir)
    then
        echo "build chain ca succussful!"
    else
        echo "please input at least Common Name!"
    fi
}

gen_agency_cert_gm() {
    chain="$2"
    agencypath="$3"
    name=$(getname "$agencypath")

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
    cp $chain/gmca.crt $agencydir/ca-agency.crt
    more $agencydir/gmagency.crt | cat >>$agencydir/ca-agency.crt
    rm -f $agencydir/gmagency.csr

    echo "build $name agency cert successful!"
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
        echo "openssl don't support secp256k1, please upgrade openssl!"
        exit $EXIT_CODE
    fi

    agpath="$2"
    agency=$(getname "$agpath")
    ndpath="$3"
    node=$(getname "$ndpath")
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

    echo "build $node node cert successful!"
}

generate_config_ini()
{
    local output=${1}
    local ip=${2}
    local offset=${ip_node_counts[${ip//./}]}
    local node_groups=(${3//,/ })
    local prefix=""
    if [ -n "$guomi_mode" ]; then
        prefix="gm"
    fi
    cat << EOF > ${output}
[rpc]
    ;rpc listen ip
    listen_ip=${listen_ip}
    ;channelserver listen port
    channel_listen_port=$(( offset + port_start[1] ))
    ;jsonrpc listen port
    jsonrpc_listen_port=$(( offset + port_start[2] ))
[p2p]
    ;p2p listen ip
    listen_ip=0.0.0.0
    ;p2p listen port
    listen_port=$(( offset + port_start[0] ))
    ;nodes to connect
    $ip_list
;certificate rejected list		
[crl]		
    ;crl.0 should be nodeid, nodeid's length is 128 
    ;crl.0=

;group configurations
;WARNING: group 0 is forbided
[group]
    group_data_path=data/
    group_config_path=${conf_path}/

;certificate configuration
[secure]
    ;directory the certificates located in
    data_path=${conf_path}/
    ;the node private key file
    key=${prefix}node.key
    ;the node certificate file
    cert=${prefix}node.crt
    ;the ca certificate file
    ca_cert=${prefix}ca.crt

;log configurations
[log]
    ;the directory of the log
    log_path=./log
    ;info debug trace 
    level=${log_level}
    max_log_file_size=209715200
    ; control log auto_flush
    flush=${auto_flush}
    ;easylog config
    format=%level|%datetime{%Y-%M-%d %H:%m:%s:%g}|%msg
    log_flush_threshold=100
EOF
}

generate_group_genesis()
{
    local output=$1
    local index=$2
    local node_list=$3
    cat << EOF > ${output} 
;consensus configuration
[consensus]
    ;consensus algorithm type, now support PBFT(consensus_type=pbft) and Raft(consensus_type=raft)
    consensus_type=${consensus_type}
    ;the max number of transactions of a block
    max_trans_num=1000
    ;the node id of leaders
    ${node_list}

[storage]
    ;storage db type, now support leveldb 
    type=${storage_type}
[state]
    ;support mpt/storage
    type=${state_type}

;tx gas limit
[tx]
    gas_limit=300000000
[group]
    index=${index}
EOF
}

function generate_group_ini()
{
    local output="${1}"
    cat << EOF > ${output}
; the ttl for broadcasting pbft message
[consensus]
    ;ttl=2
;txpool limit
[tx_pool]
    limit=10000
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
    cat << EOF > "${filepath}"
#!/bin/bash
SHELL_FOLDER=\$(cd \$(dirname \$0);pwd)

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
organizationalUnitName_default = webank
commonName =  Organizational  commonName (eg, webank)
commonName_default =  webank
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
    generate_script_template "$output/start.sh"
    cat << EOF >> "$output/start.sh"
fisco_bcos=\${SHELL_FOLDER}/../${bcos_bin_name}
cd \${SHELL_FOLDER}
node=\$(basename \${SHELL_FOLDER})
node_pid=\`ps aux|grep "\${fisco_bcos}"|grep -v grep|awk '{print \$2}'\`
if [ ! -z \${node_pid} ];then
    echo " \${node} is running, pid is \$node_pid."
else 
    nohup \${fisco_bcos} -c config.ini&
fi
EOF
    generate_script_template "$output/stop.sh"
    cat << EOF >> "$output/stop.sh"
fisco_bcos=\${SHELL_FOLDER}/../${bcos_bin_name}
node=\$(basename \${SHELL_FOLDER})
node_pid=\`ps aux|grep "\${fisco_bcos}"|grep -v grep|awk '{print \$2}'\`
try_times=2
i=0
while [ \$i -lt \${try_times} ]
do
    kill \${node_pid}
    sleep 1
    node_pid=\`ps aux|grep "\${fisco_bcos}"|grep -v grep|awk '{print \$2}'\`
    if [ -z \${node_pid} ];then
        echo " stop \${node} success. "
        exit 0
    fi
    ((i=i+1))
done

EOF
}


genTransTest()
{
    local output=$1
    local file="${output}/transTest.sh"
    generate_script_template "${file}"
    cat << EOF > "${file}"
# This script only support for block number smaller than 65535 - 256

ip_port=http://127.0.0.1:$(( port_start[2] ))
trans_num=1
target_group=1
if [ \$# -ge 1 ];then
    trans_num=\$1
fi
if [ \$# -ge 2 ];then
    target_group=\$2
fi

block_limit()
{
    result=\`curl -s -X POST --data '{"jsonrpc":"2.0","method":"getBlockNumber","params":['\${target_group}'],"id":83}' \$1\`
    if [ \`echo \${result} | grep -i failed | wc -l\` -gt 0 ] || [ -z \${result} ];then
        echo "getBlockNumber error!"
        exit 1
    fi
    blockNumber=\`echo \${result}| cut -d \" -f 10\`
    printf "%04x" \$((\$blockNumber+0x100))
}

send_a_tx()
{
    limit=\$(block_limit \$1)
    random_id="\$(date +%s)\$(printf "%09d" \${RANDOM})"
    if [ \${#limit} -gt 4 ];then echo "blockLimit exceed 0xffff, this scripts is unavailable!"; exit 0;fi
    txBytes="f8f0a02ade583745343a8f9a70b40db996fbe69c63531832858\${random_id}85174876e7ff8609184e729fff82\${limit}94d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7f7395028658d0e01b86a371ca0e33891be86f781ebacdafd543b9f4f98243f7b52d52bac9efa24b89e257a354da07ff477eb0ba5c519293112f1704de86bd2938369fbf0db2dff3b4d9723b9a87d"
    #echo \$txBytes
    curl -s -X POST --data '{"jsonrpc":"2.0","method":"sendRawTransaction","params":['\${target_group}', "'\$txBytes'"],"id":83}' \$1
}

send_many_tx()
{
    for j in \$(seq 1 \$1)
    do
        echo 'Send transaction: ' \$j
        send_a_tx \${ip_port} 
    done
}

send_many_tx \${trans_num}

EOF
}

generate_server_scripts()
{
    local output=$1
    genTransTest "${output}"
    generate_script_template "$output/start_all.sh"
    # echo "ip_array=(\$(ifconfig | grep inet | grep -v inet6 | awk '{print \$2}'))"  >> "$output/start_all.sh"
    # echo "if echo \${ip_array[@]} | grep -w \"${ip}\" &>/dev/null; then echo \"start node_${ip}_${i}\" && bash \${SHELL_FOLDER}/node_${ip}_${i}/start.sh; fi" >> "$output_dir/start_all.sh"
    cat << EOF >> "$output/start_all.sh"
for directory in \`ls \${SHELL_FOLDER}\`  
do  
    if [[ -d "\${SHELL_FOLDER}/\${directory}" && -f "\${SHELL_FOLDER}/\${directory}/start.sh" ]];then  
        echo "start \${directory}" && bash \${SHELL_FOLDER}/\${directory}/start.sh
    fi  
done  
EOF
    generate_script_template "$output/stop_all.sh"
    cat << EOF >> "$output/stop_all.sh"
for directory in \`ls \${SHELL_FOLDER}\`  
do  
    if [[ -d "\${SHELL_FOLDER}/\${directory}" && -f "\${SHELL_FOLDER}/\${directory}/stop.sh" ]];then  
        echo "stop \${directory}" && bash \${SHELL_FOLDER}/\${directory}/stop.sh
    fi  
done  
EOF
}

parse_ip_config()
{
    local config=$1
    n=0
    while read line;do
        ip_array[n]=`echo ${line} | cut -d ' ' -f 1`
        agency_array[n]=`echo ${line} | cut -d ' ' -f 2`
        group_array[n]=`echo ${line} | cut -d ' ' -f 3`
        if [ -z "${ip_array[$n]}" -o -z "${agency_array[$n]}" -o -z "${group_array[$n]}" ];then
            LOG_WARN "Please check ${config}, make sure there is no empty line!"
            return -1
        fi
        ((++n))
    done < ${config}
}

main()
{
output_dir="`pwd`/${output_dir}"
[ -z $use_ip_param ] && help 'ERROR: Please set -l or -f option.'
if [ "${use_ip_param}" == "true" ];then
    ip_array=(${ip_param//,/ })
elif [ "${use_ip_param}" == "false" ];then
    parse_ip_config $ip_file
    if [ $? -ne 0 ];then 
        echo "Parse $ip_file error!"
        exit 1
    fi
else 
    help 
fi

if [ -z ${bin_path} ];then
    bin_path=${output_dir}/${bcos_bin_name}
    Download=true
fi

dir_must_not_exists $output_dir
mkdir -p "$output_dir"

if [ "${Download}" == "true" ];then
    package_name="fisco-bcos.tar.gz"
    [ ! -z "$guomi_mode" ] && package_name="fisco-bcos-gm.tar.gz"
    version=$(curl -s https://raw.githubusercontent.com/FISCO-BCOS/FISCO-BCOS/master/release_note.txt | sed "s/^[vV]//")
    Download_Link="https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/v${version}/${package_name}"
    LOG_INFO "Downloading fisco-bcos binary from ${Download_Link} ..." 
    curl -LO ${Download_Link}
    tar -zxf ${package_name} && mv fisco-bcos ${bin_path} && rm ${package_name}
    chmod a+x ${bin_path}
fi

if [ -z ${CertConfig} ] || [ ! -e ${CertConfig} ];then
    # CertConfig="$output_dir/cert.cnf"
    generate_cert_conf "cert.cnf"
else 
   cp ${CertConfig} .
fi

if [ "${use_ip_param}" == "true" ];then
    for i in `seq 0 ${#ip_array[*]}`;do
        agency_array[i]="agency"
        group_array[i]=1
    done
fi

# prepare CA
if [ ! -e "$ca_file" ]; then
    echo "Generating CA key..."
    dir_must_not_exists $output_dir/chain
    gen_chain_cert "" $output_dir/chain >$output_dir/${logfile} 2>&1 || fail_message "openssl error!"
    mv $output_dir/chain $output_dir/cert
    if [ "${use_ip_param}" == "false" ];then
        for agency_name in ${agency_array[*]};do
            if [ ! -d $output_dir/cert/${agency_name} ];then 
                gen_agency_cert "" $output_dir/cert $output_dir/cert/${agency_name} >$output_dir/${logfile} 2>&1
            fi
        done
    else
        gen_agency_cert "" $output_dir/cert $output_dir/cert/agency >$output_dir/${logfile} 2>&1
    fi
    ca_file="$output_dir/cert/ca.key"
fi

if [ -n "$guomi_mode" ]; then
    check_and_install_tassl

    generate_cert_conf_gm "gmcert.cnf"

    echo "Generating Guomi CA key..."
    dir_must_not_exists $output_dir/gmchain
    gen_chain_cert_gm "" $output_dir/gmchain >$output_dir/build.log 2>&1 || fail_message "openssl error!"  #生成secp256k1算法的CA密钥
    mv $output_dir/gmchain $output_dir/gmcert
    gen_agency_cert_gm "" $output_dir/gmcert $output_dir/gmcert/agency >$output_dir/build.log 2>&1
    ca_file="$output_dir/gmcert/ca.key"    
fi


echo "=============================================================="
echo "Generating keys ..."
nodeid_list=""
ip_list=""
count=0
server_count=0
groups=
ip_node_counts=
groups_count=
for line in ${ip_array[*]};do
    ip=${line%:*}
    num=${line#*:}
    checkIP=$(echo $ip|grep -E "^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$")
    if [ -z "${checkIP}" ];then
        LOG_WARN "Please check IP address: ${ip}"
    fi
    [ "$num" == "$ip" -o -z "${num}" ] && num=${node_num}
    echo "Processing IP:${ip} Total:${num} Agency:${agency_array[${server_count}]} Groups:${group_array[server_count]}"
    [ -z "${ip_node_counts[${ip//./}]}" ] && ip_node_counts[${ip//./}]=0
    for ((i=0;i<num;++i));do
        echo "Processing IP:${ip} ID:${i} node's key" >> $output_dir/${logfile}
        node_dir="$output_dir/${ip}/node${ip_node_counts[${ip//./}]}"
        [ -d "${node_dir}" ] && echo "${node_dir} exist! Please delete!" && exit 1
        
        while :
        do
            gen_node_cert "" ${output_dir}/cert/${agency_array[${server_count}]} ${node_dir} >$output_dir/${logfile} 2>&1
            mkdir -p ${conf_path}/
            rm node.param node.private node.pubkey agency.crt
            mv *.* ${conf_path}/

            #private key should not start with 00
            cd $output_dir
            privateKey=$(openssl ec -in "${node_dir}/${conf_path}/node.key" -text 2> /dev/null| sed -n '3,5p' | sed 's/://g'| tr "\n" " "|sed 's/ //g')
            len=${#privateKey}
            head2=${privateKey:0:2}
            if [ "64" != "${len}" ] || [ "00" == "$head2" ];then
                rm -rf ${node_dir}
                continue;
            fi

            if [ -n "$guomi_mode" ]; then
                gen_node_cert_gm "" ${output_dir}/gmcert/agency ${node_dir} >$output_dir/build.log 2>&1
                mkdir -p ${gm_conf_path}/
                mv ./*.* ${gm_conf_path}/

                #private key should not start with 00
                cd $output_dir
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
        cat ${output_dir}/cert/${agency_array[${server_count}]}/agency.crt >> ${node_dir}/${conf_path}/node.crt

        if [ -n "$guomi_mode" ]; then
            cat ${output_dir}/gmcert/agency/gmagency.crt >> ${node_dir}/${gm_conf_path}/gmnode.crt
            cat ${output_dir}/gmcert/gmca.crt >> ${node_dir}/${gm_conf_path}/gmnode.crt

            #move origin conf to gm conf
            rm ${node_dir}/${conf_path}/node.nodeid
            cp ${node_dir}/${conf_path} ${node_dir}/${gm_conf_path}/origin_cert -r
        fi

        if [ -n "$guomi_mode" ]; then
            nodeid=$($TASSL_CMD ec -in "${node_dir}/${gm_conf_path}/gmnode.key" -text 2> /dev/null | perl -ne '$. > 6 and $. < 12 and ~s/[\n:\s]//g and print' | perl -ne 'print substr($_, 2)."\n"')
        else
            nodeid=$(openssl ec -in "${node_dir}/${conf_path}/node.key" -text 2> /dev/null | perl -ne '$. > 6 and $. < 12 and ~s/[\n:\s]//g and print' | perl -ne 'print substr($_, 2)."\n"')
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
                echo "groups_count[${j}]=${groups_count[${j}]}"  >> $output_dir/${logfile}
        groups[${j}]=$"${groups[${j}]}node.${groups_count[${j}]}=${nodeid}
    "
                ((++groups_count[j]))
            done
        else
        nodeid_list=$"${nodeid_list}node.${count}=${nodeid}
    "
        fi
        
        ip_list=$"${ip_list}node.${count}="${ip}:$(( ${ip_node_counts[${ip//./}]} + port_start[0] ))"
    "
        ip_node_counts[${ip//./}]=$(( ${ip_node_counts[${ip//./}]} + 1 ))
        ((++count))
    done
    sdk_path="$output_dir/${ip}/sdk"
    if [ ! -d ${sdk_path} ];then
        gen_node_cert "" ${output_dir}/cert/${agency_array[${server_count}]} "${sdk_path}">$output_dir/${logfile} 2>&1
        cat ${output_dir}/cert/${agency_array[${server_count}]}/agency.crt >> node.crt
        rm node.param node.private node.pubkey node.nodeid agency.crt
        cp ${output_dir}/cert/ca.crt ${sdk_path}/
        cd $output_dir
    fi
    ((++server_count))
done 
cd ..

ip_node_counts=()
echo "=============================================================="
echo "Generating configurations..."
server_count=0
for line in ${ip_array[*]};do
    ip=${line%:*}
    num=${line#*:}
    [ "$num" == "$ip" -o -z "${num}" ] && num=${node_num}
    [ -z "${ip_node_counts[${ip//./}]}" ] && ip_node_counts[${ip//./}]=0
    echo "Processing IP:${ip} Total:${num} Agency:${agency_array[${server_count}]} Groups:${group_array[server_count]}"
    for ((i=0;i<num;++i));do
        echo "Processing IP:${ip} ID:${i} config files..." >> $output_dir/${logfile}
        node_dir="$output_dir/${ip}/node${ip_node_counts[${ip//./}]}"
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
        ip_node_counts[${ip//./}]=$(( ${ip_node_counts[${ip//./}]} + 1 ))
    done
    generate_server_scripts "$output_dir/${ip}"
    cp "$bin_path" "$output_dir/${ip}/fisco-bcos"
    [ -n "$make_tar" ] && tar zcf "$output_dir/${ip}.tar.gz" "$output_dir/${ip}"
    ((++server_count))
done 
rm $output_dir/${logfile} #cert.cnf
if [ "${use_ip_param}" == "false" ];then
echo "=============================================================="
    for l in `seq 0 ${#groups_count[@]}`;do
        if [ ! -z "${groups_count[${l}]}" ];then echo "Group:${l} has ${groups_count[${l}]} nodes";fi
    done
fi

}

check_env
parse_params $@
main
print_result
