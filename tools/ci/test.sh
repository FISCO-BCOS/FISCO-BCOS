 # "Copyright [2018] <fisco-bcos>"
 # @ function : run unitest
 # @ Require  : build fisco-bcos project
 # @ attention: if dependecies are downloaded failed, 
 #              please fetch packages into "deps/src" from https://github.com/bcosorg/lib manually
 #              and execute this shell script later
 # @ author   : jimmyshi  
 # @ file     : test.sh
 # @ date     : 2018

#!/bin/sh

current_dir=`pwd`/../../
${current_dir}/build/test/test-fisco-bcos -- --testpath=${current_dir}/test/data/ 
