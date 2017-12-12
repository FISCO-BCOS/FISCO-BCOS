REM installing cryptopp
cmake -E copy_directory package\cryptopp install

REM zipping cryptopp
cd package
if exist cryptopp-5.6.2 cmake -E remove_directory cryptopp-5.6.2
cmake -E rename cryptopp cryptopp-5.6.2
tar -zcvf cryptopp-5.6.2.tar.gz cryptopp-5.6.2
cd ..

