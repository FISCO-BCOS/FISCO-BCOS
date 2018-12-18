#!/bin/bash
pkcs12_passwd=""

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
current_path=`pwd`
gen_cert_secp256k1() {
    capath="$1"
    certpath="$2"
    name="$3"
    type="$4"
    openssl ecparam -out $certpath/${type}.param -name secp256k1
    openssl genpkey -paramfile $certpath/${type}.param -out $certpath/${type}.key
    openssl pkey -in $certpath/${type}.key -pubout -out $certpath/${type}.pubkey
    openssl req -new -sha256 -subj "/CN=${name}/O=fiscobcos/OU=${type}" -key $certpath/${type}.key -config $capath/cert.cnf -out $certpath/${type}.csr
    openssl x509 -req -days 3650 -sha256 -in $certpath/${type}.csr -CAkey $capath/agency.key -CA $capath/agency.crt\
        -force_pubkey $certpath/${type}.pubkey -out $certpath/${type}.crt -CAcreateserial -extensions v3_req -extfile $capath/cert.cnf
    openssl ec -in $certpath/${type}.key -outform DER | tail -c +8 | head -c 32 | xxd -p -c 32 | cat >$certpath/${type}.private
    rm -f $certpath/${type}.csr
}

gen_sdk_cert() {
    if [ "" = "`openssl ecparam -list_curves 2>&1 | grep secp256k1`" ]; then
        echo "openssl don't support secp256k1, please upgrade openssl!"
        exit $EXIT_CODE
    fi

    agpath="$2"
    agency=`getname "$agpath"`
    sdkpath="$3"
    sdk=`getname "$sdkpath"`
    dir_must_exists "$agpath"
    file_must_exists "$agpath/agency.key"
    check_name agency "$agency"
    dir_must_not_exists "$sdkpath"
    check_name sdk "$sdk"

    mkdir -p $sdkpath
    gen_cert_secp256k1 "$agpath" "$sdkpath" "$sdk" sdk
    cd ${current_path}
    cat ${agpath}/agency.crt >> ${sdkpath}/sdk.crt
    cat ${agpath}/ca.crt >> ${sdkpath}/sdk.crt
    # create keystore
    openssl pkcs12 -export -name client -passout "pass:${pkcs12_passwd}" -in ${sdkpath}/sdk.crt -inkey ${sdkpath}/sdk.key -out ${sdkpath}/keystore.p12
    # keytool -importkeystore -destkeystore ${sdkpath}/client.keystore -srckeystore ${sdkpath}/keystore.p12 -srcstoretype pkcs12 -alias client
    #nodeid is pubkey
    openssl ec -in $sdkpath/sdk.key
    echo "build $sdk sdk cert successful!"
}
agencypath=
sdkpath="sdk"
help()
{
    echo "${1}"
    cat << EOF
Usage:
    -a <dir of agency cert>     [Required]
    -s <dir of the sdk cert>    [Optional] default is sdk
    -h Help
e.g: 
    bash sdk.sh -a agencyA -s sdk1
EOF
exit 0
}
checkParam()
{
    if [ "${agencypath}" == "" ];then
        echo "Must set agency directory"
        help
    fi
}
main()
{
while getopts "a:s:h" option;do
    case ${option} in
    a) agencypath=${OPTARG};;
    s) sdkpath=${OPTARG};;
    h) help;;
    esac
done
checkParam
sdkpath=${agencypath}"/"${sdkpath}
gen_sdk_cert "" ${agencypath} ${sdkpath}
#copy ca.crt
cp ${agencypath}/ca.crt ${sdkpath}/ca.crt
if [ $? -eq 0 ];then
    echo "Build  $sdkpath Crt suc!!!"
else
    echo "Build  $sdkpath Crt failed!!!"
fi
}
main "$@"
