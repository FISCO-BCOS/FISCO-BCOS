#!/bin/sh
set -e

PRE_DIR=`pwd`
this_script=`readlink -f $0`
THIS_SH_DIR=`dirname $this_script`

echo "Build in $THIS_SH_DIR"

cd $THIS_SH_DIR/web3lib
cnpm install

cd $THIS_SH_DIR/contract
cnpm install

cd $THIS_SH_DIR/systemcontract
cnpm install

cd $PRE_DIR
