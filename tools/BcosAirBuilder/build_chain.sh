#!/bin/bash
set -e

dirpath="$(cd "$(dirname "$0")" && pwd)"
default_listen_ip="0.0.0.0"
port_start=(30300 20200 3901)
p2p_listen_port=port_start[0]
rpc_listen_port=port_start[1]
mtail_listen_port=3901
use_ip_param=
mtail_ip_param=""
ip_array=
output_dir="./nodes"
current_dir=$(pwd)
binary_name="fisco-bcos"
lightnode_binary_name="fisco-bcos-lightnode"
mtail_binary_name="mtail"
key_page_size=10240
# for cert generation
ca_cert_dir="${dirpath}"
sm_cert_conf='sm_cert.cnf'
cert_conf="${output_dir}/cert.cnf"
days=36500
rsa_key_length=2048
sm_mode='false'
enable_hsm='false'
macOS=""
x86_64_arch="false"
arm64_arch="false"
sm2_params="sm_sm2.param"
cdn_link_header="https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS"
OPENSSL_CMD="${HOME}/.fisco/tassl-1.1.1b"
nodeid_list=""
nodeid_list_from_path=""
file_dir="./"
p2p_connected_conf_name="nodes.json"
command="deploy"
ca_dir=""
prometheus_dir=""
config_path=""
docker_mode=
default_version="v3.12.2"
compatibility_version=${default_version}
default_mtail_version="3.0.0-rc49"
compatibility_mtail_version=${default_mtail_version}
auth_mode="false"
monitor_mode="false"
auth_admin_account=
binary_path=""
lightnode_binary_path=""
download_lightnode_binary="false"
mtail_binary_path=""
wasm_mode="false"
serial_mode="true"
node_key_dir=""
# if the config.genesis path has been set, don't generate genesis file, use the config instead
genesis_conf_path=""
lightnode_flag="false"
download_timeout=240
make_tar=
default_group="group0"
default_chainid="chain0"
default_web3_chainid="20200"
use_ipv6=""
# for modifying multipy ca node
modify_node_path=""
multi_ca_path=""
consensus_type="pbft"
supported_consensus=(pbft rpbft)
log_level="info"

# for pro or max default setting
bcos_builder_package=BcosBuilder.tgz
bcos_builder_version=v3.12.2
use_exist_binary="false"
download_specific_binary_flag="false"
download_service_binary_type="cdn"
service_binary_version="v3.12.2"
download_service_binary_path="binary"
download_service_binary_path_flag="false"
service_type="all"
chain_version="air"
service_output_dir="generated"
proOrmax_port_start=(30300 20200 40400 2379 3901)
isPortSpecified="false"
tars_listen_port_space=5

#for pro or max expand
expand_dir="expand"

LOG_WARN() {
    local content=${1}
    echo -e "\033[31m[ERROR] ${content}\033[0m"
}

LOG_INFO() {
    local content=${1}
    echo -e "\033[32m[INFO] ${content}\033[0m"
}

LOG_FATAL() {
    local content=${1}
    echo -e "\033[31m[FATAL] ${content}\033[0m"
    exit 1
}

dir_must_exists() {
    if [ ! -d "$1" ]; then
        LOG_FATAL "$1 DIR does not exist, please check!"
    fi
}

dir_must_not_exists() {
    if [  -d "$1" ]; then
        LOG_FATAL "$1 DIR already exist, please check!"
    fi
}

file_must_not_exists() {
    if [ -f "$1" ]; then
        LOG_FATAL "$1 file already exist, please check!"
    fi
}

file_must_exists() {
    if [ ! -f "$1" ]; then
        LOG_FATAL "$1 file does not exist, please check!"
    fi
}

check_env() {
    if [ "$(uname)" == "Darwin" ];then
        macOS="macOS"
    elif [ "$(uname -m)" == "x86_64" ];then
        x86_64_arch="true"
    elif [ "$(uname -m)" == "arm64" ];then
        arm64_arch="true"
    elif [ "$(uname -m)" == "aarch64" ];then
        arm64_arch="true"
    fi
}

check_name() {
    local name="$1"
    local value="$2"
    [[ "$value" =~ ^[a-zA-Z0-9._-]+$ ]] || {
        LOG_FATAL "$name name [$value] invalid, it should match regex: ^[a-zA-Z0-9._-]+\$"
    }
}

generate_random_num(){
    num=`${OPENSSL_CMD} rand -hex 4`
    echo $num
}
randNum=$(generate_random_num)

generate_sm_sm2_param() {
    local output=$1
    cat << EOF > ${output}
-----BEGIN EC PARAMETERS-----
BggqgRzPVQGCLQ==
-----END EC PARAMETERS-----

EOF
}

generate_sm_cert_conf() {
    local output=$1
    cat <<EOF >"${output}"
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

default_days	= 36500			# how long to certify for
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

generate_cert_conf() {
    local output=$1
    cat <<EOF >"${output}"
[ca]
default_ca=default_ca

[default_ca]
default_days = 36500
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
organizationalUnitName_default = FISCO-BCOS
commonName =  Organizational  commonName (eg, FISCO-BCOS)
commonName_default = FISCO-BCOS
commonName_max = 64

[ v3_req ]
basicConstraints = CA:FALSE
keyUsage = nonRepudiation, digitalSignature, keyEncipherment

[ v4_req ]
basicConstraints = CA:FALSE

EOF
}

gen_chain_cert() {

    if [ ! -f "${cert_conf}" ]; then
        generate_cert_conf "${cert_conf}"
    fi

    local chaindir="${1}"

    file_must_not_exists "${chaindir}"/ca.key
    file_must_not_exists "${chaindir}"/ca.crt
    file_must_exists "${cert_conf}"

    mkdir -p "$chaindir"
    dir_must_exists "$chaindir"

    ${OPENSSL_CMD} genrsa -out "${chaindir}"/ca.key "${rsa_key_length}" 2>/dev/null
    ${OPENSSL_CMD} req -new -x509 -days "${days}" -subj "/CN=FISCO-BCOS-${randNum}/O=FISCO-BCOS-${randNum}/OU=chain" -key "${chaindir}"/ca.key -config "${cert_conf}" -out "${chaindir}"/ca.crt  2>/dev/null
    if [ ! -f "${chaindir}/cert.cnf" ];then
        mv "${cert_conf}" "${chaindir}"
    fi

    LOG_INFO "Generate ca cert successfully!"
}

gen_rsa_node_cert() {
    local capath="${1}"
    local ndpath="${2}"
    local type="${3}"

    file_must_exists "$capath/ca.key"
    file_must_exists "$capath/ca.crt"
    # check_name node "$node"

    file_must_not_exists "$ndpath"/"${type}".key
    file_must_not_exists "$ndpath"/"${type}".crt

    mkdir -p "${ndpath}"
    dir_must_exists "${ndpath}"

    ${OPENSSL_CMD} genrsa -out "${ndpath}"/"${type}".key "${rsa_key_length}" 2>/dev/null
    ${OPENSSL_CMD} req -new -sha256 -subj "/CN=FISCO-BCOS-${randNum}/O=fisco-bcos/OU=agency" -key "$ndpath"/"${type}".key -config "$capath"/cert.cnf -out "$ndpath"/"${type}".csr
    ${OPENSSL_CMD} x509 -req -days "${days}" -sha256 -CA "${capath}"/ca.crt -CAkey "$capath"/ca.key -CAcreateserial \
        -in "$ndpath"/"${type}".csr -out "$ndpath"/"${type}".crt -extensions v4_req -extfile "$capath"/cert.cnf 2>/dev/null

    ${OPENSSL_CMD} pkcs8 -topk8 -in "$ndpath"/"${type}".key -out "$ndpath"/pkcs8_node.key -nocrypt
    cp "$capath"/ca.crt "$capath"/cert.cnf "$ndpath"/

    rm -f "$ndpath"/"$type".csr
    rm -f "$ndpath"/"$type".key

    mv "$ndpath"/pkcs8_node.key "$ndpath"/"$type".key

    # extract p2p id
    ${OPENSSL_CMD} rsa -in "$ndpath"/"$type".key -pubout -out public.pem 2> /dev/null
    ${OPENSSL_CMD} rsa -pubin -in public.pem -text -noout 2> /dev/null | sed -n '3,20p' | sed 's/://g' | tr "\n" " " | sed 's/ //g' | awk '{print substr($0,3);}'  | cat > "${ndpath}/${type}.nodeid"
    rm -f public.pem

    LOG_INFO "Generate ${ndpath} cert successful!"
}

download_bin()
{
    if [ ! -z "${binary_path}" ];then
        LOG_INFO "Use binary ${binary_path}"
        return
    fi
    if [ "${x86_64_arch}" != "true" ] && [ "${arm64_arch}" != "true" ] && [ "${macOS}" != "macOS" ];then exit_with_clean "We only offer x86_64/aarch64 and macOS precompiled fisco-bcos binary, your OS architecture is not x86_64/aarch64 and macOS. Please compile from source."; fi
    binary_path="bin/${binary_name}"
    if [ -n "${macOS}" ];then
        package_name="${binary_name}-macOS-x86_64.tar.gz"
    elif [ "${arm64_arch}" == "true" ];then
        package_name="${binary_name}-linux-aarch64.tar.gz"
    elif [ "${x86_64_arch}" == "true" ];then
    	package_name="${binary_name}-linux-x86_64.tar.gz"
    fi

    local Download_Link="${cdn_link_header}/FISCO-BCOS/releases/${compatibility_version}/${package_name}"
    local github_link="https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/${compatibility_version}/${package_name}"
    # the binary can obtained from the cos
    if [ $(curl -IL -o /dev/null -s -w %{http_code} "${Download_Link}") == 200 ];then
        # try cdn_link
        LOG_INFO "Downloading fisco-bcos binary from ${Download_Link} ..."
        curl -#LO "${Download_Link}"
    else
        LOG_INFO "Downloading fisco-bcos binary from ${github_link} ..."
        curl -#LO "${github_link}"
    fi
    if [[ "$(ls -al . | grep "fisco-bcos.*tar.gz" | grep -vE "lightnode"| awk '{print $5}')" -lt "1048576" ]];then
        exit_with_clean "Download fisco-bcos failed, please try again. Or download and extract it manually from ${Download_Link} and use -e option."
    fi
    mkdir -p bin && mv ${package_name} bin && cd bin && tar -zxf ${package_name} && cd ..
    chmod a+x ${binary_path}
}

download_lightnode_bin()
{
    lightnode_binary_path="bin/${lightnode_binary_name}"
    if [ -n "${macOS}" ];then
        light_package_name="${lightnode_binary_name}-macOS-x86_64.tar.gz"
    elif [ "${arm64_arch}" == "true" ];then
        light_package_name="${lightnode_binary_name}-linux-aarch64.tar.gz"
    elif [ "${x86_64_arch}" == "true" ];then
    	light_package_name="${lightnode_binary_name}-linux-x86_64.tar.gz"
    fi

    local Download_Link="${cdn_link_header}/FISCO-BCOS/releases/${compatibility_version}/${light_package_name}"
    local github_link="https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/${compatibility_version}/${light_package_name}"
    echo "Download_Link is ${Download_Link}"
    # the binary can obtained from the cos
    if [ $(curl -IL -o /dev/null -s -w %{http_code} "${Download_Link}") == 200 ];then
        # try cdn_link
        echo "=============="
        LOG_INFO "Downloading fisco-bcos lightnode binary from ${Download_Link} ..."
        curl -#LO "${Download_Link}"
    else
        LOG_INFO "Downloading fisco-bcos lightnode binary from ${github_link} ..."
        curl -#LO "${github_link}"
    fi
    if [[ "$(ls -al . | grep "fisco-bcos-lightnode.*tar.gz" | awk '{print $5}')" -lt "1048576" ]];then
        exit_with_clean "Download fisco-bcos-lightnode failed, please try again. Or download and extract it manually from ${Download_Link} and use -e option."
    fi
    mkdir -p bin && mv ${light_package_name} bin && cd bin && tar -zxf ${light_package_name} && cd ..
    chmod a+x ${lightnode_binary_path}
}

download_monitor_bin()
{
    if [ ! -z "${mtail_binary_path}" ];then
        LOG_INFO "Use binary ${mtail_binary_path}"
        return
    fi
    local platform="$(uname -m)"
    local mtail_postfix=""
    if [[ -n "${macOS}" ]];then
        if [[ "${platform}" == "arm64" ]];then
            mtail_postfix="Darwin_arm64"
        elif [[ "${platform}" == "x86_64" ]];then
            mtail_postfix="Darwin_x86_64"
        else
            LOG_FATAL "Unsupported platform ${platform} for mtail"
            exit 1
        fi
    else
        if [[ "${platform}" == "aarch64" ]];then
            mtail_postfix="Linux_arm64"
        elif [[ "${platform}" == "x86_64" ]];then
            mtail_postfix="Linux_x86_64"
        else
            LOG_FATAL "Unsupported platform ${platform} for mtail"
            exit 1
        fi
    fi
    mtail_binary_path="bin/${mtail_binary_name}"
    package_name="${mtail_binary_name}_${compatibility_mtail_version}_${mtail_postfix}.tar.gz"

    local Download_Link="${cdn_link_header}/FISCO-BCOS/tools/mtail/${package_name}"
    local github_link="https://github.com/google/mtail/releases/download/v${compatibility_mtail_version}/${package_name}"
    # the binary can obtained from the cos
    if [ $(curl -IL -o /dev/null -s -w %{http_code} "${Download_Link}") == 200 ];then
        # try cdn_link
        LOG_INFO "Downloading monitor binary from ${Download_Link} ..."
        curl -#LO "${Download_Link}"
    else
        LOG_INFO "Downloading monitor binary from ${github_link} ..."
        curl -#LO "${github_link}"
    fi
    mkdir -p bin && mv ${package_name} bin && cd bin && tar -zxf ${package_name} && cd ..
    chmod a+x ${mtail_binary_path}
}


gen_sm_chain_cert() {
    local chaindir="${1}"
    name=$(basename "$chaindir")
    check_name chain "$name"

    if [ ! -f "${sm_cert_conf}" ]; then
        generate_sm_cert_conf ${sm_cert_conf}
    fi

    generate_sm_sm2_param "${sm2_params}"

    mkdir -p "$chaindir"
    dir_must_exists "$chaindir"

    "$OPENSSL_CMD" genpkey -paramfile "${sm2_params}" -out "$chaindir/sm_ca.key" 2>/dev/null
    "$OPENSSL_CMD" req -config sm_cert.cnf -x509 -days "${days}" -subj "/CN=FISCO-BCOS-${randNum}/O=FISCO-BCOS-${randNum}/OU=chain" -key "$chaindir/sm_ca.key" -extensions v3_ca -out "$chaindir/sm_ca.crt" 2>/dev/null
    if [ ! -f "${chaindir}/${sm_cert_conf}" ];then
        cp "${sm_cert_conf}" "${chaindir}"
    fi
    if [ ! -f "${chaindir}/${sm2_params}" ];then
        cp "${sm2_params}" "${chaindir}"
    fi
}

gen_sm_node_cert_with_ext() {
    local capath="$1"
    local certpath="$2"
    local type="$3"
    local extensions="$4"

    file_must_exists "$capath/sm_ca.key"
    file_must_exists "$capath/sm_ca.crt"

    file_must_not_exists "$ndpath/sm_${type}.crt"
    file_must_not_exists "$ndpath/sm_${type}.key"

    "$OPENSSL_CMD" genpkey -paramfile "$capath/${sm2_params}" -out "$certpath/sm_${type}.key" 2> /dev/null
    "$OPENSSL_CMD" req -new -subj "/CN=FISCO-BCOS-${randNum}/O=fisco-bcos/OU=${type}" -key "$certpath/sm_${type}.key" -config "$capath/sm_cert.cnf" -out "$certpath/sm_${type}.csr" 2> /dev/null

    "$OPENSSL_CMD" x509 -sm3 -req -CA "$capath/sm_ca.crt" -CAkey "$capath/sm_ca.key" -days "${days}" -CAcreateserial -in "$certpath/sm_${type}.csr" -out "$certpath/sm_${type}.crt" -extfile "$capath/sm_cert.cnf" -extensions "$extensions" 2> /dev/null

    rm -f "$certpath/sm_${type}.csr"
}

gen_sm_node_cert() {
    local capath="${1}"
    local ndpath="${2}"
    local type="${3}"

    file_must_exists "$capath/sm_ca.key"
    file_must_exists "$capath/sm_ca.crt"

    mkdir -p "$ndpath"
    dir_must_exists "$ndpath"
    local node=$(basename "$ndpath")
    check_name node "$node"

    gen_sm_node_cert_with_ext "$capath" "$ndpath" "${type}" v3_req
    gen_sm_node_cert_with_ext "$capath" "$ndpath" "en${type}" v3enc_req
    #nodeid is pubkey
    $OPENSSL_CMD ec -in "$ndpath/sm_${type}.key" -text -noout 2> /dev/null | sed -n '7,11p' | sed 's/://g' | tr "\n" " " | sed 's/ //g' | awk '{print substr($0,3);}'  | cat > "${ndpath}/sm_${type}.nodeid"
    cp "$capath/sm_ca.crt" "$ndpath"
}

help() {
    echo $1
    cat <<EOF
Usage:
air
    -C <Command>                        [Optional] the command, support 'deploy' and 'expand' now, default is deploy
    -g <group id>                       [Optional] set the group id, default: group0
    -I <chain id>                       [Optional] set the chain id, default: chain0
    -v <FISCO-BCOS binary version>      [Optional] Default is the latest ${default_version}
    -l <IP list>                        [Required] "ip1:nodeNum1,ip2:nodeNum2" e.g:"192.168.0.1:2,192.168.0.2:3"
    -L <fisco bcos lightnode exec>      [Optional] fisco bcos lightnode executable, input "download_binary" to download lightnode binary or assign correct lightnode binary path
    -e <fisco-bcos exec>                [Optional] fisco-bcos binary exec
    -t <mtail exec>                     [Optional] mtail binary exec
    -o <output dir>                     [Optional] output directory, default ./nodes
    -p <Start port>                     [Optional] Default 30300,20200 means p2p_port start from 30300, rpc_port from 20200
    -s <SM model>                       [Optional] SM SSL connection or not, default is false
    -H <HSM model>                      [Optional] Whether to use HSM(Hardware secure module), default is false
    -c <Config Path>                    [Required when expand node] Specify the path of the expanded node config.ini, config.genesis and p2p connection file nodes.json
    -d <CA cert path>                   [Required when expand node] When expanding the node, specify the path where the CA certificate and private key are located
    -D <docker mode>                    Default off. If set -D, build with docker
    -E <Enable debug log>               Default off. If set -E, enable debug log
    -a <Auth account>                   [Optional] when Auth mode Specify the admin account address.
    -w <WASM mode>                      [Optional] Whether to use the wasm virtual machine engine, default is false
    -R <Serial_mode>                    [Optional] Whether to use serial execute,default is true
    -k <key page size>                  [Optional] key page size, default is 10240
    -m <fisco-bcos monitor>             [Optional] node monitor or not, default is false
    -i <fisco-bcos monitor ip/port>     [Optional] When expanding the node, should specify ip and port
    -M <fisco-bcos monitor>             [Optional] When expanding the node, specify the path where prometheus are located
    -z <Generate tar packet>            [Optional] Pack the data on the chain to generate tar packet
    -n <node key path>                  [Optional] set the path of the node key file to load nodeid
    -N <node path>                      [Optional] set the path of the node modified to multi ca mode
    -u <multi ca path>                  [Optional] set the path of another ca for multi ca mode
    -6 <ipv6 mode>                      [Optional] IPv6 mode use :: as default listen ip, default is false
    -T <Consensus Algorithm>            [Optional] Default PBFT. Options can be pbft / rpbft, pbft is recommended
    -h Help
pro or max
    -C <Command>                        [Optional] the command, support 'deploy' now, default is deploy
    -g <group id>                       [Optional] set the group id, default: group0
    -I <chain id>                       [Optional] set the chain id, default: chain0
    -V <chain version>                  [Optional] support 'air'、'pro'、'max', default is 'air'
    -l <IP list>                        [Required] "ip1:nodeNum1,ip2:nodeNum2" e.g:"192.168.0.1:2,192.168.0.2:3"
    -p <Start port>                     [Optional] Default 30300、20200、40400、2379 means p2p_port start from 30300, rpc_port from 20200, tars_port from 40400, tikv_port default 2379
    -e <service binary path>            [Optional] rpc gateway node service binary path
    -y <service binary download type>   [Optional] rpc gateway node service binary download type, default type is cdn
    -v <service binary version>         [Optional] Default is the latest ${service_binary_version}
    -r <service binary download path>   [Optional] service binary download path, default is ${download_service_binary_path}
    -c <Config Path>                    [Optional] Specify the path of the deploy node config.toml
    -t <deploy type>                    [Optional] support 'rpc'、'gateway'、'node'、'all', default is 'all'
    -o <output dir>                     [Optional] output directory, default genearted
    -s <SM model>                       [Optional] SM SSL connection or not, default is false
    -h Help

deploy nodes e.g
    bash $0 -p 30300,20200 -l 127.0.0.1:4 -o nodes -e ./fisco-bcos
    bash $0 -p 30300,20200 -l 127.0.0.1:4 -o nodes -e ./fisco-bcos -m
    bash $0 -p 30300,20200 -l 127.0.0.1:4 -o nodes -e ./fisco-bcos -s
expand node e.g
    bash $0 -C expand -c config -d config/ca -o nodes/127.0.0.1/node5 -e ./fisco-bcos
    bash $0 -C expand -c config -d config/ca -o nodes/127.0.0.1/node5 -e ./fisco-bcos -m -i 127.0.0.1:5 -M monitor/prometheus/prometheus.yml
    bash $0 -C expand -c config -d config/ca -o nodes/127.0.0.1/node5 -e ./fisco-bcos -s
    bash $0 -C expand_lightnode -c config -d config/ca -o nodes/lightnode1
    bash $0 -C expand_lightnode -c config -d config/ca -o nodes/lightnode1 -L ./fisco-bcos-lightnode
modify node e.g
    bash $0 -C modify -N ./node0 -u ./ca/ca.crt
    bash $0 -C modify -N ./node0 -u ./ca/ca.crt -s
deploy pro service e.g
    bash $0 -p 30300,20200 -l [IP1]:2,[IP2]:2 -C deploy -V pro -o generate -t all
    bash $0 -p 30300,20200 -l [IP1]:2,[IP2]:2 -C deploy -V pro -o generate -t all -s
    bash $0 -p 30300,20200 -l [IP1]:2,[IP2]:2 -C deploy -V pro -o generate -e ./binary
    bash $0 -p 30300,20200,40400 -l [IP1]:2,[IP2]:2 -C deploy -V pro -o generate -y cdn -v ${service_binary_version} -r ./binaryPath
deploy max service e.g
    bash $0 -p 30300,20200,40400,2379 -l [IP1]:1,[IP2]:1,[IP3]:1,[IP4]:1 -C deploy -V max -o generate -t all
    bash $0 -p 30300,20200,40400,2379 -l [IP1]:1,[IP2]:1,[IP3]:1,[IP4]:1 -C deploy -V max -o generate -t all -e ./binary -s
    bash $0 -p 30300,20200,40400,2379 -l [IP1]:1,[IP2]:1,[IP3]:1,[IP4]:1 -C deploy -V max -o generate -y cdn -v ${service_binary_version} -r ./binaryPath
    bash $0 -c config.toml -C deploy -V max -o generate -t all
expand pro node e.g
    bash $0 -C expand_node -V pro -o expand_node -c ./config.toml
expand pro rpc/gateway e.g
    bash $0 -C expand_service -V pro -o expand_service -c ./config.toml
expand pro group e.g
    bash $0 -C expand_group -V pro -o expand_group -c ./config.toml
expand max node e.g
    bash $0 -C expand_node -V max -o expand_node -c ./config.toml
expand max rpc/gateway e.g
    bash $0 -C expand_service -V max -o expand_service -c ./config.toml

EOF
    exit 0
}

parse_params() {
    while getopts "l:C:c:o:e:t:p:d:g:G:L:v:i:I:M:k:zwDshHmEn:R:a:N:u:y:r:V:6T:" option; do
        case $option in
        6) use_ipv6="true" && default_listen_ip="::"
        ;;
        l)
            ip_param=$OPTARG
            use_ip_param="true"
            ;;
        L)
            lightnode_flag="true"
            lightnode_binary_path="$OPTARG"
            if [ "${lightnode_binary_path}" == "download_binary" ]; then
               download_lightnode_binary="true"
            fi
            ;;
        o)
            output_dir="$OPTARG"
            service_output_dir="$OPTARG"
            expand_dir="$OPTARG"
            ;;
        e)
            use_exist_binary="true"
            binary_path="$OPTARG"
            ;;
        t)
            service_type="$OPTARG"
            mtail_binary_path="$OPTARG"
            ;;
        C) command="${OPTARG}"
            ;;
        d) ca_dir="${OPTARG}"
        ;;
        c) config_path="${OPTARG}"
        ;;
        p)
            isPortSpecified="true"
            port_start=(${OPTARG//,/ })
            proOrmax_port_start=(${OPTARG//,/ })
            ;;
        s) sm_mode="true" ;;
        H) enable_hsm="true";;
        g) default_group="${OPTARG}"
            check_name "group" "${default_group}"
            ;;
        G) genesis_conf_path="${OPTARG}"
            ;;
        I) default_chainid="${OPTARG}"
            check_name "chain" "${default_chainid}"
            ;;
        m)
           monitor_mode="true"
           ;;
        E)
            log_level="debug"
            ;;
        n)
           node_key_dir="${OPTARG}"
           dir_must_exists "${node_key_dir}"
           ;;
        i)
           mtail_ip_param="${OPTARG}"
           ;;
        k) key_page_size="${OPTARG}";;
        M) prometheus_dir="${OPTARG}" ;;
        D) docker_mode="true"
           if [ -n "${macOS}" ];then
                LOG_FATAL "Not support docker mode for macOS now"
           fi
        ;;
        w) wasm_mode="true"
          auth_mode="false"
          ;;
        R) serial_mode="${OPTARG}";;
        a)
          auth_admin_account="${OPTARG}"
          auth_mode="true"
        ;;
        v) compatibility_version="${OPTARG}"
           service_binary_version="${OPTARG}"
           download_specific_binary_flag="true"
        ;;
        z) make_tar="true";;
        N)
            modify_node_path=$OPTARG
            dir_must_exists "${modify_node_path}"
            ;;
        u)
            multi_ca_path="$OPTARG"
            local last_char=${multi_ca_path: -1}
            if [ ${last_char} == "/" ]; then
                multi_ca_path=${OPTARG%/*}
            fi
            file_must_exists "${multi_ca_path}"
            ;;
         T) consensus_type=$OPTARG
                if ! echo "${supported_consensus[*]}" | grep -i "${consensus_type}" &>/dev/null; then
                    LOG_WARN "${consensus_type} is not supported. Please set one of ${supported_consensus[*]}"
                    exit 1;
                fi
            ;;
        y)
            download_service_binary_type="${OPTARG}"
            download_specific_binary_flag="true"
            ;;
        r)
            download_service_binary_path="${OPTARG}"
            download_service_binary_path_flag="true"
            ;;
        V)
            chain_version="${OPTARG}";;

        h) help ;;
        *) help ;;
        esac
    done

    if [[ "$chain_version" == "air" ]];then
        if [[ "$mtail_binary_path" != "" ]]; then
            file_must_exists "${mtail_binary_path}"
        fi
        if [[ "$use_exist_binary" = "true" ]]; then
            file_must_exists "${binary_path}"
        fi
        if [[ "$isPortSpecified" = "true" ]]; then
            if [ ${#port_start[@]} -ne 2 ]; then LOG_WARN "p2p start port error. e.g: 30300" && exit 1; fi
        fi
    else
        if [[ "$isPortSpecified" = "true" ]]; then
            if [ ${#proOrmax_port_start[@]} -lt 2 ]; then LOG_WARN "service start port error. e.g: 30300,20200" && exit 1; fi
        fi
    fi
}

print_result() {
    echo "=============================================================="
    LOG_INFO "GroupID              : ${default_group}"
    LOG_INFO "ChainID              : ${default_chainid}"
    if [ -z "${docker_mode}" ];then
        LOG_INFO "${binary_name} path      : ${binary_path}"
    else
        LOG_INFO "docker mode      : ${docker_mode}"
        LOG_INFO "docker tag       : ${compatibility_version}"
    fi
    LOG_INFO "Auth mode            : ${auth_mode}"
    if ${auth_mode} ; then
        LOG_INFO "Admin account        : ${auth_admin_account}"
    fi
    LOG_INFO "Start port           : ${port_start[*]}"
    LOG_INFO "Server IP            : ${ip_array[*]}"
    LOG_INFO "SM model             : ${sm_mode}"
    LOG_INFO "enable HSM           : ${enable_hsm}"
    LOG_INFO "nodes.json           : ${connected_nodes}"
    LOG_INFO "Output dir           : ${output_dir}"
    LOG_INFO "All completed. Files in ${output_dir}"
}

generate_all_node_scripts() {
    local output=${1}
    mkdir -p ${output}

    cat <<EOF >"${output}/start_all.sh"
#!/bin/bash
dirpath="\$(cd "\$(dirname "\$0")" && pwd)"
cd "\${dirpath}"

dirs=(\$(ls -l \${dirpath} | awk '/^d/ {print \$NF}'))
for dir in \${dirs[*]}
do
    if [[ -f "\${dirpath}/\${dir}/config.ini" && -f "\${dirpath}/\${dir}/start.sh" ]];then
        echo "try to start \${dir}"
        bash \${dirpath}/\${dir}/start.sh &
    fi
done
wait
EOF
    chmod u+x "${output}/start_all.sh"

    cat <<EOF >"${output}/stop_all.sh"
#!/bin/bash
dirpath="\$(cd "\$(dirname "\$0")" && pwd)"
cd "\${dirpath}"

dirs=(\$(ls -l \${dirpath} | awk '/^d/ {print \$NF}'))
for dir in \${dirs[*]}
do
    if [[ -f "\${dirpath}/\${dir}/config.ini" && -f "\${dirpath}/\${dir}/stop.sh" ]];then
        echo "try to stop \${dir}"
        bash \${dirpath}/\${dir}/stop.sh
    fi
done
wait
EOF
    chmod u+x "${output}/stop_all.sh"
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

generate_lightnode_scripts() {
    local output=${1}
    local lightnode_binary_name=${2}

    generate_script_template "$output/start.sh"
    generate_script_template "$output/stop.sh"
    chmod u+x "${output}/stop.sh"

    local ps_cmd="\$(ps aux|grep \${fisco_bcos}|grep -v grep|awk '{print \$2}')"
    local start_cmd="nohup \${fisco_bcos} -c config.ini -g config.genesis >>nohup.out 2>&1 &"
    local stop_cmd="kill \${node_pid}"

     cat <<EOF >> "${output}/start.sh"
fisco_bcos=\${SHELL_FOLDER}/${lightnode_binary_name}
cd \${SHELL_FOLDER}
node=\$(basename \${SHELL_FOLDER})
node_pid=${ps_cmd}
ulimit -n 1024
if [ ! -z \${node_pid} ];then
    kill -USR1 \${node_pid}
    sleep 0.2
    kill -USR2 \${node_pid}
    sleep 0.2
    echo " \${node} is running, pid is \$node_pid."
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
    if [[ ! -z \${node_pid} ]];then
        echo -e "\033[32m \${node} start successfully pid=\${node_pid}\033[0m"
        exit 0
    fi
    sleep 0.5
    ((i=i+1))
done
echo -e "\033[31m  Exceed waiting time. Please try again to start \${node} \033[0m"
${log_cmd}
EOF
    chmod u+x "${output}/start.sh"
    generate_script_template "$output/stop.sh"
    cat <<EOF >> "${output}/stop.sh"
fisco_bcos=\${SHELL_FOLDER}/${lightnode_binary_name}
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
    sleep 1
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
    chmod u+x "${output}/stop.sh"
}

generate_node_scripts() {
    local output=${1}
    local docker_mode="${2}"
    local docker_tag="${compatibility_version}"
    local ps_cmd="\$(ps aux|grep \${fisco_bcos}|grep -v grep|awk '{print \$2}')"
    local start_cmd="nohup \${fisco_bcos} -c config.ini -g config.genesis >>nohup.out 2>&1 &"
    local stop_cmd="kill \${node_pid}"
    local pid="pid"
    local log_cmd="tail -n20  nohup.out"
    local check_success="\$(${log_cmd} | grep -e running -e \"initialize server\")"
    if [ -n "${docker_mode}" ];then
        ps_cmd="\$(docker ps |grep \${SHELL_FOLDER//\//} | grep -v grep|awk '{print \$1}')"
        start_cmd="docker run -d --rm --name \${SHELL_FOLDER//\//} -v \${SHELL_FOLDER}:/data --network=host -w=/data fiscoorg/fiscobcos:${docker_tag} -c config.ini -g config.genesis"
        stop_cmd="docker kill \${node_pid} 2>/dev/null"
        pid="container id"
        log_cmd="tail -n20 \$(docker inspect --format='{{.LogPath}}' \${SHELL_FOLDER//\//})"
        check_success="success"
    fi
    generate_script_template "$output/start.sh"
    cat <<EOF >> "${output}/start.sh"
fisco_bcos=\${SHELL_FOLDER}/../${binary_name}
export RUST_LOG=bcos_wasm=error
cd \${SHELL_FOLDER}
node=\$(basename \${SHELL_FOLDER})
node_pid=${ps_cmd}
ulimit -n 1024
#start monitor
dirs=(\$(ls -l \${SHELL_FOLDER} | awk '/^d/ {print \$NF}'))
for dir in \${dirs[*]}
do
    if [[ -f "\${SHELL_FOLDER}/\${dir}/node.mtail" && -f "\${SHELL_FOLDER}/\${dir}/start_mtail_monitor.sh" ]];then
        echo "try to start \${dir}"
        bash \${SHELL_FOLDER}/\${dir}/start_mtail_monitor.sh &
    fi
done


if [ ! -z \${node_pid} ];then
    kill -USR1 \${node_pid}
    sleep 0.2
    kill -USR2 \${node_pid}
    sleep 0.2
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
        echo -e "\033[32m \${node} start successfully pid=\${node_pid}\033[0m"
        exit 0
    fi
    sleep 0.5
    ((i=i+1))
done
echo -e "\033[31m  Exceed waiting time. Please try again to start \${node} \033[0m"
${log_cmd}
EOF
    chmod u+x "${output}/start.sh"
    generate_script_template "$output/stop.sh"
    cat <<EOF >> "${output}/stop.sh"
fisco_bcos=\${SHELL_FOLDER}/../${binary_name}
node=\$(basename \${SHELL_FOLDER})
node_pid=${ps_cmd}
try_times=10
i=0
if [ -z \${node_pid} ];then
    echo " \${node} isn't running."
    exit 0
fi

#Stop monitor here
dirs=(\$(ls -l \${SHELL_FOLDER} | awk '/^d/ {print \$NF}'))
for dir in \${dirs[*]}
do
    if [[ -f "\${SHELL_FOLDER}/\${dir}/node.mtail" && -f "\${SHELL_FOLDER}/\${dir}/stop_mtail_monitor.sh" ]];then
        echo "try to start \${dir}"
        bash \${SHELL_FOLDER}/\${dir}/stop_mtail_monitor.sh &
    fi
done

[ ! -z \${node_pid} ] && ${stop_cmd} > /dev/null
while [ \$i -lt \${try_times} ]
do
    sleep 1
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
    chmod u+x "${output}/stop.sh"
}

generate_mtail_scripts() {
    local output=${1}
    local ip="${2}"
    local port="${3}"
    local node="${4}"
    local ps_cmd="\$(ps aux|grep \${mtail}|grep -v grep|awk '{print \$2}')"
    local start_cmd="nohup \${mtail} -logtostderr -progs \${mtailScript} -logs '../log/*.log' -port ${port} >>nohup.out 2>&1 &"
    local stop_cmd="kill \${node_pid}"
    local pid="pid"
    local log_cmd="tail -n20  nohup.out"
    local check_success="\$(${log_cmd} | grep Listening)"

    mkdir -p $(dirname $output/mtail/node.mtail)
    cat <<EOF >> "${output}/mtail/node.mtail"
hidden text host
host = "${ip}"

#node
hidden text node
node = "${node}"

#chain id
hidden text chain
chain = "${default_chainid}"



#group id
hidden text group
group = "${default_group}"


gauge p2p_session_actived by host , node
/\[P2PService\]\[Service\]\[METRIC\]heartBeat,connected count=(?P<count>\d+)/ {
  p2p_session_actived[host][node] = \$count
}

gauge block_exec_duration_milliseconds_gauge by chain , group , host , node
/\[CONSENSUS\]\[Core\]\[METRIC\]asyncExecuteBlock success.*?timeCost=(?P<timeCost>\d+)/ {
   block_exec_duration_milliseconds_gauge[chain][group][host][node] = \$timeCost
}

histogram block_exec_duration_milliseconds buckets 0, 50, 100, 150 by chain , group , host , node
/\[CONSENSUS\]\[Core\]\[METRIC\]asyncExecuteBlock success.*?timeCost=(?P<timeCost>\d+)/ {
   block_exec_duration_milliseconds[chain][group][host][node] = \$timeCost
}

gauge block_commit_duration_milliseconds_gauge by chain , group , host , node
/\[CONSENSUS\]\[PBFT\]\[STORAGE\]\[METRIC\]commitStableCheckPoint success.*?timeCost=(?P<timeCost>\d+)/ {
   block_commit_duration_milliseconds_gauge[chain][group][host][node] = \$timeCost
}


histogram block_commit_duration_milliseconds buckets 0, 50, 100, 150 by chain , group , host , node
/\[CONSENSUS\]\[PBFT\]\[STORAGE\]\[METRIC\]commitStableCheckPoint success.*?timeCost=(?P<timeCost>\d+)/ {
   block_commit_duration_milliseconds[chain][group][host][node] = \$timeCost
}

gauge ledger_block_height by chain , group , host , node
/\[LEDGER\]\[METRIC\]asyncPrewriteBlock,number=(?P<number>\d+)/ {
  ledger_block_height[chain][group][host][node] = \$number
}

gauge txpool_pending_tx_size by chain , group , host , node
/\[TXPOOL\]\[METRIC\]batchFetchTxs success,.*?pendingTxs=(?P<pendingTxs>\d+)/ {
  txpool_pending_tx_size[chain][group][host][node] = \$pendingTxs
}
EOF

    generate_script_template "$output/mtail/start_mtail_monitor.sh"
    cat <<EOF >> "${output}/mtail/start_mtail_monitor.sh"
mtail=\${SHELL_FOLDER}/../../mtail
mtailScript=\${SHELL_FOLDER}/node.mtail
export RUST_LOG=bcos_wasm=error
cd \${SHELL_FOLDER}
node=\$(basename \${SHELL_FOLDER})
node_pid=${ps_cmd}
if [ ! -z \${node_pid} ];then
    echo " \${node} is Listening, ${pid} is \$node_pid."
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
EOF
    chmod u+x "${output}/mtail/start_mtail_monitor.sh"


    generate_script_template "$output/mtail/stop_mtail_monitor.sh"
    cat <<EOF >> "${output}/mtail/stop_mtail_monitor.sh"
mtail=\${SHELL_FOLDER}/../../mtail
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
    sleep 1
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
    chmod u+x "${output}/mtail/stop_mtail_monitor.sh"
}


generate_monitor_scripts() {
    local output=${1}
    local mtail_host_list=""
    local ip_params="${2}"
    local monitor_ip="${3}"
    local ip_array=(${ip_params//,/ })
    local ip_length=${#ip_array[@]}
    local i=0
    for (( ; i < ip_length; i++)); do
        local ip=${ip_array[i]}
        local delim=""
        if [[ $i == $((ip_length - 1)) ]]; then
            delim=""
        else
            delim=","
        fi
        mtail_host_list="${mtail_host_list}'${ip}'${delim}"
    done


    mkdir -p $(dirname $output/compose.yaml)
    cat <<EOF >> "${output}/compose.yaml"
version: '2'

services:

  prometheus:
    container_name: prometheus
    image: prom/prometheus:latest
    restart: unless-stopped
    volumes:
      - ./prometheus/prometheus.yml:/etc/prometheus/prometheus.yml
      - /etc/localtime:/etc/localtime
    network_mode: host
   # ports:
   #   - \${PROMETHEUS_PORT}:9090

  grafana:
    container_name: grafana
    image: grafana/grafana-oss:7.3.3
    restart: unless-stopped
    user: '0'
    network_mode: host
   # ports:
   #   - \${GRAFANA_PORT}:3000
    volumes:
      - ./grafana/grafana.ini:/etc/grafana/grafana.ini
      - ./grafana/data:/var/lib/grafana
      - ./grafana/logs:/var/log/grafana
      - /etc/localtime:/etc/localtime
    environment:
      - GF_USERS_ALLOW_SIGN_UP=false
      - GF_AUTH_ANONYMOUS_ENABLED=true
      - GF_AUTH_ANONYMOUS_ORG_ROLE=Viewer
EOF

    mkdir -p $(dirname $output/prometheus/prometheus.yml)
    cat <<EOF >> "${output}/prometheus/prometheus.yml"
global:
  scrape_interval:     15s # By default, scrape targets every 15 seconds.

  # Attach these labels to any time series or alerts when communicating with
  # external systems (federation, remote storage, Alertmanager).
  external_labels:
    monitor: 'bcos'

# A scrape configuration containing exactly one endpoint to scrape:
# Here it's Prometheus itself.
scrape_configs:
  # The job name is added as a label job=<job_name> to any timeseries scraped from this config.
  - job_name: 'prometheus'

    # Override the global default and scrape targets from this job every 5 seconds.
    scrape_interval: 5s

    static_configs:
      - targets: [${mtail_host_list}]
EOF

    mkdir -p $(dirname $output/grafana/grafana.ini)
    cat <<EOF >> "${output}/grafana/grafana.ini"
[server]
# The http port  to use
http_port = 3001

[security]
# disable creation of admin user on first start of grafana
;disable_initial_admin_creation = false
;
;# default admin user, created on startup
admin_user = admin
;
;# default admin password, can be changed before first start of grafana,  or in profile settings
admin_password = admin

EOF

    generate_script_template "$output/start_monitor.sh"
    cat <<EOF >> "${output}/start_monitor.sh"

DOCKER_FILE=\${SHELL_FOLDER}/compose.yaml
docker-compose -f \${DOCKER_FILE} up -d prometheus grafana 2>&1
echo "graphna web address: http://${monitor_ip}:3001/"
echo "prometheus web address: http://${monitor_ip}:9090/"
EOF
    chmod u+x "${output}/start_monitor.sh"


    generate_script_template "$output/stop_monitor.sh"
    cat <<EOF >> "${output}/stop_monitor.sh"

DOCKER_FILE=\${SHELL_FOLDER}/compose.yaml
docker-compose -f \${DOCKER_FILE} stop
echo -e "\033[32m stop monitor successfully\033[0m"
EOF
    chmod u+x "${output}/stop_monitor.sh"
}


generate_sdk_cert() {
    local sm_mode="$1"
    local ca_cert_path="${2}"
    local sdk_cert_path="${3}"

    mkdir -p ${sdk_cert_path}
    if [[ "${sm_mode}" == "false" ]]; then
        gen_rsa_node_cert "${ca_cert_path}" "${sdk_cert_path}" "sdk" 2>&1
    else
        gen_sm_node_cert "${ca_cert_path}" "${sdk_cert_path}" "sdk" 2>&1
    fi
}

generate_node_cert() {
    local sm_mode="$1"
    local ca_cert_path="${2}"
    local node_cert_path="${3}"

    mkdir -p ${node_cert_path}
    if [[ "${sm_mode}" == "false" ]]; then
        gen_rsa_node_cert "${ca_cert_path}" "${node_cert_path}" "ssl" 2>&1
    else
        gen_sm_node_cert "${ca_cert_path}" "${node_cert_path}" "ssl" 2>&1
    fi
}

generate_chain_cert() {
    local sm_mode="$1"
    local chain_cert_path="$2"
    mkdir -p "${chain_cert_path}"
    if [[ "${sm_mode}" == "false" ]]; then
        gen_chain_cert "${chain_cert_path}" 2>&1
    else
        gen_sm_chain_cert "${chain_cert_path}" 2>&1
    fi
}
generate_config_ini() {
    local output="${1}"
    local p2p_listen_ip="${2}"
    local p2p_listen_port="${3}"

    local rpc_listen_ip="${4}"
    local rpc_listen_port="${5}"
    local disable_ssl="${6}"

    local enable_ssl_content="enable_ssl=true"
    if [[ "${disable_ssl}" == "true" ]]; then
        enable_ssl_content="enable_ssl=false"
    fi

    cat <<EOF >"${output}"
[p2p]
    listen_ip=${p2p_listen_ip}
    listen_port=${p2p_listen_port}
    ; ssl or sm ssl
    sm_ssl=false
    nodes_path=${file_dir}
    nodes_file=${p2p_connected_conf_name}
    ; enable rip protocol, default: true
    ; enable_rip_protocol=false
    ; enable compression for p2p message, default: true
    ; enable_compression=false

[certificate_blacklist]
    ; crl.0 should be nodeid, nodeid's length is 512
    ;crl.0=

[certificate_whitelist]
    ; cal.0 should be nodeid, nodeid's length is 512
    ;cal.0=

[rpc]
    listen_ip=${rpc_listen_ip}
    listen_port=${rpc_listen_port}
    thread_count=4
    ; ssl or sm ssl
    sm_ssl=false
    ; ssl connection switch, if you wan to disable the ssl connection, turn it to false, default: true
    ${enable_ssl_content}
    ; return input params in sendTransaction() return, default: true
    ; return_input_params=false
    ; tars_rpc_port=20021

[web3_rpc]
    enable=false
    listen_ip=0.0.0.0
    listen_port=8545
    thread_count=8

[cert]
    ; directory the certificates located in
    ca_path=./conf
    ; the ca certificate file
    ca_cert=ca.crt
    ; the node private key file
    node_key=ssl.key
    ; the node certificate file
    node_cert=ssl.crt
    ; directory the multiple certificates located in
    multi_ca_path=multiCaPath

EOF
    generate_common_ini "${output}"
}

generate_common_ini() {
    local output=${1}
    # LOG_INFO "Begin generate uuid"
    local uuid=$(uuidgen)
    # LOG_INFO "Generate uuid success: ${uuid}"
    cat <<EOF >>"${output}"

[security]
    private_key_path=conf/node.pem
    enable_hsm=${enable_hsm}
    ; path of hsm dynamic library
    ;hsm_lib_path=
    ;key_index=
    ;password=

[storage_security]
    ; enable data disk encryption or not, default is false
    enable=false
    ; url of the key center, in format of ip:port
    ;key_center_url=
    ;cipher_data_key=

[consensus]
    ; min block generation time(ms)
    min_seal_time=500

[executor]
    enable_dag=true
    baseline_scheduler=false
    baseline_scheduler_parallel=false

[storage]
    data_path=data
    enable_cache=true
    ; The granularity of the storage page, in bytes, must not be less than 4096 Bytes, the default is 10240 Bytes (10KB)
    ; if modify key_page_size value to 0, should clear the data directory
    key_page_size=${key_page_size}
    pd_ssl_ca_path=
    pd_ssl_cert_path=
    pd_ssl_key_path=
    enable_archive=false
    archive_ip=127.0.0.1
    archive_port=
    ; if modify enable_separate_block_state, should clear the data directory
    ;enable_separate_block_state=false
    ;sync_archived_blocks=false

[txpool]
    ; size of the txpool, default is 15000
    limit=15000
    ; txs notification threads num, default is 2
    notify_worker_num=2
    ; txs verification threads num, default is the number of CPU cores
    ;verify_worker_num=2
    ; txs expiration time, in seconds, default is 10 minutes
    txs_expiration_time = 600

[sync]
    ; send transaction by tree-topology
    ; recommend to use when deploy many consensus nodes
    send_txs_by_tree=false
    ; send block status by tree-topology
    ; recommend to use when deploy many consensus nodes
    sync_block_by_tree=false
    tree_width=3

[redis]
    ; redis server ip
    ;server_ip=127.0.0.1
    ; redis server port
    ;server_port=6379
    ; redis request timeout, unit ms
    ;request_timeout=3000
    ; redis connection pool size
    ;connection_pool_size=16
    ; redis password, default empty
    ;password=
    ; redis db, default 0th
    ;db=0

[flow_control]
    ; rate limiter stat reporter interval, unit: ms
    ; stat_reporter_interval=60000

    ; time window for rate limiter, default: 3s
    ; time_window_sec=3

    ; enable distributed rate limiter, redis required, default: false
    ; enable_distributed_ratelimit=false
    ; enable local cache for distributed rate limiter, work with enable_distributed_ratelimit, default: true
    ; enable_distributed_ratelimit_cache=true
    ; distributed rate limiter local cache percent, work with enable_distributed_ratelimit_cache, default: 20
    ; distributed_ratelimit_cache_percent=20

    ; the module that does not limit bandwidth
    ; list of all modules: raft,pbft,amop,block_sync,txs_sync,light_node,cons_txs_sync
    ;
    ; modules_without_bw_limit=raft,pbft

    ; allow the msg exceed max permit pass
    ; outgoing_allow_exceed_max_permit=false

    ; restrict the outgoing bandwidth of the node
    ; both integer and decimal is support, unit: Mb
    ;
    ; total_outgoing_bw_limit=10

    ; restrict the outgoing bandwidth of the the connection
    ; both integer and decimal is support, unit: Mb
    ;
    ; conn_outgoing_bw_limit=2
    ;
    ; specify IP to limit bandwidth, format: conn_outgoing_bw_limit_x.x.x.x=n
    ;   conn_outgoing_bw_limit_192.108.0.1=3
    ;   conn_outgoing_bw_limit_192.108.0.2=3
    ;   conn_outgoing_bw_limit_192.108.0.3=3
    ;
    ; default bandwidth limit for the group
    ; group_outgoing_bw_limit=2
    ;
    ; specify group to limit bandwidth, group_outgoing_bw_limit_groupName=n
    ;   group_outgoing_bw_limit_group0=2
    ;   group_outgoing_bw_limit_group1=2
    ;   group_outgoing_bw_limit_group2=2

    ; should not change incoming_p2p_basic_msg_type_list if you known what you would to do
    ; incoming_p2p_basic_msg_type_list=
    ; the qps limit for p2p basic msg type, the msg type has been config by incoming_p2p_basic_msg_type_list, default: -1
    ; incoming_p2p_basic_msg_type_qps_limit=-1
    ; default qps limit for all module message, default: -1
    ; incoming_module_msg_type_qps_limit=-1
    ; specify module to limit qps, incoming_module_qps_limit_moduleID=n
    ;       incoming_module_qps_limit_xxxx=1000
    ;       incoming_module_qps_limit_xxxx=2000
    ;       incoming_module_qps_limit_xxxx=3000

[log]
    enable=true
    ; print the log to std::cout or not, default print to the log files
    enable_console_output = false
    log_path=./log
    ; info debug trace
    level=${log_level}
    ; MB
    max_log_file_size=1024
    ; rotate the log every hour
    ;enable_rotate_by_hour=true
    enable_rate_collector=false
EOF
}

generate_sm_config_ini() {
    local output=${1}
    local p2p_listen_ip="${2}"
    local p2p_listen_port="${3}"
    local rpc_listen_ip="${4}"
    local rpc_listen_port="${5}"
    local disable_ssl="${6}"

    local enable_ssl_content="enable_ssl=true"
    if [[ "${disable_ssl}" == "true" ]]; then
        enable_ssl_content="enable_ssl=false"
    fi

    cat <<EOF >"${output}"
[p2p]
    listen_ip=${p2p_listen_ip}
    listen_port=${p2p_listen_port}
    ; ssl or sm ssl
    sm_ssl=true
    nodes_path=${file_dir}
    nodes_file=${p2p_connected_conf_name}
    ; enable rip protocol, default: true
    ; enable_rip_protocol=false
    ; enable compression for p2p message, default: true
    ; enable_compression=false

[certificate_blacklist]
    ; crl.0 should be nodeid, nodeid's length is 128
    ;crl.0=

[certificate_whitelist]
    ; cal.0 should be nodeid, nodeid's length is 128
    ;cal.0=

[rpc]
    listen_ip=${rpc_listen_ip}
    listen_port=${rpc_listen_port}
    thread_count=4
    ; ssl or sm ssl
    sm_ssl=true
    ; ssl connection switch, if you wan to disable the ssl connection, turn it to false, default: true
    ${enable_ssl_content}
    ; return input params in sendTransaction() return, default: true
    ; return_input_params=false

[web3_rpc]
    enable=false
    listen_ip=0.0.0.0
    listen_port=8545
    thread_count=8

[cert]
    ; directory the certificates located in
    ca_path=./conf
    ; the ca certificate file
    sm_ca_cert=sm_ca.crt
    ; the node private key file
    sm_node_key=sm_ssl.key
    ; the node certificate file
    sm_node_cert=sm_ssl.crt
    ; the node private key file
    sm_ennode_key=sm_enssl.key
    ; the node certificate file
    sm_ennode_cert=sm_enssl.crt
    ; directory the multiple certificates located in
    multi_ca_path=multiCaPath

EOF
    generate_common_ini "${output}"
}

generate_p2p_connected_conf() {
    local output="${1}"
    local ip_params="${2}"
    local template="${3}"

    local p2p_host_list=""
    if [[ "${template}" == "true" ]]; then
        p2p_host_list="${ip_params}"
    else
        local ip_array=(${ip_params//,/ })
        local ip_length=${#ip_array[@]}
        local i=0
        for (( ; i < ip_length; i++)); do
            local ip=${ip_array[i]}
            local delim=""
            if [[ $i == $((ip_length - 1)) ]]; then
                delim=""
            else
                delim=","
            fi
            p2p_host_list="${p2p_host_list}\"${ip}\"${delim}"
        done
    fi

    cat <<EOF >"${output}"
{"nodes":[${p2p_host_list}]}
EOF
}

generate_config() {
    local sm_mode="${1}"
    local node_config_path="${2}"
    local p2p_listen_ip="${3}"
    local p2p_listen_port="${4}"
    local rpc_listen_ip="${5}"
    local rpc_listen_port="${6}"
    local disable_ssl="${7}"
    local skip_generate_auth_account="${8}"

    if [[ -n "${skip_generate_auth_account}" ]]; then
        LOG_INFO "Skip generate auth account..."
    else
        check_auth_account
    fi
    if [ "${sm_mode}" == "false" ]; then
        generate_config_ini "${node_config_path}" "${p2p_listen_ip}" "${p2p_listen_port}" "${rpc_listen_ip}" "${rpc_listen_port}" "${disable_ssl}"
    else
        generate_sm_config_ini "${node_config_path}" "${p2p_listen_ip}" "${p2p_listen_port}" "${rpc_listen_ip}" "${rpc_listen_port}" "${disable_ssl}"
    fi
}

generate_secp256k1_node_account() {
    local output_path="${1}"
    local node_index="${2}"
    if [ ! -d "${output_path}" ]; then
        mkdir -p ${output_path}
    fi

    # generate nodeids from file
    if [ ! -z "${node_key_dir}" ]; then
        generate_nodeids_from_path "${node_key_dir}" "pem" "${node_index}" "${output_path}"
    else
        if [ ! -f /tmp/secp256k1.param ]; then
            ${OPENSSL_CMD} ecparam -out /tmp/secp256k1.param -name secp256k1
        fi
        ${OPENSSL_CMD} genpkey -paramfile /tmp/secp256k1.param -out ${output_path}/node.pem 2>/dev/null
        # generate nodeid
        ${OPENSSL_CMD} ec -text -noout -in "${output_path}/node.pem" 2>/dev/null | sed -n '7,11p' | tr -d ": \n" | awk '{print substr($0,3);}' | cat >"$output_path"/node.nodeid
        local node_id=$(cat "${output_path}/node.nodeid")
        nodeid_list=$"${nodeid_list}node.${node_index}=${node_id}: 1
        "
    fi
}

generate_sm_node_account() {
    local output_path="${1}"
    local node_index="${2}"
    if [ ! -d "${output_path}" ]; then
        mkdir -p ${output_path}
    fi

    if [ "${enable_hsm}" == "true" ] && [ -z "${node_key_dir}" ]; then
        LOG_FATAL "Must input path of node key in HSM mode, eg: bash build_chain.sh -H -n node_key_dir"
    fi

    if [ ! -z "${node_key_dir}" ]; then
        generate_nodeids_from_path "${node_key_dir}" "pem" "${node_index}" "${output_path}"
    else
        if [ ! -f ${sm2_params} ]; then
            generate_sm_sm2_param ${sm2_params}
        fi
        ${OPENSSL_CMD} genpkey -paramfile ${sm2_params} -out ${output_path}/node.pem 2>/dev/null
        $OPENSSL_CMD ec -in "$output_path/node.pem" -text -noout 2> /dev/null | sed -n '7,11p' | sed 's/://g' | tr "\n" " " | sed 's/ //g' | awk '{print substr($0,3);}'  | cat > "$output_path/node.nodeid"
        local node_id=$(cat "${output_path}/node.nodeid")
        nodeid_list=$"${nodeid_list}node.${node_index}=${node_id}:1
        "
    fi
}

generate_genesis_config() {
    local output=${1}
    local node_list=${2}

    cat <<EOF >"${output}"
[chain]
    ; use SM crypto or not, should nerver be changed
    sm_crypto=${sm_mode}
    ; the group id, should nerver be changed
    group_id=${default_group}
    ; the chain id, should nerver be changed
    chain_id=${default_chainid}

[web3]
    chain_id=${default_web3_chainid}

[consensus]
    ; consensus algorithm now support PBFT(consensus_type=pbft), rPBFT(consensus_type=rpbft)
    consensus_type=${consensus_type}
    ; the max number of transactions of a block
    block_tx_count_limit=1000
    ; in millisecond, block consensus timeout, at least 3000ms
    consensus_timeout=3000
    ; the number of blocks generated by each leader
    leader_period=1
    ; rpbft
    ; the working sealers num of each consensus epoch
    epoch_sealer_num=4
    ; the number of generated blocks each epoch
    epoch_block_num=1000
    ; the node id of consensusers
    ${node_list}

[version]
    ; compatible version, can be dynamically upgraded through setSystemConfig
    ; the default is ${compatibility_version//[vV]/}
    compatibility_version=${compatibility_version//[vV]/}
[tx]
    ; transaction gas limit
    gas_limit=3000000000
[executor]
    ; use the wasm virtual machine or not
    is_wasm=${wasm_mode}
    is_auth_check=${auth_mode}
    auth_admin_account=${auth_admin_account}
    is_serial_execute=${serial_mode}
EOF
}

parse_ip_config() {
    local config=$1
    n=0
    while read line; do
        ip_array[n]=$(echo ${line} | awk '{print $1}')
        agency_array[n]=$(echo ${line} | awk '{print $2}')
        group_array[n]=$(echo ${line} | awk '{print $3}')
        if [ -z "${ip_array[$n]}" -o -z "${agency_array[$n]}" -o -z "${group_array[$n]}" ]; then
            exit_with_clean "Please check ${config}, make sure there is no empty line!"
        fi
        ((++n))
    done <${config}
}

get_value() {
    local var_name=${1}
    var_name="${var_name//./}"
    var_name="var_${var_name//-/}"
    local res=$(eval echo '$'"${var_name}")
    echo ${res}
}

set_value() {
    local var_name=${1}
    var_name="${var_name//./}"
    var_name="var_${var_name//-/}"
    local var_value=${2}
    eval "${var_name}=${var_value}"
}

exit_with_clean() {
    local content=${1}
    echo -e "\033[31m[ERROR] ${content}\033[0m"
    if [ -d "${output_dir}" ]; then
        rm -rf ${output_dir}
    fi
    exit 1
}

check_and_install_tassl(){
    if [ -f "${OPENSSL_CMD}" ];then
        return
    fi
    # https://en.wikipedia.org/wiki/Uname#Examples
    local x86_64_name="x86_64"
    local arm_name="aarch64"
    local tassl_mid_name="linux"
    if [[ -n "${macOS}" ]];then
        x86_64_name="x86_64"
        arm_name="arm64"
        tassl_mid_name="macOS"
    fi

    local tassl_post_fix="x86_64"
    local platform="$(uname -m)"
    if [[ "${platform}" == "${arm_name}" ]];then
        tassl_post_fix="aarch64"
    elif [[ "${platform}" == "${x86_64_name}" ]];then
        tassl_post_fix="x86_64"
    else
        LOG_FATAL "Unsupported platform ${platform} for ${tassl_mid_name}"
        exit 1
    fi
    local tassl_package_name="tassl-1.1.1b-${tassl_mid_name}-${tassl_post_fix}"
    local tassl_tgz_name="${tassl_package_name}.tar.gz"
    # local tassl_link_prefix="${cdn_link_header}/FISCO-BCOS/tools/tassl-1.1.1b/${tassl_tgz_name}"
    local Download_Link="${cdn_link_header}/FISCO-BCOS/tools/tassl-1.1.1b/${tassl_tgz_name}"
    local github_link="https://github.com/FISCO-BCOS/TASSL/releases/download/V_1.4/${tassl_tgz_name}"
    # the binary can obtained from the cos
    if [ $(curl -IL -o /dev/null -s -w %{http_code} "${Download_Link}") == 200 ];then
        # try cdn_link
        echo "=============="
        echo "Downloading tassl binary from ${Download_Link}"
        curl -#LO "${Download_Link}"
    else
        echo "Downloading tassl binary from ${github_link}"
        curl -#LO "${github_link}"
    fi
    # wget --no-check-certificate  "${tassl_link_prefix}"
    tar zxvf ${tassl_tgz_name} && rm ${tassl_tgz_name}
    if [[ -n "${macOS}" ]];then
        xattr -d com.apple.quarantine ${tassl_package_name}
        # xattr -d com.apple.macl ${tassl_package_name}
    fi
    chmod u+x ${tassl_package_name}
    mkdir -p "${HOME}"/.fisco
    mv ${tassl_package_name} "${HOME}"/.fisco/tassl-1.1.1b
}

generate_node_account()
{
    local sm_mode="${1}"
    local account_dir="${2}"
    local count="${3}"
    if [[ "${sm_mode}" == "false" ]]; then
        if [ "${enable_hsm}" == "true" ]; then
            LOG_FATAL "HSM is only supported in sm mode"
        fi
        generate_secp256k1_node_account "${account_dir}" "${count}"
    else
        generate_sm_node_account "${account_dir}" "${count}"
    fi
}

expand_node()
{
    local sm_mode="${1}"
    local ca_dir="${2}"
    local node_dir="${3}"
    local config_path="${4}"
    local mtail_ip_param="${5}"
    local prometheus_dir="${6}"
    if [ -d "${node_dir}" ];then
        LOG_FATAL "expand node failed for ${node_dir} already exists!"
    fi

    if "${monitor_mode}" ; then
       LOG_INFO "start generate monitor scripts"
       ip=`echo $mtail_ip_param | awk '{split($0,a,":");print a[1]}'`
        if [ -z $(echo $ip | grep -E "^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$") ]; then
            LOG_WARN "Please check IP address: ${ip}, if you use domain name please ignore this."
        fi
       num=`echo $mtail_ip_param | awk '{split($0,a,":");print a[2]}'`
       local port=$((mtail_listen_port + num))
       connected_mtail_nodes="${ip}:${port}"
       generate_mtail_scripts "${node_dir}" "${ip}" "${port}" "node${num}"
       if [ -n "${prometheus_dir}" ];then
           sed -i  "s/]/,\'${connected_mtail_nodes}\']/g" ${prometheus_dir}
       fi
       LOG_INFO "generate monitor scripts success"
    fi

    file_must_exists "${config_path}/config.ini"
    file_must_exists "${config_path}/config.genesis"
    file_must_exists "${config_path}/nodes.json"
    # check binary
    parent_path=$(dirname ${node_dir})
    if [ -z "${docker_mode}" ];then
        download_bin
        if [ ! -f ${binary_path} ];then
            LOG_FATAL "fisco bcos binary exec ${binary_path} not exist, please input the correct path."
        fi
    fi
    mkdir -p "${node_dir}"
    sdk_path="${parent_path}/sdk"
    if [ ! -d "${sdk_path}" ];then
        LOG_INFO "generate sdk cert, path: ${sdk_path} .."
        generate_sdk_cert "${sm_mode}" "${ca_dir}" "${sdk_path}"
        LOG_INFO "generate sdk cert success.."
    fi
    start_all_script_path="${parent_path}/start_all.sh"
    if [ ! -f "${start_all_script_path}" ];then
         LOG_INFO "generate start_all.sh and stop_all.sh ..."
         generate_all_node_scripts "${parent_path}"
         LOG_INFO "generate start_all.sh and stop_all.sh success..."
    fi
    LOG_INFO "generate_node_scripts ..."
    generate_node_scripts "${node_dir}" "${docker_mode}"
    LOG_INFO "generate_node_scripts success..."
    # generate cert
    LOG_INFO "generate_node_cert ..."
    generate_node_cert "${sm_mode}" "${ca_dir}" "${node_dir}/conf"
    LOG_INFO "generate_node_cert success..."
    # generate node account
    LOG_INFO "generate_node_account ..."
    generate_node_account "${sm_mode}" "${node_dir}/conf" "${i}"
    LOG_INFO "generate_node_account success..."

    LOG_INFO "copy configurations ..."
    cp "${config_path}/config.ini" "${node_dir}"
    cp "${config_path}/config.genesis" "${node_dir}"
    cp "${config_path}/nodes.json" "${node_dir}"
    LOG_INFO "copy configurations success..."
    echo "=============================================================="
    if [ -z "${docker_mode}" ];then
        LOG_INFO "${binary_name} Path       : ${binary_path}"
    else
        LOG_INFO "docker mode        : ${docker_mode}"
        LOG_INFO "docker tag     : ${compatibility_version}"
    fi
    LOG_INFO "sdk dir         : ${sdk_path}"
    LOG_INFO "SM Model         : ${sm_mode}"

    LOG_INFO "output dir         : ${output_dir}"
    LOG_INFO "All completed. Files in ${output_dir}"
}

expand_lighenode()
{
    local sm_mode="${1}"
    local ca_dir="${2}"
    local lightnode_dir="${3}"
    local config_path="${4}"
    local mtail_ip_param="${5}"
    local prometheus_dir="${6}"
    if [ -d "${lightnode_dir}" ];then
        LOG_FATAL "expand node failed for ${lightnode_dir} already exists!"
    fi
    file_must_exists "${config_path}/config.ini"
    file_must_exists "${config_path}/config.genesis"
    file_must_exists "${config_path}/nodes.json"
    # check binary,parent_path is ./nodes
    parent_path=$(dirname ${lightnode_dir})
    mkdir -p "${lightnode_dir}"
    if [ "${lightnode_flag}" == "true" ] && [ -f  "${lightnode_binary_path}" ]; then
        echo "Checking ${lightnode_flag}, lightnode_binary_path is ${lightnode_binary_path}, lightnode_dir is ${lightnode_dir}"
        chmod u+x ${lightnode_binary_path}
        cp "${lightnode_binary_path}" ${lightnode_dir}/
    elif [ "${lightnode_flag}" == "false" ]; then
        download_lightnode_bin
        cp "${lightnode_binary_path}" ${lightnode_dir}/
    fi

    LOG_INFO "generate_lightnode_scripts ..."
    generate_lightnode_scripts "${lightnode_dir}" "fisco-bcos-lightnode"
    LOG_INFO "generate_lightnode_scripts success..."

    # generate cert
    LOG_INFO "generate_lightnode_cert ..."
    generate_node_cert "${sm_mode}" "${ca_dir}" "${lightnode_dir}/conf"
    LOG_INFO "generate_lightnode_cert success..."
    # generate node account
    LOG_INFO "generate_lightnode_account ..."
    generate_node_account "${sm_mode}" "${lightnode_dir}/conf" "${i}"
    LOG_INFO "generate_lightnode_account success..."

    LOG_INFO "copy configurations ..."
    cp "${config_path}/config.ini" "${lightnode_dir}"
    cp "${config_path}/config.genesis" "${lightnode_dir}"
    cp "${config_path}/nodes.json" "${lightnode_dir}"
    LOG_INFO "copy configurations success..."
    echo "=============================================================="

    LOG_INFO "SM Model         : ${sm_mode}"
    LOG_INFO "output dir         : ${output_dir}"
    LOG_INFO "All completed. Files in ${output_dir}"
}

deploy_nodes()
{
    dir_must_not_exists "${output_dir}"
    mkdir -p "$output_dir"
    dir_must_exists "${output_dir}"
    cert_conf="${output_dir}/cert.cnf"
    p2p_listen_port=port_start[0]
    rpc_listen_port=port_start[1]
    [ -z $use_ip_param ] && help 'ERROR: Please set -l or -f option.'
    if [ "${use_ip_param}" == "true" ]; then
        ip_array=(${ip_param//,/ })
    elif [ "${use_ip_param}" == "false" ]; then
        if ! parse_ip_config $ip_file; then
            exit_with_clean "Parse $ip_file error!"
        fi
    else
        help
    fi
    #check the binary
    if [ -z "${docker_mode}" ];then
        download_bin
        if [[ ! -f "$binary_path" ]]; then
            LOG_FATAL "fisco bcos binary exec ${binary_path} not exist, Must copy binary file ${binary_name} to ${binary_path}"
        fi
    fi
    #check the lightnode binary
    if [ -f "${lightnode_binary_path}" ] && [ "${download_lightnode_binary}" != "true" ];then
      LOG_INFO "Use lightnode binary ${lightnode_binary_path}"
    fi
    if [ "${download_lightnode_binary}" == "true" ];then
        download_lightnode_bin

    fi
    if [ "${lightnode_flag}" == "true" ] && [ ! -f "${lightnode_binary_path}" ] && [ "${download_lightnode_binary}" != "true" ];then
      LOG_FATAL "please input \"download_binary\" or assign correct lightnode binary path"
    fi


    if "${monitor_mode}" ;then
        download_monitor_bin
        if [[ ! -f "$mtail_binary_path" ]]; then
            LOG_FATAL "mtail binary exec ${mtail_binary_path} not exist, Must copy binary file ${mtail_binary_name} to ${mtail_binary_path}"
        fi
    fi

    local i=0
    node_count=0
    local count=0
    connected_nodes=""
    connected_mtail_nodes=""
    local monitor_ip=""
    # Note: must generate the node account firstly
    ca_dir="${output_dir}"/ca
    generate_chain_cert "${sm_mode}" "${ca_dir}"

    for line in ${ip_array[*]}; do
        if echo "$line" | grep -E "\[.*\]:[0-9]+" >/dev/null; then
            ip=${line%\]*}
            ip=${ip#*\[}
            num=${line#*\]}
            num=${num#*:}
            use_ipv6="true"
        if [[ -n "${use_ipv6}" && $ip != "::" && -z "$(echo "$ip" | grep -E "^([0-9a-fA-F]{0,4}:){1,7}[0-9a-fA-F]{1,4}$")" ]]; then
            LOG_FATAL "Please check IPv6 address: ${ip}"
            fi
        else
            ip=${line%:*}
            num=${line#*:}
        fi
        if [[ -z "${use_ipv6}" && -z $(echo $ip | grep -E "^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$") ]]; then
            LOG_WARN "Please check IPv4 address: ${ip}, if you use domain name please ignore this."
        fi
        # echo $num
        local ip_var_name=${ip//./_}
        ip_var_name="${ip_var_name//:/_}_count"
        [ "$num" == "$ip" ] || [ -z "${num}" ] && num=${node_num}
        echo "Processing IP ${ip} Total:${num}"
        [ -z "$(get_value ${ip_var_name})" ] && set_value ${ip_var_name} 0

        nodes_dir="${output_dir}/${ip}"
        # start_all.sh and stop_all.sh
        generate_all_node_scripts "${nodes_dir}"
        if [ -z "${docker_mode}" ];then
            cp "${binary_path}" "${nodes_dir}"
        fi
        if "${monitor_mode}" ;then
            cp $mtail_binary_path "${nodes_dir}"
        fi
        ca_cert_dir="${nodes_dir}"/ca
        mkdir -p ${ca_cert_dir}
        cp -r ${ca_dir}/* ${ca_cert_dir}

        # generate sdk cert
        generate_sdk_cert "${sm_mode}" "${ca_dir}" "${nodes_dir}/sdk"
        for ((i = 0; i < num; ++i)); do
            local node_count=$(get_value ${ip_var_name})
            node_dir="${output_dir}/${ip}/node${node_count}"
            mkdir -p "${node_dir}"
            generate_node_cert "${sm_mode}" "${ca_dir}" "${node_dir}/conf"
            generate_node_scripts "${node_dir}" "${docker_mode}"
            if "${monitor_mode}" ;then
                local port=$((mtail_listen_port + node_count))
                connected_mtail_nodes=${connected_mtail_nodes}"${ip}:${port}, "
                if [[ $count == 0 ]]; then
                    monitor_ip="${ip}"
                fi
                generate_mtail_scripts "${node_dir}" "${ip}" "${port}" "node${node_count}"
            fi
            local port=$((p2p_listen_port + node_count))
            if [[ -n "${use_ipv6}" ]]; then
                connected_nodes=${connected_nodes}"[${ip}]:${port},"
            else
                connected_nodes=${connected_nodes}"${ip}:${port},"
            fi
            account_dir="${node_dir}/conf"
            generate_node_account "${sm_mode}" "${account_dir}" "${count}"
            set_value ${ip_var_name} $(($(get_value ${ip_var_name}) + 1))
            ((++count))
            ((++node_count))
        done
    done

    if "${monitor_mode}" ;then
        monitor_dir="${output_dir}/monitor"
        generate_monitor_scripts "${monitor_dir}" "${connected_mtail_nodes}" ${monitor_ip}
    fi

    local i=0
    local count=0
    for line in ${ip_array[*]}; do
        if echo "$line" | grep -E "\[.*\]:[0-9]+" >/dev/null; then
            ip=${line%\]*}
            ip=${ip#*\[}
            num=${line#*\]}
            num=${num#*:}
            use_ipv6="true"
        if [ -n "${use_ipv6}" ] && [ "$ip" != "::" ] && [ -z "$(echo "$ip" | grep -E "^([0-9a-fA-F]{0,4}:){1,7}[0-9a-fA-F]{1,4}$")" ]; then
            LOG_FATAL "Please check IPv6 address: ${ip}"
            fi
        else
            ip=${line%:*}
            num=${line#*:}
        fi
        if [[ -z "${use_ipv6}" && -z $(echo $ip | grep -E "^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$") ]]; then
            LOG_WARN "Please check IPv4 address: ${ip}, if you use domain name please ignore this."
        fi
        local ip_var_name=${ip//./_}
        ip_var_name="${ip_var_name//:/_}_count"
        [ "$num" == "$ip" ] || [ -z "${num}" ] && num=${node_num}
        set_value ${ip_var_name} 0
        for ((i = 0; i < num; ++i)); do
            local node_count=$(get_value ${ip_var_name})
            node_dir="${output_dir}/${ip}/node${node_count}"
            local p2p_port=$((p2p_listen_port + node_count))
            local rpc_port=$((rpc_listen_port + node_count))
            generate_config "${sm_mode}" "${node_dir}/config.ini" "${default_listen_ip}" "${p2p_port}" "${default_listen_ip}" "${rpc_port}"
            generate_p2p_connected_conf "${node_dir}/${p2p_connected_conf_name}" "${connected_nodes}" "false"
            if [ ! -z "${node_key_dir}" ]; then
                # generate nodeids from file
                generate_genesis_config "${node_dir}/config.genesis" "${nodeid_list_from_path}"
            else
                generate_genesis_config "${node_dir}/config.genesis" "${nodeid_list}"
            fi
            set_value ${ip_var_name} $(($(get_value ${ip_var_name}) + 1))
            ((++count))
        done
        if [ -n "$make_tar" ];then
            cd ${output_dir}
            tar zcf "${ip}.tar.gz" "${ip}" &
            cd ${current_dir}
        fi
    done
    wait
    # Generate lightnode cert
    if [ -e "${lightnode_binary_path}" ]; then
        local lightnode_dir="${output_dir}/lightnode"
        mkdir -p ${lightnode_dir}
        generate_genesis_config "${lightnode_dir}/config.genesis" "${nodeid_list}"

        generate_node_cert "${sm_mode}" "${ca_dir}" "${lightnode_dir}/conf"
        generate_lightnode_scripts "${lightnode_dir}" "fisco-bcos-lightnode"
        local lightnode_account_dir="${lightnode_dir}/conf"
        generate_node_account "${sm_mode}" "${lightnode_account_dir}" ${count}

        local p2p_port=$((p2p_listen_port + num))
        local rpc_port=$((rpc_listen_port + num))
        generate_config "${sm_mode}" "${lightnode_dir}/config.ini" "${default_listen_ip}" "${p2p_port}" "${default_listen_ip}" "${rpc_port}"
        generate_p2p_connected_conf "${lightnode_dir}/${p2p_connected_conf_name}" "${connected_nodes}" "false"

        cp "${lightnode_binary_path}" ${lightnode_dir}/
        if [ -n "$make_tar" ];then cd ${output_dir} && tar zcf "lightnode.tar.gz" "../${lightnode_dir}" && cd ${current_dir};fi
    fi

    print_result
}

generate_template_package()
{
    local node_name="${1}"
    local binary_path="${2}"
    local genesis_conf_path="${3}"
    local output_dir="${4}"

    # do not support docker
    if [ -n "${docker_mode}" ];then
        LOG_FATAL "Docker mode is not supported on building template install package"
    fi

    # do not support monitor
    if "${monitor_mode}" ;then
        LOG_FATAL "Monitor mode is not support on building template install package"
    fi

    # check if node.nodid dir exist
    file_must_exists "${genesis_conf_path}"
    file_must_exists "${binary_path}"
    # dir_must_not_exists "${output_dir}"

    mkdir -p "${output_dir}"
    dir_must_exists "${output_dir}"

    # mkdir node dir
    node_dir="${output_dir}/${node_name}"
    mkdir -p "${node_dir}"
    mkdir -p "${node_dir}/conf"

    p2p_listen_ip="[#P2P_LISTEN_IP]"
    rpc_listen_ip="[#RPC_LISTEN_IP]"

    p2p_listen_port="[#P2P_LISTEN_PORT]"
    rpc_listen_port="[#RPC_LISTEN_PORT]"

    # copy binary file
    cp "${binary_path}" "${node_dir}/../"
    # copy config.genesis
    cp "${genesis_conf_path}" "${node_dir}/"

    # generate start_all.sh and stop_all.sh
    generate_all_node_scripts "${node_dir}/../"

    # generate node start.sh stop.sh
    generate_node_scripts "${node_dir}"

    local connected_nodes="[#P2P_CONNECTED_NODES]"
    # generate config for node
    generate_config "${sm_mode}" "${node_dir}/config.ini" "${p2p_listen_ip}" "${p2p_listen_port}" "${rpc_listen_ip}" "${rpc_listen_port}" "true" "true"
    generate_p2p_connected_conf "${node_dir}/${p2p_connected_conf_name}" "${connected_nodes}" "true"

    LOG_INFO "Building template intstall package"
    LOG_INFO "Auth mode            : ${auth_mode}"
    if ${auth_mode} ; then
        LOG_INFO "Auth account     : ${auth_admin_account}"
    fi
    LOG_INFO "SM model             : ${sm_mode}"
    LOG_INFO "enable HSM           : ${enable_hsm}"
    LOG_INFO "All completed. Files in ${output_dir}"
}

generate_nodeids_from_path()
{
    local node_key_dir="${1}"
    local file_type="${2}"
    local file_index="${3}"
    local output_dir="${4}"

    if [ ! -d "${output_dir}" ]; then
        mkdir -p "${output_dir}"
    fi

    local node_key_file_list=$(ls "${node_key_dir}" | tr "\n" " ")
    local node_key_file_array=(${node_key_file_list})

    if [[ ! -f "${node_key_dir}/${node_key_file_array[${file_index}]}" ]]; then
        LOG_WARN " node key file not exist, ${node_key_dir}/${node_key_file_array[${file_index}]}"
        continue
    fi
    local nodeid=""
    if [ "${file_type}" == "pem" ]; then
        ${OPENSSL_CMD} ec -text -noout -in "${node_key_dir}/${node_key_file_array[${file_index}]}" 2>/dev/null | sed -n '7,11p' | tr -d ": \n" | awk '{print substr($0,3);}' | cat >"$node_key_dir"/node.nodeid
        nodeid=$(cat "${node_key_dir}/node.nodeid")
        cp -f "${node_key_dir}/${node_key_file_array[${file_index}]}" "${output_dir}/node.pem"
        rm -f "${node_key_dir}/node.nodeid"
    elif [ "${file_type}" == "nodeid" ]; then
        nodeid=$(cat "${nodeid_dir}/${nodeid_file}")
    else
        LOG_FATAL "generate_nodeids_from_path function only support pem and nodeid file type, don't support ${file_type} yet"
    fi
    nodeid_list_from_path=$"${nodeid_list_from_path}node.${node_index}=${nodeid}: 1
    "
}

generate_genesis_config_by_nodeids()
{
    local nodeid_dir="${1}"
    local output_dir="${2}"

    local nodeid_file_list=$(ls "${nodeid_dir}" | tr "\n" " ")
    local nodeid_file_array=(${nodeid_file_list})

    local total_files_sum="${#nodeid_file_array[*]}"
    for ((local file_index=0; file_index < total_files_sum; ++file_index)); do
        generate_nodeids_from_path "${nodeid_file_array}" "nodeid" "${file_index}" "${output_dir}"
    done

    if [[ -z "${nodeid_list_from_path}" ]]; then
        LOG_FATAL "generate config.genesis failed, please check if the nodeids directory correct"
    fi

    generate_genesis_config "${output_dir}/config.genesis" "${generate_nodeids_from_path}"
}

check_auth_account()
{
  if [ -z "${auth_admin_account}" ]; then
    # get account string to auth_admin_account
    generate_auth_account
  else
    if ! [[ ${auth_admin_account} =~ ^0x[0-9a-fA-F]{40}$ ]]; then
        LOG_FATAL "auth_admin_account must be a valid, please check it!"
        exit 1
    fi
  fi
}

generate_auth_account()
{
  local account_script="get_account.sh"
  if ${sm_mode}; then
    account_script="get_gm_account.sh"
  fi
  if [ ! -f "${HOME}/.fisco/${account_script}" ];then
        local Download_Link="${cdn_link_header}/FISCO-BCOS/tools/${account_script}"
        local github_link="https://github.com/FISCO-BCOS/console/raw/master/tools/${account_script}"
        # the binary can obtained from the cos
        if [ $(curl -IL -o /dev/null -s -w %{http_code} "${Download_Link}") == 200 ];then
            # try cdn_link
            LOG_INFO "Downloading ${account_script} from ${Download_Link}..."
            curl -#LO "${Download_Link}"
        else
            LOG_INFO "Downloading ${account_script} from ${github_link}..."
            curl -#LO "${github_link}"
        fi
        chmod u+x ${account_script}
        mv ${account_script} "${HOME}/.fisco/"
  fi
  auth_admin_account=$(bash ${HOME}/.fisco/${account_script} | grep Address | sed -r "s/\x1B\[([0-9]{1,2}(;[0-9]{1,2})?)?[m|K]//g" | awk '{print $5}')
  LOG_INFO "Admin account: ${auth_admin_account}"
  if [[ ${chain_version} == "air" ]];then
      mv accounts* "${ca_dir}"
  else
      mv accounts* "${BcosBuilder_path}/${chain_version}"
  fi
  if [ "$?" -ne "0" ]; then
      LOG_INFO "Admin account generate failed, please check ${HOME}/.fisco/${account_script}."
      exit 1
  fi
}

modify_node_ca_setting(){
    dir_must_not_exists "${modify_node_path}/conf/multiCaPath"
    file_must_exists "${multi_ca_path}"
    mkdir ${modify_node_path}/conf/multiCaPath 2>/dev/null
    cp ${multi_ca_path} ${modify_node_path}/conf/multiCaPath/${1}.0 2>/dev/null
    # sed -i "s/; mul_ca_path=multiCaPath/mul_ca_path=multiCaPath/g" ${modify_node_path}/config.ini

    LOG_INFO "Modify node ca setting success"
}

modify_multiple_ca_node(){
    check_and_install_tassl
    subject_hash=`${OPENSSL_CMD} x509 -hash -noout -in ${multi_ca_path} 2>/dev/null`
    if [[ ! "${multi_ca_path}" || ! "${modify_node_path}" ]];then
        LOG_FATAL "multi_ca_path and modify_node_path are required!"
    fi
    modify_node_ca_setting "${subject_hash}"
}

convert_to_absolute_path() {
    local path="$1"
    # Convert to absolute path
    if [[ ! $path =~ ^/ ]]; then
        local current_dir=$(pwd)

        path="$current_dir/$path"
    fi
    echo "$path"
}

download_bcos_builder(){
    local Download_Link="${cdn_link_header}/FISCO-BCOS/releases/${bcos_builder_version}/${bcos_builder_package}"
    local github_link="https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/${bcos_builder_version}/${bcos_builder_package}"
    # the binary can obtained from the cos
    if [ $(curl -IL -o /dev/null -s -w %{http_code} "${Download_Link}") == 200 ];then
        # try cdn_link
        LOG_INFO "Downloading bcos-builder tools from ${Download_Link} ..."
        curl -#LO "${Download_Link}"
    else
        LOG_INFO "Downloading bcos-builder tools from ${github_link} ..."
        curl -#LO "${github_link}"
    fi
    if [[ "$(ls -al . | grep "BcosBuilder.tgz" | awk '{print $5}')" -lt "1048576" ]];then
        exit_with_clean "Download bcos-builder tools failed, please try again. Or download and extract it manually from ${Download_Link}."
    fi
    tar -zxf ${bcos_builder_package}
}

download_service_bin(){
    local BcosBuilder_path=$1
    if [[ ${download_service_binary_path_flag} == "true" ]];then
        download_service_binary_path=$(convert_to_absolute_path "$download_service_binary_path")
    fi
    cd "${BcosBuilder_path}/${chain_version}"
    if [[ ${chain_version} == "pro" ]] && [[ ${download_specific_binary_flag} == "true" || ${download_service_binary_path_flag} == "true" || ! -d "binary" || ! -f "binary/BcosGatewayService/BcosGatewayService" || \
     ! -f "binary/BcosNodeService/BcosNodeService" || ! -f "binary/BcosRpcService/BcosRpcService" ]] || \
    [[ ${chain_version} == "max" ]] && [[ ${download_specific_binary_flag} == "true" || ${download_service_binary_path_flag} == "true" || ! -d "binary" || ! -f "binary/BcosGatewayService/BcosGatewayService" || \
     ! -f "binary/BcosExecutorService/BcosExecutorService" || ! -f "binary/BcosMaxNodeService/BcosMaxNodeService" || ! -f "binary/BcosRpcService/BcosRpcService" ]];then

        download_service_binary_path=$(convert_to_absolute_path "$download_service_binary_path")
        python3 build_chain.py download_binary -t ${download_service_binary_type} -v ${service_binary_version} -p ${download_service_binary_path}
        binary_path=$download_service_binary_path
    else
        binary_path=${BcosBuilder_path}/${chain_version}/binary
    fi
    cd ../../
}

gen_chain_template() {
    local config_path=$1
    cat <<EOF >"${config_path}"
[tars]
tars_pkg_dir = "${binary_path}"

[chain]
chain_id="${default_chainid}"
# the rpc-service enable sm-ssl or not, default disable sm-ssl
rpc_sm_ssl=${sm_mode}
# the gateway-service enable sm-ssl or not, default disable sm-ssm
gateway_sm_ssl=${sm_mode}
# the existed rpc service ca path, will generate new ca if not configured
#rpc_ca_cert_path=""
# the existed gateway service ca path, will generate new ca if not configured
#gateway_ca_cert_path=""

EOF
}

gen_group_template() {
    local config_path=$1
    local binary_version=${service_binary_version}
    if [ "${service_binary_version:0:1}" == "v" ]; then
        binary_version=${service_binary_version:1}
    fi
    cat <<EOF >>"${config_path}"
[[group]]
group_id="${default_group}"
# the genesis configuration path of the group, will generate new genesis configuration if not configured
# genesis_config_path = ""
# VM type, now only support evm/wasm
vm_type="evm"
# use sm-crypto or not
sm_crypto=false
# enable auth-check or not
auth_check=false
init_auth_address="${auth_admin_account}"

# the genesis config
# the number of blocks generated by each leader
leader_period = 1
# the max number of transactions of a block
block_tx_count_limit = 1000
# consensus algorithm now support PBFT(consensus_type=pbft)
consensus_type = "pbft"
# transaction gas limit
gas_limit = "3000000000"
# compatible version, can be dynamically upgraded through setSystemConfig
compatibility_version="${binary_version}"

EOF
}

gen_gateway_template() {
    local agencyName=$1
    local ip=$2
    local num=$3
    local config_path=$4
    local peers_str=$5
    local p2p_listen_port=${proOrmax_port_start[0]}
    local rpc_listen_port=${proOrmax_port_start[1]}

    cat <<EOF >>"${config_path}"
[[agency]]
name = "${agencyName}"
EOF

if [[ ${chain_version} == "max" ]];then
        tars_listen_port_space=6
        cat <<EOF >>"${config_path}"
failover_cluster_url = "${ip}:${tikv_listen_port}"
EOF
fi

    cat <<EOF >>"${config_path}"
# enable data disk encryption for rpc/gateway or not, default is false
enable_storage_security = false
# url of the key center, in format of ip:port, please refer to https://github.com/FISCO-BCOS/key-manager for details
# key_center_url =
# cipher_data_key =

    [agency.rpc]
    deploy_ip=["$ip"]
    # rpc listen ip
    listen_ip="0.0.0.0"
    # rpc listen port
    listen_port=${rpc_listen_port}
    thread_count=4
    # rpc tars server listen ip
    tars_listen_ip="0.0.0.0"
    # rpc tars server listen port
    tars_listen_port=${tars_listen_port}

    [agency.gateway]
    deploy_ip=["$ip"]
    # gateway listen ip
    listen_ip="0.0.0.0"
    # gateway listen port
    listen_port=${p2p_listen_port}
    # gateway connected peers, should be all of the gateway peers info
    peers=${peers_str}
    tars_listen_port=$((${tars_listen_port}+1))

EOF
    tars_listen_port=$((tars_listen_port + 2))
    local i
    for((i = 0; i < ${num}; i++)); do
        gen_node_template ${i} ${tars_listen_port} ${config_path} ${ip}
        tars_listen_port=$((tars_listen_port + ${tars_listen_port_space}))
    done
}

gen_node_template(){
    local node_id=$1
    local node_name="node${node_id}"
    local tars_listen_port=$2
    local config_path=$3
    local ip=$4

    if [[ ${chain_version} == "pro" ]];then
    cat <<EOF >>"${config_path}"
    [[agency.group]]
        group_id = "${default_group}"

        [[agency.group.node]]
        # node name, Notice: node_name in the same agency and group must be unique
        node_name = "${node_name}"
        deploy_ip = "${ip}"
        # node tars server listen ip
        tars_listen_ip="0.0.0.0"
        # node tars server listen port, Notice: the tars server of the node will cost five ports, then the port tars_listen_port ~ tars_listen_port + 4 should be in free
        tars_listen_port=${tars_listen_port}
        # enable data disk encryption for bcos node or not, default is false
        enable_storage_security = false
        # url of the key center, in format of ip:port, please refer to https://github.com/FISCO-BCOS/key-manager for details
        # key_center_url =
        # cipher_data_key =
        monitor_listen_port = "${monitor_listen_port}"
        # monitor log path example:"/home/fisco/tars/framework/app_log/"
        monitor_log_path = ""

EOF
    fi
    if [[ ${chain_version} == "max" ]];then
        cat <<EOF >>"${config_path}"
     [[agency.group]]
        group_id = "${default_group}"
        [[agency.group.node]]
        node_name =  "${node_name}"
        # the tikv storage pd-addresses
        pd_addrs = "${ip}:${tikv_listen_port}"
        key_page_size=10240
        deploy_ip = ["$ip"]
        executor_deploy_ip=["$ip"]
        monitor_listen_port =  "${monitor_listen_port}"
        # the tikv storage pd-addresses
        # monitor log path example:"/home/fisco/tars/framework/app_log/"
        monitor_log_path = ""
        # node tars server listen ip
        tars_listen_ip="0.0.0.0"
        # node tars server listen port
        tars_listen_port=${tars_listen_port}

EOF
    fi
}

getPeers(){
    local ip_param=$1
    ip_array=(${ip_param//,/ })

    peer=""
    local gateway_port=${proOrmax_port_start[0]}

    for line in ${ip_array[*]}; do
        if [ -n "$(echo ${line} | grep "\.")" ];then
            ip=${line%:*}
        fi
        peer+="\"${ip[$n]}:${gateway_port}\", "
    done
    peers=[$(echo "${peer::-2}")]
    echo $peers
}

gen_gateway_config() {
    local config_path="$1"
    [ -z $use_ip_param ] && help 'ERROR: Please set -l or -f option.'
    if [ "${use_ip_param}" == "true" ]; then
        ip_array=(${ip_param//,/ })
    else
        help
    fi
    peers_str=$(getPeers $ip_param)
    if [[ "${sm_mode}" == "false" && -d "${BcosBuilder_path}/${chain_version}/accounts" ]]; then
        rm -rf ${BcosBuilder_path}/${chain_version}/accounts
    fi
    if [[ "${sm_mode}" == "true" && -d "${BcosBuilder_path}/${chain_version}/accounts_gm" ]]; then
        rm -rf ${BcosBuilder_path}/${chain_version}/accounts_gm
    fi
    check_auth_account
    gen_chain_template "${config_path}"
    gen_group_template "${config_path}"
    local k=0
    if [[ -z ${proOrmax_port_start[2]} ]];then
       tars_listen_port=40400
    else
        tars_listen_port=${proOrmax_port_start[2]}
    fi

    if [[ -z ${proOrmax_port_start[3]} ]];then
        tikv_listen_port=2379
    else
        tikv_listen_port=${proOrmax_port_start[3]}
    fi

    if [[ -z ${proOrmax_port_start[4]} ]];then
        monitor_listen_port=3901
    else
        monitor_listen_port=${proOrmax_port_start[4]}
    fi

    for line in ${ip_array[*]}; do
        if [ -n "$(echo ${line} | grep "\.")" ];then
            ip=${line%:*}
            num=${line#*:}
        fi
        letter=$(printf "\\$(printf '%03o' $((k + 65)))")
        (( k += 1 ))

        gen_gateway_template agency${letter} ${ip} ${num} ${config_path} "${peers_str}"
    done
}

install_python_package(){
    if command -v python >/dev/null 2>&1; then
        file_must_exists "${BcosBuilder_path}/requirements.txt"
        dir_must_exists "${BcosBuilder_path}/${chain_version}"
        # package not exist, install it
        while IFS= read -r package; do
            if ! python3 -c "import $package" >/dev/null 2>&1; then
                if ! pip3 install $package >/dev/null 2>&1; then
                    LOG_FATAL "Failed to install python package ${package}. Please install it manually, e.g., sudo pip3 install ${package}"
                fi
            fi
        done < "${BcosBuilder_path}/requirements.txt"
    fi
}
# deploy pro or max chain service
deploy_pro_or_max_nodes(){
    dir_must_not_exists "${service_output_dir}"
    mkdir -p "$service_output_dir"
    dir_must_exists "${service_output_dir}"
    BcosBuilder_path=${current_dir}/BcosBuilder
    if [ ! -d "$BcosBuilder_path" ]; then
        download_bcos_builder
    fi
    install_python_package
    local build_chain_file="${BcosBuilder_path}/${chain_version}/build_chain.py"
    file_must_exists ${build_chain_file}
    if [ "$use_exist_binary" == "true" ];then
        binary_path=$(convert_to_absolute_path "$binary_path")
        dir_must_exists "${binary_path}"
    else
        download_service_bin "${BcosBuilder_path}"
    fi
    if [[ "$config_path" != "" ]]; then
        config_path=$(convert_to_absolute_path "$config_path")
        file_must_exists "${config_path}"
    else
        config_path=${BcosBuilder_path}/${chain_version}/config.toml
        gen_gateway_config "${config_path}"
    fi
    service_output_dir=$(convert_to_absolute_path "$service_output_dir")
    cd "${BcosBuilder_path}/${chain_version}"
    python3 build_chain.py build -t ${service_type} -c ${config_path} -O ${service_output_dir}
    cd ../../
}

# remove excess files from the results folder
removeExcessFiles(){
    local folder_path="$1"

    for file in "$folder_path"/*
    do
        filename=$(basename "$file")

        # Determine if the file name is named with IP
        if [[ $filename =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            # Keep only group_node files, remove rpc gateway files
            if [ -d  ${file}/rpc* ]; then
                rm -rf ${file}/rpc*
            fi
            if [ -d ${file}/gateway* ]; then
                rm -r ${file}/gateway*
            fi
        else
            # If the file name is not named after IP, delete the file
            rm -r  $file
        fi
    done
}

expand_pro_max(){
    dir_must_not_exists "${expand_dir}"
    mkdir -p "$expand_dir"
    dir_must_exists "${expand_dir}"
    BcosBuilder_path=${current_dir}/BcosBuilder
    if [ ! -d "$BcosBuilder_path" ]; then
        download_bcos_builder
    fi
    install_python_package

    local build_chain_file="${BcosBuilder_path}/${chain_version}/build_chain.py"
    file_must_exists ${build_chain_file}

    if [[ "$config_path" != "" ]]; then
        config_path=$(convert_to_absolute_path "$config_path")
    else
        config_path=${BcosBuilder_path}/${chain_version}/config.toml
    fi

    file_must_exists "${config_path}"
    # echo ${config_path}
    expand_dir=$(convert_to_absolute_path "$expand_dir")
    cd "${BcosBuilder_path}/${chain_version}"
    python3 build_chain.py build -t all -c ${config_path} -O ${expand_dir}

    cd ../../

    if [[ "${command}" == "expand_node" || "${command}" == "expand_group" ]]; then
        removeExcessFiles ${expand_dir}
    fi
}

main() {
    check_env
    check_and_install_tassl
    parse_params "$@"
    if [[ "${chain_version}" == "air" ]]; then
    if [[ "${command}" == "deploy" ]]; then
        deploy_nodes
    elif [[ "${command}" == "expand" ]]; then
        dir_must_exists "${ca_dir}"
        expand_node "${sm_mode}" "${ca_dir}" "${output_dir}" "${config_path}" "${mtail_ip_param}" "${prometheus_dir}"
    elif [[ "${command}" == "expand_lightnode" ]]; then
        dir_must_exists "${ca_dir}"
        expand_lighenode "${sm_mode}" "${ca_dir}" "${output_dir}" "${config_path}" "${mtail_ip_param}" "${prometheus_dir}"
    elif [[ "${command}" == "generate-template-package"  ]]; then
        local node_name="node0"
        if [[ -n "${genesis_conf_path}" ]]; then
            dir_must_not_exists "${output_dir}"
            # config.genesis is set
            file_must_exists "${genesis_conf_path}"
            generate_template_package "${node_name}" "${binary_path}" "${genesis_conf_path}" "${output_dir}"
        elif [[ -n "${node_key_dir}" ]] && [[ -d "${node_key_dir}" ]]; then
            dir_must_not_exists "${output_dir}"
            generate_genesis_config_by_nodeids "${node_key_dir}" "${output_dir}/"
            file_must_exists "${output_dir}/config.genesis"
            generate_template_package "${node_name}" "${binary_path}" "${output_dir}/config.genesis" "${output_dir}"
        else
            echo "bash build_chain.sh generate-template-package -h "
            echo "  eg:"
            echo "      bash build_chain.sh -C generate-template-package -e ./fisco-bcos -o ./nodes -G ./config.genesis "
            echo "      bash build_chain.sh -C generate-template-package -e ./fisco-bcos -o ./nodes -G ./config.genesis -s"
            echo "      bash build_chain.sh -C generate-template-package -e ./fisco-bcos -o ./nodes -n nodeids -s -R"
        fi
    elif [[ "${command}" == "generate_cert" ]]; then
      mkdir -p "${output_dir}"
      if "${sm_mode}" ; then
          generate_sm_sm2_param "${output_dir}/${sm2_params}"
      else
          generate_cert_conf "${output_dir}/cert.cnf"
      fi
      generate_node_account "${sm_mode}" "${output_dir}" "1"
      if [[ -n "${ca_dir}" ]]; then
        generate_node_cert "${sm_mode}" "${ca_dir}" "${output_dir}"
      else
        generate_chain_cert "${sm_mode}" "${output_dir}/ca"
        generate_node_cert "${sm_mode}" "${output_dir}/ca" "${output_dir}"
      fi
    elif [[ "${command}" == "modify" ]]; then
        modify_multiple_ca_node
    else
        LOG_FATAL "Unsupported command ${command}, only support \'deploy\' and \'expand\' now!"
    fi
    elif [[ "${chain_version}" == "pro" || "${chain_version}" == "max" ]]; then
        if [[ "${command}" == "deploy" ]]; then
            deploy_pro_or_max_nodes
        elif [[ "${command}" == "expand_node" || "${command}" == "expand_group" || "${command}" == "expand_service" ]]; then
            expand_pro_max
        fi
    fi
}

main "$@"
