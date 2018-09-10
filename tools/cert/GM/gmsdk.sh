#!/bin/bash

function LOG_ERROR()
{
    local content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

function LOG_INFO()
{
    local content=${1}
    echo -e "\033[32m"${content}"\033[0m"
}

function execute_cmd()
{
    local command="${1}"
    eval ${command}
    local ret=$?
    if [ $ret -ne 0 ];then
        LOG_ERROR "execute command ${command} FAILED"
        exit 1
    else
        LOG_INFO "execute command ${command} SUCCESS"
    fi
}


if [ "" = "`java -version 2>&1 | grep version`" ];
then
    LOG_ERROR " Please Install JDK 1.8"
    exit;
fi
if [ "" = "`java -version 2>&1 | grep 1.8`" ];
then
    LOG_ERROR " Please Upgrade JDK To  1.8"
    exit;
fi


function gen_sdk_cert() {
	local sdk="${1}/${2}"
	local agency_name="${1}"
	local node_dir="${sdk}"
	local manual="yes"
	if [ $# -gt 2 ];then
		sdk="${2}/${1}"
		node_dir="${2}"
		manual="${3}"
	fi
	if [ -z "$sdk" ];  then
		LOG_INFO "Usage:sdk.sh   sdk_name node_dir ${manual}"
	elif [  -f "${sdk}/server.crt" ]; then
		LOG_ERROR "${sdk}/server.crt DIR exist! please clean all old DIR!"
	else
		mkdir -p ${sdk} && cp cert.cnf ${sdk}
		cd ${sdk}
		echo '
		[ca]
		default_ca=default_ca
		[default_ca]
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
		organizationalUnitName_default = fisco-dev
		commonName =  Organizational  commonName (eg, fisco-dev)
		commonName_default = fisco-dev
		commonName_max = 64
		[ v3_req ]
		# Extensions to add to a certificate request
		basicConstraints = CA:FALSE
		keyUsage = nonRepudiation, digitalSignature, keyEncipherment
		' > RSA.cnf
		LOG_INFO "----------------------gen sdk root ca------------------------------------"
		openssl genrsa -out ca.key 2048
		LOG_INFO "----------------------gen sdk server ca----------------------------------"
		if [ "${manual}" == "yes" ];then
			openssl req -config cert.cnf -new -x509 -days 3650 -key ca.key  -config cert.cnf -out ca.crt
			openssl genrsa -out server.key 2048
			openssl req -new -key server.key -config cert.cnf -out server.csr
			openssl x509 -req -days 3650 -CA ca.crt -CAkey ca.key -CAcreateserial -in server.csr -out server.crt -extensions v3_req -extfile RSA.cnf
			openssl pkcs12 -export -name client -in server.crt -inkey server.key -out keystore.p12
			keytool -importkeystore -destkeystore client.keystore -srckeystore keystore.p12 -srcstoretype pkcs12 -alias client
		else
			openssl req -config cert.cnf -new -x509 -days 3650 -key ca.key  -config cert.cnf -out ca.crt -batch
			openssl genrsa -out server.key 2048
			openssl req -new -key server.key -config cert.cnf -out server.csr -batch
			openssl x509 -req -days 3650 -CA ca.crt -CAkey ca.key -CAcreateserial -in server.csr -out server.crt -extensions v3_req -extfile RSA.cnf
			openssl pkcs12 -export -name client -in server.crt -inkey server.key -password pass:123456 -out keystore.p12
			keytool -importkeystore -srcstorepass 123456 -storepass 123456 -destkeystore client.keystore -srckeystore keystore.p12 -srcstoretype pkcs12 -alias client
		fi
			rm -rf ca.srl server.csr RSA.cnf keystore.p12
		if [ "${sdk}" != "${node_dir}" ];then
			cp ca.crt ca.key server.crt server.key client.keystore ${node_dir}
		fi
	fi
}

gen_sdk_cert "$@"
