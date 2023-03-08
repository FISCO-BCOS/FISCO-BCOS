#!/bin/bash
execT=$(cat $1 |grep -iaE "asyncExecuteBlock success,sysBlock|BlockSync: applyBlock success" |awk -F '=' '{print $9}' |awk -F ',' '{print $1}' |awk '{sum+=$1} END {print sum/NR}')
waitT=$(cat $1 |grep -ia "waitT" |awk -F '=' '{print $8}' |awk '{sum+=$1} END {print sum/NR}')
commitT=$(cat $1 |grep -iaE "CommitBlock success" |awk -F '=' '{print $3}' |awk '{sum+=$1} END {print sum/NR}')
echo -e "exec\t\t" ${execT}
echo -e "wait\t\t" ${waitT}
echo -e "commit\t\t" ${commitT}
echo " -------------------- "
cat $1 |grep -iaE "tx_pool_in" |grep -v "lastQPS(request/s)=0" |awk -F '=' '{print $7}' |awk '{sum+=$1} END {print "inTps   \t", sum/NR}'
cat $1 |grep -iaE "tx_pool_seal" |grep -v "lastQPS(request/s)=0" |awk -F '=' '{print $7}' |awk '{sum+=$1} END {print "sealTps \t", sum/NR}'
cat $1 |grep -iaE "tx_pool_rm" |grep -v "lastQPS(request/s)=0" |awk -F '=' '{print $7}' |awk '{sum+=$1} END {print "rmTps   \t", sum/NR}'
echo " -------------------- "
blkTxs=$(cat $1 |grep "Report,sea" |awk -F '=' '{print $3}' |awk -F ',' '{print $1}'|awk '{sum+=$1} END {print sum/NR}')
echo -e "blkTxs\t\t" ${blkTxs}
cat $1 |grep "Report,sea" |awk -F '=' '{print $3}' |awk -F ',' '{print $1}'|awk '{sum+=$1} END {print "totalTxs \t", sum}'
maxGap=$(echo "$execT + $waitT"|bc)
tps=$(echo "1000 / $maxGap * $blkTxs"|bc)
echo -e "tps\t\t" ${tps}


