REM installing miniupnpc
cmake -E copy_directory package\miniupnpc install

REM zipping miniupnpc
cd package 
if exist miniupnpc-1.9 cmake -E remove_directory miniupnpc-1.9
cmake -E rename miniupnpc miniupnpc-1.9
tar -zcvf miniupnpc-1.9.tar.gz miniupnpc-1.9
cd ..

