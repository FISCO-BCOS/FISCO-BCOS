#!/bin/bash
function Usage()
{
    echo "Usage:"
    echo "bash $0 log_file_path start_time end_time"
    exit 1
}


log_file="${1}"
if [ $# -eq 1 ];then
    if [ "${log_file}"="-h" ]||[ "${log_file}"="--help" ];then
       Usage
    fi
fi
if [ $# -lt 3 ];then
    Usage
fi

start_time="${2}"
end_time="${3}"
#txList=$(cat "${log_file}" | grep Report | awk -F',' '{print $5}' | cut -d'=' -f2 )
timeList=$(grep Report "${log_file}" | grep PBFT | awk -F' ' '{print $2}' | cut -d ' ' -f2 | cut -d'|' -f1 | sort | uniq)
txTotal=0
statistic_end=${end_time}
statistic_start=${start_time}
started="0"
for time in ${timeList}
do
   if [ "${time}" \> "${start_time}" ]&&[ "${time}" \< "$end_time" ];then
	#echo "====time:${time}"
        if [ "${started}" == "0" ];then
           started="1"
           statistic_start="${time}"  
        else
   	txList=$(grep Report "${log_file}" | grep PBFT| grep ${time} | awk -F',' '{print $6}' | cut -d'=' -f2 )
   	for tx in ${txList}
   	do
  	    txTotal=$((txTotal+tx))
   	done
        statistic_end=${time}
        fi
   fi
done
echo "statistic_end = ${statistic_end}"
echo "statistic_start = ${statistic_start}"

start_hour=$(echo ${statistic_start} | awk -F':' '{print $1}')
start_min=$(echo ${statistic_start} | awk -F':' '{print $2}')
start_s=$(echo ${statistic_start} | awk -F':' '{print $3}'| cut -d'.' -f1)
start_ms=$(echo ${statistic_start} | awk -F':' '{print $3}'| cut -d'.' -f2)
start_t=$(echo "$start_hour*3600000+ $start_min*60000 + $start_s*1000 + $start_ms/1000" | bc)

end_hour=$(echo ${statistic_end} | awk -F':' '{print $1}')
end_min=$(echo ${statistic_end} | awk -F':' '{print $2}')
end_s=$(echo ${statistic_end} | awk -F':' '{print $3}'| cut -d'.' -f1)
end_ms=$(echo ${statistic_end} | awk -F':' '{print $3}'| cut -d'.' -f2)
end_t=$(echo "$end_hour*3600000+ $end_min*60000 + $end_s*1000 + $end_ms/1000" | bc)

delta_time=$((end_t-start_t))

tps=$(echo "${txTotal}*1000/$delta_time" | bc)

#$(echo "scale=9; $interest_r/12 + 1.0" | bc)
echo "total transactions = ${txTotal}, execute_time = ${delta_time}ms, tps = ${tps} (tx/s)"
