#!/bin/bash

name=$1

if [ -z "$name" ];  then
    echo "Usage:agency.sh    agency_name "
elif [  -d "$name" ]; then
    echo "$name DIR exist! please clean all old DIR!"
else
    mkdir $name

    openssl genrsa -out agency.key 2048
    openssl req -new  -key agency.key -config cert.cnf -out agency.csr
    openssl x509 -req -days 3650 -CA ca.crt -CAkey ca.key -CAcreateserial -in agency.csr -out agency.crt  -extensions v4_req -extfile cert.cnf

    cp ca.crt cert.cnf $name/
    mv agency.key agency.csr agency.crt  $name/
    echo "Build $name Agency Crt suc!!!"
fi
