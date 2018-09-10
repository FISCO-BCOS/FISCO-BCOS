#!/bin/bash
set -e

LOG_ERROR()
{
    local content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

LOG_INFO()
{
    local content=${1}
    echo -e "\033[32m"${content}"\033[0m"
}

execute_cmd()
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

yes_go_other_exit()
{
    read -r -p "[Y/n]: " input
    case $input in
        [yY][eE][sS]|[yY])
            ;;

        [nN][oO]|[nN])
            exit 1
                ;;

        *)
        exit 1
        ;;
    esac    
}

output_dir=
manual_input=
agency_dir=

this_script=$0
help() {
    LOG_ERROR "${1}"
    echo "Usage:"
    echo "    -d  <agency dir>    The agency cert dir that the node belongs to"
    echo "Optional:"
    echo "    -m                  Input agency information manually"
    echo "    -h                  This help"
    echo "Example:"
    echo "    bash $this_script -d /mydata/test_agency"
    echo "    bash $this_script -d /mydata/test_agency -m"
exit -1
}


while getopts "d:mh" option;do
	case $option in
    d) agency_dir=$OPTARG;;
    m) manual_input="yes";;
	h) help;;
	esac
done

[ -z $agency_dir ] && help 'Error: Please specify <agency dir> using -d'

generate_cert_config() {
    output_dir=$1
    echo '
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
organizationalUnitName_default = test_org
commonName =  Organizational  commonName (eg, test_org)
commonName_default = test_org
commonName_max = 64
[ v3_req ]
basicConstraints = CA:FALSE
keyUsage = nonRepudiation, digitalSignature, keyEncipherment
[ v4_req ]
basicConstraints = CA:TRUE
' > $output_dir/cert.cnf
}

generate_cert() {
    prev_dir=`pwd`
    agency_dir=$1

    generate_cert_config $agency_dir

    batch=
    [ ! -n "$manual_input" ] && batch=-batch

    cd  $agency_dir
    sdk_dir=sdk
    mkdir -p $sdk_dir
    
    openssl ecparam -out sdk.param -name secp256k1
    openssl genpkey -paramfile sdk.param -out sdk.key
    openssl pkey -in sdk.key -pubout -out sdk.pubkey
    openssl req -new -key sdk.key -config cert.cnf  -out sdk.csr $batch
    openssl x509 -req -days 3650 -in sdk.csr -CAkey agency.key -CA agency.crt -force_pubkey sdk.pubkey -out sdk.crt -CAcreateserial -extensions v3_req -extfile cert.cnf
    openssl ec -in sdk.key -outform DER |tail -c +8 | head -c 32 | xxd -p -c 32 | cat >sdk.private
   
    cp ca.crt ca-agency.crt
    more agency.crt | cat >>ca-agency.crt
    cp ca-agency.crt $sdk_dir
    mv sdk.key sdk.csr sdk.crt sdk.param sdk.private sdk.pubkey  $sdk_dir

    cd $sdk_dir
    mv ca-agency.crt ca.crt
    
    openssl pkcs12 -export -name client -in sdk.crt -inkey sdk.key -out keystore.p12
    keytool -importkeystore -destkeystore client.keystore -srckeystore keystore.p12 -srcstoretype pkcs12 -alias client

    cd $prev_dir
}


if [  -d "$agency_dir/sdk" ]; then
    echo "Attention! Duplicate generation of \"$agency_dir/sdk\". Overwrite?"
    yes_go_other_exit
fi

check_java_version() {
    if [ "" = "`java -version 2>&1 | grep version`" ];
    then
        help "Error: Please Install JDK 1.8"
    fi

    if [ "" = "`java -version 2>&1 | grep TM`" ];
    then
        help "Error: Please Install Java(TM) 1.8"
    fi

    if [ "" = "`java -version 2>&1 | grep 1.8`" ];
    then
        help "Error: Please Upgrade JDK To  1.8"
    fi
}

check_java_version
generate_cert $agency_dir
LOG_INFO "Generate success! SDK keys has been generated into \"`readlink -f $agency_dir/sdk`\""

