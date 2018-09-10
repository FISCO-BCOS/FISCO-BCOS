#! /bin/sh
#stop fisco-bcos
name=`pwd`/config.json
agent_pid=`ps aux|grep "$name"|grep -v grep|awk '{print $2}'`
kill_cmd="kill -9 ${agent_pid}"
echo "${kill_cmd}"
eval ${kill_cmd}

#stop tail -f log
name=`pwd`/log/info
agent_pid=`ps aux|grep "$name"|grep -v grep|awk '{print $2}'`
kill_cmd="kill -9 ${agent_pid}"
echo "${kill_cmd}"
eval ${kill_cmd}
