#!/bin/bash
set -e

dirpath="$(cd "$(dirname "$0")" && pwd)"
listen_ip="0.0.0.0"
port_start=(30300 20200 3901)
p2p_listen_port=port_start[0]
rpc_listen_port=port_start[1]
mtail_listen_port=3901
use_ip_param=
mtail_ip_param=""
ip_array=
output_dir="./nodes"
binary_name="fisco-bcos"
mtail_binary_name="mtail"

# for cert generation
ca_cert_dir="${dirpath}"
sm_cert_conf='sm_cert.cnf'
cert_conf="${output_dir}/cert.cnf"
days=36500
rsa_key_length=2048
sm_mode='false'
macOS=""
x86_64_arch="true"
sm2_params="sm_sm2.param"
cdn_link_header="https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS"
OPENSSL_CMD="${HOME}/.fisco/tassl-1.1.1b"
nodeid_list=""
file_dir="./"
nodes_json_file_name="nodes.json"
command="deploy"
ca_dir=""
prometheus_dir=""
config_path=""
docker_mode=
default_version="v3.0.0-rc4"
compatibility_version=${default_version}
default_mtail_version="3.0.0-rc48"
compatibility_mtail_version=${default_mtail_version}
auth_mode="false"
monitor_mode="false"
auth_admin_account=
binary_path=""
mtail_binary_path=""
wasm_mode="false"
download_timeout=240

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
    echo -e "\033[31m[FALT] ${content}\033[0m"
    exit 1
}

dir_must_exists() {
    if [ ! -d "$1" ]; then
        LOG_FATAL "$1 DIR does not exist, please check!"
    fi
}

dir_must_not_exists() {
    if [  -d "$1" ]; then
        LOG_FATAL "$1 DIR does not exist, please check!"
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
    fi
    if [ "$(uname -m)" != "x86_64" ];then
        x86_64_arch="false"
    fi
}

check_name() {
    local name="$1"
    local value="$2"
    [[ "$value" =~ ^[a-zA-Z0-9._-]+$ ]] || {
        LOG_FATAL "$name name [$value] invalid, it should match regex: ^[a-zA-Z0-9._-]+\$"
    }
}

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

generate_cert_conf() {
    local output=$1
    cat <<EOF >"${output}"
[ca]
default_ca=default_ca
[default_ca]
default_days = 3650
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
basicConstraints = CA:TRUE

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
    ${OPENSSL_CMD} req -new -x509 -days "${days}" -subj "/CN=FISCO-BCOS/O=FISCO-BCOS/OU=chain" -key "${chaindir}"/ca.key -config "${cert_conf}" -out "${chaindir}"/ca.crt  2>/dev/null
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
    ${OPENSSL_CMD} req -new -sha256 -subj "/CN=FISCO-BCOS/O=fisco-bcos/OU=agency" -key "$ndpath"/"${type}".key -config "$capath"/cert.cnf -out "$ndpath"/"${type}".csr
    ${OPENSSL_CMD} x509 -req -days "${days}" -sha256 -CA "${capath}"/ca.crt -CAkey "$capath"/ca.key -CAcreateserial \
        -in "$ndpath"/"${type}".csr -out "$ndpath"/"${type}".crt -extensions v4_req -extfile "$capath"/cert.cnf 2>/dev/null

    ${OPENSSL_CMD} pkcs8 -topk8 -in "$ndpath"/"${type}".key -out "$ndpath"/pkcs8_node.key -nocrypt
    cp "$capath"/ca.crt "$capath"/cert.cnf "$ndpath"/

    rm -f "$ndpath"/"$type".csr
    rm -f "$ndpath"/"$type".key

    mv "$ndpath"/pkcs8_node.key "$ndpath"/"$type".key

    LOG_INFO "Generate ${ndpath} cert successful!"
}

download_bin()
{
    if [ ! -z "${binary_path}" ];then
        LOG_INFO "Use binary ${binary_path}"
        return
    fi
    if [ "${x86_64_arch}" != "true" ];then exit_with_clean "We only offer x86_64 precompiled fisco-bcos binary, your OS architecture is not x86_64. Please compile from source."; fi
    binary_path="bin/${binary_name}"
    package_name="${binary_name}-linux-x86_64.tar.gz"
    if [ -n "${macOS}" ];then
        package_name="${binary_name}-macOS-x86_64.tar.gz"
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
    if [[ "$(ls -al . | grep "fisco-bcos.*tar.gz" | awk '{print $5}')" -lt "1048576" ]];then
        exit_with_clean "Download fisco-bcos failed, please try again. Or download and extract it manually from ${Download_Link} and use -e option."
    fi
    mkdir -p bin && mv ${package_name} bin && cd bin && tar -zxf ${package_name} && cd ..
    chmod a+x ${binary_path}
}

download_monitor_bin()
{
    if [ ! -z "${mtail_binary_path}" ];then
        LOG_INFO "Use binary ${mtail_binary_path}"
        return
    fi
    if [ "${x86_64_arch}" != "true" ];then exit_with_clean "We only offer x86_64 precompiled fisco-bcos binary, your OS architecture is not x86_64. Please compile from source."; fi
    mtail_binary_path="bin/${mtail_binary_name}"
    package_name="${mtail_binary_name}_${compatibility_mtail_version}_Linux_x86_64.tar.gz"
    if [ -n "${macOS}" ];then
        exit_with_clean "mtail binary is support macOS"
    fi
    
    local github_link="https://github.com/google/mtail/releases/download/v${compatibility_mtail_version}/${package_name}"
    # the binary can obtained from the cos
    LOG_INFO "Downloading mtail binary from ${github_link} ..."
    curl -#LO "${github_link}"
    
    if [[ "$(ls -al . | grep "mtail.*tar.gz" | awk '{print $5}')" -lt "7618709" ]];then
        exit_with_clean "Download mtail failed, please try again. Or download and extract it manually from ${github_link} and use -e option."
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
    "$OPENSSL_CMD" req -config sm_cert.cnf -x509 -days "${days}" -subj "/CN=FISCO-BCOS/O=FISCO-BCOS/OU=chain" -key "$chaindir/sm_ca.key" -extensions v3_ca -out "$chaindir/sm_ca.crt" 2>/dev/null
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
    "$OPENSSL_CMD" req -new -subj "/CN=FISCO-BCOS/O=fisco-bcos/OU=${type}" -key "$certpath/sm_${type}.key" -config "$capath/sm_cert.cnf" -out "$certpath/sm_${type}.csr" 2> /dev/null

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
    -C <Command>                        [Optional] the command, support 'deploy' and 'expand' now, default is deploy
    -v <FISCO-BCOS binary version>      Default is the latest ${default_version}
    -l <IP list>                        [Required] "ip1:nodeNum1,ip2:nodeNum2" e.g:"192.168.0.1:2,192.168.0.2:3"
    -o <output dir>                     [Optional] output directory, default ./nodes
    -e <fisco-bcos exec>                [Required] fisco-bcos binary exec
    -p <Start Port>                     Default 30300,20200 means p2p_port start from 30300, rpc_port from 20200
    -s <SM model>                       [Optional] SM SSL connection or not, default is false
    -c <Config Path>                    [Required when expand node] Specify the path of the expanded node config.ini, config.genesis and p2p connection file nodes.json
    -d <CA cert path>                   [Required when expand node] When expanding the node, specify the path where the CA certificate and private key are located
    -D <docker mode>                    Default off. If set -d, build with docker
    -A <Auth mode>                      Default off. If set -A, build chain with auth, and generate admin account.
    -a <Auth account>                   [Optional when Auth mode] Specify the admin account address.
    -w <WASM mode>                      [Optional] Whether to use the wasm virtual machine engine, default is false
    -m <fisco-bcos monitor>             [Optional] node monitor or not, default is false
    -i <fisco-bcos monitor ip/port>     [Optional] When expanding the node, should specify ip and prot
    -M <fisco-bcos monitor>             [Optional] When expanding the node, specify the path where prometheus are located
    -h Help

deploy nodes e.g
    bash $0 -p 30300,20200 -l 127.0.0.1:4 -o nodes -e ./fisco-bcos
    bash $0 -p 30300,20200 -l 127.0.0.1:4 -o nodes -e ./fisco-bcos -m
    bash $0 -p 30300,20200 -l 127.0.0.1:4 -o nodes -e ./fisco-bcos -s
expand node e.g
    bash $0 -C expand -c config -d config/ca -o nodes/127.0.0.1/node5 -e ./fisco-bcos 
    bash $0 -C expand -c config -d config/ca -o nodes/127.0.0.1/node5 -e ./fisco-bcos -m -i 127.0.0.1:5 -M monitor/prometheus/prometheus.yml
    bash $0 -C expand -c config -d config/ca -o nodes/127.0.0.1/node5 -e ./fisco-bcos -s
EOF
    exit 0
}

parse_params() {
    while getopts "l:C:c:o:e:p:d:v:i:M:wDshmAa:" option; do
        case $option in
        l)
            ip_param=$OPTARG
            use_ip_param="true"
            ;;
        o)
            output_dir="$OPTARG"
            ;;
        e)
            binary_path="$OPTARG"
            file_must_exists "${binary_path}"
            ;;
        C) command="${OPTARG}"
            ;;
        d) ca_dir="${OPTARG}"
        ;;
        c) config_path="${OPTARG}"
        ;;
        p)
            port_start=(${OPTARG//,/ })
            if [ ${#port_start[@]} -ne 2 ]; then LOG_WARN "p2p start port error. e.g: 30300" && exit 1; fi
            ;;
        s) sm_mode="true" ;;
        m) 
           monitor_mode="true" 
           ;;
        i) 
           mtail_ip_param="${OPTARG}"
           ;;   
        M) prometheus_dir="${OPTARG}" ;;   
        D) docker_mode="true"
           if [ -n "${macOS}" ];then
                LOG_FATAL "Not support docker mode for macOS now"
           fi
        ;;
        A) auth_mode="true" ;;
        w) wasm_mode="true";; 
        a)
          auth_mode="true"
          auth_admin_account="${OPTARG}"
        ;;
        v) compatibility_version="${OPTARG}";;
        h) help ;;
        *) help ;;
        esac
    done
}

print_result() {
    echo "=============================================================="
    if [ -z "${docker_mode}" ];then
        LOG_INFO "${binary_name} Path     : ${binary_path}"
    else
        LOG_INFO "docker mode     : ${docker_mode}"
        LOG_INFO "docker tag      : ${compatibility_version}"
    fi
    LOG_INFO "Auth Mode           : ${auth_mode}"
    if ${auth_mode} ; then
        LOG_INFO "Auth init account   : ${auth_admin_account}"
    fi
    LOG_INFO "Start Port          : ${port_start[*]}"
    LOG_INFO "Server IP           : ${ip_array[*]}"
    LOG_INFO "SM Model            : ${sm_mode}"
    LOG_INFO "output dir          : ${output_dir}"
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

generate_node_scripts() {
    local output=${1}
    local docker_mode="${2}"
    local docker_tag="${compatibility_version}"
    local ps_cmd="\$(ps aux|grep \${fisco_bcos}|grep -v grep|awk '{print \$2}')"
    local start_cmd="nohup \${fisco_bcos} -c config.ini -g config.genesis >>nohup.out 2>&1 &"
    local stop_cmd="kill \${node_pid}"
    local pid="pid"
    local log_cmd="tail -n20  nohup.out"
    local check_success="\$(${log_cmd} | grep running)"
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

#启动监控程序
dirs=(\$(ls -l \${SHELL_FOLDER} | awk '/^d/ {print \$NF}'))
for dir in \${dirs[*]}
do
    if [[ -f "\${SHELL_FOLDER}/\${dir}/node.mtail" && -f "\${SHELL_FOLDER}/\${dir}/start_mtail_monitor.sh" ]];then
        echo "try to start \${dir}"
        bash \${SHELL_FOLDER}/\${dir}/start_mtail_monitor.sh &
    fi
done


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

#启动监控程序
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
    local start_cmd="nohup \${mtail} -logtostderr -progs \${mtailScript} -logs \${logDir} -port ${port} >>nohup.out 2>&1 &"
    local stop_cmd="kill \${node_pid}"
    local pid="pid"
    local log_cmd="tail -n20  nohup.out"
    local check_success="\$(${log_cmd} | grep Listening)"

    mkdir -p $(dirname $output/mtail/node.mtail)
    cat <<EOF >> "${output}/mtail/node.mtail"
hidden text host
host = "${ip}"

#节点
hidden text node
node = "${node}" 

#链id
hidden text chain
chain = "chain0"



#账本id
hidden text group
group = "group0"


gauge p2p_session_actived by host , node
/\[P2PService\]\[Service\]heartBeat,connected count=(?P<count>\d+)/ {
  p2p_session_actived[host][node] = \$count
}

gauge block_exec_duration_milliseconds_gauge by chain , group , host , node
/\[CONSENSUS\]\[Core\]asyncExecuteBlock success.*?timeCost=(?P<timeCost>\d+)/ {
   block_exec_duration_milliseconds_gauge[chain][group][host][node] = \$timeCost
}


histogram block_exec_duration_milliseconds buckets 0, 50, 100, 150 by chain , group , host , node
/\[CONSENSUS\]\[Core\]asyncExecuteBlock success.*?timeCost=(?P<timeCost>\d+)/ {
   block_exec_duration_milliseconds[chain][group][host][node] = \$timeCost
}

gauge block_commit_duration_milliseconds_gauge by chain , group , host , node
/\[CONSENSUS\]\[PBFT\]\[STORAGE\]commitStableCheckPoint success.*?timeCost=(?P<timeCost>\d+)/ {
   block_commit_duration_milliseconds_gauge[chain][group][host][node] = \$timeCost
} 

histogram block_commit_duration_milliseconds buckets 0, 50, 100, 150 by chain , group , host , node
/\[CONSENSUS\]\[PBFT\]\[STORAGE\]commitStableCheckPoint success.*?timeCost=(?P<timeCost>\d+)/ {
   block_commit_duration_milliseconds[chain][group][host][node] = \$timeCost
}

gauge ledger_block_height by chain , group , host , node
/\[CONSENSUS\]\[PBFT\]reachNewView,committedIndex=(?P<committedIndex>\d+)/ {
  ledger_block_height[chain][group][host][node] = \$committedIndex
}

gauge txpool_pending_tx_size by chain , group , host , node
/\[CONSENSUS\]\[PBFT\]reachNewView,.*?unsealedTxs=(?P<unsealedTxs>\d+)/ {
  txpool_pending_tx_size[chain][group][host][node] = \$unsealedTxs
}
EOF

    generate_script_template "$output/mtail/start_mtail_monitor.sh"
    cat <<EOF >> "${output}/mtail/start_mtail_monitor.sh"
mtail=\${SHELL_FOLDER}/../../mtail
mtailScript=\${SHELL_FOLDER}/node.mtail
logDir=\'\${SHELL_FOLDER}/../log/*.log\'
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
    image: grafana/grafana-oss:latest
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
# The http port  to use
http_port = 3001
EOF

    generate_script_template "$output/start_monitor.sh"
    cat <<EOF >> "${output}/start_monitor.sh"

DOCKER_FILE=./compose.yaml
docker-compose -f \${DOCKER_FILE} up -d prometheus grafana 2>&1

EOF
    chmod u+x "${output}/start_monitor.sh"


    generate_script_template "$output/stop_monitor.sh"
    cat <<EOF >> "${output}/stop_monitor.sh"

DOCKER_FILE=./compose.yaml
docker-compose -f \${DOCKER_FILE} down

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
    local p2p_listen_port="${2}"
    local rpc_listen_port="${3}"
    cat <<EOF >"${output}"
[p2p]
    listen_ip=${listen_ip}
    listen_port=${p2p_listen_port}
    ; ssl or sm ssl
    sm_ssl=false
    nodes_path=${file_dir}
    nodes_file=${nodes_json_file_name}

[rpc]
    listen_ip=${listen_ip}
    listen_port=${rpc_listen_port}
    thread_count=4
    ; ssl or sm ssl
    sm_ssl=false
    ; ssl connection switch, if disable the ssl connection, default: false
    ;disable_ssl=true

[cert]
    ; directory the certificates located in
    ca_path=./conf
    ; the ca certificate file
    ca_cert=ca.crt
    ; the node private key file
    node_key=ssl.key
    ; the node certificate file
    node_cert=ssl.crt
EOF
    generate_common_ini "${output}"
}

generate_common_ini() {
    local output=${1}
    LOG_INFO "Begin generate uuid"
    local uuid=$(uuidgen)
    LOG_INFO "Generate uuid success: ${uuid}"
    cat <<EOF >>"${output}"

[chain]
    ; use SM crypto or not, should nerver be changed
    sm_crypto=${sm_mode}
    ; the group id, should nerver be changed
    group_id=group0
    ; the chain id, should nerver be changed
    chain_id=chain0

[security]
    private_key_path=conf/node.pem

[consensus]
    ; min block generation time(ms)
    min_seal_time=500

[executor]
    ; use the wasm virtual machine or not
    is_wasm=${wasm_mode}
    is_auth_check=${auth_mode}
    auth_admin_account=${auth_admin_account}

[storage]
    data_path=data
    enable_cache=true
    type=RocksDB
    pd_addrs=

[txpool]
    ; size of the txpool, default is 15000
    limit=15000
    ; txs notification threads num, default is 2
    notify_worker_num=2
    ; txs verification threads num, default is the number of CPU cores
    ;verify_worker_num=2
    ; txs expiration time, in seconds, default is 10 minutes
    txs_expiration_time = 600
[failover]
    ; enable failover or not, default disable
    enable = false
    ; the uuid that uniquely identifies the node
    member_id=${uuid}
    ; failover time, in seconds, default is 3s
    lease_ttl=3
    ; the address of etcd, can configure multiple comma-separated
    cluster_url=127.0.0.1:2379
[log]
    enable=true
    log_path=./log
    ; info debug trace
    level=DEBUG
    ; MB
    max_log_file_size=200
EOF
}

generate_sm_config_ini() {
    local output=${1}
    local p2p_listen_port="${2}"
    local rpc_listen_port="${3}"
    cat <<EOF >"${output}"
[p2p]
    listen_ip=${listen_ip}
    listen_port=${p2p_listen_port}
    ; ssl or sm ssl
    sm_ssl=true
    nodes_path=${file_dir}
    nodes_file=${nodes_json_file_name}

[rpc]
    listen_ip=${listen_ip}
    listen_port=${rpc_listen_port}
    thread_count=16
    ; ssl or sm ssl
    sm_ssl=true
    ;ssl connection switch, if disable the ssl connection, default: false
    ;disable_ssl=true

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
EOF
    generate_common_ini "${output}"
}

generate_nodes_json() {
    local output=${1}
    local p2p_host_list=""
    local ip_params="${2}"
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

    cat <<EOF >"${output}"
{"nodes":[${p2p_host_list}]}
EOF
}

generate_config() {
    local sm_mode="${1}"
    local node_config_path="${2}"
    local node_json_config_path="${3}"
    local connected_nodes="${4}"
    local p2p_listen_port="${5}"
    local rpc_listen_port="${6}"
    check_auth_account
    if [ "${sm_mode}" == "false" ]; then
        generate_config_ini "${node_config_path}" "${p2p_listen_port}" "${rpc_listen_port}"
    else
        generate_sm_config_ini "${node_config_path}" "${p2p_listen_port}" "${rpc_listen_port}"
    fi
    generate_nodes_json "${node_json_config_path}/${nodes_json_file_name}" "${connected_nodes}"
}

generate_secp256k1_node_account() {
    local output_path="${1}"
    local node_index="${2}"
    if [ ! -d "${output_path}" ]; then
        mkdir -p ${output_path}
    fi
    if [ ! -f /tmp/secp256k1.param ]; then
        ${OPENSSL_CMD} ecparam -out /tmp/secp256k1.param -name secp256k1
    fi
    ${OPENSSL_CMD} genpkey -paramfile /tmp/secp256k1.param -out ${output_path}/node.pem 2>/dev/null
    # generate nodeid
    ${OPENSSL_CMD} ec -text -noout -in "${output_path}/node.pem" 2>/dev/null | sed -n '7,11p' | tr -d ": \n" | awk '{print substr($0,3);}' | cat >"$output_path"/node.nodeid
    local node_id=$(cat "${output_path}/node.nodeid")
    nodeid_list=$"${nodeid_list}node.${node_index}=${node_id}: 1
    "
}

generate_sm_node_account() {
    local output_path="${1}"
    local node_index="${2}"
    if [ ! -d "${output_path}" ]; then
        mkdir -p ${output_path}
    fi
    if [ ! -f ${sm2_params} ]; then
        generate_sm_sm2_param ${sm2_params}
    fi
    ${OPENSSL_CMD} genpkey -paramfile ${sm2_params} -out ${output_path}/node.pem 2>/dev/null
    $OPENSSL_CMD ec -in "$output_path/node.pem" -text -noout 2> /dev/null | sed -n '7,11p' | sed 's/://g' | tr "\n" " " | sed 's/ //g' | awk '{print substr($0,3);}'  | cat > "$output_path/node.nodeid"
    local node_id=$(cat "${output_path}/node.nodeid")
    nodeid_list=$"${nodeid_list}node.${node_index}=${node_id}:1
    "
}

generate_genesis_config() {
    local output=${1}
    local node_list=${2}

    cat <<EOF >"${output}"
[consensus]
    ; consensus algorithm now support PBFT(consensus_type=pbft)
    consensus_type=pbft
    ; the max number of transactions of a block
    block_tx_count_limit=1000
    ; in millisecond, block consensus timeout, at least 3000ms
    consensus_timeout=3000
    ; the number of blocks generated by each leader
    leader_period=1
    ; the node id of consensusers
    ${node_list}

[version]
    ; compatible version, can be dynamically upgraded through setSystemConfig
    ; the default is 3.0.0-rc4
    compatibility_version=3.0.0-rc4
[tx]
    ; transaction gas limit
    gas_limit=3000000000
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
    var_name=var_${var_name//./}
    local res=$(eval echo '$'"${var_name}")
    echo ${res}
}

set_value() {
    local var_name=${1}
    var_name=var_${var_name//./}
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
    local x86_64_name="x86_64"
    local arm_name="aarch64"
    local tassl_mid_name="linux"
    if [[ -n "${macOS}" ]];then
        x86_64_name="i386"
        arm_name="arm"
        tassl_mid_name="macOS"
    fi

    local tassl_post_fix="x86_64"
    local platform="$(uname -p)"
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
    local tassl_link_prefix="${cdn_link_header}/FISCO-BCOS/tools/tassl-1.1.1b/${tassl_tgz_name}"
    LOG_INFO "Downloading tassl binary from ${tassl_link_prefix}..."
    wget --no-check-certificate  "${tassl_link_prefix}"
    tar zxvf ${tassl_tgz_name} && rm ${tassl_tgz_name}
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

    if [ "${monitor_mode}" ]; then
       ip=`echo $mtail_ip_param | awk '{split($0,a,":");print a[1]}'`
       num=`echo $mtail_ip_param | awk '{split($0,a,":");print a[2]}'`
       local port=$((mtail_listen_port + num))
       connected_mtail_nodes="${ip}:${port}"
       generate_mtail_scripts "${node_dir}" "${ip}" "${port}" "node${num}"
       sed -i  "s/]/,\'${connected_mtail_nodes}\']/g" ${prometheus_dir}
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
    if [ "${monitor_mode}" ];then
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
    # Note: must generate the node account firstly
    ca_dir="${output_dir}"/ca
    generate_chain_cert "${sm_mode}" "${ca_dir}"

    for line in ${ip_array[*]}; do
        ip=${line%:*}
        num=${line#*:}
        if [ -z $(echo $ip | grep -E "^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$") ]; then
            LOG_WARN "Please check IP address: ${ip}, if you use domain name please ignore this."
        fi
        [ "$num" == "$ip" ] || [ -z "${num}" ] && num=${node_num}
        echo "Processing IP:${ip} Total:${num}"
        [ -z "$(get_value ${ip//./}_count)" ] && set_value ${ip//./}_count 0

        nodes_dir="${output_dir}/${ip}"
        # start_all.sh and stop_all.sh 
        generate_all_node_scripts "${nodes_dir}"
        if [ -z "${docker_mode}" ];then
            cp "${binary_path}" "${nodes_dir}"
        fi
        if [ "${monitor_mode}" ];then 
            cp $mtail_binary_path "${nodes_dir}"   
        fi
        ca_cert_dir="${nodes_dir}"/ca
        mkdir -p ${ca_cert_dir}
        cp -r ${ca_dir}/* ${ca_cert_dir}

        # generate sdk cert
        generate_sdk_cert "${sm_mode}" "${ca_dir}" "${nodes_dir}/sdk"
        for ((i = 0; i < num; ++i)); do
            local node_count=$(get_value ${ip//./}_count)
            node_dir="${output_dir}/${ip}/node${node_count}"
            mkdir -p "${node_dir}"
            generate_node_cert "${sm_mode}" "${ca_dir}" "${node_dir}/conf"
            generate_node_scripts "${node_dir}" "${docker_mode}"
            if [ "${monitor_mode}" ];then 
                local port=$((mtail_listen_port + node_count))
                connected_mtail_nodes=${connected_mtail_nodes}"${ip}:${port}, "
                generate_mtail_scripts "${node_dir}" "${ip}" "${port}" "node${node_count}"
            fi
            local port=$((p2p_listen_port + node_count))
            connected_nodes=${connected_nodes}"${ip}:${port}, "
            account_dir="${node_dir}/conf"
            generate_node_account "${sm_mode}" "${account_dir}" "${count}"
            set_value ${ip//./}_count $(($(get_value ${ip//./}_count) + 1))
            ((++count))
            ((++node_count))
        done
    done

    if [ "${monitor_mode}" ];then 
        monitor_dir="${output_dir}/monitor"
        generate_monitor_scripts "${monitor_dir}" "${connected_mtail_nodes}"
    fi

    local i=0
    local count=0
    for line in ${ip_array[*]}; do
        ip=${line%:*}
        num=${line#*:}
        if [ -z $(echo $ip | grep -E "^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$") ]; then
            LOG_WARN "Please check IP address: ${ip}, if you use domain name please ignore this."
        fi
        [ "$num" == "$ip" ] || [ -z "${num}" ] && num=${node_num}
        set_value ${ip//./}_count 0
        for ((i = 0; i < num; ++i)); do
            local node_count=$(get_value ${ip//./}_count)
            node_dir="${output_dir}/${ip}/node${node_count}"
            local p2p_port=$((p2p_listen_port + node_count))
            local rpc_port=$((rpc_listen_port + node_count))
            generate_config "${sm_mode}" "${node_dir}/config.ini" "${node_dir}" "${connected_nodes}" "${p2p_port}" "${rpc_port}"
            generate_genesis_config "${node_dir}/config.genesis" "${nodeid_list}"
            set_value ${ip//./}_count $(($(get_value ${ip//./}_count) + 1))
            ((++count))
        done
    done
    print_result
}

check_auth_account()
{
  if ${auth_mode} ; then
      if [ -z "${auth_admin_account}" ]; then
        # get account string to auth_admin_account
        generate_auth_account
      fi
  fi
}

generate_auth_account()
{
  local account_script="get_account.sh"
  if ${sm_mode}; then
    account_script="get_gm_account.sh"
  fi

  if [ ! -f ${account_script} ]; then
        local get_account_link="${cdn_link_header}/FISCO-BCOS/tools/${account_script}"
        LOG_INFO "Downloading ${account_script} from ${get_account_link}..."
        curl -#LO "${get_account_link}"
  fi
  auth_admin_account=$(bash ${account_script} | grep Address | sed -r "s/\x1B\[([0-9]{1,2}(;[0-9]{1,2})?)?[m|K]//g" | awk '{print $5}')
  mv accounts* "${ca_dir}"
}

main() {
    check_env
    check_and_install_tassl
    parse_params "$@"
    if [[ "${command}" == "deploy" ]]; then
        deploy_nodes
    elif [[ "${command}" == "expand" ]]; then
        dir_must_exists "${ca_dir}"
        expand_node "${sm_mode}" "${ca_dir}" "${output_dir}" "${config_path}" "${mtail_ip_param}" "${prometheus_dir}"
    else
        LOG_FATAL "Unsupported command ${command}, only support \'deploy\' and \'expand\' now!"
    fi
}

main "$@"
