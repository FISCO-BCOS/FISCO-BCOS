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
ca_dir=
manual_input=
agency_name=

this_script=$0
help() {
    LOG_ERROR "${1}"
    echo "Usage:"
    echo "    -c <ca dir>         The dir of chain cert files(ca.crt and ca.key)"
    echo "    -o <output dir>     Where agency.crt agency.key generate "
    echo "    -n <agency name>    Name of agency"
    echo "Optional:"
    echo "    -m                  Input agency information manually"
    echo "    -g                  Generate agency certificates with guomi algorithms"
    echo "    -d                  The Path of Guomi Directory"
    echo "    -h                  This help"
    echo "Example:"
    echo "    bash $this_script -c /mydata -o /mydata -n test_agency"
    echo "    bash $this_script -c /mydata -o /mydata -n test_agency -m"
    echo "guomi Example:"
    echo "    bash $this_script -c /mydata -o /mydata -n test_agency -g"
    echo "    bash $this_script -c /mydata -o /mydata -n test_agency -m -g" 
exit -1
}

enable_guomi=0
guomi_dir=../cert/GM
while getopts "c:o:n:d:gmh" option;do
	case $option in
    c) ca_dir=$OPTARG;;
	o) output_dir=$OPTARG;;
    n) agency_name=$OPTARG;;
    d) guomi_dir=${OPTARG};;
    g) enable_guomi=1;;
    m) manual_input="yes";;
	h) help;;
	esac
done

[ -z $ca_dir ] && help 'Error: Please specify <ca dir> using -c'
[ -z $output_dir ] && help 'Error: Please specify <output dir> using -o'
[ -z $agency_name ] && help 'Error: Please specify <agency name> using -n'

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
    output_dir=$1
    cp $ca_dir/ca.crt $output_dir/
    generate_cert_config $output_dir

    batch=
    [ ! -n "$manual_input" ] && batch=-batch

    openssl genrsa -out $output_dir/agency.key 2048
    openssl req -new  -key $output_dir/agency.key -config $output_dir/cert.cnf -out $output_dir/agency.csr $batch
    openssl x509 -req -days 3650 -CA $ca_dir/ca.crt -CAkey $ca_dir/ca.key -CAcreateserial -in $output_dir/agency.csr -out $output_dir/agency.crt  -extensions v4_req -extfile $output_dir/cert.cnf
}

current_dir=`pwd`
function generate_guomi_cert() {
    local agency_name="${1}"
    local agency_dir="${2}"
    local ca_dir="${3}"
    local guomi_dir="${4}"
    local manual="no"
    if [ "${manual_input}" == "yes" ];then
        manual="yes"
    fi
    execute_cmd "cd ${guomi_dir}"
    execute_cmd "chmod a+x gmagency.sh"
    execute_cmd "./gmagency.sh ${agency_name} ${agency_dir} ${ca_dir} ${manual}"
    execute_cmd "cd ${current_dir}"
}

##generate guomi cert
if [ ${enable_guomi} -eq 1 ];then
    generate_guomi_cert ${agency_name} ${output_dir} ${ca_dir} ${guomi_dir}
    exit 0
fi


name=`readlink -f $output_dir/$agency_name`  
if [ -z "$name" ];  then
    help "Error: Please input agency name!"
    exit 1
elif [  -d "$name" ]; then
    echo "Attention! Duplicate generation of \"$name\". Overwrite?"
    yes_go_other_exit
fi
mkdir -p $name
generate_cert $name
LOG_INFO "Generate success! Agency keys has been generated into "$name

