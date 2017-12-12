REM installing jsonrpccpp
cmake -E copy_directory package\jsonrpccpp install

REM zipping jsonrpccpp
cd package 
if exist json-rpc-cpp-0.5.0 cmake -E remove_directory json-rpc-cpp-0.5.0
cmake -E rename jsonrpccpp json-rpc-cpp-0.5.0
tar -zcvf json-rpc-cpp-0.5.0.tar.gz json-rpc-cpp-0.5.0
cd ..

