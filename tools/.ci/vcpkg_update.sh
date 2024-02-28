#!/bin/bash
current_path=`pwd`
echo "current_path: ${current_path}"

current_baseline_path="../vcpkg-configuration.json"
old_baseline_path=".ci/old_baseline.txt"

# 从vcpkg-configuration.json文件中提取当前的baseline值
current_baseline=$(awk -F'"' '/"baseline":/{print $4}' "$current_baseline_path")
echo "current_baseline: ${current_baseline}"

# 在脚本中记录的旧baseline值
old_baseline=$(<"$old_baseline_path")
echo "old_baseline: ${old_baseline}"

# 比较当前baseline和旧baseline是否一致
if [ "$current_baseline" = "$old_baseline" ]; then
    echo "baseline not changed, vcpkg not need update..."
    exit 0
else
    echo "Baseline has changed, need ./bootstrap-vcpkg.sh ..."
    cd ../vcpkg  && git fetch origin master && ./bootstrap-vcpkg.sh && cd ..
    echo "Update current_baseline to  old_baseline.txt..."
    echo "$current_baseline" > "$old_baseline_path"
fi
