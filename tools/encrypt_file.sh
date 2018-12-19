#! /bin/sh
set -e
[ ! -n "$3" ] && echo "Usage: sh $0 <URL 127.0.0.1:31443> <file> <cipherdatakey>" && exit;

fileStream=`base64 $2 |tr -d "\n"`
echo $fileStream

curl -X POST --data '{"jsonrpc":"2.0","method":"encWithCipherKey","params":["'$fileStream'", "'$3'"],"id":83}' $1 |jq

cypherDataKey=`curl -X POST --data '{"jsonrpc":"2.0","method":"encWithCipherKey","params":["'$fileStream'", "'$3'"],"id":83}' $1 |jq .result.dataKey  |sed 's/\"//g'`
[ -z "$cypherDataKey" ] && echo "Generate failed." && exit;
echo "Encfile is: $cypherDataKey"
echo -e $cypherDataKey"\c > $2.enc