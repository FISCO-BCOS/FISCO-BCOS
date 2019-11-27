#!/bin/bash
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
    openssl req -new -sha256 -subj "/CN=$name/O=fiscobcos/OU=agency" -key $agencydir/agency.key -config $chain/cert.cnf -out $agencydir/agency.csr
    openssl x509 -req -days 3650 -sha256 -CA $chain/ca.crt -CAkey $chain/ca.key -CAcreateserial\
        -in $agencydir/agency.csr -out $agencydir/agency.crt  -extensions v4_req -extfile $chain/cert.cnf
    
    cp $chain/ca.crt $chain/cert.cnf $agencydir/
    cp $chain/ca.crt $agencydir/ca-agency.crt
    more $agencydir/agency.crt | cat >>$agencydir/ca-agency.crt
    rm -f $agencydir/agency.csr

    echo "build $name agency cert successful!"
}

chain=
agency=

help()
{
    echo "${1}"
    cat << EOF
Usage:
    -c <ca directory>           [Required]
    -a <agency directory>       [Required]
    -h Help
e.g: 
    bash agency.sh -c nodes/cert -a agencyA
EOF
exit 0
}

checkParam()
{
if [ "${chain}" == "" ];  then
    echo "must set chain certificate directory"
    help
elif [  "${agency}" == "" ]; then
    echo "must set agency directory"
    help
fi
}

main()
{
while getopts "c:a:h" option;do
    case ${option} in
    c) chain=${OPTARG};;
    a) agency=${OPTARG};;
    h) help;;
    esac
done
checkParam
if [ ! -z "$(openssl version | grep reSSL)" ];then
    export PATH="/usr/local/opt/openssl/bin:$PATH"
fi
gen_agency_cert "" ${chain}  ${agency} > build.log 2>&1
rm build.log
if [ $? -eq 0 ];then
    echo "Build ${agency} Agency Crt suc!!!"
else
    echo "Build ${agency} Agency Crt failed!!!"
fi
}

main "$@"
