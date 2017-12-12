REM installing leveldb
cmake -E copy_directory package\leveldb install

REM zipping leveldb
cd package
if exist leveldb-1.2 cmake -E remove_directory leveldb-1.2
cmake -E rename leveldb leveldb-1.2
tar -zcvf leveldb-1.2.tar.gz leveldb-1.2
cd ..

