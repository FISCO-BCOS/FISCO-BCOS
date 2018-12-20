#! /bin/sh
LOG_WARN()
{
    local content=${1}
    echo -e "\033[31m${content}\033[0m"
}

LOG_INFO()
{
    local content=${1}
    echo -e "\033[32m${content}\033[0m"
}

set -e
[ ! -n "$2" ] && LOG_WARN "Usage: sh $0 <URL 127.0.0.1:31443> <datakey>" && exit;

cypherDataKey=$(curl -X POST --data '{"jsonrpc":"2.0","method":"encDataKey","params":["'$2'"],"id":83}' $1 |jq .result.dataKey  |sed 's/\"//g')
[ -z "$cypherDataKey" ] && echo "Generate failed." && exit;
echo "CiherDataKey generated: $cypherDataKey"
LOG_INFO "Append these into config.ini to enable disk encryption:"
echo "[diskencryption]
enable=true
keyCenterUrl=$1
cipherDataKey=$cypherDataKey"