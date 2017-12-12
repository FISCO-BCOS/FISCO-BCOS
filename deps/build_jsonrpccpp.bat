set MAINDIR="%cd%"
REM clone jsonrpccpp
if not exist build\jsonrpccpp git clone -q https://github.com/debris/libjson-rpc-cpp build\jsonrpccpp
cd build\jsonrpccpp
git checkout -qf fb5cde7cb677fd14f916cda04b28f45461bfc9e2

REM create jsonrpccpp build dirs
if %PLATFORM% == Win32 if not exist build mkdir build
if %PLATFORM% == Win32 cd build
if %PLATFORM% == x64 if not exist build64 mkdir build64
if %PLATFORM% == x64 cd build64

REM run jsonrpccpp cmake
if %PLATFORM% == Win32 cmake -DCMAKE_PREFIX_PATH="%MAINDIR%\install\Win32" -DCOMPILE_STUBGEN=NO -DCOMPILE_TESTS=NO ..
if %PLATFORM% == x64 cmake -G "Visual Studio 14 2015 Win64" -DCMAKE_PREFIX_PATH="%MAINDIR%\install\x64" -DCOMPILE_STUBGEN=NO -DCOMPILE_TESTS=NO ..

REM build jsonrpccpp
%MSBuild% libjson-rpc-cpp.sln /property:Configuration=%CONFIGURATION% /property:Platform=%PLATFORM% /target:jsonrpcclientStatic /verbosity:minimal
%MSBuild% libjson-rpc-cpp.sln /property:Configuration=%CONFIGURATION% /property:Platform=%PLATFORM% /target:jsonrpcserverStatic /verbosity:minimal

REM jsonrpcpp built
cd ..\..\..
