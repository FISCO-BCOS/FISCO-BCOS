#!/bin/bash

log_file=$1
start_block_number=$2
end_block_number=$3
key_words="EXECUTOR|STORAGE|SCHEDULER|METRIC"
filter_key_words="s_hash_2_tx|s_tables"

echo "Extracting log file: $log_file"
if [[ "x${end_block_number}" == "x" ]];then
    end_block_number=${start_block_number}
fi
temp_log="${log_file}_clean.log"
cat $log_file | egrep -ai "${key_words}" | egrep -aiv "${filter_key_words}" > "${temp_log}"
LANG=C sed -i "" -e "s/EXECUTOR\:[0-9]*/EXECUTOR/g" "${temp_log}"
LANG=C sed -i "" -e "s/requestTimestamp=[0-9]*//g" "${temp_log}"
LANG=C sed -i "" -e "s/timeCost=[0-9]*//g" "${temp_log}"

start_line=$(cat -n "${temp_log}" | grep -ai "${start_block_number}]ExecuteBlock request" | grep "${start_block_number}" | head -n 1 | awk '{print $1}')
end_line=$(cat -n "${temp_log}" | grep -ai "ExecuteBlock success" | grep "${end_block_number}" | head -n 1 | awk '{print $1}')
# end_line=$(cat -n "${temp_log}" | grep -ai "${end_block_number}]GetTableHashes success" | grep "${end_block_number}" | head -n 1 | awk '{print $1}')

echo "temp_log line: $start_line - $end_line is wanted"
result="${log_file}_${start_block_number}_${end_block_number}"
sed -n "${start_line},${end_line}p" "${temp_log}" > "${result}.log"
cat "${result}.log" | cut -d '|' -f 3-20  > "${result}_clean.log"
cat "${result}_clean.log" | sort  > "${result}_sort.log"
cat "${result}_sort.log" | grep -ai storage  > "${result}_sort_storage.log"
cat "${result}_sort_storage.log" | grep -ai "hash"  > "${result}_sort_storage_hash.log"

# cat "${result}_clean.log" | sort  > "${result}_sort.log"
echo "done. result in ${result}.log"
