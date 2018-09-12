#!/bin/bash

name=$1

if [ -z "$name" ];  then
    echo "Usage:agency.sh node_name "
elif [  -d "$name" ]; then
    echo "$name DIR exist! please clean all old DIR!"
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
    openssl x509 -req  -in node.csr -CAkey ca/ca.key -CA ca/ca.crt -out node.crt -CAcreateserial -extensions v3_req -extfile cert.cnf 
    openssl ec -in node.key -text 2> /dev/null | perl -ne '$. > 6 and $. < 12 and ~s/[\n:\s]//g and print' | perl -ne 'print substr($_, 2)."\n"' > node.nodeid
    mkdir $name
	
	cp ca.crt cert.cnf $name
    mv node.nodeid node.key node.csr node.crt $name
    echo "Build $name node Crt suc!!!"
fi
