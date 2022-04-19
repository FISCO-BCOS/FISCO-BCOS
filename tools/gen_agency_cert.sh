#!/bin/bash
set -e

guomi_mode=
logfile=${PWD}/build.log
ca_path=
gmca_path=
agency=
root_crt=
gmroot_crt=
TASSL_CMD="${HOME}"/.fisco/tassl
days=36500
rsa_key_length=2048
EXIT_CODE=1

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
        LOG_WARN "$name name [$value] invalid, it should match regex: ^[a-zA-Z0-9._-]+\$"
        exit "${EXIT_CODE}"
    }
}

file_must_exists() {
    if [ ! -f "$1" ]; then
        LOG_WARN "$1 file does not exist, please check!"
        exit "${EXIT_CODE}"
    fi
}

file_must_not_exists() {
    if [ -f "$1" ]; then
        LOG_WARN "$1 file exists, please check!"
        exit "${EXIT_CODE}"
    fi
}

dir_must_exists() {
    if [ ! -d "$1" ]; then
        LOG_WARN "$1 DIR does not exist, please check!"
        exit "${EXIT_CODE}"
    fi
}

dir_must_not_exists() {
    if [ -d "$1" ]; then
        LOG_WARN "$1 DIR exists, please clean old DIR!"
        exit "${EXIT_CODE}"
    fi
}

help()
{
    cat << EOF
Usage:
    -c <ca path>           [Required]
    -a <agency name>       [Required]
    -g <gm ca path>        gmcert ca key path, if generate gm node cert
    -X <Certificate expiration time>    Default 36500 days
    -h Help
e.g:
    bash $0 -c nodes/cert -a newAgency
    bash $0 -c nodes/cert -g nodes/gmcert -a newAgency
EOF
exit 0
}

parse_params()
{
    while getopts "c:a:g:hX:" option;do
        case $option in
        c) ca_path="${OPTARG}"
            if [ ! -f "$ca_path/ca.key" ]; then LOG_WARN "$ca_path/ca.key not exist" && exit 1; fi
            if [ ! -f "$ca_path/ca.crt" ]; then LOG_WARN "$ca_path/ca.crt not exist" && exit 1; fi
            if [ -f "$ca_path/root.crt" ]; then root_crt="$ca_path/root.crt";fi
        ;;
        a) agency="${OPTARG}"
            if [ -z "$agency" ]; then LOG_WARN "$agency not specified" && exit 1; fi
        ;;
        g) guomi_mode="yes" && gmca_path=$OPTARG
            if [ ! -f "$gmca_path/gmca.key" ]; then LOG_WARN "$gmca_path/gmca.key not exist" && exit 1; fi
            if [ ! -f "$gmca_path/gmca.crt" ]; then LOG_WARN "$gmca_path/gmca.crt not exist" && exit 1; fi
            if [ -f "$gmca_path/gmroot.crt" ]; then gmroot_crt="${gmca_path}/gmroot.crt"; fi
        ;;
        X) days="$OPTARG";;
        h) help;;
        *) LOG_WARN "invalid option $option";;
        esac
    done
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

gen_agency_cert() {
    local chain="${1}"
    local agencypath="${2}"
    name=$(basename "$agencypath")

    dir_must_exists "$chain"
    file_must_exists "$chain/ca.key"
    check_name agency "$name"
    agencydir="$agencypath"
    dir_must_not_exists "$agencydir"
    mkdir -p "$agencydir"

    # openssl genrsa -out "$agencydir/agency.key" 2048 2> /dev/null
    openssl ecparam -out "$agencydir/secp256k1.param" -name secp256k1 2> /dev/null
    openssl genpkey -paramfile "$agencydir/secp256k1.param" -out "$agencydir/agency.key" 2> /dev/null
    openssl req -new -sha256 -subj "/CN=$name/O=fisco-bcos/OU=agency" -key "$agencydir/agency.key" -out "$agencydir/agency.csr" 2> /dev/null
    openssl x509 -req -days ${days} -sha256 -CA "$chain/ca.crt" -CAkey "$chain/ca.key" -CAcreateserial\
        -in "$agencydir/agency.csr" -out "$agencydir/agency.crt"  -extensions v4_req -extfile "$chain/cert.cnf" 2> /dev/null

    cp "$chain/ca.crt" "$chain/cert.cnf" "$agencydir/"
    if [[ -n "${root_crt}" ]];then
        echo "Use user specified root cert as ca.crt, $agencydir" >>"${logfile}"
        cp "${root_crt}" "$agencydir/ca.crt"
    else
        cp "$chain/ca.crt" "$chain/cert.cnf" "$agencydir/"
    fi
    rm -f "$agencydir/agency.csr" "$agencydir/secp256k1.param"

    echo "build $name cert successful!"
}

check_and_install_tassl(){
    if [ ! -f "${TASSL_CMD}" ];then
        LOG_INFO "Downloading tassl binary ..."
        if [[ "$(uname)" == "Darwin" ]];then
            curl -#LO https://github.com/FISCO-BCOS/LargeFiles/raw/master/tools/tassl_mac.tar.gz
            mv tassl_mac.tar.gz tassl.tar.gz
        else
            curl -#LO https://github.com/FISCO-BCOS/LargeFiles/raw/master/tools/tassl.tar.gz
        fi
        tar zxvf tassl.tar.gz && rm tassl.tar.gz
        chmod u+x tassl
        mkdir -p "${HOME}"/.fisco
        mv tassl "${HOME}"/.fisco/tassl
    fi
}

generate_cert_conf_gm()
{
    local output="$1"
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

generate_gmsm2_param()
{
    local output="$1"
    cat << EOF > "${output}"
-----BEGIN EC PARAMETERS-----
BggqgRzPVQGCLQ==
-----END EC PARAMETERS-----

EOF
}

gen_agency_cert_gm() {
    local chain="${1}"
    local agencypath="${2}"
    name=$(basename "$agencypath")

    dir_must_exists "$chain"
    file_must_exists "$chain/gmca.key"
    check_name agency "$name"
    agencydir="${agencypath}-gm"
    dir_must_not_exists "$agencydir"
    mkdir -p "$agencydir"

    $TASSL_CMD genpkey -paramfile "$chain/gmsm2.param" -out "$agencydir/gmagency.key" >> "${logfile}" 2>&1
    $TASSL_CMD req -new -subj "/CN=$name/O=fisco-bcos/OU=agency" -key "$agencydir/gmagency.key" -config "$chain/gmcert.cnf" -out "$agencydir/gmagency.csr" >> "${logfile}" 2>&1
    $TASSL_CMD x509 -req -CA "$chain/gmca.crt" -CAkey "$chain/gmca.key" -days "${days}" -CAcreateserial -in "$agencydir/gmagency.csr" -out "$agencydir/gmagency.crt" -extfile "$chain/gmcert.cnf" -extensions v3_agency_root 2> /dev/null

    if [[ -n "${gmroot_crt}" ]];then
        echo "Use user specified gmroot cert as gmca.crt, $agencydir" >>"${logfile}"
        cp "${gmroot_crt}" "$agencydir/gmca.crt"
    else
        cp "$chain/gmca.crt" "$chain/gmcert.cnf" "$chain/gmsm2.param" "$agencydir/"
    fi
    rm -f "$agencydir/gmagency.csr"
}

gen_rsa_chain_cert() {
    local name="${1}"
    local chaindir="${2}"

    mkdir -p "${chaindir}"

    LOG_INFO "chaindir: ${chaindir}"

    file_must_not_exists "${chaindir}"/ca.key
    file_must_not_exists "${chaindir}"/ca.crt
    # file_must_exists "${cert_conf}"

    mkdir -p "$chaindir"
    dir_must_exists "$chaindir"

    openssl genrsa -out "${chaindir}"/ca.key "${rsa_key_length}" 2>/dev/null
    openssl req -new -x509 -days "${days}" -subj "/CN=${name}/O=fisco-bcos/OU=chain" -key "${chaindir}"/ca.key -out "${chaindir}"/ca.crt  2>/dev/null

    LOG_INFO "Generate rsa ca cert successfully!"
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

    openssl genrsa -out "${ndpath}"/"${type}".key "${rsa_key_length}" 2>/dev/null
    openssl req -new -sha256 -subj "/CN=FISCO-BCOS/O=fisco-bcos/OU=agency" -key "$ndpath"/"${type}".key -out "$ndpath"/"${type}".csr
    openssl x509 -req -days "${days}" -sha256 -CA "${capath}"/ca.crt -CAkey "$capath"/ca.key -CAcreateserial \
        -in "$ndpath"/"${type}".csr -out "$ndpath"/"${type}".crt -extensions v4_req 2>/dev/null

    openssl pkcs8 -topk8 -in "$ndpath"/"${type}".key -out "$ndpath"/pkcs8_node.key -nocrypt

    rm -f "$ndpath"/"$type".csr
    rm -f "$ndpath"/"$type".key

    mv "$ndpath"/pkcs8_node.key "$ndpath"/"$type".key
    cp "$capath/ca.crt" "$ndpath"

    # LOG_INFO "Generate ${ndpath} node rsa cert successful!"
}

main()
{
    if openssl version | grep -q reSSL ;then
        export PATH="/usr/local/opt/openssl/bin:$PATH"
    fi
    generate_cert_conf "${ca_path}/cert.cnf"
    gen_agency_cert "${ca_path}" "${ca_path}/${agency}" > "${logfile}" 2>&1
    # gen rsa cert for channel ssl
    gen_rsa_chain_cert "${agency}" "${ca_path}/${agency}/channel" >>"${logfile}" 2>&1
    if [ -n "${guomi_mode}" ]; then
        check_and_install_tassl
        generate_cert_conf_gm "${gmca_path}/gmcert.cnf"
        generate_gmsm2_param "${gmca_path}/gmsm2.param"
        gen_agency_cert_gm "${gmca_path}" "${gmca_path}/${agency}" > "${logfile}"
    fi
    rm "${logfile}"
}

print_result()
{
    echo "=============================================================="
    LOG_INFO "Cert Path   : ${ca_path}/${agency}"
    [ -n "${guomi_mode}" ] && LOG_INFO "GM Cert Path: ${gmca_path}/${agency}-gm"
    LOG_INFO "All completed."
}

parse_params "$@"
main
print_result
