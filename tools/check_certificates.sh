#!/bin/bash
# "Copyright [2019] <FISCO BCOS>"
# @ function : check certificates to solve handshake failed (appliable for centos, ubuntu)
# @ Require  : openssl
# @ author   : asherli
# @ file     : check_certificates.sh
# @ date     : 2019

help() 
{
    echo $1
    cat << EOF
Usage:
    -t <check the validity period>     [Required] "certificates_path" eg "~/mydata/node.crt"
    -v <check the root certificate>    [Required] "root_certificates_path node_certificates_path" eg "~/mydata/node.crt"

e.g 
    $0 -t ~/ca.crt
    $0 -v ~/ca.crt ~/node.crt
EOF
}

echo_red()
{
    content=${1}
    echo -e "\033[31m${content}\033[0m"
}

function check_time()
{  
    certificates=$1
    cert_name=$(basename $certificates)
    echo "check ${cert_name} time started"
    end_time=$(openssl x509 -enddate -noout -in ${certificates} | cut -d "=" -f 2)
    start_time=$(openssl x509 -startdate -noout -in ${certificates} | cut -d "=" -f 2)
    ret=$?
    if [ $ret -ne 0 ];then
        echo_red "get certificate time failed, this is not a x509 cert"
        exit 1
    fi
    echo "${cert_name} valid time is NOT BEFORE ${start_time} and NOT AFTER ${end_time}"
    expiry_time=$( date -d "$end_time" +%s )
    get_time=$( date -d "$start_time" +%s )
    now_time=$(date +%s)
    if [[ $now_time < $get_time ]];then
        echo_red "Your time is before signed time => $start_time"
        exit 1
    fi
    if [[ $now_time > $expiry_time ]];then
        echo_red "Your certificate expiried, end time => $end_time"
        exit 1
    fi
    echo "check ${cert_name} time successful"

}

function verfy_certificate()
{
    ca_cert=$1
    node_cert=$2
    ca_name=$(basename ${ca_cert})
    node_name=$(basename ${node_cert})
    check_time ${ca_cert}
    check_time ${node_cert}
    if [ "${ca_name}" != "ca.crt" ];then
        echo_red "root certificate should be ca.crt, your root is ${ca_name}"
        exit 1
    fi
    agency_start=$(< ${node_cert} grep -n "BEGIN CERTIFICATE" | cut -d ":" -f 1 | sed -n "2,1p")
    if [ -z "${agency_start}" ];then
        echo_red "${node_name} type check failed, this certificate doesn't contain agency.crt"
        exit 1
    fi
    agency_end=$(< ${node_cert} grep -n "END CERTIFICATE" | cut -d ":" -f 1 | sed -n "2,1p")
    if [ -z "${agency_end}" ];then
        echo_red "${node_name} type check failed, this certificate doesn't contain agency.crt"
        exit 1
    fi
    sed -n "${agency_start},${agency_end}p" ${node_cert} > ./.agency.crt
    result=$(openssl verify -CAfile ${ca_cert} ./.agency.crt)
    ret=$?
    if [ $ret -ne 0 ];then
        echo_red "use ${ca_cert} verify ${node_cert} failed"
        echo ${result}
        exit 1
    fi
    rm ./.agency.crt
    echo "use ${ca_cert} verify ${node_name} successful"
}

function main()
{
    case "$1" in
    -t)
        check_time "$2"
        ;;
    -v)
        verfy_certificate "$2" "$3"
        ;;
    -h)
        help
        ;;
    *)
        help
    esac
}
main $1 $2 $3