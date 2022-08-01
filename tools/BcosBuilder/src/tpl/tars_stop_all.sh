#!/bin/bash
dirpath="$(cd "$(dirname "$0")" && pwd)"
cd "${dirpath}"

dirs=($(ls -l ${dirpath} | awk '/^d/ {print $NF}'))
for dir in ${dirs[*]}
do
    if [[ -f "${dirpath}/${dir}/conf/config.ini" && -f "${dirpath}/${dir}/stop.sh" ]];then
        echo "try to stop ${dir}"
        bash ${dirpath}/${dir}/stop.sh
    fi
done
wait

