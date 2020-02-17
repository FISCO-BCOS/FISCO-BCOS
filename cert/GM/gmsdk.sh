#!/bin/bash

if [ "" = "`java -version 2>&1 | grep version`" ];
then
    echo " Please Install JDK 1.8"
    exit;
fi
if [ "" = "`java -version 2>&1 | grep 1.8`" ];
then
    echo " Please Upgrade JDK To  1.8"
    exit;
fi

agency=$1
sdk=$2

if [ -z "$agency" ];  then
    echo "Usage:sdk.sh   agency_name sdk_name "
elif [ -z "$sdk" ];  then
    echo "Usage:sdk.sh   agency_name sdk_name "
elif [ ! -d "$agency" ]; then
    echo "$agency DIR Don't exist! please Check DIR!"
elif [  -d "$agency/$sdk" ]; then
    echo "$agency/$sdk DIR exist! please clean all old DIR!"
else
	cat >RSA.cnf <<EOT
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
organizationalUnitName_default = fisco
commonName =  Organizational  commonName (eg, fisco)
commonName_default = fisco
commonName_max = 64
[ v3_req ]
# Extensions to add to a certificate request
basicConstraints = CA:FALSE
keyUsage = nonRepudiation, digitalSignature, keyEncipherment
EOT
	echo "----------------------gen sdk root ca------------------------------------"
	mkdir $agency/$sdk
	openssl genrsa -out ca.key 2048
	openssl req -config cert.cnf -new -x509 -days 3650 -key ca.key -out ca.crt
	echo "----------------------gen sdk server ca----------------------------------"
	openssl genrsa -out server.key 2048
	openssl req -new -key server.key -config cert.cnf -out server.csr
	openssl x509 -req -days 3650 -CA ca.crt -CAkey ca.key -CAcreateserial -in server.csr -out server.crt -extensions v3_req -extfile RSA.cnf
	openssl pkcs12 -export -name client -in server.crt -inkey server.key -password pass:123456 -out keystore.p12
	keytool -importkeystore -srcstorepass 123456 -storepass 123456 -destkeystore client.keystore -srckeystore keystore.p12 -srcstoretype pkcs12 -alias client
	rm -rf ca.srl server.csr RSA.cnf keystore.p12
	mv ca.crt ca.key server.crt server.key client.keystore $agency/$sdk
fi