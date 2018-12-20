#! /bin/sh
set -e
[ ! -n "$2" ] && echo "Usage: sh $0 <URL 127.0.0.1:31443> <datakey>" && exit;

cypherDataKey=$(curl -X POST --data '{"jsonrpc":"2.0","method":"encDataKey","params":["'$2'"],"id":83}' $1 |jq .result.dataKey  |sed 's/\"//g')
[ -z "$cypherDataKey" ] && echo "Generate failed." && exit;
echo "CiherDataKey generated: $cypherDataKey
Append these into config.ini to enable disk encryption:

[diskencryption]
enable=true
keyCenterUrl=$1
cipherDataKey=$cypherDataKey"