REM installing curl
cmake -E copy_directory package\curl install

REM zipping curl
cd package
if exist curl-7.4.2 cmake -E remove_directory curl-7.4.2
cmake -E rename curl curl-7.4.2
tar -zcvf curl-7.4.2.tar.gz curl-7.4.2
cd ..

