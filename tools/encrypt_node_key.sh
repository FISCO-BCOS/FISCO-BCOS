#! /bin/sh
LOG_WARN()
{
    local content=${1}
    echo -e "\033[31m[WARNING] ${content}\033[0m"
}

LOG_INFO()
{
    local content=${1}
    echo -e "\033[32m[INFO] ${content}\033[0m"
}

check_file()
{
    file=$1

    [ ! -f "$file" ] && LOG_WARN "$file is not exist." && exit;

    [ -z "$(grep 'BEGIN PRIVATE KEY' $file)" ] && LOG_WARN "\"$file\" has already encrypted." && exit;

    LOG_INFO "Check file Ok."
}

set -e
[ ! -n "$3" ] && LOG_WARN "Usage: sh $0 <URL 127.0.0.1:31443> <file> <cipherdatakey>" && exit;

URL=$1
ORIGIN_FILE=$2
CIPHER_KEY=$3
BACKUP_FILE=$ORIGIN_FILE.bak.$(date +%s)

check_file $ORIGIN_FILE

fileStream=$(base64 $ORIGIN_FILE |tr -d "\n")
#echo $fileStream

curl -X POST --data '{"jsonrpc":"2.0","method":"encWithCipherKey","params":["'$fileStream'", "'$CIPHER_KEY'"],"id":83}' $URL |jq

cypherDataKey=$(curl -X POST --data '{"jsonrpc":"2.0","method":"encWithCipherKey","params":["'$fileStream'", "'$CIPHER_KEY'"],"id":83}' $URL |jq .result.dataKey  |sed 's/\"//g')
[ -z "$cypherDataKey" ] && echo "Generate failed." && exit;
cp $ORIGIN_FILE $BACKUP_FILE
echo -e "$cypherDataKey\c" > $ORIGIN_FILE

LOG_INFO "File backup to \"$BACKUP_FILE\""
LOG_INFO "\"$ORIGIN_FILE\" encrypted!"