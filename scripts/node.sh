#!/bin/bash
set -e

nodeName=
SHELL_FOLDER=$(cd "$(dirname "$0")";pwd)
CLIENTCERT_PWD=123456
KEYSTORE_PWD=123456

if [ $# -eq 1  ];  then
    nodeName=`pwd`/$1
elif [  $# -eq 2 ]; then
    nodeName=`pwd`/$1
    CLIENTCERT_PWD=$2
elif [  $# -eq 3 ]; then
    nodeName=`pwd`/$1
    CLIENTCERT_PWD=$2
    KEYSTORE_PWD=$3
else
    echo "Usage:node.sh nodeName [CLIENTCERT_PWD] [KEYSTORE_PWD]"
    echo "nodeName is relative to the current path"
    echo "CLIENTCERT_PWD default 123456"
    echo "KEYSTORE_PWD default 123456"
    exit 1
fi

if [ -d "${nodeName}" ]; then
    echo "${nodeName} DIR exist! please clean all old DIR!"
elif [ ! -f ${SHELL_FOLDER}/ca.key ] || [ ! -f ${SHELL_FOLDER}/ca.crt ];then
	echo "${SHELL_FOLDER}/ca.key or ca.crt not exist!"
else
    cat << EOF > cert.cnf
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
	stateOrProvinceName_default = GuangDong 
	localityName = Locality Name (eg, city) 
	localityName_default = ShenZhen 
	organizationalUnitName = Organizational Unit Name (eg, section) 
	organizationalUnitName_default = WeBank
	commonName =  Organizational  commonName (eg, WeBank)
	commonName_default = WeBank
	commonName_max = 64 
	[ v3_req ] 
	# Extensions to add to a certificate request 
	basicConstraints = CA:FALSE 
	keyUsage = nonRepudiation, digitalSignature, keyEncipherment
EOF
    openssl ecparam -out server.param -name secp256k1 
    openssl genpkey -paramfile server.param -out node.key 
    openssl req -new -key node.key -config cert.cnf -out node.csr 
    openssl x509 -req  -in node.csr -CAkey ${SHELL_FOLDER}/ca.key -CA ${SHELL_FOLDER}/ca.crt -out node.crt -CAcreateserial -extensions v3_req -extfile cert.cnf 
    openssl ec -in node.key -text 2> /dev/null | perl -ne '$. > 6 and $. < 12 and ~s/[\n:\s]//g and print' | perl -ne 'print substr($_, 2)."\n"' > node.nodeid
    mkdir -p ${nodeName}/data
	cp ${SHELL_FOLDER}/ca.crt ${nodeName}/data/
    mv node.nodeid node.key node.csr node.crt cert.cnf ${nodeName}/data/
	# generate sdk
	mkdir -p ${nodeName}/sdk
	echo ${CLIENTCERT_PWD} > ${nodeName}/sdk/pwd.conf
	# 使用node.key和node.crt，生成p12证书文件，此处要输入密码，按需要输入
	openssl pkcs12 -export -name client -in ${nodeName}/data/node.crt -inkey ${nodeName}/data/node.key -out keystore.p12 -password file:${nodeName}/sdk/pwd.conf
	# 生成JKS keystore，此处要输入JKS keystore的密码和上一步p12的密码，按需要输入
	keytool -importkeystore -destkeystore ${nodeName}/sdk/client.keystore -srckeystore keystore.p12 -srcstoretype pkcs12 -alias client -srcstorepass ${CLIENTCERT_PWD} -deststorepass ${KEYSTORE_PWD}

	cp ${SHELL_FOLDER}/ca.crt ${nodeName}/sdk/
	rm ${nodeName}/sdk/pwd.conf
	echo "All completed. All files in ${nodeName}"
	echo "============================="
	echo "node Path      : ${nodeName}"
	echo "CLIENT_CERT_PWD: $CLIENTCERT_PWD"
	echo "KEYSTORE_PWD   : $KEYSTORE_PWD"
	echo "node Key       : ${nodeName}/data/node.key"
	echo "sdk Dir        : ${nodeName}/sdk"
	echo "============================="
fi
