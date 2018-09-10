#!/bin/bash

if [  -f "ca.key" ]; then
    echo "ca.key exist! please clean all old file!"
elif [  -f "ca.crt" ]; then
    echo "ca.crt exist! please clean all old file!"
else
    openssl genrsa -out ca.key 2048
    openssl req -new -x509 -days 3650 -key ca.key -out ca.crt 

    echo "Build Ca suc!!!"
fi

