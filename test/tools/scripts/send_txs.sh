#! /bin/sh
# This script only support for block number smaller than 65535 - 256

block_limit()
{
    blockNumber=`curl -s -X POST --data '{"jsonrpc":"2.0","method":"syncStatus","params":[1],"id":83}' $1 |jq .result.blockNumber`
    printf "%04x" $(($blockNumber+256))
}

send_a_tx()
{
    txBytes="f8f0a02ade583745343a8f9a70b40db996fbe69c63531832858`date +%s%N`85174876e7ff8609184e729fff82`block_limit $1`94d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7f7395028658d0e01b86a371ca0e33891be86f781ebacdafd543b9f4f98243f7b52d52bac9efa24b89e257a354da07ff477eb0ba5c519293112f1704de86bd2938369fbf0db2dff3b4d9723b9a87d"
    #echo $txBytes
    curl -s -X POST --data '{"jsonrpc":"2.0","method":"sendRawTransaction","params":[1, "'$txBytes'"],"id":83}' $1 |jq
}

send_many_tx()
{
    for j in $(seq 1 $2)
    do
        echo 'Send transaction: ' $j
        send_a_tx $1 
    done
}


[ ! -n "$1" ] && echo "Usage: sh $0 <URL 127.0.0.1:30302> <Txs number to send>" && exit;

send_many_tx $1 $2
