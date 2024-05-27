#!/bin/bash
# "Copyright [2023] <fisco-bcos>"
# @ function: check unittest coverage of code file
# @ author  : kyonRay
# @ file    : check_coverage.sh
# @ date    : 2023

LOG_ERROR() {
  content=${1}
  echo -e "\033[31m"${content}"\033[0m"
}

LOG_INFO() {
  content=${1}
  echo -e "\033[32m"${content}"\033[0m"
}

execute_cmd() {
  command="${1}"
  eval ${command}
  ret=$?
  if [ $ret -ne 0 ]; then
    LOG_ERROR "FAILED of command: ${command}"
    exit 1
  else
    LOG_INFO "SUCCESS of command: ${command}"
  fi
}

find . -type f -name '*.gcno' -o -type f -name '*.gcda' | grep -vE "test**" | awk '
{
    split($0, arr, "/");
    dir_path = arr[2];
    if (substr($0, length($0) - 4) == ".gcno") {
            gcno_all++;
            gcno_files[dir_path]++;
    }
    else if (substr($0, length($0) - 4) == ".gcda") {
            gcda_all++
            gcda_files[dir_path]++;
    }
}
END {
  printf "%s\t%s\t%s\t%s\n", "gcno", "gcda", "cov", "dir"
  for (dir in gcno_files) {
    gcda = gcda_files[dir]
    if(gcda_files[dir]==""){
        gcda = 0
    }
    printf "%s\t%s\t%d%%\t%s\n", gcno_files[dir], gcda, gcda/gcno_files[dir]*100, dir
  }
  printf "%s\t%s\t%d%%\t%s\n", gcno_all, gcda_all, gcda_all/gcno_all*100, "ALL"
}'
